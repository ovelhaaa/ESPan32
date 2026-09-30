#include "dsp/synth_engine.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>

using namespace pocketpan;
static size_t allocations = 0;
void* operator new(size_t n) {
    ++allocations;
    if (void* p = std::malloc(n)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, size_t) noexcept { ::operator delete(p); }

static void strike(dsp::SynthEngine& e, uint8_t note) {
    midi::MidiEvent event{};
    event.type = midi::MidiEventType::NoteOn;
    event.data1 = note; event.data2 = 90;
    e.handleMidiEvent(event);
}

int main() {
    dsp::SynthEngine engine;
    engine.init(48000);
    int32_t block[256]{};
    const size_t before = allocations;
    for (unsigned model=0;model<unsigned(dsp::InstrumentModel::Count);++model) {
        engine.setInstrumentModel(static_cast<dsp::InstrumentModel>(model));
        strike(engine,62);
        for (unsigned i=0;i<64;++i) engine.renderBlock(block,128);
    }
    assert(allocations == before);

    // Stable voice counts make partitioning irrelevant to headroom targets.
    std::vector<int32_t> whole(8192*2), split(8192*2), dry(8192*2);
    auto render = [&](std::vector<int32_t>& pcm, size_t frames, bool motor, float depth) {
        engine.setInstrumentModel(dsp::InstrumentModel::Vibraphone);
        engine.setVibraphoneMotor(motor,4.5f,depth);
        strike(engine,62); strike(engine,65);
        for (size_t i=0;i<8192;i+=frames)
            engine.renderBlock(pcm.data()+i*2,std::min(frames,8192-i));
    };
    render(whole,128,true,.32f);
    render(split,37,true,.32f);
    assert(whole==split);
    render(dry,128,false,0);
    render(split,128,true,0);
    assert(dry==split);
    assert(dry!=whole);

    // Paired trials, reversed order; init/coefficient work excluded. Keep voices
    // ringing by restriking between timed groups (outside the timed section).
    std::array<double,7> off{}, on{};
    volatile int32_t sink=0;
    auto bench=[&](bool enabled) {
        engine.setInstrumentModel(dsp::InstrumentModel::Vibraphone);
        engine.setVibraphoneMotor(enabled);
        double seconds=0;
        for (unsigned g=0;g<100;++g) {
            engine.reset(); strike(engine,62);
            const auto start=std::chrono::steady_clock::now();
            for (unsigned b=0;b<128;++b) engine.renderBlock(block,128);
            seconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
            sink=block[0];
        }
        return seconds*1e6/12800;
    };
    for (unsigned i=0;i<7;++i) {
        if (i%2) {on[i]=bench(true); off[i]=bench(false);}
        else {off[i]=bench(false); on[i]=bench(true);}
    }
    std::sort(off.begin(),off.end()); std::sort(on.begin(),on.end());
    std::cout << "VIBRAPHONE host median us/block: motor_off=" << off[3]
              << " motor_on=" << on[3] << " delta=" << on[3]-off[3]
              << " (host timing only; 7 paired trials, 12800 blocks each)\n"
              << "No realtime allocations; motor partition/depth-zero checks passed.\n";
    (void)sink;
}
