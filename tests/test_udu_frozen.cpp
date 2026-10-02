// Authoritative UDU V1 oracles: B + CENTERED + R1. No listening exploration.
#define UDU_DYNAMIC_TEST
#include "test_udu.cpp"

static uint64_t pcmHash(const Result& r) {
    uint64_t h=14695981039346656037ull;
    for(auto x:r.pcm) for(unsigned s=0;s<32;s+=8) {
        h^=(uint32_t(x)>>s)&255u;h*=1099511628211ull;
    }
    return h;
}
int main(int argc,char** argv) {
    if(argc>1) std::filesystem::create_directories(argv[1]);
    const dsp::UduConfig config{};
    assert(config.curve==dsp::UduOpeningCurve::Centered);
    assert(config.restrike==dsp::UduRestrike::Retain);
    assert(config.cavityHz==105 && config.compressedRange);
    std::vector<std::pair<std::string,std::vector<Event>>> fixtures;
    for(uint8_t v:{30,70,110,127}) fixtures.push_back({"single_v"+std::to_string(v),{{0,60,v}}});
    std::vector<Event> groove,chord,cluster,rapid;
    const uint8_t notes[]={36,84,60,84,36,60,84,60,36,84,60,84,36,60,84,60};
    const uint8_t velocities[]={110,45,85,60,100,70,50,95,115,50,80,55,100,70,45,90};
    for(unsigned i=0;i<16;++i) {
        groove.push_back({i*12000,notes[i],velocities[i]});
        if(i%4==3) groove.push_back({i*12000+6000,84,35});
    }
    for(uint8_t n:{36,48,60,84}) chord.push_back({0,n,127});
    for(unsigned i=0;i<8;++i) cluster.push_back({0,uint8_t(36+i*6),127});
    for(unsigned i=0;i<80;++i) rapid.push_back({i*960,60,uint8_t(i%2?127:30)});
    fixtures.push_back({"dynamic_groove",groove});fixtures.push_back({"chord4",chord});
    fixtures.push_back({"cluster8",cluster});fixtures.push_back({"rapid_restrike",rapid});
    // Captured from the accepted M7.7.1 runtime before any memory refactor.
    constexpr uint64_t expected[]={0x5d45252726edededull,0xd8994f36036e4f99ull,
        0x3b7acf2fd8f4234dull,0x1ad5c9137ae41bf5ull,0xa3aac03f8ae05829ull,
        0x468e7c01255461d5ull,0x12e6cab468bb78e9ull,0x31b12820676fe041ull};
    for(size_t i=0;i<fixtures.size();++i) {
        const auto r=measured(config,fixtures[i].second);
        const auto h=pcmHash(r);
        std::cout<<fixtures[i].first<<" 0x"<<std::hex<<h<<std::dec<<'\n';
        assert(h==expected[i]);
        assert(r.pcm==measured(config,fixtures[i].second,128,false).pcm);
        if(argc>1 && i==4) wav(std::filesystem::path(argv[1])/"udu_v1_dynamic_groove.wav",r,r.rms);
    }
    auto fixed=config;fixed.curve=dsp::UduOpeningCurve::Fixed;
    assert(pcmHash(measured(fixed,groove))==0x1a768c0057636a2dull);
    std::cout<<"UDU V1 FROZEN: B + CENTERED + R1\n";
}
