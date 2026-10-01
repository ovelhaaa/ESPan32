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
    // Extraction leaves the modal sum bit-identical, including Nyquist-pruned
    // banks, safety modes, and reset. A bank excited only in mode 0 is an independent oracle.
    const auto& config = dsp::getInstrumentModelConfig(dsp::InstrumentModel::Vibraphone);
    for (bool safety : {false, true}) for (float hz : {146.83f, 293.66f, 440.0f, 14000.0f}) {
        dsp::ModalResonatorBank full, reference, fundamental;
        for (auto* bank : {&full, &reference, &fundamental}) {
            bank->init(48000);
            auto rc = config.resonator; rc.internalSafetySaturation = safety;
            bank->setConfig(rc);
        }
        full.setPreset(*config.modalPreset); reference.setPreset(*config.modalPreset);
        fundamental.setPreset(*config.modalPreset);
        const float coupling[dsp::kMaxModesPerVoice] = {1.0f};
        fundamental.setExcitationCoupling(coupling, dsp::kMaxModesPerVoice);
        for (auto* bank : {&full, &reference, &fundamental}) bank->updatePitchAndDamping(hz, 0);
        for (unsigned i=0; i<8192; ++i) {
            const float excitation = i % 997 == 0 ? .0005f : 0.0f;
            assert(full.processSample(excitation) == reference.processSampleReference(excitation));
            assert(full.fundamentalSample() == fundamental.processSample(excitation));
        }
        full.reset(); assert(full.fundamentalSample() == 0);
    }
    // Independent sample-by-sample oracle for the stable-eight specialization,
    // including exciter -> sustain, and exact modal state after bypassing tube.
    {
        dsp::VoiceAllocator fused;
        fused.init(48000); fused.setModelConfig(config);
        std::array<dsp::ModalVoice,8> reference;
        for(unsigned v=0;v<8;++v) {
            const uint8_t n=uint8_t(50+v);
            const float hz=146.8324f*std::pow(2.0f,float(v)/12);
            fused.noteOn(n,.8f,hz);
            reference[v].init(48000); reference[v].setModelConfig(config);
            reference[v].trigger(n,hz,.8f);
        }
        float output[128], response[128];
        for(unsigned b=0;b<400;++b) {
            for(unsigned i=0;i<128;++i) {
                const float open=float((b*128+i)%997)/997;
                response[i]=.10f+.90f*open;
            }
            fused.renderVibraphoneBlock(output,128,response,.75f,1.0f);
            for(unsigned i=0;i<128;++i) {
                float dry=0,direct=0;
                for(unsigned v=0;v<8;++v) {
                    dry+=reference[v].processSample();
                    const float tap=reference[v].fundamentalSample();
                    direct+=tap;
                }
                assert(output[i]==dry+direct*.75f*1.0f*response[i]);
            }
        }
        fused.renderBlock(output,128);
        for(unsigned i=0;i<128;++i) {
            float dry=0; for(auto& voice:reference) dry+=voice.processSample();
            assert(output[i]==dry);
        }
    }
    // Force lifetime transitions inside non-aligned segments. This checks the
    // active-mask dispatch after voices die, including the last dying sample.
    for (unsigned count : {1u,4u,8u}) {
        auto shortConfig=config;
        shortConfig.voicing.silenceThreshold=1.0f;
        dsp::VoiceAllocator fused;
        fused.init(48000); fused.setModelConfig(shortConfig);
        std::array<dsp::ModalVoice,8> reference;
        for(unsigned v=0;v<count;++v) {
            const float hz=293.66f+v*17;
            fused.noteOn(uint8_t(62+v),.55f,hz);
            reference[v].init(48000); reference[v].setModelConfig(shortConfig);
            reference[v].trigger(uint8_t(62+v),hz,.55f);
        }
        float output[37], response[37];
        std::fill(response,response+37,.45f);
        for(unsigned b=0;b<200;++b) {
            fused.renderVibraphoneBlock(output,37,response,.75f,1.0f);
            for(unsigned i=0;i<37;++i) {
                float dry=0,direct=0;
                for(unsigned v=0;v<count;++v) if(reference[v].isActive()) {
                    dry+=reference[v].processSample();
                    direct+=reference[v].fundamentalSample();
                }
                assert(output[i]==dry+direct*.75f*1.0f*response[i]);
            }
        }
        assert(fused.getActiveVoiceCount()==0);
        for(unsigned v=0;v<count;++v) assert(!reference[v].isActive());
    }
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
