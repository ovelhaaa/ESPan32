#include "dsp/synth_engine.h"
#include "midi/midi_mapping.h"
#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace pocketpan;
using dsp::InstrumentModel;
constexpr size_t fs=48000, duration=fs*6;
struct Event { size_t frame; uint8_t note, velocity; };
struct Result {
    std::vector<int32_t> pcm;
    double rms=0, peak=0, mean=0, pre=0, maxGr=0, avgGr=0, body=0;
    unsigned gr01=0, gr1=0, hard=0, sat=0, nonfinite=0, voices=0, steals=0;
};
static Result render(const dsp::InstrumentModelConfig& config,
                     const std::vector<Event>& events, bool buzz=true,
                     bool canonical=false, bool cache=true, bool fast=true,
                     size_t partition=128, bool referenceTrigger=false) {
    dsp::SynthEngine engine; engine.init(fs); engine.setInstrumentModel(config.id);
    if (!canonical) {
        auto override=config;
        // The engine stays MBIRA (contact/DC path), but a PAN identity on the
        // otherwise identical voicing forces the historical libm trigger math.
        if(referenceTrigger) override.id=InstrumentModel::Pan;
        engine.setModelConfigForTest(override);
    }
    engine.setMbiraBuzzEnabled(buzz);
#if POCKETPAN_PREPARED_NOTE_CACHE
    engine.setPreparedNoteCacheEnabledForTest(cache);
#else
    (void)cache;
#endif
#if POCKETPAN_SUSTAIN_FASTPATH
    engine.setSustainFastPathEnabledForTest(fast);
#endif
#if POCKETPAN_ATTACK_VOICE_FASTPATH
    engine.setAttackFastPathEnabledForTest(fast);
#endif
    (void)fast;
    Result r; r.pcm.resize(duration*2); size_t next=0;
    for(size_t frame=0;frame<duration;) {
        while(next<events.size() && events[next].frame==frame) {
            midi::MidiEvent e{}; e.type=midi::MidiEventType::NoteOn;
            e.data1=events[next].note; e.data2=events[next++].velocity;
            engine.handleMidiEvent(e);
        }
        size_t n=std::min(partition,duration-frame);
        if(next<events.size()) n=std::min(n,events[next].frame-frame);
        engine.renderBlock(r.pcm.data()+2*frame,n); frame+=n;
        r.voices=std::max(r.voices,unsigned(engine.getVoiceAllocator().getActiveVoiceCount()));
    }
    double sum=0,sq=0;
    for(size_t i=0;i<duration;++i) {
        assert(r.pcm[2*i]==r.pcm[2*i+1]);
        double x=double(r.pcm[2*i])/2147483648.0;
        sum+=x; sq+=x*x; r.peak=std::max(r.peak,std::abs(x));
    }
    r.mean=sum/duration; r.rms=std::sqrt(sq/duration);
    r.pre=engine.getPreLimiterPeak(); r.maxGr=engine.getMaxGainReductionDb();
    r.avgGr=engine.getAverageGainReductionDb(); r.gr01=engine.getGainReductionOver0p1DbSamples();
    r.gr1=engine.getGainReductionOver1DbSamples(); r.hard=engine.getHardClampCount();
    r.sat=engine.getModalInternalSaturationCount(); r.nonfinite=engine.getNonfiniteCount();
    r.body=engine.getBodyRms(); r.steals=engine.getVoiceAllocator().getVoiceStealCount();
    assert(r.hard==0 && r.sat==0 && r.nonfinite==0);
    if(r.maxGr!=0) std::cerr<<"Limiter: note="<<int(events.front().note)
        <<" velocity="<<int(events.front().velocity)<<" events="<<events.size()
        <<" exciter="<<config.exciter.gain<<" pre="<<r.pre<<" GR="<<r.maxGr<<'\n';
    assert(r.maxGr==0 && r.gr01==0 && r.gr1==0);
    if(std::abs(r.mean)>=1e-4) std::cerr<<"DC: model="<<int(config.id)<<" note="<<int(events.front().note)
        <<" events="<<events.size()<<" mean="<<r.mean<<" RMS="<<r.rms<<'\n';
    assert(std::abs(r.mean)<1e-4 && std::isfinite(r.pre));
    assert(engine.verifyPreparedNoteCanaries());
    return r;
}
static void wav(const std::filesystem::path& path,const Result& r,double target) {
    double gain=target/r.rms; assert(r.peak*gain<.98);
    std::ofstream out(path,std::ios::binary);
    auto u16=[&](uint16_t v){char b[2]={char(v),char(v>>8)};out.write(b,2);};
    auto u32=[&](uint32_t v){u16(uint16_t(v));u16(uint16_t(v>>16));};
    out.write("RIFF",4);u32(36+uint32_t(r.pcm.size()*2));out.write("WAVEfmt ",8);
    u32(16);u16(1);u16(2);u32(fs);u32(fs*4);u16(4);u16(16);
    out.write("data",4);u32(uint32_t(r.pcm.size()*2));
    for(auto x:r.pcm) u16(uint16_t(int16_t(std::lround(double(x)/65536.0*gain))));
    assert(out.good());
}
static double differenceRms(const Result& a,const Result& b) {
    double sum=0;for(size_t i=0;i<a.pcm.size();i+=2) {
        double d=(double(a.pcm[i])-double(b.pcm[i]))/2147483648.0;sum+=d*d;
    }return std::sqrt(sum/duration);
}
static double upperEnergyDb(const Result& r,const dsp::InstrumentModelConfig& config,uint8_t note) {
    const double fundamental=midi::MidiMapping::noteToHz(note);
    double energy[6]={};
    for(unsigned m=0;m<config.modalPreset->modeCount;++m) {
        const double hz=fundamental*config.modalPreset->modes[m].ratio;
        if(hz>=fs*.45) continue;
        const double coeff=2*std::cos(6.283185307179586*hz/fs);
        double a=0,b=0;
        for(size_t i=48;i<7248;++i) {
            const double y=double(r.pcm[2*i])/2147483648.0+coeff*a-b;b=a;a=y;
        }
        energy[m]=a*a+b*b-coeff*a*b;
    }
    double upper=0;for(unsigned m=1;m<config.modalPreset->modeCount;++m) upper+=energy[m];
    assert(energy[0]>upper); // The pitch remains dominant even with raw twang.
    return 10*std::log10(std::max(upper,1e-30)/energy[0]);
}
int main(int argc,char** argv) {
    const bool output=argc>1;
    const std::filesystem::path directory=output?argv[1]:".";
    if(output) std::filesystem::create_directories(directory);
    const auto base=dsp::getInstrumentModelConfig(InstrumentModel::Mbira);
    assert(base.modalPreset->modeCount==6 && base.exciter.shape==dsp::ExciterShape::Pluck);
    assert(std::string(dsp::instrumentModelName(base.id))=="MBIRA");
    for(unsigned cycle=0;cycle<2;++cycle) {
        auto model=InstrumentModel::Pan;
        for(unsigned i=0;i<9;++i) {
            assert(unsigned(model)==i); model=dsp::nextInstrumentModel(model);
        }
        assert(model==InstrumentModel::Pan);
    }
    auto warm=base, raw=base;
    auto warmPreset=*base.modalPreset, rawPreset=*base.modalPreset;
    warm.exciter={.145f,.04f,900.0f,9500.0f,.90f,.55f,dsp::ExciterShape::Pluck};
    warm.voicing.strikeHardnessMin=.16f; warm.voicing.strikeHardnessMax=.65f;
    warm.body.outputGain=.11f;warm.mbiraBuzzGain=.12f;
    raw.exciter={.095f,.07f,2800.0f,19000.0f,.90f,.55f,dsp::ExciterShape::Pluck};
    raw.voicing.strikeHardnessMin=.70f;raw.voicing.strikeHardnessMax=1.0f;
    raw.body.outputGain=.025f;raw.mbiraBuzzGain=4.0f;
    for(size_t i=1;i<6;++i) {
        warmPreset.modes[i].gain*=.45f;warmPreset.modes[i].ratio*=.94f;
        warmPreset.modes[i].t60*=.80f;
        rawPreset.modes[i].gain*=1.65f;rawPreset.modes[i].ratio*=1.06f;
        rawPreset.modes[i].t60*=1.15f;
    }
    warm.modalPreset=&warmPreset;raw.modalPreset=&rawPreset;
    std::array<dsp::InstrumentModelConfig,3> configs={warm,base,raw};
    std::ofstream report;
    if(output) report.open(directory/"host_metrics.csv");
    report<<std::setprecision(10)<<"fixture,candidate,peak,rms,crest,dc_mean,pre_peak,max_gr_db,avg_gr_db,gr_gt_0p1,gr_gt_1,hard_clamps,modal_sat,nonfinite,active_voices,steals,body_rms,buzz_delta_rms,upper_fundamental_db\n";
    auto record=[&](const std::string& name,char candidate,const Result& r,double buzz=0,double upper=0) {
        report<<name<<','<<candidate<<','<<r.peak<<','<<r.rms<<','<<r.peak/r.rms<<','<<r.mean<<','<<r.pre<<','<<r.maxGr<<','<<r.avgGr<<','<<r.gr01<<','<<r.gr1<<','<<r.hard<<','<<r.sat<<','<<r.nonfinite<<','<<r.voices<<','<<r.steals<<','<<r.body<<','<<buzz<<','<<upper<<'\n';
    };
    for(uint8_t note:{50,57,62,69,74}) {
        double previous=0, previousBuzz=0, previousUpper=-1000;
        for(uint8_t velocity:{30,70,110,127}) {
            const auto r=render(base,{{0,note,velocity}},true,true);
            const auto off=render(base,{{0,note,velocity}},false,true);
            const double buzz=differenceRms(r,off);
            assert(r.rms>previous);assert(buzz>=previousBuzz);
            previous=r.rms;previousBuzz=buzz;
            const double upper=upperEnergyDb(r,base,note);
            assert(upper>previousUpper);previousUpper=upper;
            record("note"+std::to_string(note)+"_v"+std::to_string(velocity),'B',r,buzz,upper);
            // Exact canonical cache/fallback and fast/reference attack/tail.
            if(note==62) {
                assert(r.pcm==render(base,{{0,note,velocity}},true,true,false).pcm);
                assert(r.pcm==render(base,{{0,note,velocity}},true,true,true,false).pcm);
                assert(r.pcm==render(base,{{0,note,velocity}},true,false,false,true,128,true).pcm);
            }
        }
    }
    struct Fixture {std::string name;std::vector<Event> events;};
    std::vector<Fixture> listening={
        {"D3_v70",{{0,50,70}}},{"D4_v70",{{0,62,70}}},{"D4_v110",{{0,62,110}}},
        {"chord",{{0,50,110},{0,57,110},{0,62,110},{0,65,110}}},{"groove",{}}};
    // Two interlocking six-pulse hands, offset by one eighth-note (150 ms).
    const uint8_t left[]={50,57,62,57,50,60},right[]={69,65,74,69,65,62};
    for(unsigned i=0;i<24;++i) {
        listening.back().events.push_back({i*14400,left[i%6],uint8_t(i%3==0?110:70)});
        listening.back().events.push_back({i*14400+7200,right[i%6],uint8_t(i%3==1?100:62)});
    }
    // Last 600 ms retain the tail; events beyond the render duration are excluded.
    auto& groove=listening.back().events;
    groove.erase(std::remove_if(groove.begin(),groove.end(),[](Event e){return e.frame>=fs*5.4;}),groove.end());
    for(const auto& f:listening) {
        std::array<Result,3> results;
        double target=1;
        for(unsigned i=0;i<3;++i) {
            results[i]=render(configs[i],f.events);
            const double upper=f.events.size()==1 ? upperEnergyDb(results[i],configs[i],f.events[0].note) : 0;
            record(f.name,char('A'+i),results[i],0,upper);
            target=std::min(target,results[i].rms);
        }
        assert(differenceRms(results[0],results[1])>.001);
        assert(differenceRms(results[1],results[2])>.001);
        if(output) for(unsigned i=0;i<3;++i)
            wav(directory/("mbira_"+f.name+"_"+char('A'+i)+".wav"),results[i],target);
    }
    for(const auto& f:{listening[1],listening[2],listening[4]}) {
        const auto off=render(base,f.events,false,true),on=render(base,f.events,true,true);
        record(f.name+"_buzz_off",'B',off);record(f.name+"_buzz_on",'B',on,differenceRms(on,off));
        if(output) {
            const double target=std::min(off.rms,on.rms);
            wav(directory/("mbira_"+f.name+"_buzz_off.wav"),off,target);
            wav(directory/("mbira_"+f.name+"_buzz_on.wav"),on,target);
        }
    }
    std::vector<Fixture> stress={
        {"cluster8",{{0,50,127},{0,52,127},{0,54,127},{0,56,127},{0,57,127},{0,59,127},{0,61,127},{0,62,127}}},
        {"restrike100",{{0,62,30},{4800,62,110}}},
        {"restrike250",{{0,62,30},{12000,62,110}}},
        {"roll",{}},{"steal",{}}};
    size_t frame=0;
    for(unsigned spacing:{12000,6000,2400}) for(unsigned i=0;i<8;++i) {
        stress[3].events.push_back({frame,62,uint8_t(70+(i%3)*20)});frame+=spacing;
    }
    for(unsigned i=0;i<16;++i) stress[4].events.push_back({i*240, uint8_t(50+i),110});
    for(const auto& f:stress) for(unsigned i=0;i<3;++i) {
        const auto r=render(configs[i],f.events);
        record(f.name,char('A'+i),r);
        if(i==1) assert(r.pcm==render(configs[i],f.events,true,false,true,false).pcm);
    }
    // Nyquist pruning, fallback pitches, pressure, release and reset are also
    // covered by the extended PreparedNote suite across all nine instruments.
    auto kalimba=dsp::getInstrumentModelConfig(InstrumentModel::Kalimba);
    for(uint8_t velocity:{70,110}) {
        const auto k=render(kalimba,{{0,62,velocity}},false,true);
        record("kalimba_D4_v"+std::to_string(velocity),'K',k,0,upperEnergyDb(k,kalimba,62));
        if(output) wav(directory/("comparison_kalimba_D4_v"+std::to_string(velocity)+".wav"),k,k.rms);
    }
    // Partition invariance with one voice and fixed event frames.
    const std::vector<Event> events={{0,62,110},{4800,62,70}};
    assert(render(base,events,true,true).pcm==render(base,events,true,true,true,true,37).pcm);
    std::cout<<"MBIRA: safety, velocity, cache, fast paths, partitioning, two cycles passed.\n"
             <<"PreparedNote="<<sizeof(dsp::PreparedNote)<<" table="<<sizeof(dsp::PreparedNoteTable)
             <<" total tables="<<9*sizeof(dsp::PreparedNoteTable)<<" engine="<<sizeof(dsp::SynthEngine)<<'\n';
}
