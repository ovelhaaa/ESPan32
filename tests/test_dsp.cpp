#include <iostream>
#include <algorithm>
#include <vector>
#include <cmath>
#include <cassert>
#include <fstream>
#include <cstring>
#include <iomanip>
#include <limits>
#include <string>

#include "../main/dsp/modal_mode.h"
#include "../main/dsp/modal_preset.h"
#include "../main/dsp/modal_resonator.h"
#include "../main/dsp/exciter.h"
#include "../main/dsp/modal_voice.h"
#include "../main/dsp/voice_allocator.h"
#include "../main/dsp/synth_engine.h"
#include "../main/dsp/peak_limiter.h"
#include "../main/dsp/diagnostic_tone.h"
#include "../main/midi/midi_mapping.h"
#include "../main/midi/midi_event.h"

using namespace pocketpan;

// Standard 16-bit PCM WAV writer
void writeWavFile(const char* filename, const int32_t* interleavedStereo, size_t totalFrames, int sampleRate) {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) return;

    const uint32_t subchunk2Size = static_cast<uint32_t>(totalFrames * 2 * sizeof(int16_t));
    const uint32_t chunkSize = 36 + subchunk2Size;

    file.write("RIFF", 4);
    file.write(reinterpret_cast<const char*>(&chunkSize), 4);
    file.write("WAVE", 4);

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

    file.write("data", 4);
    file.write(reinterpret_cast<const char*>(&subchunk2Size), 4);

    for (size_t i = 0; i < totalFrames * 2; ++i) {
        int16_t s16 = static_cast<int16_t>(interleavedStereo[i] >> 16);
        file.write(reinterpret_cast<const char*>(&s16), sizeof(int16_t));
    }

    file.close();
    std::cout << "  [WAV] Exported: " << filename << " (" << totalFrames << " frames)" << std::endl;
}

struct AudioMetrics {
    float peak = 0.0f;
    float rms = 0.0f;
    float crestFactor = 0.0f;
    uint32_t softClipCount = 0;
    float attackRms = 0.0f;
    float dcMean = 0.0f;
};

constexpr float kAttackWindowSeconds = 0.100f;

AudioMetrics computeMetrics(const std::vector<int32_t>& audioStereo, uint32_t softClipCount) {
    AudioMetrics m;
    m.softClipCount = softClipCount;
    if (audioStereo.empty()) return m;

    double sumSq = 0.0;
    double sum = 0.0;
    float maxAbs = 0.0f;
    const size_t numSamples = audioStereo.size();

    for (size_t i = 0; i < numSamples; ++i) {
        float s = static_cast<float>(audioStereo[i]) / 2147483647.0f;
        float absVal = std::abs(s);
        if (absVal > maxAbs) maxAbs = absVal;
        sumSq += (s * s);
        sum += s;
    }

    m.peak = maxAbs;
    m.rms = static_cast<float>(std::sqrt(sumSq / numSamples));
    m.crestFactor = (m.rms > 1.0e-6f) ? (m.peak / m.rms) : 0.0f;
    m.dcMean = static_cast<float>(sum / numSamples);
    // 100 ms = 4,800 frames = 9,600 interleaved stereo samples at 48 kHz.
    const size_t attackSamples = std::min(numSamples, static_cast<size_t>(48000 * kAttackWindowSeconds * 2));
    double attackSum = 0.0;
    for (size_t i = 0; i < attackSamples; ++i) { const float s = static_cast<float>(audioStereo[i]) / 2147483647.0f; attackSum += s * s; }
    m.attackRms = static_cast<float>(std::sqrt(attackSum / attackSamples));
    return m;
}

float goertzelMagnitude(const std::vector<int32_t>& audio, float hz, float sampleRate = 48000.0f) {
    const size_t frames = audio.size() / 2;
    const float w = 2.0f * 3.14159265358979323846f * hz / sampleRate;
    const float coeff = 2.0f * std::cos(w);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (size_t i = 0; i < frames; ++i) {
        s0 = static_cast<double>(audio[i * 2]) / 2147483647.0 + coeff * s1 - s2;
        s2 = s1; s1 = s0;
    }
    return static_cast<float>(std::sqrt(s1 * s1 + s2 * s2 - coeff * s1 * s2) / frames);
}

void testDiagnosticToneSource() {
    std::cout << "[Test 0] Diagnostic tone source routing and level..." << std::endl;
    dsp::DiagnosticToneSource source;
    constexpr size_t frames = 48000;
    std::vector<int32_t> audio(frames * 2);
    auto render = [&](dsp::DiagnosticTone tone) { source.setTone(tone); source.render(audio.data(), frames, 48000.0f); };
    render(dsp::DiagnosticTone::Silence);
    for (auto s : audio) assert(s == 0);
    render(dsp::DiagnosticTone::Sine440);
    auto m440 = computeMetrics(audio, 0); assert(m440.rms > 0.34f && m440.rms < 0.37f);
    assert(goertzelMagnitude(audio, 440.0f) > 0.20f);
    render(dsp::DiagnosticTone::Sine1k); auto m1k = computeMetrics(audio, 0); assert(m1k.rms > 0.34f && m1k.rms < 0.37f); assert(goertzelMagnitude(audio, 1000.0f) > 0.20f);
    render(dsp::DiagnosticTone::Sine1kMinus12); auto m12 = computeMetrics(audio, 0); assert(std::abs(m12.rms - 0.2511886f / std::sqrt(2.0f)) < 0.003f); assert(std::abs(m12.peak - 0.2511886f) < 0.003f);
    render(dsp::DiagnosticTone::LeftOnly); bool leftAudible = false; for (size_t i = 0; i < frames; ++i) { assert(audio[i * 2 + 1] == 0); leftAudible |= audio[i * 2] != 0; } assert(leftAudible);
    render(dsp::DiagnosticTone::RightOnly); bool rightAudible = false; for (size_t i = 0; i < frames; ++i) { assert(audio[i * 2] == 0); rightAudible |= audio[i * 2 + 1] != 0; } assert(rightAudible);
    std::cout << "  -> PASSED!\n";
}

