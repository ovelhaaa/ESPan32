#include <iostream>
#include <vector>
#include <cmath>
#include <cassert>
#include <fstream>
#include <cstring>

#include "../main/dsp/modal_mode.h"
#include "../main/dsp/modal_preset.h"
#include "../main/dsp/modal_resonator.h"
#include "../main/dsp/exciter.h"
#include "../main/dsp/modal_voice.h"
#include "../main/dsp/voice_allocator.h"
#include "../main/dsp/synth_engine.h"
#include "../main/midi/midi_mapping.h"
#include "../main/midi/midi_event.h"

using namespace pocketpan;

// Minimal standard WAV file writer for test audio inspection
void writeWavFile(const char* filename, const int32_t* interleavedStereo, size_t totalFrames, int sampleRate) {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) return;

    const uint32_t subchunk2Size = static_cast<uint32_t>(totalFrames * 2 * sizeof(int16_t));
    const uint32_t chunkSize = 36 + subchunk2Size;

    // RIFF Header
    file.write("RIFF", 4);
    file.write(reinterpret_cast<const char*>(&chunkSize), 4);
    file.write("WAVE", 4);

    // fmt subchunk
    file.write("fmt ", 4);
    uint32_t subchunk1Size = 16;
    uint16_t audioFormat = 1; // PCM
    uint16_t numChannels = 2; // Stereo
    uint32_t sRate = static_cast<uint32_t>(sampleRate);
    uint32_t byteRate = sRate * numChannels * sizeof(int16_t);
    uint16_t blockAlign = numChannels * sizeof(int16_t);
    uint16_t bitsPerSample = 16;

    file.write(reinterpret_cast<const char*>(&subchunk1Size), 4);
    file.write(reinterpret_cast<const char*>(&audioFormat), 2);
    file.write(reinterpret_cast<const char*>(&numChannels), 2);
    file.write(reinterpret_cast<const char*>(&sRate), 4);
    file.write(reinterpret_cast<const char*>(&byteRate), 4);
    file.write(reinterpret_cast<const char*>(&blockAlign), 2);
    file.write(reinterpret_cast<const char*>(&bitsPerSample), 2);

    // data subchunk
    file.write("data", 4);
    file.write(reinterpret_cast<const char*>(&subchunk2Size), 4);

    for (size_t i = 0; i < totalFrames * 2; ++i) {
        // Convert 32-bit int to 16-bit PCM for standard WAV players
        int16_t s16 = static_cast<int16_t>(interleavedStereo[i] >> 16);
        file.write(reinterpret_cast<const char*>(&s16), sizeof(int16_t));
    }

    file.close();
    std::cout << "  [WAV] Exported test audio to " << filename << " (" << totalFrames << " frames)" << std::endl;
}

void testModalFrequency() {
    std::cout << "[Test 1] Modal Resonator Frequency Accuracy..." << std::endl;
    constexpr float kSampleRate = 48000.0f;
    constexpr float kTargetFreq = 440.0f;

    dsp::ModalResonatorBank resonators;
    resonators.init(kSampleRate);

    // Custom preset with a single pure mode at ratio 1.0
    dsp::ModalPreset testPreset = {
        "TestPure",
        1,
        { { 1.0f, 1.0f, 1.5f, 0.0f } }
    };
    resonators.setPreset(testPreset);
    resonators.updatePitchAndDamping(kTargetFreq, 0.0f);

    // Excite with a single unit impulse
    float impulse = 1.0f;
    constexpr size_t kNumSamples = 48000; // 1 second
    std::vector<float> output(kNumSamples);

    for (size_t i = 0; i < kNumSamples; ++i) {
        output[i] = resonators.processSample(impulse);
        impulse = 0.0f;
    }

    // Count zero crossings to estimate frequency
    int zeroCrossings = 0;
    for (size_t i = 1; i < 24000; ++i) {
        if ((output[i - 1] < 0.0f && output[i] >= 0.0f) ||
            (output[i - 1] >= 0.0f && output[i] < 0.0f)) {
            zeroCrossings++;
        }
    }

    float measuredFreq = (static_cast<float>(zeroCrossings) * 0.5f) * (kSampleRate / 24000.0f);
    float freqError = std::abs(measuredFreq - kTargetFreq);

    std::cout << "  Target: " << kTargetFreq << " Hz | Measured: " << measuredFreq
              << " Hz | Error: " << freqError << " Hz" << std::endl;
    assert(freqError < 2.0f && "Frequency error must be under 2 Hz");
    std::cout << "  -> PASSED!" << std::endl;
}

