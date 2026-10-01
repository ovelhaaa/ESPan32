// Reuse the M7.7 host renderer/safety/allocation guard, without its A/B/C pack.
#define UDU_DYNAMIC_TEST
#include "test_udu.cpp"

static double frequency(const dsp::UduAirCoefficients& c,unsigned m=0) {
    const double r=std::sqrt(-double(c.a2[m]));
    return std::acos(std::clamp(double(c.a1[m])/(2*r),-1.0,1.0))*fs/(2*3.141592653589793);
}
static double decay(const dsp::UduAirCoefficients& c,unsigned m=0) {
    return -6.907755279/(fs*std::log(std::sqrt(-double(c.a2[m]))));
}
static uint64_t hash(const Result& r) {
    uint64_t h=14695981039346656037ull;
    for(auto x:r.pcm) for(unsigned shift=0;shift<32;shift+=8) {
        h^=(uint32_t(x)>>shift)&255u;h*=1099511628211ull;
    }
    return h;
}
// Host occupancy probe of the device's repeating 8192-block groove fixture;
// it supplies no simulated device timings or hardware qualification results.
static uint64_t repeatingGrooveVoiceFrames(const dsp::UduConfig& config) {
    dsp::SynthEngine engine;engine.init(fs);engine.setInstrumentModel(dsp::InstrumentModel::Udu);
    engine.setUduConfigForTest(config);
    const uint8_t notes[]={36,84,60,84,36,60,84,60,36,84,60,84,36,60,84,60};
    const uint8_t velocities[]={110,45,85,60,100,70,50,95,115,50,80,55,100,70,45,90};
    std::array<int32_t,256> pcm{};uint64_t frames=0;
    forbidAllocation=true;
    for(unsigned block=0;block<8192;++block) {
        if(block && (block-1)%94==0) {
            const unsigned step=((block-1)/94)%16;
            midi::MidiEvent event{};event.type=midi::MidiEventType::NoteOn;
            event.data1=notes[step];event.data2=velocities[step];engine.handleMidiEvent(event);
        }
        frames+=128*engine.getVoiceAllocator().getActiveVoiceCount();engine.renderBlock(pcm.data(),128);
    }
    forbidAllocation=false;return frames;
}
int main(int argc,char** argv) {
    const bool output=argc>1;
    const std::filesystem::path dir=output?argv[1]:".";
    if(output) std::filesystem::create_directories(dir);
    dsp::UduConfig dynamic,fixed;fixed.curve=dsp::UduOpeningCurve::Fixed;
    dsp::UduCache cache;cache.prepare(fs,dynamic);
    static_assert(sizeof(dsp::PreparedNote)==132);
    static_assert(sizeof(dsp::UduCache)-3576+8*(sizeof(dsp::UduVoice)-104)<3072);
    std::ofstream report;if(output) report.open(dir/"host_metrics.csv");
    report<<std::setprecision(12)<<"fixture,curve,restrike,peak,rms,crest,dc,pre_peak,max_gr_db,avg_gr_db,hard_clamps,modal_sat,nonfinite,opening,hz_multiplier,t60_multiplier,pcm_fnv64,voice_frames\n";
    auto record=[&](const std::string& name,const std::string& curve,const std::string& policy,
                    const Result& r,float opening=-1) {
        const auto c=cache.airFor(60,opening<0?.5f:opening);
        report<<name<<','<<curve<<','<<policy<<','<<r.peak<<','<<r.rms<<','<<r.peak/r.rms<<','<<r.dc
            <<','<<r.pre<<','<<r.maxGr<<','<<r.avgGr<<','<<r.hard<<','<<r.sat<<','<<r.nonfinite<<',';
        if(opening>=0) report<<opening<<','<<frequency(c)/105<<','<<decay(c)/.42;
        else report<<",,"; // A groove/restrike has several strike states.
        report<<','<<std::hex<<hash(r)<<std::dec<<','<<r.voiceFrames<<'\n';
    };
    // Verify actual poles (not merely nominal multipliers) across every pot size
    // and MIDI velocity, including exact PARTIAL and unchanged ceramic modes.
    dsp::UduCache fixedCache;fixedCache.prepare(fs,fixed);
    for(unsigned n=36;n<=84;++n) {
        for(unsigned m=2;m<6;++m) {
            assert(cache.find(uint8_t(n)).a1[m]==fixedCache.find(uint8_t(n)).a1[m]);
            assert(cache.find(uint8_t(n)).a2[m]==fixedCache.find(uint8_t(n)).a2[m]);
            assert(cache.find(uint8_t(n)).injection[m]==fixedCache.find(uint8_t(n)).injection[m]);
        }
        double previousHz[2]={0,0},previousDecay[2]={100,100};
        for(unsigned v=0;v<=127;++v) {
            const auto air=cache.airFor(uint8_t(n),dsp::uduOpeningForVelocity(v/127.f,dynamic.curve));
            for(unsigned m=0;m<2;++m) {
                assert(air.a2[m]>-1 && 1-air.a1[m]-air.a2[m]>0 && 1+air.a1[m]-air.a2[m]>0);
                const double hz=frequency(air,m),t60=decay(air,m);
                assert(hz>previousHz[m] && t60<previousDecay[m]);
                previousHz[m]=hz;previousDecay[m]=t60;
            }
        }
        const auto partial=cache.airFor(uint8_t(n),.5f);
        for(unsigned m=0;m<2;++m) {
            assert(partial.a1[m]==cache.find(uint8_t(n)).a1[m]);
            assert(partial.a2[m]==cache.find(uint8_t(n)).a2[m]);
            assert(partial.injection[m]==cache.find(uint8_t(n)).injection[m]);
        }
    }
    assert(dsp::uduOpeningForVelocity(.3f,dynamic.curve)==.15f);
    assert(dsp::uduOpeningForVelocity(.6f,dynamic.curve)==.5f);
    // R1 never changes a ringing cavity, R2 preserves energy but moves poles.
    for(auto policy:{dsp::UduRestrike::Retain,dsp::UduRestrike::Update}) {
        auto config=dynamic;config.restrike=policy;cache.prepare(fs,config);
        dsp::UduVoice voice;voice.setCache(&cache);voice.strike(60,30/127.f,true);
        for(unsigned i=0;i<4800;++i) assert(std::isfinite(voice.process(0)));
        const auto before=voice.airCoefficients();const float energy=voice.energy(),opening=voice.opening();
        voice.strike(60,1,false);assert(voice.energy()==energy);
        if(policy==dsp::UduRestrike::Retain) {
            assert(voice.opening()==opening && voice.airCoefficients().a1[0]==before.a1[0]);
        } else assert(voice.opening()==1 && voice.airCoefficients().a1[0]!=before.a1[0]);
        for(unsigned i=0;i<6*fs;++i) assert(std::isfinite(voice.process(0)));
        voice.strike(60,1,false);assert(voice.opening()==1); // dead cavity accepts new strike
        assert(voice.faults()==0);
    }
    cache.prepare(fs,dynamic);
    std::vector<Event> groove;
    const uint8_t notes[]={36,84,60,84,36,60,84,60,36,84,60,84,36,60,84,60};
    const uint8_t velocities[]={110,45,85,60,100,70,50,95,115,50,80,55,100,70,45,90};
    for(unsigned i=0;i<16;++i) {
        groove.push_back({i*12000,notes[i],velocities[i]});
        if(i%4==3) groove.push_back({i*12000+6000,84,35});
    }
    std::array<Result,4> singles;double target=1;float previous=0;
    const unsigned velocities4[]={30,70,110,127};
    for(unsigned i=0;i<4;++i) {
        const unsigned v=velocities4[i];const float o=dsp::uduOpeningForVelocity(v/127.f,dynamic.curve);
        assert(o>previous);previous=o;
        singles[i]=measured(dynamic,{{0,60,uint8_t(v)}});target=std::min(target,singles[i].rms);
        record("v"+std::to_string(v),"centered","R1",singles[i],o);
    }
    if(output) for(unsigned i=0;i<4;++i)
        wav(dir/("udu_B_dynamic_v"+std::to_string(velocities4[i])+".wav"),singles[i],target);
    const auto reference=measured(fixed,groove);
    assert(hash(reference)==0x1a768c0057636a2dull); // M7.7 B/PARTIAL production PCM
    const auto chosen=measured(dynamic,groove);
    std::cout<<"groove_voice_frames fixed="<<reference.voiceFrames<<" dynamic="<<chosen.voiceFrames<<'\n';
    std::cout<<"repeating_groove_voice_frames fixed="<<repeatingGrooveVoiceFrames(fixed)
        <<" dynamic="<<repeatingGrooveVoiceFrames(dynamic)<<'\n';
    assert(difference(reference,chosen)>.001);
    record("groove","fixed_partial","R1",reference,.5f);record("groove","centered","R1",chosen);
    if(output) {
        target=std::min(reference.rms,chosen.rms);
        wav(dir/"udu_B_groove_fixed_partial.wav",reference,target);
        wav(dir/"udu_B_groove_dynamic_opening.wav",chosen,target);
        // Exact legacy regression uses its original opening-group RMS gain.
        auto closed=fixed,open=fixed;closed.opening=dsp::UduOpening::Closed;open.opening=dsp::UduOpening::Open;
        const double legacyTarget=std::min({reference.rms,measured(closed,groove).rms,measured(open,groove).rms});
        wav(dir/"legacy_partial_check.wav",reference,legacyTarget);
    }
    std::array<Result,3> curves;const char* curveNames[]={"linear","smooth","centered"};target=1;
    for(unsigned i=0;i<3;++i) {
        auto config=dynamic;config.curve=static_cast<dsp::UduOpeningCurve>(i+1);
        curves[i]=measured(config,groove);target=std::min(target,curves[i].rms);
        record("curve_groove",curveNames[i],"R1",curves[i]);
    }
    if(output) for(unsigned i=0;i<3;++i)
        wav(dir/("udu_B_groove_dynamic_"+std::string(curveNames[i])+".wav"),curves[i],target);
    // Focused R1/R2 listening pair: repeated same pot, soft->hard and hard->soft.
    const std::vector<Event> restrikes={{0,60,30},{4800,60,127},{12000,60,70},{18000,60,110},
        {24000,60,30},{30000,60,127},{36000,60,30},{42000,60,70}};
    std::array<Result,2> policies;target=1;
    for(unsigned i=0;i<2;++i) {
        auto config=dynamic;config.restrike=static_cast<dsp::UduRestrike>(i);
        policies[i]=measured(config,restrikes);target=std::min(target,policies[i].rms);
        record("restrike","centered",i?"R2":"R1",policies[i]);
        assert(policies[i].pcm==measured(config,restrikes,37,false).pcm);
    }
    if(output) for(unsigned i=0;i<2;++i)
        wav(dir/("udu_B_restrike_R"+std::to_string(i+1)+".wav"),policies[i],target);
    // Both policies: extreme notes/velocities, steals, simultaneous bursts,
    // pressure and 20 ms alternating-velocity retriggers.
    std::vector<Event> cluster,chord,rapid,steals;
    for(unsigned i=0;i<8;++i) cluster.push_back({0,uint8_t(36+i*6),127});
    for(uint8_t n:{36,48,60,84}) chord.push_back({0,n,127});
    for(unsigned i=0;i<80;++i) rapid.push_back({i*960,60,uint8_t(i%2?127:30)});
    for(unsigned i=0;i<16;++i) steals.push_back({i*240,uint8_t(36+i*3),127});
    for(auto policy:{dsp::UduRestrike::Retain,dsp::UduRestrike::Update}) {
        auto config=dynamic;config.restrike=policy;const std::string name=policy==dsp::UduRestrike::Retain?"R1":"R2";
        for(uint8_t n:{0,36,60,84,127}) for(uint8_t v:{30,70,110,127})
            record("note"+std::to_string(n)+"_v"+std::to_string(v),"centered",name,measured(config,{{0,n,v}}));
        for(const auto& f:std::vector<std::pair<std::string,std::vector<Event>>>{{"cluster8",cluster},{"chord4",chord},
            {"rapid",rapid},{"steals",steals},{"pressure",{{0,60,127},{4800,60,127,midi::MidiEventType::PolyPressure}}}}) {
            const auto r=measured(config,f.second);record(f.first,"centered",name,r);
            assert(r.pcm==measured(config,f.second,128,false).pcm);
            if(f.first=="steals") assert(r.steals>0);
        }
    }
    assert(measured(dynamic,{{0,0,70}}).pcm==measured(dynamic,{{0,36,70}}).pcm);
    assert(measured(dynamic,{{0,127,70}}).pcm==measured(dynamic,{{0,84,70}}).pcm);
    assert(render(dynamic,groove).pcm==chosen.pcm);
    std::cout<<"UDU dynamic: B preserved; poles stable/monotonic; R1/R2; safety; partition; allocation PASS\n"
        <<"fixed_partial_raw_fnv64="<<std::hex<<hash(reference)<<std::dec<<'\n'
        <<"UduCache="<<sizeof(dsp::UduCache)<<" UduVoice="<<sizeof(dsp::UduVoice)
        <<" SynthEngine="<<sizeof(dsp::SynthEngine)<<" PreparedNote="<<sizeof(dsp::PreparedNote)<<'\n';
}