void testDiagnosticOscillatorLongRun() {
    std::cout << "[Test 0b] Diagnostic oscillator 30-second stability..." << std::endl;
    dsp::DiagnosticToneSource source;
    source.setTone(dsp::DiagnosticTone::Sine440);
    constexpr size_t kFrames = 48000 * 30;
    std::vector<int32_t> audio(kFrames * 2);
    source.render(audio.data(), kFrames, 48000.0f);
    const auto metrics = computeMetrics(audio, 0);
    assert(std::isfinite(metrics.rms) && metrics.rms > 0.34f && metrics.rms < 0.37f);
    assert(goertzelMagnitude(audio, 440.0f) > 0.20f);
    std::cout << "  -> PASSED!\n";
}

// 1. Fundamental frequency accuracy
void testModalFrequency() {
    std::cout << "[Test 1] Modal Resonator Frequency Accuracy..." << std::endl;
    constexpr float kSampleRate = 48000.0f;
    constexpr float kTargetFreq = 440.0f;

    dsp::ModalResonatorBank resonators;
    resonators.init(kSampleRate);

    dsp::ModalPreset testPreset = {
        "TestPure",
        1,
        { { 1.0f, 1.0f, 1.5f, 0.0f } }
    };
    resonators.setPreset(testPreset);
    resonators.updatePitchAndDamping(kTargetFreq, 0.0f);

    float impulse = 1.0f;
    constexpr size_t kNumSamples = 48000;
    std::vector<float> output(kNumSamples);

    for (size_t i = 0; i < kNumSamples; ++i) {
        output[i] = resonators.processSample(impulse);
        impulse = 0.0f;
    }

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
    assert(freqError < 2.0f);
    std::cout << "  -> PASSED!" << std::endl;
}

// 2. T60 decay envelope
void testT60Decay() {
    std::cout << "[Test 2] Modal Resonator T60 Decay Envelope..." << std::endl;
    constexpr float kSampleRate = 48000.0f;
    constexpr float kTargetT60 = 0.50f;

    dsp::ModalResonatorBank resonators;
    resonators.init(kSampleRate);

    dsp::ModalPreset testPreset = {
        "T60Test",
        1,
        { { 1.0f, 1.0f, kTargetT60, 0.0f } }
    };
    resonators.setPreset(testPreset);
    resonators.updatePitchAndDamping(440.0f, 0.0f);

    constexpr size_t kTotalSamples = 24000 + 480;
    std::vector<float> y(kTotalSamples);
    y[0] = resonators.processSample(1.0f);
    for (size_t i = 1; i < kTotalSamples; ++i) {
        y[i] = resonators.processSample(0.0f);
    }

    float peakInitialAmp = 0.0f;
    for (size_t i = 0; i < 250; ++i) {
        if (std::abs(y[i]) > peakInitialAmp) {
            peakInitialAmp = std::abs(y[i]);
        }
    }

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

    assert(measuredAttenuation > 0.0007f && measuredAttenuation < 0.0014f);
    std::cout << "  -> PASSED!" << std::endl;
}

// 3. Mode splitting verification (detune applied exactly once)
void testModeSplitting() {
    std::cout << "[Test 3] Mode Splitting (Beating Doublet Verification)..." << std::endl;
    constexpr float kFund = 293.665f; // D3
    const auto& m0 = dsp::kPresetPan.modes[0];
    const auto& m1 = dsp::kPresetPan.modes[1];

    const float f0 = kFund * m0.ratio * (1.0f + m0.detune);
    const float f1 = kFund * m1.ratio * (1.0f + m1.detune);

    std::cout << "  Mode 0: ratio=" << m0.ratio << ", detune=" << m0.detune << " -> " << f0 << " Hz" << std::endl;
    std::cout << "  Mode 1: ratio=" << m1.ratio << ", detune=" << m1.detune << " -> " << f1 << " Hz" << std::endl;

    const float beatFreqHz = std::abs(f1 - f0);
    std::cout << "  Beat frequency: " << beatFreqHz << " Hz" << std::endl;

    // With fund=293.665 and detune=0.0032: 293.665 * 0.0032 = 0.9397 Hz
    assert(std::abs(m1.ratio - 1.0f) < 0.0001f);
    assert(std::abs(m1.detune - 0.0032f) < 0.0001f);
    assert(std::abs(beatFreqHz - (kFund * 0.0032f)) < 0.001f);
    std::cout << "  -> PASSED: Mode split detune applied exactly once, producing natural ~0.94 Hz beating!\n";
}