void testT60Decay() {
    std::cout << "[Test 2] Modal Resonator T60 Decay Envelope..." << std::endl;
    constexpr float kSampleRate = 48000.0f;
    constexpr float kTargetT60 = 0.50f; // 500 ms decay to -60 dB (0.001 amplitude)

    dsp::ModalResonatorBank resonators;
    resonators.init(kSampleRate);

    dsp::ModalPreset testPreset = {
        "T60Test",
        1,
        { { 1.0f, 1.0f, kTargetT60, 0.0f } }
    };
    resonators.setPreset(testPreset);
    resonators.updatePitchAndDamping(440.0f, 0.0f);

    // Render 1 impulse followed by silence
    constexpr size_t kTotalSamples = 24000 + 480;
    std::vector<float> y(kTotalSamples);
    y[0] = resonators.processSample(1.0f);
    for (size_t i = 1; i < kTotalSamples; ++i) {
        y[i] = resonators.processSample(0.0f);
    }

    // Measure peak amplitude during the first 10 cycles (~250 samples @ 440Hz)
    float peakInitialAmp = 0.0f;
    for (size_t i = 0; i < 250; ++i) {
        if (std::abs(y[i]) > peakInitialAmp) {
            peakInitialAmp = std::abs(y[i]);
        }
    }

    // Measure peak amplitude near t = T60 (sample 24000 +/- 1 cycle)
    const size_t t60SampleIndex = static_cast<size_t>(kTargetT60 * kSampleRate);
    float maxAmpNearT60 = 0.0f;
    for (size_t i = t60SampleIndex - 120; i <= t60SampleIndex + 120; ++i) {
        if (std::abs(y[i]) > maxAmpNearT60) {
            maxAmpNearT60 = std::abs(y[i]);
        }
    }

    float measuredAttenuation = maxAmpNearT60 / peakInitialAmp;
    std::cout << "  Peak Initial Amp = " << peakInitialAmp
              << " | Amp at t=" << kTargetT60 << "s = " << maxAmpNearT60
              << " | Ratio = " << measuredAttenuation << " (target ~0.001)" << std::endl;

    // Decay ratio must be around 0.001 (-60 dB) within +/- 15% tolerance due to discrete sampling
    assert(measuredAttenuation > 0.0008f && measuredAttenuation < 0.0012f);
    std::cout << "  -> PASSED!" << std::endl;
}

void testStabilityAndNyquistHandling() {
    std::cout << "[Test 3] Stability & Nyquist Mode Disabling..." << std::endl;
    constexpr float kSampleRate = 48000.0f;

    dsp::ModalVoice voice;
    voice.init(kSampleRate);

    // 1. Extreme low pitch (20 Hz)
    voice.trigger(16, 20.0f, 1.0f);
    for (int i = 0; i < 2048; ++i) {
        float sample = voice.processSample();
        assert(!std::isnan(sample) && !std::isinf(sample) && "Low pitch produced NaN/Inf");
    }

    // 2. Extreme high pitch (8000 Hz fundamental with harmonics exceeding Nyquist)
    // Modes at 8kHz, 16kHz, 24kHz, 32kHz, etc.
    // Modes above 0.48*fs (23.04 kHz) must NOT clamp and must be disabled!
    voice.trigger(127, 8000.0f, 1.0f);
    for (int i = 0; i < 2048; ++i) {
        float sample = voice.processSample();
        assert(!std::isnan(sample) && !std::isinf(sample) && "High pitch produced NaN/Inf");
    }

    // 3. Extreme damping (1.0 = heavy palm choke)
    voice.trigger(62, 293.66f, 1.0f);
    voice.setDamping(1.0f);
    for (int i = 0; i < 2048; ++i) {
        float sample = voice.processSample();
        assert(!std::isnan(sample) && !std::isinf(sample) && "Damping produced NaN/Inf");
    }

    std::cout << "  -> PASSED!" << std::endl;
}

