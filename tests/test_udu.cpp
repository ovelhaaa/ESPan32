#include "dsp/synth_engine.h"
#include <array>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <new>
#include <vector>
using namespace pocketpan;
constexpr size_t fs=48000, duration=6*fs;
static bool forbidAllocation=false;
void* operator new(size_t n) {
    assert(!forbidAllocation); if(void* p=std::malloc(n)) return p; throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,size_t) noexcept { std::free(p); }
struct Event { size_t frame; uint8_t note,velocity; midi::MidiEventType type=midi::MidiEventType::NoteOn; };
struct Result {
    std::vector<int32_t> pcm;
    double peak=0,rms=0,dc=0,pre=0,maxGr=0,avgGr=0;
    unsigned gr01=0,gr1=0,hard=0,sat=0,nonfinite=0,voices=0,steals=0;
    uint64_t voiceFrames=0;
};
static Result render(const dsp::UduConfig& config,const std::vector<Event>& events,
                     size_t partition=128,bool fast=true) {
    dsp::SynthEngine engine; engine.init(fs); engine.setInstrumentModel(dsp::InstrumentModel::Udu);
    engine.setUduConfigForTest(config);
#if POCKETPAN_SUSTAIN_FASTPATH
    engine.setSustainFastPathEnabledForTest(fast);
#endif
#if POCKETPAN_ATTACK_VOICE_FASTPATH
    engine.setAttackFastPathEnabledForTest(fast);
#endif
    (void)fast;
    Result r; r.pcm.resize(duration*2);
    forbidAllocation=true;
    size_t next=0;
    for(size_t frame=0;frame<duration;) {
        while(next<events.size() && events[next].frame==frame) {
            midi::MidiEvent e{};e.type=events[next].type;
            e.data1=events[next].note;e.data2=events[next++].velocity;
            engine.handleMidiEvent(e);
        }
        size_t n=std::min(partition,duration-frame);
        if(next<events.size()) n=std::min(n,events[next].frame-frame);
        assert(n>0);
        engine.renderBlock(r.pcm.data()+2*frame,n);frame+=n;
        r.voices=std::max(r.voices,unsigned(engine.getVoiceAllocator().getActiveVoiceCount()));
    }
    // Model-switch RT allocation guard covers all ten models, twice.
    for(unsigned cycle=0;cycle<2;++cycle) {
        auto model=dsp::InstrumentModel::Pan;
        for(unsigned i=0;i<unsigned(dsp::InstrumentModel::Count);++i) {
            assert(unsigned(model)==i);engine.setInstrumentModel(model);
            model=dsp::nextInstrumentModel(model);
        }
        assert(model==dsp::InstrumentModel::Pan);
    }
    forbidAllocation=false;
    // Safety diagnostics must be captured before the model switches reset them.
    return r;
}
static Result measured(const dsp::UduConfig& config,const std::vector<Event>& events,
                       size_t partition=128,bool fast=true) {
    // Separate measured render avoids resetting the retained diagnostics.
    dsp::SynthEngine engine;engine.init(fs);engine.setInstrumentModel(dsp::InstrumentModel::Udu);
    engine.setUduConfigForTest(config);
#if POCKETPAN_SUSTAIN_FASTPATH
    engine.setSustainFastPathEnabledForTest(fast);
#endif
#if POCKETPAN_ATTACK_VOICE_FASTPATH
    engine.setAttackFastPathEnabledForTest(fast);
#endif
    (void)fast;
    Result r;r.pcm.resize(duration*2);size_t next=0;
    forbidAllocation=true;
    for(size_t frame=0;frame<duration;) {
        while(next<events.size() && events[next].frame==frame) {
            midi::MidiEvent e{};e.type=events[next].type;e.data1=events[next].note;
            e.data2=events[next++].velocity;engine.handleMidiEvent(e);
        }
        size_t n=std::min(partition,duration-frame);
        if(next<events.size()) n=std::min(n,events[next].frame-frame);
        assert(n>0);
        r.voiceFrames+=n*engine.getVoiceAllocator().getActiveVoiceCount();
        engine.renderBlock(r.pcm.data()+2*frame,n);frame+=n;
        r.voices=std::max(r.voices,unsigned(engine.getVoiceAllocator().getActiveVoiceCount()));
    }
    forbidAllocation=false;
    double sum=0,sq=0;
    for(size_t i=0;i<duration;++i) {
        assert(r.pcm[2*i]==r.pcm[2*i+1]);
        const double x=double(r.pcm[2*i])/2147483648.0;
        sum+=x;sq+=x*x;r.peak=std::max(r.peak,std::abs(x));
    }
    r.rms=std::sqrt(sq/duration);r.dc=sum/duration;
    r.pre=engine.getPreLimiterPeak();r.maxGr=engine.getMaxGainReductionDb();
    r.avgGr=engine.getAverageGainReductionDb();r.gr01=engine.getGainReductionOver0p1DbSamples();
    r.gr1=engine.getGainReductionOver1DbSamples();r.hard=engine.getHardClampCount();
    r.sat=engine.getModalInternalSaturationCount();r.nonfinite=engine.getNonfiniteCount();
    r.steals=engine.getVoiceAllocator().getVoiceStealCount();
    if(r.maxGr>0) std::cerr<<"GR "<<r.maxGr<<" peak "<<r.pre<<" events "<<events.size()<<'\n';
    assert(r.hard==0 && r.sat==0 && r.nonfinite==0 && r.maxGr==0);
    if(std::abs(r.dc)>=1e-4) std::cerr<<"DC="<<r.dc<<" note="<<unsigned(events[0].note)
        <<" events="<<events.size()<<" cavity="<<config.cavityHz<<'\n';
    assert(r.gr01==0 && r.gr1==0 && std::abs(r.dc)<1e-4);
    assert(engine.verifyPreparedNoteCanaries());
    assert(engine.getVoiceAllocator().getActiveVoiceCount()==0);
    return r;
}
static double difference(const Result& a,const Result& b) {
    double sq=0;for(size_t i=0;i<a.pcm.size();i+=2) {
        const double d=(double(a.pcm[i])-b.pcm[i])/2147483648.0;sq+=d*d;
    }return std::sqrt(sq/duration);
}
static void wav(const std::filesystem::path& path,const Result& r,double target) {
    const double gain=target/r.rms;assert(gain<=1.000001 && r.peak*gain<.98);
    std::ofstream out(path,std::ios::binary);
    auto u16=[&](uint16_t v){char b[2]={char(v),char(v>>8)};out.write(b,2);};
    auto u32=[&](uint32_t v){u16(uint16_t(v));u16(uint16_t(v>>16));};
    out.write("RIFF",4);u32(36+uint32_t(r.pcm.size()*2));out.write("WAVEfmt ",8);
    u32(16);u16(1);u16(2);u32(fs);u32(fs*4);u16(4);u16(16);
    out.write("data",4);u32(uint32_t(r.pcm.size()*2));
    for(auto x:r.pcm) u16(uint16_t(int16_t(std::lround(double(x)/65536.0*gain))));
    assert(out.good());
}
#ifndef UDU_DYNAMIC_TEST
int main(int argc,char** argv) {
    const bool output=argc>1;const std::filesystem::path directory=output?argv[1]:".";
    if(output) std::filesystem::create_directories(directory);
    auto base=dsp::uduCandidateConfig(dsp::UduCandidate::Balanced);
    base.curve=dsp::UduOpeningCurve::Fixed;
    std::array<dsp::UduConfig,3> configs={dsp::uduCandidateConfig(dsp::UduCandidate::Deep),
        base,dsp::uduCandidateConfig(dsp::UduCandidate::Dry)};
    for(auto& c:configs) c.curve=dsp::UduOpeningCurve::Fixed;
    assert(std::string(dsp::instrumentModelName(dsp::InstrumentModel::Udu))=="UDU");
    assert(dsp::nextInstrumentModel(dsp::InstrumentModel::Mbira)==dsp::InstrumentModel::Udu);
    assert(dsp::nextInstrumentModel(dsp::InstrumentModel::Udu)==dsp::InstrumentModel::Pan);
    std::ofstream report;if(output) report.open(directory/"host_metrics.csv");
    report<<std::setprecision(10)<<"fixture,candidate,peak,rms,crest,dc,pre_peak,max_gr_db,avg_gr_db,gr_gt_0p1,gr_gt_1,hard_clamps,modal_sat,nonfinite,active_voices,steals\n";
    auto record=[&](const std::string& name,const std::string& candidate,const Result& r) {
        report<<name<<','<<candidate<<','<<r.peak<<','<<r.rms<<','<<r.peak/r.rms<<','<<r.dc
            <<','<<r.pre<<','<<r.maxGr<<','<<r.avgGr<<','<<r.gr01<<','<<r.gr1<<','<<r.hard
            <<','<<r.sat<<','<<r.nonfinite<<','<<r.voices<<','<<r.steals<<'\n';
    };
    struct Fixture {std::string name;std::vector<Event> events;};
    std::vector<Fixture> pack={{"low_v70",{{0,36,70}}},{"mid_v70",{{0,60,70}}},
        {"mid_v110",{{0,60,110}}},{"high_v70",{{0,84,70}}},{"groove",{}}};
    // Two bars at 120 BPM: palm bass, body accents, finger taps and soft ghosts.
    const uint8_t notes[]={36,84,60,84,36,60,84,60,36,84,60,84,36,60,84,60};
    const uint8_t velocities[]={110,45,85,60,100,70,50,95,115,50,80,55,100,70,45,90};
    for(unsigned i=0;i<16;++i) {
        pack.back().events.push_back({i*12000,notes[i],velocities[i]});
        if(i%4==3) pack.back().events.push_back({i*12000+6000,84,35});
    }
    for(const auto& f:pack) {
        std::array<Result,3> results;double target=1;
        for(unsigned c=0;c<3;++c) {
            results[c]=measured(configs[c],f.events);target=std::min(target,results[c].rms);
            record(f.name,std::string(1,char('A'+c)),results[c]);
        }
        assert(difference(results[0],results[1])>.001 && difference(results[1],results[2])>.001);
        if(output) for(unsigned c=0;c<3;++c)
            wav(directory/("udu_"+f.name+"_"+char('A'+c)+".wav"),results[c],target);
    }
    std::array<Result,3> holes;double holeTarget=1;
    const char* holeNames[]={"open","partial","closed"};
    for(unsigned i=0;i<3;++i) {
        auto c=base;c.opening=static_cast<dsp::UduOpening>(i);
        holes[i]=measured(c,pack[4].events);holeTarget=std::min(holeTarget,holes[i].rms);
        record("hole_groove",holeNames[i],holes[i]);
    }
    assert(difference(holes[0],holes[1])>.001 && difference(holes[1],holes[2])>.001);
    if(output) for(unsigned i=0;i<3;++i) wav(directory/("udu_B_"+std::string(holeNames[i])+".wav"),holes[i],holeTarget);
    std::vector<Fixture> stress={{"chord4",{{0,36,127},{0,48,127},{0,60,127},{0,84,127}}},
        {"cluster8",{}},{"roll",{}},{"steal",{}},
        {"pressure",{{0,60,110},{4800,60,100,midi::MidiEventType::PolyPressure}}}};
    for(unsigned i=0;i<8;++i) stress[1].events.push_back({0,uint8_t(36+i*6),127});
    for(unsigned i=0;i<24;++i) stress[2].events.push_back({i*2400,60,127});
    for(unsigned i=0;i<16;++i) stress[3].events.push_back({i*240,uint8_t(36+i*3),127});
    for(const auto& f:stress) for(unsigned c=0;c<3;++c) {
        auto r=measured(configs[c],f.events);record(f.name,std::string(1,char('A'+c)),r);
        assert(r.pcm==measured(configs[c],f.events,128,false).pcm);
        if(f.name=="steal") assert(r.steals>0);
    }
    for(unsigned c=0;c<3;++c) for(uint8_t note:{0,24,36,60,84,96,127}) {
        double previous=0;
        for(uint8_t velocity:{30,70,110,127}) {
            const auto r=measured(configs[c],{{0,note,velocity}});
            assert(r.rms>previous);previous=r.rms;
            record("note"+std::to_string(note)+"_v"+std::to_string(velocity),std::string(1,char('A'+c)),r);
        }
    }
    // Investigate full keyboard transposition versus the bounded acoustic size family.
    auto pitched=base;pitched.compressedRange=false;
    for(uint8_t note:{36,60,84}) {
        const auto r=measured(pitched,{{0,note,70}});record("pitched_note"+std::to_string(note),"B",r);
        if(output) wav(directory/("udu_mapping_pitched_"+std::to_string(note)+".wav"),r,r.rms);
    }
    assert(measured(base,{{0,0,70}}).pcm==measured(base,{{0,36,70}}).pcm);
    assert(measured(base,{{0,127,70}}).pcm==measured(base,{{0,84,70}}).pcm);
    assert(measured(base,{{0,60,110},{4800,60,70}}).pcm==
        measured(base,{{0,60,110},{4800,60,70}},37,false).pcm);
    assert(render(base,pack[4].events).pcm==measured(base,pack[4].events).pcm);
    std::cout<<"UDU: hybrid air/shell, A/B/C, holes, mapping, groove, safety, partitions, allocation, cycles PASS\n"
        <<"UduCache="<<sizeof(dsp::UduCache)<<" UduVoice="<<sizeof(dsp::UduVoice)
        <<" SynthEngine="<<sizeof(dsp::SynthEngine)<<" PreparedNote="<<sizeof(dsp::PreparedNote)
        <<" full tables="<<9*sizeof(dsp::PreparedNoteTable)<<'\n';
}
#endif