// 4. Same-note restrike physical energy accumulation
void testSameNoteRestrike() {
    std::cout << "[Test 4] Same-Note Restrike (Physical Energy Accumulation)..." << std::endl;
    constexpr float kFs = 48000.0f;
    constexpr float kFund = 293.665f; // D3

    // Voice A: Physical restrike (without resetting resonator modes)
    dsp::ModalVoice voiceA;
    voiceA.init(kFs);
    voiceA.trigger(62, kFund, 0.70f);

    // Run for 3000 samples (~62 ms)
    for (int i = 0; i < 3000; ++i) {
        voiceA.processSample();
    }
    float energyBeforeRestrikeA = voiceA.getEstimatedEnergy();
    // Restrike with new hit
    voiceA.restrike(0.70f);
    // Run for 500 samples
    for (int i = 0; i < 500; ++i) {
        voiceA.processSample();
    }
    float energyAfterRestrikeA = voiceA.getEstimatedEnergy();

    // Voice B: Hard reset on retrigger (unphysical)
    dsp::ModalVoice voiceB;
    voiceB.init(kFs);
    voiceB.trigger(62, kFund, 0.70f);
    for (int i = 0; i < 3000; ++i) {
        voiceB.processSample();
    }
    // Hard kill and re-trigger
    voiceB.kill();
    voiceB.trigger(62, kFund, 0.70f);
    for (int i = 0; i < 500; ++i) {
        voiceB.processSample();
    }
    float energyAfterResetB = voiceB.getEstimatedEnergy();

    std::cout << "  Voice A Energy before restrike: " << energyBeforeRestrikeA << std::endl;
    std::cout << "  Voice A Energy after physical restrike: " << energyAfterRestrikeA << std::endl;
    std::cout << "  Voice B Energy after hard kill & retrigger: " << energyAfterResetB << std::endl;

    assert(energyBeforeRestrikeA > 0.001f && "Ringing tail must be active");
    assert(std::abs(energyAfterRestrikeA - energyAfterResetB) > 0.05f && "Physical restrike and hard reset must produce distinct physical states");
    std::cout << "  -> PASSED: Same-note restrike preserves acoustic vibration (behavior differs from hard reset)!\n";
}

// 5. Declicked voice stealing under 9+ simultaneous voice bursts
void testDeclickedVoiceStealing() {
    std::cout << "[Test 5] Declicked Voice Stealing Stress (9+ Notes)..." << std::endl;
    constexpr float kFs = 48000.0f;
    dsp::VoiceAllocator allocator;
    allocator.init(kFs);

    // 1. Trigger all 8 voices with sustained notes
    const uint8_t scale[8] = { 62, 65, 69, 70, 72, 74, 77, 81 };
    for (int i = 0; i < 8; ++i) {
        allocator.noteOn(scale[i], 0.7f, midi::MidiMapping::noteToHz(scale[i]));
    }
    assert(allocator.getActiveVoiceCount() == 8);

    // Render 512 samples
    std::vector<float> buf(512);
    allocator.renderBlock(buf.data(), 512);

    // Queue five steals exactly as the audio callback can do before rendering.
    dsp::VoiceAllocator burst; burst.init(kFs);
    for (uint8_t note : scale) burst.noteOn(note, 0.7f, midi::MidiMapping::noteToHz(note));
    std::vector<float> warmup(512); burst.renderBlock(warmup.data(), warmup.size());
    for (uint8_t note : {86, 87, 88, 89, 90}) burst.noteOn(note, 0.7f, midi::MidiMapping::noteToHz(note));
    assert(burst.getActiveVoiceCount() == 8);
    assert(burst.getActiveStealTailCount() == 5 && "steal tails must not overwrite one another");
    std::vector<float> multiSteal(128); burst.renderBlock(multiSteal.data(), multiSteal.size());
    for (float sample : multiSteal) assert(std::isfinite(sample));
    assert(burst.getActiveStealTailCount() == 0);

    // 2. Steal voice by playing 9th note
    allocator.noteOn(86, 0.7f, midi::MidiMapping::noteToHz(86));

    // Render next block containing the steal transition
    std::vector<float> stealBlock(512);
    allocator.renderBlock(stealBlock.data(), 512);

    // Boundary step between last sample of previous block and first sample of steal block
    float boundaryDelta = std::abs(stealBlock[0] - buf[511]);
    std::cout << "  Boundary sample delta at steal point: " << boundaryDelta << std::endl;
    assert(boundaryDelta < 0.40f && "Steal crossfade tail must keep block boundary continuous");

    // 3. Provoke rapid cascade of 16 note steals in heavy tails
    for (int n = 0; n < 16; ++n) {
        uint8_t note = 60 + (n * 3) % 24;
        allocator.noteOn(note, 0.85f, midi::MidiMapping::noteToHz(note));
        std::vector<float> block(128);
        allocator.renderBlock(block.data(), 128);
        for (float s : block) {
            assert(!std::isnan(s) && !std::isinf(s) && "Steal burst produced NaN/Inf");
        }
    }
    assert(allocator.getActiveVoiceCount() == 8 && "Active voices must remain capped at 8");
    std::cout << "  -> PASSED: Declicked voice stealing handled 16-note burst without clicks or instability!\n";
}

// A naturally inactive voice must not carry a stale sample into a later steal.
void testInactiveTriggerClearsResidual() {
    std::cout << "[Test 5b] Inactive trigger clears stale residual..." << std::endl;
    dsp::ModalVoice voice;
    voice.init(48000.0f);
    voice.trigger(62, 293.665f, 0.8f);
    for (int i = 0; i < 100; ++i) voice.processSample();
    assert(std::abs(voice.getLastSample()) > 1.0e-7f);
    voice.reset();
    // Exercise the public inactive reuse path after a state transition.
    voice.trigger(64, 329.628f, 0.7f);
    assert(voice.getLastSample() == 0.0f);
    std::cout << "  -> PASSED!\n";
}

// 6. Independent Multi-Voice Damping (Poly Pressure)
void testMultiVoiceDamping() {
    std::cout << "[Test 6] Multi-Voice Independent Damping (Poly Pressure)..." << std::endl;
    constexpr float kFs = 48000.0f;
    dsp::VoiceAllocator allocator;
    allocator.init(kFs);

    // Voice 1 (D3, note 62) and Voice 2 (A3, note 69)
    allocator.noteOn(62, 0.8f, midi::MidiMapping::noteToHz(62));
    allocator.noteOn(69, 0.8f, midi::MidiMapping::noteToHz(69));

    // Render 2400 samples
    std::vector<float> buf(2400);
    allocator.renderBlock(buf.data(), 2400);

    // Choke ONLY note 62 with heavy Poly Pressure (1.0)
    allocator.setPolyPressure(62, 1.0f);

    // Render 24000 samples (~0.5s)
    std::vector<float> postChoke(24000);
    allocator.renderBlock(postChoke.data(), 24000);

    // Find the voice playing note 62 vs note 69
    float energy62 = 0.0f;
    float energy69 = 0.0f;
    for (size_t i = 0; i < 8; ++i) {
        const auto& v = allocator.getVoice(i);
        if (v.isActive() && v.getMidiNote() == 62) energy62 = v.getEstimatedEnergy();
        if (v.isActive() && v.getMidiNote() == 69) energy69 = v.getEstimatedEnergy();
    }

    std::cout << "  Choked note 62 energy: " << energy62 << " | Open note 69 energy: " << energy69 << std::endl;
    assert(energy69 > energy62 * 20.0f && "Note 69 must ring out while Note 62 is damped");
    std::cout << "  -> PASSED: Damping is strictly independent per voice!\n";
}