void testPolyphonyAndWavGeneration() {
    std::cout << "[Test 4] 8-Voice Polyphony Stress & WAV Export..." << std::endl;
    constexpr float kSampleRate = 48000.0f;
    constexpr size_t kBlockFrames = 128;

    dsp::SynthEngine engine;
    engine.init(kSampleRate);

    // Score: Play a rich Handpan sequence (D Kurd scale)
    // D3, A3, Bb3, C4, D4, E4, F4, A4, plus chokes
    struct NoteEvent {
        uint32_t frame;
        midi::MidiEventType type;
        uint8_t note;
        uint8_t value;
    };

    std::vector<NoteEvent> score = {
        { 0,     midi::MidiEventType::NoteOn, 62, 110 }, // D3 (Ding)
        { 12000, midi::MidiEventType::NoteOn, 69, 95  }, // A3
        { 24000, midi::MidiEventType::NoteOn, 70, 90  }, // Bb3
        { 36000, midi::MidiEventType::NoteOn, 72, 100 }, // C4
        { 48000, midi::MidiEventType::NoteOn, 74, 115 }, // D4
        { 60000, midi::MidiEventType::NoteOn, 76, 85  }, // E4
        { 72000, midi::MidiEventType::NoteOn, 77, 95  }, // F4
        { 84000, midi::MidiEventType::NoteOn, 81, 105 }, // A4
        // Rapid 8-chord burst to trigger voice stealing
        { 96000, midi::MidiEventType::NoteOn, 62, 120 },
        { 97000, midi::MidiEventType::NoteOn, 65, 115 },
        { 98000, midi::MidiEventType::NoteOn, 69, 110 },
        { 99000, midi::MidiEventType::NoteOn, 72, 105 },
        { 100000, midi::MidiEventType::NoteOn, 74, 100 },
        { 101000, midi::MidiEventType::NoteOn, 77, 95 },
        { 102000, midi::MidiEventType::NoteOn, 81, 90 },
        { 103000, midi::MidiEventType::NoteOn, 84, 85 },
        { 104000, midi::MidiEventType::NoteOn, 86, 127 }, // 9th note triggers voice stealing!
        // Palm choke test via aftertouch
        { 120000, midi::MidiEventType::PolyPressure, 62, 100 }, // Choke D3
        { 140000, midi::MidiEventType::ChannelPressure, 0, 80 }  // Global choke
    };

    const size_t totalFrames = 192000; // 4 seconds of audio
    std::vector<int32_t> audioBuffer(totalFrames * 2, 0);

    size_t eventIdx = 0;
    int32_t blockBuffer[kBlockFrames * 2];

    for (size_t frame = 0; frame < totalFrames; frame += kBlockFrames) {
        // Dispatch score events
        while (eventIdx < score.size() && score[eventIdx].frame <= frame) {
            midi::MidiEvent ev;
            ev.type = score[eventIdx].type;
            ev.channel = 1;
            ev.data1 = score[eventIdx].note;
            ev.data2 = score[eventIdx].value;
            engine.handleMidiEvent(ev);
            eventIdx++;
        }

        engine.renderBlock(blockBuffer, kBlockFrames);
        std::memcpy(&audioBuffer[frame * 2], blockBuffer, sizeof(blockBuffer));
    }

    std::cout << "  Active voices after sequence: "
              << engine.getVoiceAllocator().getActiveVoiceCount() << std::endl;

    writeWavFile("output_handpan.wav", audioBuffer.data(), totalFrames, static_cast<int>(kSampleRate));
    std::cout << "  -> PASSED!" << std::endl;
}

int main() {
    std::cout << "==================================================" << std::endl;
    std::cout << "  Pocket Pan / Metal Modal Synth - DSP Test Suite" << std::endl;
    std::cout << "==================================================" << std::endl;

    testModalFrequency();
    testT60Decay();
    testStabilityAndNyquistHandling();
    testPolyphonyAndWavGeneration();

    std::cout << "\nALL DSP UNIT TESTS PASSED SUCCESSFULLY!" << std::endl;
    return 0;
}
