// Structural audit and bit-exact coefficient oracle for M8.
#include "dsp/synth_engine.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <array>
#include <cstdlib>
#include <new>
using namespace pocketpan;
static bool forbidAllocation=false;
void* operator new(size_t n) {
    assert(!forbidAllocation);if(void* p=std::malloc(n)) return p;throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,size_t) noexcept { std::free(p); }
template<size_t N> void verifyPacked(dsp::VoiceAllocator& allocator,
    const dsp::InstrumentModelConfig& config,const dsp::PreparedNoteTable& legacy) {
    static dsp::PackedPreparedNoteTable<N> packed;
    allocator.preparePreparedNoteTable(config,packed);
    assert(packed.ready && packed.guarded());
    const auto view=packed.view();
    for(const auto& old:legacy.entries) {
        dsp::PreparedNote restored;
        assert(view.load(old.midiNote,old.fundamentalFrequencyHz,restored));
        assert(std::memcmp(&old,&restored,sizeof(old))==0);
    }
    dsp::PreparedNote scratch;
    const auto& first=legacy.entries[0];
    assert(!view.load(23,first.fundamentalFrequencyHz,scratch));
    assert(!view.load(97,first.fundamentalFrequencyHz,scratch));
    assert(!view.load(first.midiNote,first.fundamentalFrequencyHz+1,scratch));
    packed.guardBefore^=1;
    assert(!packed.guarded() && !view.load(first.midiNote,first.fundamentalFrequencyHz,scratch));
    packed.guardBefore^=1;packed.guardAfter^=1;
    assert(!packed.guarded() && !view.load(first.midiNote,first.fundamentalFrequencyHz,scratch));
    packed.guardAfter^=1;
    packed.reset();assert(!view.load(first.midiNote,first.fundamentalFrequencyHz,scratch));
}
int main() {
    static dsp::VoiceAllocator allocator;
    static dsp::PreparedNoteTable legacy;
    allocator.init(48000);
    unsigned totalUnused=0,totalInactive=0;
    for(unsigned m=0;m<9;++m) {
        const auto& config=dsp::getInstrumentModelConfig(static_cast<dsp::InstrumentModel>(m));
        allocator.preparePreparedNoteTable(config,legacy);
        switch(config.modalPreset->modeCount) {
            case 5: verifyPacked<5>(allocator,config,legacy);break;
            case 6: verifyPacked<6>(allocator,config,legacy);break;
            case 7: verifyPacked<7>(allocator,config,legacy);break;
            case 8: verifyPacked<8>(allocator,config,legacy);break;
            case 10: verifyPacked<10>(allocator,config,legacy);break;
            default: assert(false);
        }
        unsigned unused=0,inactive=0;
        for(const auto& n:legacy.entries) {
            unused+=dsp::kMaxModesPerVoice-n.modeCount;
            for(unsigned k=0;k<n.modeCount;++k) inactive+=!(n.activeMask&(1u<<k));
        }
        totalUnused+=unused;totalInactive+=inactive;
        std::cout<<dsp::instrumentModelName(config.id)<<" modes="<<unsigned(legacy.entries[0].modeCount)
            <<" unused_slots="<<unused<<" inactive_slots="<<inactive<<'\n';
    }
    std::cout<<"unused_tuple_bytes="<<totalUnused*12<<" inactive_tuple_bytes="<<totalInactive*12
        <<" duplicated_frequency_bytes="<<8*73*4<<" legacy_bytes="<<9*sizeof(legacy)<<'\n';
    static_assert(dsp::kPackedPreparedNoteTotalBytes==60552);
    std::cout<<"packed_bytes="<<dsp::kPackedPreparedNoteTotalBytes<<'\n';
    static dsp::SynthEngine engine;
    static std::array<std::array<int32_t,256>,10> cold;
    std::array<int32_t,256> pcm{};
    const char* names[]={"PAN","BELL","TONGUE","BOWL","KALIMBA","GLASS","MARIMBA","VIBRAPHONE","MBIRA","UDU"};
    midi::MidiEvent strike{};strike.type=midi::MidiEventType::NoteOn;strike.data1=60;strike.data2=90;
    for(unsigned m=0;m<10;++m) {
        engine.init(48000);engine.setInstrumentModel(static_cast<dsp::InstrumentModel>(m));
        engine.handleMidiEvent(strike);engine.renderBlock(cold[m].data(),128);
    }
    forbidAllocation=true;
    for(unsigned i=0;i<1000;++i) {
        const auto model=static_cast<dsp::InstrumentModel>(i%10);
        assert(std::strcmp(dsp::instrumentModelName(model),names[i%10])==0);
        engine.setInstrumentModel(model);engine.handleMidiEvent(strike);engine.renderBlock(pcm.data(),128);
        assert(pcm==cold[i%10] && engine.verifyPreparedNoteCanaries());
    }
    forbidAllocation=false;
    std::cout<<"657 exact coefficient records; corruption/fallback guards; 1000 exact allocation-free switches PASS\n";
}