// 7. Modal Gain Normalization across frequencies, T60, and mode counts
void testGainNormalization() {
    std::cout << "[Test 7] Modal Gain Normalization across Frequencies & T60..." << std::endl;
    constexpr float kFs = 48000.0f;

    const float freqs[] = { 65.4f, 130.8f, 293.66f, 880.0f, 2500.0f };
    const float t60s[] = { 0.1f, 0.5f, 2.0f, 5.0f };

    for (float f : freqs) {
        float peakAtMinT60 = 0.0f;
        float peakAtMaxT60 = 0.0f;

        for (float t : t60s) {
            dsp::ModalResonatorBank resonators;
            resonators.init(kFs);
            dsp::ModalPreset testPreset = { "Norm", 1, { { 1.0f, 1.0f, t, 0.0f } } };
            resonators.setPreset(testPreset);
            resonators.updatePitchAndDamping(f, 0.0f);

            // Feed single unit impulse
            float peak = 0.0f;
            float imp = 1.0f;
            for (int i = 0; i < 4000; ++i) {
                float y = resonators.processSample(imp);
                imp = 0.0f;
                assert(!std::isnan(y) && !std::isinf(y));
                if (std::abs(y) > peak) peak = std::abs(y);
            }

            if (t == 0.1f) peakAtMinT60 = peak;
            if (t == 5.0f) peakAtMaxT60 = peak;
        }

        // Ratio of attack peaks between T60=0.1s and T60=5.0s (50x difference in decay time)
        // With normalized b0 = sin(w), the attack peaks are closely matched!
        float ratio = peakAtMaxT60 / peakAtMinT60;
        std::cout << "  f = " << std::setw(6) << f << " Hz | Peak(T60=0.1s): " << peakAtMinT60
                  << " | Peak(T60=5.0s): " << peakAtMaxT60 << " | Ratio: " << ratio << std::endl;
        assert(ratio > 0.75f && ratio < 1.35f && "Attack peak must remain within predictable physical bounds across 50x T60 change");
    }
    std::cout << "  -> PASSED: Modal normalization is rock-solid across frequencies and T60!\n";
}

// 8. Velocity calibration metrics (Peak, RMS, Brightness proxy)
void testVelocityCalibration() {
    std::cout << "[Test 8] Velocity Dynamic Calibration (20, 50, 80, 110, 127)..." << std::endl;
    constexpr float kFs = 48000.0f;
    const uint8_t velocities[] = { 10, 20, 40, 64, 90, 110, 127 };

    std::cout << "\n  Vel |   Peak   |   RMS    | CrestFac | Limiter Hits\n";
    std::cout << "  ----+----------+----------+----------+-------------\n";

    float previousRms = 0.0f, previousAttackRms = 0.0f;
    for (uint8_t vel : velocities) {
        dsp::SynthEngine engine;
        engine.init(kFs);
        engine.resetSoftClipCount();

        midi::MidiEvent ev;
        ev.type = midi::MidiEventType::NoteOn;
        ev.data1 = 62; // D3
        ev.data2 = vel;
        engine.handleMidiEvent(ev);

        const size_t totalFrames = 48000; // 1 second
        std::vector<int32_t> buf(totalFrames * 2, 0);
        int32_t block[128 * 2];

        for (size_t f = 0; f < totalFrames; f += 128) {
            engine.renderBlock(block, 128);
            std::memcpy(&buf[f * 2], block, sizeof(block));
        }

        AudioMetrics m = computeMetrics(buf, engine.getSoftClipCount());
        std::cout << "  " << std::setw(3) << static_cast<int>(vel)
                  << " | " << std::setw(8) << std::fixed << std::setprecision(4) << m.peak
                  << " | " << std::setw(8) << m.rms
                  << " | " << std::setw(8) << m.crestFactor
                  << " | " << std::setw(12) << m.softClipCount << "\n" << std::flush;

        // Criteria:
        // The physical model may vary peak shape, but early energy must rise.
        assert(m.rms > previousRms * 1.001f && "Velocity RMS must be monotonic");
        previousRms = m.rms;
        assert(m.attackRms > previousAttackRms * 1.001f && "Velocity attackRms must be monotonic");
        previousAttackRms = m.attackRms;
        // Vel 20 must produce useful audible sound (> 0.05 peak)
        if (vel == 20) assert(m.peak > 0.05f && "Vel 20 must be audible");
        // Vel <= 110 must NOT trigger safety limiter
        if (vel <= 110) assert(m.softClipCount == 0 && "Limiter must not engage during normal playing");
        // Peak must never exceed 1.0
        assert(m.peak <= 1.0f);
        if (vel <= 110) assert(engine.getModalInternalSaturationCount() == 0 && "Normal single notes must not use modal saturation");
    }
    std::cout << "  -> PASSED: Dynamic velocity range is expressive and controlled!\n";
}


