#include "dsp/synth_engine.h"
#include "midi/midi_event.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace pocketpan;
struct Event { unsigned sample; midi::MidiEventType type; uint8_t note, value; };
static uint32_t bits(float f) { uint32_t u; std::memcpy(&u,&f,4); return u; }

static void compare(dsp::InstrumentModel model, unsigned fixture, const std::vector<unsigned>& partition) {
    static dsp::SynthEngine optimized, reference;
    dsp::m81::sharedNoiseBlocks = 0;
    optimized.init(48000); reference.init(48000);
    optimized.setInstrumentModel(model); reference.setInstrumentModel(model);
    reference.setSustainFastPathEnabledForTest(false);
    reference.setAttackFastPathEnabledForTest(false);
    std::vector<Event> events;
    constexpr uint8_t notes[]={48,50,53,55,60,62,65,67};
    const unsigned count=fixture==0 ? 1 : fixture==1 ? 4 : 8;
    for(unsigned v=0;v<count;++v) events.push_back({0,midi::MidiEventType::NoteOn,notes[v],100});
    if(fixture==3) for(unsigned n=0;n<20;++n) events.push_back({1024+n*389,midi::MidiEventType::NoteOn,uint8_t(48+n%24),uint8_t(30+n*4)});
    if(fixture==4) for(unsigned v=0;v<8;++v) events.push_back({4099+v*127,midi::MidiEventType::NoteOn,notes[v],uint8_t(30+v*12)});
    if(fixture==5) {
        events.push_back({4099,midi::MidiEventType::PolyPressure,notes[3],100});
        events.push_back({8197,midi::MidiEventType::ChannelPressure,0,0});
    }
    if(fixture==6) for(unsigned v=0;v<8;++v) events.push_back({4099+v*17,midi::MidiEventType::NoteOff,notes[v],0});
    if(fixture==7) for(unsigned v=0;v<8;++v) events.push_back({4099+v*193,midi::MidiEventType::NoteOn,uint8_t(72+v),110});
    if(fixture==8) {
        // Staggered attacks force different age-aligned lifetime boundaries.
        events.clear();
        for(unsigned v=0;v<8;++v) events.push_back({v*73,midi::MidiEventType::NoteOn,notes[v],30});
    }
    std::stable_sort(events.begin(),events.end(),[](auto a,auto b){return a.sample<b.sample;});
    std::array<int32_t,256> a{},b{};
    size_t next=0,block=0;
    const unsigned total=(fixture==6 || fixture==8) ? 576000 : 192000;
    for(unsigned pos=0;pos<total;) {
        while(next<events.size() && events[next].sample==pos) {
            const auto& event=events[next++]; midi::MidiEvent e{};
            e.type=event.type;e.data1=event.note;e.data2=event.value;
            optimized.handleMidiEvent(e);reference.handleMidiEvent(e);
        }
        unsigned frames=std::min(partition[block++%partition.size()],total-pos);
        if(next<events.size()) frames=std::min(frames,events[next].sample-pos);
        assert(frames>0 && frames<=128);
        optimized.renderBlock(a.data(),frames);
        const auto optimizedHits = dsp::m81::sharedNoiseBlocks;
        reference.renderBlock(b.data(),frames);
        assert(dsp::m81::sharedNoiseBlocks == optimizedHits);
        if(!std::equal(a.begin(),a.begin()+frames*2,b.begin())) {
            std::fprintf(stderr,"M81 PCM mismatch model=%u fixture=%u sample=%u partition=%u\n",unsigned(model),fixture,pos,frames);
            assert(false);
        }
        const auto& av=optimized.getVoiceAllocator();const auto& bv=reference.getVoiceAllocator();
        assert(av.getActiveVoiceCount()==bv.getActiveVoiceCount());
        for(unsigned v=0;v<8;++v) {
            const auto& x=av.getVoice(v);const auto& y=bv.getVoice(v);
            assert(x.isActive()==y.isActive() && x.getAge()==y.getAge());
            assert(bits(x.getEstimatedEnergy())==bits(y.getEstimatedEnergy()));
            assert(bits(x.getLastSample())==bits(y.getLastSample()));
        }
        pos+=frames;
    }
#if POCKETPAN_COMMON_NOISE && POCKETPAN_ATTACK_VOICE_FASTPATH
    if (model == dsp::InstrumentModel::Pan && fixture == 2)
        assert(dsp::m81::sharedNoiseBlocks > 0);
#endif
}
int main() {
    for(unsigned m=0;m<10;++m) for(unsigned f=0;f<9;++f)
        for(const auto& p:std::vector<std::vector<unsigned>>{{128},{37},{1,79,13,128,37}})
            compare(static_cast<dsp::InstrumentModel>(m),f,p);
    std::puts("M81: all ten models, nine fixtures, three partitions: PCM and voice state exact (270 cases)");
}