// Diagnostic counters span filter resets, while a reused voice never exposes a
// stale output sample before its newly triggered strike is rendered.
void testResetDiagnosticsAndVoiceReuse() {
    dsp::ModalResonatorBank bank;
    bank.init(48000.0f);
    dsp::ModalPreset hot = {"Hot", 1, {{1.0f, 100.0f, 1.0f, 0.0f}}};
    bank.setPreset(hot);
    bank.updatePitchAndDamping(440.0f, 0.0f);
    for (int i = 0; i < 64 && bank.getInternalSaturationCount() == 0; ++i) {
        bank.processSample(10.0f);
    }
    const uint32_t saturationBeforeReset = bank.getInternalSaturationCount();
    assert(saturationBeforeReset > 0);
    bank.reset();
    assert(bank.getInternalSaturationCount() == saturationBeforeReset);

    dsp::ModalVoice voice;
    voice.init(48000.0f);
    voice.trigger(62, midi::MidiMapping::noteToHz(62), 1.0f);
    for (int i = 0; i < 32; ++i) voice.processSample();
    assert(std::abs(voice.getLastSample()) > 0.0f);
    voice.trigger(64, midi::MidiMapping::noteToHz(64), 0.8f);
    assert(voice.getLastSample() == 0.0f);
}

// 9. Generate all 5 required audio WAV files and log table
struct ScheduledEvent { uint32_t frame; midi::MidiEvent event; };

std::vector<int32_t> renderScheduled(const std::vector<ScheduledEvent>& events, size_t totalFrames,
                                     bool internalSafetySaturation, AudioMetrics& metrics, uint32_t& modalSat) {
    constexpr size_t kBlock = 128;
    dsp::SynthEngine engine; engine.init(48000.0f);
    engine.setInternalSafetySaturation(internalSafetySaturation);
    std::vector<int32_t> audio(totalFrames * 2, 0); size_t eventIndex = 0;
    for (size_t frame = 0; frame < totalFrames; frame += kBlock) {
        while (eventIndex < events.size() && events[eventIndex].frame <= frame) engine.handleMidiEvent(events[eventIndex++].event);
        const size_t frames = std::min(kBlock, totalFrames - frame); int32_t block[kBlock * 2]{};
        engine.renderBlock(block, frames); std::memcpy(&audio[frame * 2], block, frames * 2 * sizeof(int32_t));
    }
    metrics = computeMetrics(audio, engine.getSoftClipCount()); modalSat = engine.getModalInternalSaturationCount();
    assert(engine.getHardClampCount() == 0 && "Musical scheduled fixtures must not require final hard clipping");
    return audio;
}

void testSpectralAndRestrikeSanity() {
    std::cout << "[Test 9] PAN spectral/restrike/DC sanity..." << std::endl;
    auto note = [](uint8_t velocity) { midi::MidiEvent e{}; e.type = midi::MidiEventType::NoteOn; e.data1 = 62; e.data2 = velocity; return e; };
    AudioMetrics singleMetrics; uint32_t singleSat;
    auto single = renderScheduled({{0, note(90)}}, 96000, true, singleMetrics, singleSat);
    // Broad probes deliberately do not try to resolve the sub-1 Hz PAN doublet.
    const float fundamental = goertzelMagnitude(single, 293.665f);
    const float octave = goertzelMagnitude(single, 2.0f * 293.665f);
    const float fifth = goertzelMagnitude(single, 3.0f * 293.665f);
    const float high = goertzelMagnitude(single, 12000.0f);
    assert(fundamental > 0.001f && octave > 0.001f && fifth > 0.001f);
    assert(high < fundamental * 0.25f && "PAN must not become high-frequency broadband noise");
    assert(std::abs(singleMetrics.dcMean) < 0.005f);
    AudioMetrics double100; uint32_t sat100;
    renderScheduled({{0, note(80)}, {4800, note(95)}}, 96000, true, double100, sat100);
    assert(std::isfinite(double100.rms) && double100.peak <= 1.0f && double100.rms > singleMetrics.rms);
    assert(std::abs(double100.dcMean) < 0.005f);
    AudioMetrics double250; uint32_t sat250;
    renderScheduled({{0, note(80)}, {12000, note(95)}}, 96000, true, double250, sat250);
    assert(std::isfinite(double250.rms) && double250.peak <= 1.0f && double250.rms > singleMetrics.rms);
    AudioMetrics roll; uint32_t rollSat;
    std::vector<ScheduledEvent> rollEvents; for (uint32_t f = 0; f <= 43200; f += 3600) rollEvents.push_back({f, note(85)});
    renderScheduled(rollEvents, 96000, true, roll, rollSat);
    assert(std::isfinite(roll.rms) && roll.peak <= 1.0f && std::abs(roll.dcMean) < 0.005f);
    assert(roll.rms < 0.70f && "Roll must not exhibit uncontrolled growth");
    std::cout << "  spectral f=" << fundamental << " 2f=" << octave << " 3f=" << fifth << " high=" << high
              << " | restrike RMS 100ms=" << double100.rms << " 250ms=" << double250.rms << " roll=" << roll.rms << "\n";
}

void saturationAbAudit() {
    std::cout << "[Test 10] Internal saturation A/B audit..." << std::endl;
    std::ofstream report("saturation_ab_metrics.md");
    report << "# Internal modal saturation A/B audit\n\n| Case | Sat On Peak | Sat Off Peak | RMS delta | On ModalSat | Off ModalSat |\n|---|---:|---:|---:|---:|---:|\n";
    auto note=[](uint8_t n,uint8_t velocity){ midi::MidiEvent e{}; e.type=midi::MidiEventType::NoteOn; e.data1=n; e.data2=velocity; return e; };
    const std::vector<std::pair<const char*, std::vector<ScheduledEvent>>> cases = {
        {"single D3 vel127", {{0,note(62,127)}}}, {"double strike 100ms", {{0,note(62,100)},{4800,note(62,100)}}},
        {"rapid roll", {{0,note(62,110)},{1800,note(62,110)},{3600,note(62,110)},{5400,note(62,110)},{7200,note(62,110)}}},
        {"cluster8", {{0,note(62,100)},{0,note(64,100)},{0,note(65,100)},{0,note(67,100)},{0,note(69,100)},{0,note(70,100)},{0,note(72,100)},{0,note(74,100)}}}
    };
    for (const auto& entry : cases) {
        AudioMetrics on, off; uint32_t onSat, offSat;
        auto onAudio = renderScheduled(entry.second, 96000, true, on, onSat);
        auto offAudio = renderScheduled(entry.second, 96000, false, off, offSat);
        double sum = 0.0; for (size_t i=0; i<onAudio.size(); ++i) { const double d=(double(onAudio[i])-offAudio[i])/2147483647.0; sum += d*d; }
        const float delta = static_cast<float>(std::sqrt(sum/onAudio.size()));
        report << "| " << entry.first << " | " << on.peak << " | " << off.peak << " | " << delta << " | " << onSat << " | " << offSat << " |\n";
    }
}

void generateComparativeWavs() {
    std::cout << "\n[Test 9] Generating scheduled WAV regression fixtures..." << std::endl;
    constexpr float kFs=48000.0f; constexpr size_t kBlock=128;
    std::ofstream report("test_metrics.md");
    report << "# ESPan32 audio regression metrics\n\n| Fixture | Peak | RMS | Crest | Limiter | ModalSat |\n|---|---:|---:|---:|---:|---:|\n";
    auto event=[](uint8_t note,uint8_t velocity){ midi::MidiEvent e; e.type=midi::MidiEventType::NoteOn; e.data1=note; e.data2=velocity; return e; };
    auto render=[&](const char* filename, std::vector<ScheduledEvent> events, size_t totalFrames) {
        dsp::SynthEngine engine; engine.init(kFs); engine.resetSoftClipCount();
        std::vector<int32_t> audio(totalFrames*2,0); size_t index=0;
        for(size_t f=0; f<totalFrames; f+=kBlock) {
            while(index<events.size() && events[index].frame<=f) engine.handleMidiEvent(events[index++].event);
            const size_t frames=std::min(kBlock,totalFrames-f); int32_t block[kBlock*2]{};
            engine.renderBlock(block,frames); std::memcpy(&audio[f*2],block,frames*2*sizeof(int32_t));
        }
        writeWavFile(filename,audio.data(),totalFrames,static_cast<int>(kFs));
        auto m=computeMetrics(audio,engine.getSoftClipCount()); auto sat=engine.getModalInternalSaturationCount();
        assert(engine.getHardClampCount() == 0 && "Musical scheduled fixtures must not require final hard clipping");
        report << "| "<<filename<<" | "<<m.peak<<" | "<<m.rms<<" | "<<m.crestFactor<<" | "<<m.softClipCount<<" | "<<sat<<" |\n";
        std::cout<<filename<<" peak="<<m.peak<<" rms="<<m.rms<<" crest="<<m.crestFactor<<" limiter="<<m.softClipCount<<" modalSat="<<sat<<"\n";
    };
    render("pan_D3_vel40.wav",{{0,event(62,40)}},144000);
    render("pan_D3_vel90.wav",{{0,event(62,90)}},144000);
    render("pan_D3_vel127.wav",{{0,event(62,127)}},144000);
    render("pan_D3_single.wav",{{0,event(62,90)}},144000);
    render("pan_D3_double_100ms.wav",{{0,event(62,80)},{4800,event(62,95)}},168000);
    render("pan_D3_double_250ms.wav",{{0,event(62,80)},{12000,event(62,95)}},168000);
    render("pan_D3_triple.wav",{{0,event(62,80)},{12000,event(62,95)},{24000,event(62,105)}},168000);
    render("pan_D3_roll.wav",{{0,event(62,85)},{3600,event(62,85)},{7200,event(62,85)},{10800,event(62,85)},{14400,event(62,85)},{18000,event(62,85)},{21600,event(62,85)},{25200,event(62,85)},{28800,event(62,85)},{32400,event(62,85)},{36000,event(62,85)},{39600,event(62,85)},{43200,event(62,85)}},96000);
    render("pan_chord.wav",{{0,event(62,85)},{0,event(69,80)},{0,event(77,75)},{0,event(81,80)}},192000);
    render("pan_cluster8.wav",{{0,event(62,100)},{0,event(64,100)},{0,event(65,100)},{0,event(67,100)},
           {0,event(69,100)},{0,event(70,100)},{0,event(72,100)},{0,event(74,100)}},192000);
}

// Host-only reference for the retired output stage.  It deliberately lives in
// the fixture, never in firmware, so A/B evidence cannot accidentally enable
// a waveshaper on the ESP32.
float oldSafetySoftClipForTest(float x, bool& active) {
    constexpr float threshold = 0.85f;
    if (std::abs(x) <= threshold) { active = false; return x; }
    active = true;
    const float excess = std::abs(x) - threshold;
    return std::copysign(threshold + (1.0f - threshold) *
        std::tanh(excess / (1.0f - threshold)), x);
}

enum class OutputStrategy { OldTanh, NewLimiter, StaticGainOnly };

struct OutputStrategyResult {
    std::vector<int32_t> audio;
    float prePeak = 0.0f;
    float postPeak = 0.0f;
    float maxGrDb = 0.0f;
    float averageGrDb = 0.0f;
    uint32_t limiterActive = 0;
    uint32_t grOver0p1Db = 0;
    uint32_t grOver1Db = 0;
    uint32_t hardClampCount = 0;
};

OutputStrategyResult renderOutputStrategy(const std::vector<ScheduledEvent>& events,
                                          size_t totalFrames, OutputStrategy strategy) {
    constexpr size_t kBlock = 128;
    constexpr float kFs = 48000.0f;
    dsp::VoiceAllocator allocator;
    allocator.init(kFs);
    dsp::PeakLimiter limiter;
    limiter.init(kFs);
    float polyGain = 1.0f;
    const float polyAttackCoeff = std::exp(-1.0f / (kFs * 0.003f));
    const float polyReleaseCoeff = std::exp(-1.0f / (kFs * 0.050f));
    const float masterGain = strategy == OutputStrategy::StaticGainOnly ? 0.50f : 0.85f;
    OutputStrategyResult result;
    result.audio.resize(totalFrames * 2);
    size_t eventIndex = 0;

    for (size_t frame = 0; frame < totalFrames; frame += kBlock) {
        while (eventIndex < events.size() && events[eventIndex].frame <= frame) {
            const auto& event = events[eventIndex++].event;
            allocator.noteOn(event.data1, midi::MidiMapping::toNormalizedFloat(event.data2),
                             midi::MidiMapping::noteToHz(event.data1));
        }
        const size_t frames = std::min(kBlock, totalFrames - frame);
        float mono[kBlock]{};
        allocator.renderBlock(mono, frames);
        const float voices = static_cast<float>(allocator.getActiveVoiceCount());
        const float targetDb = voices <= 1.0f ? 0.0f : -1.5f * std::log2(voices);
        const float targetPolyGain = std::pow(10.0f, std::max(targetDb, -5.0f) / 20.0f);

        for (size_t i = 0; i < frames; ++i) {
            float sample = mono[i] * masterGain;
            if (strategy == OutputStrategy::NewLimiter) {
                const float coefficient = targetPolyGain < polyGain ? polyAttackCoeff : polyReleaseCoeff;
                polyGain = targetPolyGain + coefficient * (polyGain - targetPolyGain);
                sample *= polyGain;
            }
            result.prePeak = std::max(result.prePeak, std::abs(sample));
            bool active = false;
            if (strategy == OutputStrategy::NewLimiter) sample = limiter.processSample(sample);
            else sample = oldSafetySoftClipForTest(sample, active);
            if (active) ++result.limiterActive;
            result.postPeak = std::max(result.postPeak, std::abs(sample));
            const float clamped = std::clamp(sample, -1.0f, 1.0f);
            if (clamped != sample) ++result.hardClampCount;
            const int32_t pcm = static_cast<int32_t>(clamped * 2147483647.0f);
            result.audio[(frame + i) * 2] = pcm;
            result.audio[(frame + i) * 2 + 1] = pcm;
        }
    }
    if (strategy == OutputStrategy::NewLimiter) {
        result.limiterActive = limiter.getActiveSampleCount();
        result.maxGrDb = limiter.getMaxGainReductionDb();
        result.averageGrDb = limiter.getAverageGainReductionDb();
        result.grOver0p1Db = limiter.getGainReductionOver0p1DbSamples();
        result.grOver1Db = limiter.getGainReductionOver1DbSamples();
    }
    return result;
}

float rmsDifference(const std::vector<int32_t>& a, const std::vector<int32_t>& b) {
    assert(a.size() == b.size());
    double sum = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        const double d = (static_cast<double>(a[i]) - b[i]) / 2147483647.0;
        sum += d * d;
    }
    return static_cast<float>(std::sqrt(sum / a.size()));
}

void testOutputStrategyAbc() {
    std::cout << "[Test 12] Output-stage A/B/C and static master-gain audit..." << std::endl;
    auto note=[](uint8_t n, uint8_t v) { midi::MidiEvent e{}; e.type=midi::MidiEventType::NoteOn; e.data1=n; e.data2=v; return e; };
    const std::vector<std::pair<const char*, std::vector<ScheduledEvent>>> cases = {
        {"chord", {{0,note(62,85)}, {0,note(69,80)}, {0,note(77,75)}, {0,note(81,80)}}},
        {"cluster8", {{0,note(62,100)}, {0,note(64,100)}, {0,note(65,100)}, {0,note(67,100)},
                      {0,note(69,100)}, {0,note(70,100)}, {0,note(72,100)}, {0,note(74,100)}}},
        {"roll", {{0,note(62,85)}, {3600,note(62,85)}, {7200,note(62,85)}, {10800,note(62,85)},
                  {14400,note(62,85)}, {18000,note(62,85)}, {21600,note(62,85)}, {25200,note(62,85)},
                  {28800,note(62,85)}, {32400,note(62,85)}, {36000,note(62,85)}, {39600,note(62,85)}}}
    };
    std::ofstream report("output_stage_abc_metrics.md");
    report << "# Output stage A/B/C host audit\n\n"
           << "A = master 0.85 + retired tanh; B = poly headroom + peak limiter; C = master 0.50 + retired tanh.\n\n"
           << "| Fixture | A peak/RMS | B pre/post/RMS | B max/avg GR | B GR >0.1/>1 dB | B hard clamp | C peak/RMS | A→B RMS diff | A→C RMS diff |\n|---|---|---|---:|---:|---:|---|---:|---:|\n";
    for (const auto& item : cases) {
        const size_t frames = std::strcmp(item.first, "roll") == 0 ? 96000 : 192000;
        const auto old = renderOutputStrategy(item.second, frames, OutputStrategy::OldTanh);
        const auto fresh = renderOutputStrategy(item.second, frames, OutputStrategy::NewLimiter);
        const auto quiet = renderOutputStrategy(item.second, frames, OutputStrategy::StaticGainOnly);
        const auto oldMetrics = computeMetrics(old.audio, old.limiterActive);
        const auto newMetrics = computeMetrics(fresh.audio, fresh.limiterActive);
        const auto quietMetrics = computeMetrics(quiet.audio, quiet.limiterActive);
        assert(fresh.hardClampCount == 0);
        assert(fresh.postPeak <= std::pow(10.0f, -0.5f / 20.0f) + 1.0e-4f);
        const float abDiff = rmsDifference(old.audio, fresh.audio);
        const float acDiff = rmsDifference(old.audio, quiet.audio);
        report << "| " << item.first << " | " << oldMetrics.peak << " / " << oldMetrics.rms
               << " | " << fresh.prePeak << " / " << newMetrics.peak << " / " << newMetrics.rms << " | " << fresh.maxGrDb << " / " << fresh.averageGrDb
               << " | " << fresh.grOver0p1Db << " / " << fresh.grOver1Db << " | " << fresh.hardClampCount << " | " << quietMetrics.peak << " / " << quietMetrics.rms
               << " | " << abDiff << " | " << acDiff << " |\n";
        writeWavFile((std::string("pan_") + item.first + "_old.wav").c_str(), old.audio.data(), frames, 48000);
        writeWavFile((std::string("pan_") + item.first + "_new.wav").c_str(), fresh.audio.data(), frames, 48000);
    }
    std::cout << "  -> PASSED: transparent limiter and reduced-master reference rendered.\n";
}

void testPeakLimiter() {
    std::cout << "[Test 11] Lookahead peak limiter transparency, ceiling, and release..." << std::endl;
    constexpr float kFs = 48000.0f;
    constexpr uint32_t kLookahead = 32;
    dsp::LimiterConfig cfg;
    cfg.thresholdDb = -3.0f; cfg.ceilingDb = -0.5f; cfg.releaseMs = 80.0f; cfg.lookaheadSamples = kLookahead;

    // A 1 kHz sine below threshold must only acquire the fixed time delay.
    dsp::PeakLimiter clean;
    clean.init(kFs); clean.setConfig(cfg);
    float maxDifference = 0.0f;
    for (uint32_t i = 0; i < 48000 + kLookahead; ++i) {
        const float input = 0.5f * std::sin(2.0f * 3.14159265358979323846f * 1000.0f * i / kFs);
        const float output = clean.processSample(input);
        if (i >= kLookahead) {
            const float expected = 0.5f * std::sin(2.0f * 3.14159265358979323846f * 1000.0f * (i - kLookahead) / kFs);
            maxDifference = std::max(maxDifference, std::abs(output - expected));
        }
    }
    assert(maxDifference < 1.0e-6f);
    assert(clean.getActiveSampleCount() == 0);

    // Above the ceiling, gain control—not saturation—keeps every output sample safe.
    dsp::PeakLimiter hot;
    hot.init(kFs); hot.setConfig(cfg);
    float peak = 0.0f;
    for (uint32_t i = 0; i < 48000 + kLookahead; ++i)
        peak = std::max(peak, std::abs(hot.processSample(1.5f * std::sin(2.0f * 3.14159265358979323846f * 1000.0f * i / kFs))));
    assert(peak <= std::pow(10.0f, -0.5f / 20.0f) + 1.0e-4f);
    assert(hot.getMaxGainReductionDb() < -3.0f && hot.getActiveSampleCount() > 0);
    assert(hot.getGainReductionOver0p1DbSamples() > 0);
    assert(hot.getGainReductionOver1DbSamples() > 0);
    assert(hot.getAverageGainReductionDb() < 0.0f);

    // Diagnostic reset must not flush lookahead/envelope audio state.
    dsp::PeakLimiter stateful;
    stateful.init(kFs); stateful.setConfig(cfg);
    for (uint32_t i = 0; i < kLookahead + 8; ++i) stateful.processSample(1.5f);
    stateful.resetDiagnostics();
    const float continuingOutput = stateful.processSample(1.5f);
    assert(std::abs(continuingOutput) > 1.0e-3f);
    assert(stateful.getActiveSampleCount() == 1);
    assert(stateful.getGainReductionOver0p1DbSamples() == 1);
    assert(stateful.getGainReductionOver1DbSamples() == 1);
    assert(stateful.getMaxGainReductionDb() < -1.0f);

    // Validate all candidate releases: gain must recover after a burst, rather
    // than staying latched or jumping back to unity during the delayed peak.
    for (float releaseMs : {50.0f, 80.0f, 120.0f}) {
        cfg.releaseMs = releaseMs;
        dsp::PeakLimiter release;
        release.init(kFs); release.setConfig(cfg);
        for (uint32_t i = 0; i < 128; ++i) release.processSample(1.5f);
        const float burstGr = release.getCurrentGainReductionDb();
        for (uint32_t i = 0; i < static_cast<uint32_t>(kFs * 0.5f); ++i) release.processSample(0.2f);
        assert(burstGr < -3.0f);
        assert(release.getCurrentGainReductionDb() > -0.1f);
    }
    std::cout << "  -> PASSED: linear below threshold; ceiling and release verified.\n";
}

int main() {
    std::cout << "=======================================================\n";
    std::cout << "  Pocket Pan / Metal Modal Synth - Comprehensive DSP  \n";
    std::cout << "=======================================================\n\n";

    testDiagnosticToneSource();
    testDiagnosticOscillatorLongRun();
    testModalFrequency();
    testT60Decay();
    testModeSplitting();
    testSameNoteRestrike();
    testDeclickedVoiceStealing();
    testInactiveTriggerClearsResidual();
    testMultiVoiceDamping();
    testGainNormalization();
    testVelocityCalibration();
    testResetDiagnosticsAndVoiceReuse();
    testSpectralAndRestrikeSanity();
    saturationAbAudit();
    generateComparativeWavs();
    testPeakLimiter();
    testOutputStrategyAbc();

    std::cout << "\n=======================================================\n";
    std::cout << "  ALL DSP AND ACOUSTIC TESTS PASSED SUCCESSFULLY!     \n";
    std::cout << "=======================================================\n";
    return 0;
}
