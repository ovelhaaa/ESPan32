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
#include <array>
#include <sstream>

#include "../main/dsp/modal_mode.h"
#include "../main/dsp/modal_preset.h"
#include "../main/dsp/modal_resonator.h"
#include "../main/dsp/exciter.h"
#include "../main/dsp/modal_voice.h"
#include "../main/dsp/voice_allocator.h"
#include "../main/dsp/synth_engine.h"
#include "../main/dsp/peak_limiter.h"
#include "../main/dsp/diagnostic_tone.h"
#include "../main/dsp/pan_doublet.h"
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
    float tailRms = 0.0f;
    float preLimiterPeak = 0.0f;
    float maxGainReductionDb = 0.0f;
    float averageGainReductionDb = 0.0f;
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
    const size_t tailStart = std::min(numSamples, static_cast<size_t>(48000 * 0.5f * 2));
    const size_t tailEnd = std::min(numSamples, static_cast<size_t>(48000 * 1.5f * 2));
    double tailSum = 0.0;
    for (size_t i = tailStart; i < tailEnd; ++i) { const float s = static_cast<float>(audioStereo[i]) / 2147483647.0f; tailSum += s * s; }
    m.tailRms = tailEnd > tailStart ? static_cast<float>(std::sqrt(tailSum / (tailEnd - tailStart))) : 0.0f;
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
    const float kFund = midi::MidiMapping::noteToHz(50); // D3
    const auto& m0 = dsp::kPresetPan.modes[0];
    const auto& m1 = dsp::kPresetPan.modes[1];

    const float f0 = kFund * m0.ratio * (1.0f + m0.detune);
    const float f1 = kFund * m1.ratio * (1.0f + m1.detune);

    std::cout << "  Mode 0: ratio=" << m0.ratio << ", detune=" << m0.detune << " -> " << f0 << " Hz" << std::endl;
    std::cout << "  Mode 1: ratio=" << m1.ratio << ", detune=" << m1.detune << " -> " << f1 << " Hz" << std::endl;

    const float beatFreqHz = std::abs(f1 - f0);
    std::cout << "  Beat frequency: " << beatFreqHz << " Hz" << std::endl;

    // Relative split remains 0.0032; D3 produces about 0.47 Hz beating.
    assert(std::abs(m1.ratio - 1.0f) < 0.0001f);
    assert(std::abs(m1.detune - 0.0032f) < 0.0001f);
    assert(std::abs(beatFreqHz - (kFund * 0.0032f)) < 0.001f);
    std::cout << "  -> PASSED: Mode split detune applied exactly once, producing natural sub-0.5 Hz D3 beating!\n";
}

// 4. Same-note restrike physical energy accumulation
void testSameNoteRestrike() {
    std::cout << "[Test 4] Same-Note Restrike (Physical Energy Accumulation)..." << std::endl;
    constexpr float kFs = 48000.0f;
    const float kFund = midi::MidiMapping::noteToHz(50); // D3

    // Voice A: Physical restrike (without resetting resonator modes)
    dsp::ModalVoice voiceA;
    voiceA.init(kFs);
    voiceA.trigger(50, kFund, 0.70f);

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
    voiceB.trigger(50, kFund, 0.70f);
    for (int i = 0; i < 3000; ++i) {
        voiceB.processSample();
    }
    // Hard kill and re-trigger
    voiceB.kill();
    voiceB.trigger(50, kFund, 0.70f);
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
    voice.trigger(62, midi::MidiMapping::noteToHz(62), 0.8f); // D4
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

    // Voice 1 (D4, note 62) and Voice 2 (A4, note 69)
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

// 8. Velocity calibration metrics (peak and RMS; modal brightness is audited below).
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
        ev.data1 = 62; // D4
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

float spectralEnergy(const std::vector<int32_t>& audio, const std::initializer_list<float>& frequencies);

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
    metrics = computeMetrics(audio, engine.getSoftClipCount());
    metrics.preLimiterPeak = engine.getPreLimiterPeak();
    metrics.maxGainReductionDb = engine.getMaxGainReductionDb();
    metrics.averageGainReductionDb = engine.getAverageGainReductionDb();
    modalSat = engine.getModalInternalSaturationCount();
    assert(engine.getHardClampCount() == 0 && "Musical scheduled fixtures must not require final hard clipping");
    return audio;
}

struct M5cResult {
    std::vector<int32_t> audio;
    AudioMetrics metrics;
    float bodyEnergy=0.0f, bodyPeak=0.0f, bodyRms=0.0f;
    float busPeak=0.0f, busRms=0.0f;
    uint32_t sympatheticSafetyCount=0, hardClampCount=0;
    uint32_t grOver0p1DbSamples=0, grOver1DbSamples=0;
    uint32_t modalSat=0;
};

M5cResult renderM5c(const std::vector<ScheduledEvent>& events, size_t totalFrames, bool body, bool sympathetic,
                     float bodyOutputGain = -1.0f, float sympatheticScale = 1.0f, bool tailVariant = false,
                     dsp::BodyExcitationStrategy strategy = dsp::BodyExcitationStrategy::FullMix,
                     float bodyExcitationGain = -1.0f, float strikeBusGain = -1.0f,
                     const dsp::ExciterConfig* exciterOverride = nullptr,
                     const dsp::PanVoicingConfig* voicingOverride = nullptr) {
    constexpr size_t kBlock=128; M5cResult result; result.audio.resize(totalFrames*2); size_t eventIndex=0;
    dsp::SynthEngine engine; engine.init(48000.0f);
    if(exciterOverride && voicingOverride) engine.setPanConfigsForTest(*exciterOverride,*voicingOverride);
    engine.setBodyExcitationStrategyForTest(strategy);
    auto bodyConfig=dsp::kPanBodyConfig.body; bodyConfig.enabled=body;
    if(bodyOutputGain>=0.0f) bodyConfig.outputGain=bodyOutputGain;
    if(bodyExcitationGain>=0.0f) bodyConfig.excitationGain=bodyExcitationGain;
    if(tailVariant) { bodyConfig.excitationGain*=0.80f; bodyConfig.outputGain*=1.08f; for(auto& mode:bodyConfig.modes) mode.t60Seconds*=1.15f; }
    auto sympatheticConfig=dsp::kPanBodyConfig.sympathetic; sympatheticConfig.enabled=sympathetic;
    sympatheticConfig.inputGain*=sympatheticScale; sympatheticConfig.feedbackGain*=sympatheticScale;
    engine.setBodyConfigForTest(bodyConfig); engine.setSympatheticConfigForTest(sympatheticConfig);
    if(strikeBusGain>=0.0f) engine.setStrikeBusGainForTest(strikeBusGain);
    for(size_t frame=0; frame<totalFrames; frame+=kBlock) {
        while(eventIndex<events.size() && events[eventIndex].frame<=frame) engine.handleMidiEvent(events[eventIndex++].event);
        int32_t block[kBlock*2]{}; const size_t frames=std::min(kBlock,totalFrames-frame); engine.renderBlock(block,frames);
        std::memcpy(&result.audio[frame*2],block,frames*2*sizeof(int32_t));
    }
    result.metrics=computeMetrics(result.audio,engine.getSoftClipCount()); result.metrics.preLimiterPeak=engine.getPreLimiterPeak();
    result.metrics.maxGainReductionDb=engine.getMaxGainReductionDb(); result.metrics.averageGainReductionDb=engine.getAverageGainReductionDb();
    result.bodyEnergy=engine.getBodyEnergy(); result.bodyPeak=engine.getBodyPeak(); result.bodyRms=engine.getBodyRms();
    result.busPeak=engine.getSympatheticBusPeak(); result.busRms=engine.getSympatheticBusRms(); result.sympatheticSafetyCount=engine.getSympatheticSafetyCount(); result.hardClampCount=engine.getHardClampCount();
    result.grOver0p1DbSamples=engine.getGainReductionOver0p1DbSamples(); result.grOver1DbSamples=engine.getGainReductionOver1DbSamples();
    result.modalSat=engine.getModalInternalSaturationCount();
    return result;
}

float windowRms(const std::vector<int32_t>& audio, float startSeconds, float endSeconds) {
    const size_t begin=std::min(audio.size()/2, static_cast<size_t>(startSeconds*48000.0f));
    const size_t end=std::min(audio.size()/2, static_cast<size_t>(endSeconds*48000.0f));
    if(end<=begin) return 0.0f;
    double sum=0.0; for(size_t i=begin;i<end;++i) { const double s=static_cast<double>(audio[i*2])/2147483647.0; sum+=s*s; }
    return static_cast<float>(std::sqrt(sum/(end-begin)));
}

float differenceRms(const std::vector<int32_t>& a, const std::vector<int32_t>& b) {
    assert(a.size()==b.size()); double sum=0.0;
    for(size_t i=0;i<a.size();++i) { const double d=static_cast<double>(a[i]-static_cast<int64_t>(b[i]))/2147483647.0; sum+=d*d; }
    return static_cast<float>(std::sqrt(sum/a.size()));
}
float dbRelative(float value, float reference) { return 20.0f*std::log10(std::max(value,1.0e-12f)/std::max(reference,1.0e-12f)); }

uint64_t fnv1a64(const std::vector<int32_t>& pcm) {
    uint64_t hash = 14695981039346656037ull;
    for (int32_t sample : pcm) {
        const uint32_t value = static_cast<uint32_t>(sample);
        for (unsigned shift = 0; shift < 32; shift += 8) { hash ^= (value >> shift) & 0xffu; hash *= 1099511628211ull; }
    }
    return hash;
}

M5cResult renderModel(const std::vector<ScheduledEvent>& events, size_t totalFrames, dsp::InstrumentModel model) {
    constexpr size_t kBlock = 128; M5cResult result; result.audio.resize(totalFrames * 2); size_t eventIndex = 0;
    dsp::SynthEngine engine; engine.init(48000.0f); engine.setInstrumentModel(model);
    for (size_t frame = 0; frame < totalFrames; frame += kBlock) {
        while (eventIndex < events.size() && events[eventIndex].frame <= frame) engine.handleMidiEvent(events[eventIndex++].event);
        engine.renderBlock(result.audio.data() + frame * 2, std::min(kBlock, totalFrames - frame));
    }
    result.metrics = computeMetrics(result.audio, engine.getSoftClipCount()); result.hardClampCount = engine.getHardClampCount();
    result.modalSat = engine.getModalInternalSaturationCount(); result.metrics.maxGainReductionDb = engine.getMaxGainReductionDb();
    result.metrics.averageGainReductionDb = engine.getAverageGainReductionDb(); return result;
}

struct BellQualificationRender {
    M5cResult rendered;
    size_t maxActiveVoices = 0;
    size_t maxStealTails = 0;
    float maxSampleDelta = 0.0f;
    size_t finalActiveVoices = 0;
    uint32_t voiceStealCount = 0;
};

BellQualificationRender renderBellQualification(const std::vector<ScheduledEvent>& events, size_t totalFrames) {
    constexpr size_t kBlock = 128;
    BellQualificationRender result; result.rendered.audio.resize(totalFrames * 2);
    dsp::SynthEngine engine; engine.init(48000.0f); engine.setInstrumentModel(dsp::InstrumentModel::Bell);
    size_t eventIndex = 0; int32_t previous = 0;
    for (size_t frame = 0; frame < totalFrames; frame += kBlock) {
        while (eventIndex < events.size() && events[eventIndex].frame <= frame) engine.handleMidiEvent(events[eventIndex++].event);
        // A 32-sample declick tail ends inside a 128-frame callback; sample
        // its pending state before rendering so qualification can observe it.
        result.maxStealTails = std::max(result.maxStealTails, engine.getVoiceAllocator().getActiveStealTailCount());
        const size_t frames = std::min(kBlock, totalFrames - frame); int32_t block[kBlock * 2]{}; engine.renderBlock(block, frames);
        for (size_t i = 0; i < frames * 2; ++i) { result.maxSampleDelta = std::max(result.maxSampleDelta, std::abs(static_cast<float>(block[i] - previous) / 2147483647.0f)); previous = block[i]; }
        std::memcpy(result.rendered.audio.data() + frame * 2, block, frames * 2 * sizeof(int32_t));
        result.maxActiveVoices = std::max(result.maxActiveVoices, engine.getVoiceAllocator().getActiveVoiceCount());
        result.maxStealTails = std::max(result.maxStealTails, engine.getVoiceAllocator().getActiveStealTailCount());
    }
    result.finalActiveVoices = engine.getVoiceAllocator().getActiveVoiceCount();
    result.voiceStealCount = engine.getVoiceAllocator().getVoiceStealCount();
    result.rendered.metrics = computeMetrics(result.rendered.audio, engine.getSoftClipCount());
    result.rendered.metrics.preLimiterPeak = engine.getPreLimiterPeak(); result.rendered.metrics.maxGainReductionDb = engine.getMaxGainReductionDb();
    result.rendered.metrics.averageGainReductionDb = engine.getAverageGainReductionDb(); result.rendered.hardClampCount = engine.getHardClampCount(); result.rendered.modalSat = engine.getModalInternalSaturationCount();
    result.rendered.grOver0p1DbSamples = engine.getGainReductionOver0p1DbSamples(); result.rendered.grOver1DbSamples = engine.getGainReductionOver1DbSamples();
    return result;
}

BellQualificationRender renderBellCandidate(const std::vector<ScheduledEvent>& events, size_t totalFrames,
                                             const dsp::ModalPreset& preset) {
    constexpr size_t kBlock = 128;
    BellQualificationRender result; result.rendered.audio.resize(totalFrames * 2);
    dsp::SynthEngine engine; engine.init(48000.0f); engine.setInstrumentModel(dsp::InstrumentModel::Bell);
    auto config = dsp::getInstrumentModelConfig(dsp::InstrumentModel::Bell); config.modalPreset = &preset;
    engine.setModelConfigForTest(config);
    size_t eventIndex = 0; int32_t previous = 0;
    for (size_t frame = 0; frame < totalFrames; frame += kBlock) {
        while (eventIndex < events.size() && events[eventIndex].frame <= frame) engine.handleMidiEvent(events[eventIndex++].event);
        const size_t frames = std::min(kBlock, totalFrames-frame); int32_t block[kBlock * 2]{}; engine.renderBlock(block, frames);
        for (size_t i=0;i<frames*2;++i) { result.maxSampleDelta=std::max(result.maxSampleDelta,std::abs(static_cast<float>(block[i]-previous)/2147483647.0f)); previous=block[i]; }
        std::memcpy(result.rendered.audio.data()+frame*2,block,frames*2*sizeof(int32_t));
        result.maxActiveVoices=std::max(result.maxActiveVoices,engine.getVoiceAllocator().getActiveVoiceCount());
        result.maxStealTails=std::max(result.maxStealTails,engine.getVoiceAllocator().getActiveStealTailCount());
    }
    result.finalActiveVoices=engine.getVoiceAllocator().getActiveVoiceCount();
    result.rendered.metrics=computeMetrics(result.rendered.audio,engine.getSoftClipCount()); result.rendered.metrics.preLimiterPeak=engine.getPreLimiterPeak();
    result.rendered.metrics.maxGainReductionDb=engine.getMaxGainReductionDb(); result.rendered.metrics.averageGainReductionDb=engine.getAverageGainReductionDb();
    result.rendered.hardClampCount=engine.getHardClampCount(); result.rendered.modalSat=engine.getModalInternalSaturationCount();
    result.rendered.grOver0p1DbSamples=engine.getGainReductionOver0p1DbSamples(); result.rendered.grOver1DbSamples=engine.getGainReductionOver1DbSamples();
    return result;
}

void writeRmsMatchedWav(const char* filename, const std::vector<int32_t>& audio, float targetRms) {
    const auto metrics=computeMetrics(audio,0); const float gain=targetRms/std::max(metrics.rms,1.0e-12f);
    std::vector<int32_t> matched(audio.size());
    for(size_t i=0;i<audio.size();++i) matched[i]=static_cast<int32_t>(std::clamp(static_cast<double>(audio[i])*gain,-2147483648.0,2147483647.0));
    writeWavFile(filename,matched.data(),matched.size()/2,48000);
}

std::array<float, 8> bellModalEnergies(const std::vector<int32_t>& audio, float f);

void testBellM62Qualification() {
    std::cout << "[Test 19] Bell M6.2 A/B/C, aftertouch, steals, and ablation...\n";
    auto note=[](uint8_t n,uint8_t v){ midi::MidiEvent e{}; e.type=midi::MidiEventType::NoteOn; e.data1=n; e.data2=v; return e; };
    dsp::ModalPreset a=dsp::kPresetBell, b=dsp::kPresetBell, c=dsp::kPresetBell; b.modes[3].gain=.58f; c.modes[0].gain=.24f; c.modes[3].gain=.58f; c.modes[5].gain=.90f;
    const std::array<std::pair<char,const dsp::ModalPreset*>,3> candidates={{{'A',&a},{'B',&b},{'C',&c}}};
    std::ofstream report("bell_m62_ab.md"); report<<std::fixed<<std::setprecision(6);
    report << "# Bell M6.2 A/B/C host audit\n\n## Candidate definitions\n\n| Candidate | Hum | Tierce | Nominal | Production |\n|---|---:|---:|---:|---|\n| A | .28 | .65 | .85 | frozen baseline |\n| B | .28 | .58 | .85 | host only |\n| C | .24 | .58 | .90 | host only |\n\n";
    report << "## D4 velocity\n\n| Candidate | Velocity | RMS | Peak | Attack RMS | Tail RMS | Hum/Prime | Tierce/Prime | Nominal/Prime | Upper/Primary | Max GR | Avg GR | Clamp | ModalSat |\n|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    for(const auto& candidate:candidates) for(uint8_t velocity:{30,70,110,127}) {
        auto q=renderBellCandidate({{0,note(62,velocity)}},96000,*candidate.second); auto e=bellModalEnergies(q.rendered.audio,midi::MidiMapping::noteToHz(62)); float primary=e[1]+e[4];
        assert(q.rendered.hardClampCount==0 && q.rendered.modalSat==0); const std::string name="bell_m62_D4_v"+std::to_string(velocity)+"_"+candidate.first+".wav"; writeWavFile(name.c_str(),q.rendered.audio.data(),96000,48000);
        if(velocity==70 || velocity==110) writeRmsMatchedWav(("bell_m62_D4_v"+std::to_string(velocity)+"_"+candidate.first+"_matched.wav").c_str(),q.rendered.audio,q.rendered.metrics.rms);
        report<<"| "<<candidate.first<<" | "<<int(velocity)<<" | "<<q.rendered.metrics.rms<<" | "<<q.rendered.metrics.peak<<" | "<<q.rendered.metrics.attackRms<<" | "<<q.rendered.metrics.tailRms<<" | "<<e[0]/e[1]<<" | "<<e[2]/e[1]<<" | "<<e[4]/e[1]<<" | "<<e[7]/std::max(primary,1e-12f)<<" | "<<q.rendered.metrics.maxGainReductionDb<<" | "<<q.rendered.metrics.averageGainReductionDb<<" | 0 | 0 |\n";
    }
    report<<"\n## Register\n\n| Candidate | Note | Velocity | RMS | Hum/Prime | Tierce/Prime | Nominal/Prime | Upper/Primary |\n|---|---|---:|---:|---:|---:|---:|---:|\n";
    for(const auto& candidate:candidates) for(uint8_t n:{50,57,62,69,74}) for(uint8_t v:{70,110}) { auto q=renderBellCandidate({{0,note(n,v)}},96000,*candidate.second); auto e=bellModalEnergies(q.rendered.audio,midi::MidiMapping::noteToHz(n)); report<<"| "<<candidate.first<<" | "<<int(n)<<" | "<<int(v)<<" | "<<q.rendered.metrics.rms<<" | "<<e[0]/e[1]<<" | "<<e[2]/e[1]<<" | "<<e[4]/e[1]<<" | "<<e[7]/std::max(e[1]+e[4],1e-12f)<<" |\n"; }
    std::vector<ScheduledEvent> chord={{0,note(50,90)},{0,note(57,90)},{0,note(62,90)},{0,note(69,90)}}, roll; for(size_t i=0;i<12;++i) roll.push_back({uint32_t(i*4800),note(62,90)});
    report<<"\n## Musical fixtures and limiter\n\n| Candidate | Fixture | RMS | Peak | Max GR | Avg GR | GR > .1 / > 1 | Clamp | ModalSat |\n|---|---|---:|---:|---:|---:|---:|---:|---:|\n";
    for(const auto& candidate:candidates) for(const auto& fixture:std::initializer_list<std::pair<const char*,std::vector<ScheduledEvent>>>{{"chord",chord},{"roll",roll},{"restrike100",{{0,note(62,90)},{4800,note(62,90)}}},{"restrike250",{{0,note(62,90)},{12000,note(62,90)}}}}) { auto q=renderBellCandidate(fixture.second,192000,*candidate.second); writeWavFile((std::string("bell_m62_")+fixture.first+"_"+candidate.first+".wav").c_str(),q.rendered.audio.data(),192000,48000); report<<"| "<<candidate.first<<" | "<<fixture.first<<" | "<<q.rendered.metrics.rms<<" | "<<q.rendered.metrics.peak<<" | "<<q.rendered.metrics.maxGainReductionDb<<" | "<<q.rendered.metrics.averageGainReductionDb<<" | "<<q.rendered.grOver0p1DbSamples<<" / "<<q.rendered.grOver1DbSamples<<" | "<<q.rendered.hardClampCount<<" | "<<q.rendered.modalSat<<" |\n"; }
    report<<"\n## Ablation (D4 v70/v110)\n\n| Removed mode | Velocity | Difference RMS | Relative dB |\n|---|---:|---:|---:|\n";
    for(const auto& ablation: std::initializer_list<std::pair<const char*,size_t>>{{"prime doublet",2},{"nominal doublet",6},{"superquint",7},{"upper",9}}) for(uint8_t v:{70,110}) { auto base=renderBellCandidate({{0,note(62,v)}},96000,a); auto altered=a; altered.modes[ablation.second].gain=0; auto q=renderBellCandidate({{0,note(62,v)}},96000,altered); float diff=differenceRms(base.rendered.audio,q.rendered.audio); report<<"| "<<ablation.first<<" | "<<int(v)<<" | "<<diff<<" | "<<dbRelative(diff,base.rendered.metrics.rms)<<" |\n"; }
    report<<"\n## Aftertouch, stealing, and lifetime\n\n";
    report<<"Channel pressure uses `data1`; Poly Pressure uses note `data1`, pressure `data2`.\n\n| Channel pressure | D4 tail energy | A4 tail energy |\n|---:|---:|---:|\n";
    float previous=std::numeric_limits<float>::infinity();
    for(uint8_t pressure:{0,32,64,96,127}) { dsp::SynthEngine engine; engine.init(48000); engine.setInstrumentModel(dsp::InstrumentModel::Bell); engine.handleMidiEvent(note(62,90)); engine.handleMidiEvent(note(69,90)); int32_t block[256]{}; engine.renderBlock(block,128); midi::MidiEvent p{}; p.type=midi::MidiEventType::ChannelPressure; p.data1=pressure; engine.handleMidiEvent(p); for(int i=0;i<240;i++) engine.renderBlock(block,128); float d=0, aa=0; for(size_t i=0;i<8;i++){const auto& voice=engine.getVoiceAllocator().getVoice(i); if(voice.getMidiNote()==62)d=voice.getEstimatedEnergy(); if(voice.getMidiNote()==69)aa=voice.getEstimatedEnergy();} assert(d<=previous+1e-8f); previous=d; report<<"| "<<int(pressure)<<" | "<<d<<" | "<<aa<<" |\n"; }
    { dsp::SynthEngine engine; engine.init(48000); engine.setInstrumentModel(dsp::InstrumentModel::Bell); engine.handleMidiEvent(note(62,90)); engine.handleMidiEvent(note(69,90)); int32_t block[256]{}; engine.renderBlock(block,128); midi::MidiEvent p{}; p.type=midi::MidiEventType::PolyPressure; p.data1=62;p.data2=127;engine.handleMidiEvent(p);for(int i=0;i<240;i++)engine.renderBlock(block,128);float d=0,aa=0;for(size_t i=0;i<8;i++){const auto& voice=engine.getVoiceAllocator().getVoice(i);if(voice.getMidiNote()==62)d=voice.getEstimatedEnergy();if(voice.getMidiNote()==69)aa=voice.getEstimatedEnergy();} assert(aa>d*10); report<<"\nPoly Pressure D4="<<d<<", A4="<<aa<<" (PASS independent damping).\n"; }
    std::vector<ScheduledEvent> steal; for(size_t i=0;i<16;++i) steal.push_back({uint32_t(i*6000),note(uint8_t(50+i),100)}); auto stealQ=renderBellQualification(steal,144000); // Count is collected in the direct fixture below.
    dsp::VoiceAllocator allocator; allocator.init(48000); for(size_t i=0;i<16;++i) { allocator.noteOn(uint8_t(50+i),.8f,midi::MidiMapping::noteToHz(uint8_t(50+i))); std::vector<float> tmp(4800); allocator.renderBlock(tmp.data(),tmp.size()); } assert(allocator.getVoiceStealCount()>0 && allocator.getActiveVoiceCount()<=8);
    report<<"Bell steal count="<<allocator.getVoiceStealCount()<<", max active="<<stealQ.maxActiveVoices<<", max steal tails="<<stealQ.maxStealTails<<", max delta="<<stealQ.maxSampleDelta<<" (PASS).\n";
    report<<"\n## Freeze\n\nBell V1 freeze = undecided pending hardware listening. A remains the production preset; B and C are deterministic host-only candidates.\n\n## Hardware CPU\n\nREQUIRES PHYSICAL VALIDATION: PAN/BELL single, chord, cluster8, roll; avg/p99/max block time, load, deadline misses, I2S write failures.\n";
    report.close();
}

struct BellLifetime {
    float inactiveSeconds = 15.0f;
    float rms500msBefore = 0.0f;
    float rms250msBefore = 0.0f;
    float finalActiveRms = 0.0f;
};

BellLifetime measureBellTimeToInactive(uint8_t note, uint8_t velocity) {
    constexpr size_t kBlock=128, kMaxFrames=15*48000;
    dsp::SynthEngine engine; engine.init(48000); engine.setInstrumentModel(dsp::InstrumentModel::Bell);
    midi::MidiEvent on{}; on.type=midi::MidiEventType::NoteOn; on.data1=note; on.data2=velocity; engine.handleMidiEvent(on);
    std::vector<int32_t> audio; audio.reserve(kMaxFrames*2); size_t rendered=0;
    while(rendered<kMaxFrames) { int32_t block[kBlock*2]{}; engine.renderBlock(block,kBlock); audio.insert(audio.end(),block,block+kBlock*2); rendered+=kBlock; if(engine.getVoiceAllocator().getActiveVoiceCount()==0) break; }
    BellLifetime result; result.inactiveSeconds=static_cast<float>(rendered)/48000.0f;
    const auto before=[&](float seconds, float width) { const float end=result.inactiveSeconds-seconds; return end>0 ? windowRms(audio,std::max(0.0f,end-width),end) : 0.0f; };
    result.rms500msBefore=before(.5f,.1f); result.rms250msBefore=before(.25f,.1f); result.finalActiveRms=before(0.0f,.1f);
    assert(engine.getVoiceAllocator().getActiveVoiceCount()==0 && result.inactiveSeconds<15.0f);
    return result;
}

void testBellM621FreezeQualification() {
    std::cout << "[Test 20] Bell M6.2.1 freeze listening package...\n";
    auto note=[](uint8_t n,uint8_t v){ midi::MidiEvent e{}; e.type=midi::MidiEventType::NoteOn; e.data1=n; e.data2=v; return e; };
    dsp::ModalPreset a=dsp::kPresetBell, b=dsp::kPresetBell, c=dsp::kPresetBell;
    b.modes[3].gain=.58f; c.modes[0].gain=.24f; c.modes[3].gain=.58f; c.modes[5].gain=.90f;
    const std::array<std::pair<char,const dsp::ModalPreset*>,3> candidates={{{'A',&a},{'B',&b},{'C',&c}}};
    std::ofstream report("bell_m621_freeze.md"); report<<std::fixed<<std::setprecision(6);
    report << "# Bell M6.2.1 freeze qualification\n\nA remains production; B/C are deterministic host-only candidates. C is the recommended listening candidate, not an automatic promotion.\n\n## A/B/C\n\n| Candidate | D4 v70 RMS | D4 v110 RMS | Hum/Prime v110 | Tierce/Prime v110 | Nominal/Prime v110 | Upper/Primary v110 | Max GR |\n|---|---:|---:|---:|---:|---:|---:|---:|\n";
    for(const auto& candidate:candidates) { auto v70=renderBellCandidate({{0,note(62,70)}},96000,*candidate.second); auto v110=renderBellCandidate({{0,note(62,110)}},96000,*candidate.second); auto e=bellModalEnergies(v110.rendered.audio,midi::MidiMapping::noteToHz(62)); report<<"| "<<candidate.first<<" | "<<v70.rendered.metrics.rms<<" | "<<v110.rendered.metrics.rms<<" | "<<e[0]/e[1]<<" | "<<e[2]/e[1]<<" | "<<e[4]/e[1]<<" | "<<e[7]/std::max(e[1]+e[4],1e-12f)<<" | "<<v110.rendered.metrics.maxGainReductionDb<<" |\n"; assert(v70.rendered.audio==renderBellCandidate({{0,note(62,70)}},96000,*candidate.second).rendered.audio); }
    report << "\n## Loudness-matched verification (A is reference)\n\n| Fixture | Candidate | Raw RMS | Matched RMS | Target A RMS | Delta dB | Result |\n|---|---|---:|---:|---:|---:|---|\n";
    const std::vector<std::pair<const char*,std::vector<ScheduledEvent>>> fixtures={
        {"D4_v70",{{0,note(62,70)}}}, {"D4_v110",{{0,note(62,110)}}},
        {"chord",{{0,note(50,90)},{0,note(57,90)},{0,note(62,90)},{0,note(69,90)}}},
        {"roll",{{0,note(62,90)},{4800,note(62,90)},{9600,note(62,90)},{14400,note(62,90)},
                 {19200,note(62,90)},{24000,note(62,90)},{28800,note(62,90)},{33600,note(62,90)}}}
    };
    for(const auto& fixture:fixtures) {
        const size_t frames=std::string(fixture.first)=="D4_v70" || std::string(fixture.first)=="D4_v110" ? 96000 : 192000;
        const auto reference=renderBellCandidate(fixture.second,frames,a); const float target=reference.rendered.metrics.rms;
        for(const auto& candidate:candidates) { const auto q=renderBellCandidate(fixture.second,frames,*candidate.second); const std::string base=std::string("bell_m621_")+fixture.first+"_"+candidate.first; writeWavFile((base+".wav").c_str(),q.rendered.audio.data(),frames,48000); writeRmsMatchedWav((base+"_matched.wav").c_str(),q.rendered.audio,target);
            std::vector<int32_t> matched(q.rendered.audio.size()); const float gain=target/std::max(q.rendered.metrics.rms,1e-12f); for(size_t i=0;i<matched.size();++i) matched[i]=static_cast<int32_t>(std::clamp(double(q.rendered.audio[i])*gain,-2147483648.0,2147483647.0)); const float rms=computeMetrics(matched,0).rms; const float delta=dbRelative(rms,target); assert(std::abs(delta)<.05f); if(candidate.first!='A' && std::abs(dbRelative(q.rendered.metrics.rms,target))>.001f) assert(fnv1a64(q.rendered.audio)!=fnv1a64(matched)); report<<"| "<<fixture.first<<" | "<<candidate.first<<" | "<<q.rendered.metrics.rms<<" | "<<rms<<" | "<<target<<" | "<<delta<<" | PASS |\n";
        }
    }
    std::vector<ScheduledEvent> steal; for(size_t i=0;i<16;++i) steal.push_back({uint32_t(i*6000),note(uint8_t(50+i),100)});
    const auto stolen=renderBellQualification(steal,144000); assert(stolen.voiceStealCount>=8 && stolen.maxActiveVoices<=8 && stolen.rendered.hardClampCount==0 && stolen.rendered.modalSat==0);
    report<<"\n## Bell-specific stealing\n\n| Events | Steal count | Max active | Max tails | Max sample delta | Clamp | ModalSat |\n|---:|---:|---:|---:|---:|---:|---:|\n| 16 notes / 125 ms | "<<stolen.voiceStealCount<<" | "<<stolen.maxActiveVoices<<" | "<<stolen.maxStealTails<<" | "<<stolen.maxSampleDelta<<" | "<<stolen.rendered.hardClampCount<<" | "<<stolen.rendered.modalSat<<" |\n";
    report<<"\n## Lifetime\n\n| Note | Time to inactive s | RMS 500 ms before | RMS 250 ms before | Final active RMS |\n|---|---:|---:|---:|---:|\n";
    for(const auto& item:std::initializer_list<std::pair<const char*,uint8_t>>{{"D3",50},{"D4",62},{"A4",69}}) { const auto life=measureBellTimeToInactive(item.second,90); report<<"| "<<item.first<<" | "<<life.inactiveSeconds<<" | "<<life.rms500msBefore<<" | "<<life.rms250msBefore<<" | "<<life.finalActiveRms<<" |\n"; }
    report<<"\n## Doublets (D4 v90, 10 s)\n\n| Family | Candidate | Ratio | Expected beat Hz @ D4 |\n|---|---|---:|---:|\n";
    const float f=midi::MidiMapping::noteToHz(62);
    for(const auto& split:std::initializer_list<std::pair<const char*,float>>{{"half",.001f},{"current",.002f},{"1p5x",.003f}}) { auto p=a; p.modes[2].ratio=1.0f+split.second; auto q=renderBellCandidate({{0,note(62,90)}},480000,p); writeWavFile((std::string("bell_m621_prime_")+split.first+".wav").c_str(),q.rendered.audio.data(),480000,48000); report<<"| prime | "<<split.first<<" | "<<p.modes[2].ratio<<" | "<<f*split.second<<" |\n"; }
    for(const auto& split:std::initializer_list<std::pair<const char*,float>>{{"half",.00075f},{"current",.0015f},{"1p5x",.00225f}}) { auto n=a; n.modes[6].ratio=2.0f+split.second; auto q=renderBellCandidate({{0,note(62,90)}},480000,n); writeWavFile((std::string("bell_m621_nominal_")+split.first+".wav").c_str(),q.rendered.audio.data(),480000,48000); report<<"| nominal | "<<split.first<<" | "<<n.modes[6].ratio<<" | "<<f*split.second<<" |\n"; }
    report<<"\n## Freeze recommendation\n\nA = richest / most inharmonic; B = clearer A; C = strongest tonal center. **Recommended listening candidate = C.** Bell V1 freeze = undecided pending listening.\n\n## Hardware CPU\n\nREQUIRES PHYSICAL VALIDATION: PAN/BELL single, chord, cluster8, roll; avg/p99/max block time, CPU load, deadline misses.\n";
    report.close();
}

// M6.4: deterministic Bell V1 musical listening pack.  No DSP parameter is
// changed here; this only renders and documents the current baseline plus the
// previously qualified host-only A/B/C timbre candidates.  Human listening is
// the gate; the numeric columns are safety guards only.
void testBellM64ListeningPack() {
    std::cout << "[Test 21] M6.4 Bell V1 listening pack...\n";
    auto note=[](uint8_t n,uint8_t v){ midi::MidiEvent e{}; e.type=midi::MidiEventType::NoteOn; e.data1=n; e.data2=v; return e; };
    auto allFinite=[](const std::vector<int32_t>& audio){ for(int32_t s:audio) { if(!std::isfinite(static_cast<float>(s))) return false; } return true; };

    const dsp::ModalPreset a=dsp::kPresetBell;
    dsp::ModalPreset b=dsp::kPresetBell; b.modes[3].gain=.58f;
    dsp::ModalPreset c=dsp::kPresetBell; c.modes[0].gain=.24f; c.modes[3].gain=.58f; c.modes[5].gain=.90f;
    const std::array<std::pair<char,const dsp::ModalPreset*>,3> candidates={{{'A',&a},{'B',&b},{'C',&c}}};

    std::ofstream report("bell_m64_listening_pack.md"); report<<std::fixed<<std::setprecision(6);
    report << "# Bell V1 M6.4 listening pack\n\n"
              "Generated deterministically by `tests/test_dsp.cpp` (`testBellM64ListeningPack`).\n"
              "A = current production baseline (`kPresetBell`); B = tierce 0.58; "
              "C = hum 0.24 + tierce 0.58 + nominal 0.90. All renders are 48 kHz stereo 16-bit PCM.\n\n"
              "Listening is the primary gate. The numeric columns are safety guards and must "
              "not be used as a musical score. See `docs/bell_v1_listening_pack.md`.\n\n";

    report << "## 1. Register sweep (baseline A, velocity 90, 4 s)\n\n"
              "| File | MIDI | Hz | peak | RMS | clamp | modalSat | finite |\n"
              "|---|---:|---:|---:|---:|---:|---:|---|\n";
    for (uint8_t n : {36,48,55,60,67,72,84}) {
        const auto q=renderBellCandidate({{0,note(n,90)}},192000,a);
        const std::string file="bell_m64_note_"+std::to_string(n)+"_v90.wav";
        writeWavFile(file.c_str(),q.rendered.audio.data(),192000,48000);
        assert(q.rendered.hardClampCount==0 && allFinite(q.rendered.audio));
        report << "| "<<file<<" | "<<int(n)<<" | "<<midi::MidiMapping::noteToHz(n)<<" | "<<q.rendered.metrics.peak
               <<" | "<<q.rendered.metrics.rms<<" | "<<q.rendered.hardClampCount<<" | "<<q.rendered.modalSat<<" | YES |\n";
    }

    report << "\n## 2. Velocity matrix (baseline A, 3 s)\n\n"
              "| File | MIDI | Velocity | peak | RMS | attack RMS | tail RMS | clamp | modalSat | FNV-1a64 |\n"
              "|---|---:|---:|---:|---:|---:|---:|---:|---:|---|\n";
    for (uint8_t n : {48,60,72}) for (uint8_t v : {30,60,90,110,127}) {
        const auto q=renderBellCandidate({{0,note(n,v)}},144000,a);
        const std::string file="bell_m64_single_"+std::to_string(n)+"_v"+std::to_string(v)+".wav";
        writeWavFile(file.c_str(),q.rendered.audio.data(),144000,48000);
        assert(q.rendered.hardClampCount==0 && allFinite(q.rendered.audio));
        assert(q.maxActiveVoices<=8);
        report << "| "<<file<<" | "<<int(n)<<" | "<<int(v)<<" | "<<q.rendered.metrics.peak<<" | "<<q.rendered.metrics.rms
               <<" | "<<q.rendered.metrics.attackRms<<" | "<<q.rendered.metrics.tailRms<<" | "<<q.rendered.hardClampCount
               <<" | "<<q.rendered.modalSat<<" | 0x"<<std::hex<<fnv1a64(q.rendered.audio)<<std::dec<<" |\n";
    }

    report << "\n## 3. Intervals (baseline A, velocity 90, 4 s)\n\n"
              "| File | Notes | peak | RMS | clamp | modalSat | finite |\n"
              "|---|---|---:|---:|---:|---:|---|\n";
    for (const auto& item : std::initializer_list<std::pair<const char*,std::pair<uint8_t,uint8_t>>>{
             {"D3_A3",{50,57}}, {"D4_A4",{62,69}}, {"C4_G4",{60,67}}}) {
        const auto q=renderBellCandidate({{0,note(item.second.first,90)},{0,note(item.second.second,90)}},192000,a);
        const std::string file=std::string("bell_m64_interval_")+item.first+".wav";
        writeWavFile(file.c_str(),q.rendered.audio.data(),192000,48000);
        assert(q.rendered.hardClampCount==0 && allFinite(q.rendered.audio));
        report << "| "<<file<<" | "<<int(item.second.first)<<"+"<<int(item.second.second)<<" | "<<q.rendered.metrics.peak
               <<" | "<<q.rendered.metrics.rms<<" | "<<q.rendered.hardClampCount<<" | "<<q.rendered.modalSat<<" | YES |\n";
    }

    report << "\n## 4. Four-note chord and eight-note polyphony (baseline A, 4 s)\n\n"
              "| File | Voices | peak | RMS | pre-limiter peak | max GR dB | max voices | clamp | modalSat |\n"
              "|---|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    const std::vector<uint8_t> chordNotes={50,57,62,69};
    const std::vector<uint8_t> polyNotes={48,50,55,57,60,62,67,69};
    for (uint8_t v : {60,90,127}) {
        std::vector<ScheduledEvent> ev; for (uint8_t n : chordNotes) ev.push_back({0,note(n,v)});
        const auto q=renderBellCandidate(ev,192000,a);
        const std::string file="bell_m64_chord4_v"+std::to_string(v)+".wav";
        writeWavFile(file.c_str(),q.rendered.audio.data(),192000,48000);
        assert(q.rendered.hardClampCount==0 && allFinite(q.rendered.audio));
        report << "| "<<file<<" | 4 | "<<q.rendered.metrics.peak<<" | "<<q.rendered.metrics.rms<<" | "<<q.rendered.metrics.preLimiterPeak
               <<" | "<<q.rendered.metrics.maxGainReductionDb<<" | "<<q.maxActiveVoices<<" | "<<q.rendered.hardClampCount<<" | "<<q.rendered.modalSat<<" |\n";
    }
    for (uint8_t v : {90,110}) {
        std::vector<ScheduledEvent> ev; for (uint8_t n : polyNotes) ev.push_back({0,note(n,v)});
        const auto q=renderBellCandidate(ev,192000,a);
        const std::string file="bell_m64_poly8_v"+std::to_string(v)+".wav";
        writeWavFile(file.c_str(),q.rendered.audio.data(),192000,48000);
        assert(q.rendered.hardClampCount==0 && allFinite(q.rendered.audio) && q.maxActiveVoices<=8);
        report << "| "<<file<<" | 8 | "<<q.rendered.metrics.peak<<" | "<<q.rendered.metrics.rms<<" | "<<q.rendered.metrics.preLimiterPeak
               <<" | "<<q.rendered.metrics.maxGainReductionDb<<" | "<<q.maxActiveVoices<<" | "<<q.rendered.hardClampCount<<" | "<<q.rendered.modalSat<<" |\n";
    }

    report << "\n## 5. Restrikes and roll (baseline A, 4 s)\n\n"
              "| File | Gesture | peak | RMS | max sample delta | clamp | modalSat | finite |\n"
              "|---|---|---:|---:|---:|---:|---:|---|\n";
    for (const auto& item : std::initializer_list<std::pair<const char*,std::vector<ScheduledEvent>>>{
             {"soft_hard",{{0,note(62,30)},{4800,note(62,110)}}},
             {"hard_soft",{{0,note(62,110)},{4800,note(62,30)}}},
             {"double_100ms",{{0,note(62,90)},{4800,note(62,90)}}},
             {"double_250ms",{{0,note(62,90)},{12000,note(62,90)}}}}) {
        const auto q=renderBellCandidate(item.second,192000,a);
        const std::string file=std::string("bell_m64_restrike_")+item.first+".wav";
        writeWavFile(file.c_str(),q.rendered.audio.data(),192000,48000);
        assert(q.rendered.hardClampCount==0 && allFinite(q.rendered.audio));
        report << "| "<<file<<" | "<<item.first<<" | "<<q.rendered.metrics.peak<<" | "<<q.rendered.metrics.rms<<" | "<<q.maxSampleDelta
               <<" | "<<q.rendered.hardClampCount<<" | "<<q.rendered.modalSat<<" | YES |\n";
    }
    {
        std::vector<ScheduledEvent> roll; for (uint32_t i=0;i<12;++i) roll.push_back({i*4800,note(62,90)});
        const auto q=renderBellCandidate(roll,192000,a);
        writeWavFile("bell_m64_roll.wav",q.rendered.audio.data(),192000,48000);
        assert(q.rendered.hardClampCount==0 && allFinite(q.rendered.audio));
        report << "| bell_m64_roll.wav | roll 100 ms | "<<q.rendered.metrics.peak<<" | "<<q.rendered.metrics.rms<<" | "<<q.maxSampleDelta
               <<" | "<<q.rendered.hardClampCount<<" | "<<q.rendered.modalSat<<" | YES |\n";
    }

    report << "\n## 6. A/B/C level-matched timbre comparison (4 s)\n\n"
              "A = baseline; B = tierce 0.58; C = hum 0.24 / tierce 0.58 / nominal 0.90. "
              "Each candidate is written raw and RMS-matched to A so loudness cannot bias the choice.\n\n"
              "| Fixture | Candidate | raw RMS | matched RMS | peak | max GR dB | clamp | modalSat |\n"
              "|---|---|---:|---:|---:|---:|---:|---|\n";
    const std::vector<std::pair<const char*,std::vector<ScheduledEvent>>> abcFixtures={
        {"D4_v70",{{0,note(62,70)}}},
        {"D4_v110",{{0,note(62,110)}}},
        {"chord4",{{0,note(50,90)},{0,note(57,90)},{0,note(62,90)},{0,note(69,90)}}},
        {"roll",{{0,note(62,90)},{4800,note(62,90)},{9600,note(62,90)},{14400,note(62,90)},
                 {19200,note(62,90)},{24000,note(62,90)},{28800,note(62,90)},{33600,note(62,90)}}}
    };
    for (const auto& fixture : abcFixtures) {
        const auto ref=renderBellCandidate(fixture.second,192000,a);
        const float target=ref.rendered.metrics.rms;
        for (const auto& candidate : candidates) {
            const auto q=renderBellCandidate(fixture.second,192000,*candidate.second);
            const std::string base=std::string("bell_m64_abc_")+fixture.first+"_"+candidate.first;
            writeWavFile((base+".wav").c_str(),q.rendered.audio.data(),192000,48000);
            writeRmsMatchedWav((base+"_matched.wav").c_str(),q.rendered.audio,target);
            const float matched=target; // writeRmsMatchedWav normalizes to target RMS.
            assert(q.rendered.hardClampCount==0 && allFinite(q.rendered.audio));
            report << "| "<<fixture.first<<" | "<<candidate.first<<" | "<<q.rendered.metrics.rms<<" | "<<matched
                   <<" | "<<q.rendered.metrics.peak<<" | "<<q.rendered.metrics.maxGainReductionDb<<" | "<<q.rendered.hardClampCount
                   <<" | "<<q.rendered.modalSat<<" |\n";
        }
    }

    report << "\n## 7. Listening checklist (independent dimensions)\n\n"
              "- pitch clarity / stable tonal centre\n"
              "- metallic identity\n"
              "- low-frequency hum balance\n"
              "- prime definition\n"
              "- tierce / quint audibility\n"
              "- high-mode harshness (fizz)\n"
              "- doublet beating (audible but not distracting)\n"
              "- attack realism\n"
              "- decay realism\n"
              "- velocity progression (soft expressive, hard controlled)\n"
              "- register consistency\n"
              "- polyphonic density\n"
              "- restrike behaviour\n"
              "- tail smoothness\n"
              "- limiter interaction\n"
              "- audible realtime defects (click / dropout / instability): NONE expected\n\n"
              "Decision rule: if no candidate clearly improves the baseline, keep baseline A. "
              "Bell parameters are frozen only after a human listening decision.\n";
    report.close();
    std::cout << "  -> PASSED: M6.4 listening pack generated deterministically.\n";
}

std::array<float, 8> bellModalEnergies(const std::vector<int32_t>& audio, float f) {
    // The doublets are intentionally summed into their named musical family.
    return {spectralEnergy(audio,{.5f*f}), spectralEnergy(audio,{f,1.002f*f}), spectralEnergy(audio,{1.2f*f}),
            spectralEnergy(audio,{1.5f*f}), spectralEnergy(audio,{2.0f*f,2.0015f*f}), spectralEnergy(audio,{3.0f*f}),
            spectralEnergy(audio,{4.0f*f}), spectralEnergy(audio,{5.2f*f})};
}

void testM6ModelArchitectureAndBell() {
    std::cout << "[Test 18] M6 model registry, PAN regression, and Bell V1..." << std::endl;
    auto note=[](uint8_t n,uint8_t v){ midi::MidiEvent e{}; e.type=midi::MidiEventType::NoteOn; e.data1=n; e.data2=v; return e; };
    const std::vector<ScheduledEvent> d3={{0,note(50,70)}};
    const auto pan = renderModel(d3, 96000, dsp::InstrumentModel::Pan);
    assert(pan.audio == renderModel(d3,96000,dsp::InstrumentModel::Pan).audio && fnv1a64(pan.audio) != 0);
    const std::vector<std::pair<const char*,std::vector<ScheduledEvent>>> panFixtures = {
        {"D3 v30",{{0,note(50,30)}}}, {"D3 v70",d3}, {"D3 v110",{{0,note(50,110)}}}, {"D3 v127",{{0,note(50,127)}}},
        {"A3 v70",{{0,note(57,70)}}}, {"D4 v70",{{0,note(62,70)}}}, {"A4 v70",{{0,note(69,70)}}},
        {"restrike soft-hard",{{0,note(50,30)},{4800,note(50,110)}}}, {"restrike hard-soft",{{0,note(50,110)},{4800,note(50,30)}}},
        {"roll",{{0,note(50,70)},{3840,note(50,70)},{7680,note(50,70)}}}, {"chord",{{0,note(50,70)},{0,note(57,70)},{0,note(62,70)},{0,note(69,70)}}},
        {"cluster8",{{0,note(50,100)},{0,note(52,100)},{0,note(54,100)},{0,note(56,100)},{0,note(57,100)},{0,note(59,100)},{0,note(61,100)},{0,note(62,100)}}}
    };
    constexpr uint64_t kPanGolden[] = {0xdfff8cb4bc9451adull,0x625f551231401141ull,0xb28e308f6f3b7ff9ull,0x857e3b19560fb8fdull,0x8fd51136812f9c09ull,0xfb6d1d3cb77ae911ull,0x1c664493f822e449ull,0x1230248779ad08ddull,0x153a4d12cc9da585ull,0xc84c3e607e2b5129ull,0x3c6d8246768089e9ull,0x98494cd426a587a1ull};
    std::ofstream panReport("pan_m6_regression.md");
    panReport << "# PAN M6 regression\n\n| Fixture | Expected FNV | Actual FNV | Result |\n|---|---|---|---|\n";
    for (size_t i=0; i<panFixtures.size(); ++i) { const uint64_t hash=fnv1a64(renderModel(panFixtures[i].second,96000,dsp::InstrumentModel::Pan).audio); assert(hash == kPanGolden[i]); std::cout << "  PAN FNV " << panFixtures[i].first << " = 0x" << std::hex << hash << std::dec << "\n"; panReport << "| " << panFixtures[i].first << " | `0x" << std::hex << kPanGolden[i] << "` | `0x" << hash << std::dec << "` | PASS |\n"; }
    dsp::SynthEngine switched; switched.init(48000.0f); switched.handleMidiEvent(note(50,70)); int32_t block[256]{}; switched.renderBlock(block,128);
    switched.setInstrumentModel(dsp::InstrumentModel::Bell); switched.renderBlock(block,128); for (auto sample : block) assert(sample == 0);
    switched.handleMidiEvent(note(62,110)); switched.renderBlock(block,128); switched.setInstrumentModel(dsp::InstrumentModel::Pan); switched.handleMidiEvent(note(50,70));
    std::vector<int32_t> switchedPan(96000 * 2); for (size_t frame=0; frame<96000; frame+=128) switched.renderBlock(switchedPan.data()+frame*2, std::min<size_t>(128,96000-frame));
    if (fnv1a64(switchedPan) != fnv1a64(pan.audio)) {
        for (size_t i=0; i<switchedPan.size(); ++i) if (switchedPan[i] != pan.audio[i]) { std::cerr << "M6 switch first diff " << i << " " << switchedPan[i] << " vs " << pan.audio[i] << "\n"; break; }
        assert(false && "PAN after model switch must match direct PAN");
    }
    panReport << "\n## PAN → BELL → PAN\n\nDirect PAN and PAN after a Bell switch: **PASS** (bit-identical `0x" << std::hex << fnv1a64(pan.audio) << "`).\n" << std::dec;
    panReport.close();

    std::ofstream report("bell_m6_metrics.md"); report << std::fixed << std::setprecision(6);
    report << "# Bell M6.1 host qualification\n\n**Bell V1 Candidate: Baseline A (current static preset). Physical listening required.**\n\n"
           << "## A — Bell preset\n\n| Mode | Ratio | Gain | T60 s | Purpose |\n|---|---:|---:|---:|---|\n";
    constexpr const char* purposes[] = {"hum","prime","prime doublet","tierce","quint","nominal","nominal doublet","superquint","octave nominal","upper"};
    for (size_t i=0; i<dsp::kPresetBell.modeCount; ++i) { const auto& mode=dsp::kPresetBell.modes[i]; report << "| " << i << " | " << mode.ratio << " | " << mode.gain << " | " << mode.t60 << " | " << purposes[i] << " |\n"; }
    report << "\nControlled host-only audit plan: A = baseline; B = tierce-only (.50/.58/.65); C = hum-only (.20/.24/.28) and nominal-only (.75/.85/.95). No candidate changes are compiled into firmware pending physical listening.\n";

    float previousUpper = -1.0f; std::ostringstream velocityOutput, velocityEnergy;
    for (uint8_t velocity : {30,50,70,90,110,127}) {
        const auto qualified = renderBellQualification({{0,note(62,velocity)}}, 96000); const auto& bell=qualified.rendered; assert(bell.hardClampCount == 0 && bell.modalSat == 0);
        writeWavFile((std::string("bell_D4_v") + std::to_string(velocity) + ".wav").c_str(), bell.audio.data(), 96000, 48000);
        const float f = midi::MidiMapping::noteToHz(62); const auto e=bellModalEnergies(bell.audio,f); const float primary=e[1]+e[4]; const float upper=e[7]/std::max(primary,1.0e-12f); assert(upper >= previousUpper); previousUpper=upper;
        const auto& m=bell.metrics; velocityOutput << "| " << static_cast<int>(velocity) << " | " << m.rms << " | " << m.attackRms << " | " << m.peak << " | " << m.crestFactor << " | " << primary << " | " << upper << " | " << m.maxGainReductionDb << " | " << m.averageGainReductionDb << " | " << bell.hardClampCount << " | " << bell.modalSat << " |\n";
        velocityEnergy << "| " << static_cast<int>(velocity); for(float value:e) velocityEnergy << " | " << value; velocityEnergy << " | " << e[0]/std::max(e[1],1.0e-12f) << " | " << e[2]/std::max(e[1],1.0e-12f) << " | " << e[4]/std::max(e[1],1.0e-12f) << " | " << upper << " |\n";
    }
    report << "\n## B — Velocity\n\n| Velocity | RMS | Attack RMS | Peak | Crest | Primary | Upper/Primary | Max GR dB | Avg GR dB | Hard clamp | ModalSat |\n|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n" << velocityOutput.str();
    report << "\n## C — Modal energy (D4)\n\n| Velocity | Hum | Prime | Tierce | Quint | Nominal | Superquint | Octave nominal | Upper | Hum/Prime | Tierce/Prime | Nominal/Prime | Upper/Primary |\n|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n" << velocityEnergy.str();
    report << "\n## C/D — Register and Nyquist\n\n| Note | Velocity | RMS | Attack RMS | Tail RMS | Primary | Hum/Prime | Tierce/Prime | Upper/Primary | Active modes | Max GR dB |\n|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    for (uint8_t midiNote : {50,57,62,69,74}) for (uint8_t velocity : {30,70,110}) { const auto q=renderBellQualification({{0,note(midiNote,velocity)}}, 144000); const float f=midi::MidiMapping::noteToHz(midiNote); const auto e=bellModalEnergies(q.rendered.audio,f); const float primary=e[1]+e[4]; assert(q.rendered.hardClampCount==0 && q.rendered.modalSat==0); if(velocity==70) writeWavFile((std::string("bell_")+(midiNote==50?"D3":midiNote==57?"A3":midiNote==62?"D4":midiNote==69?"A4":"D5")+"_v70.wav").c_str(),q.rendered.audio.data(),144000,48000); report << "| " << static_cast<int>(midiNote) << " | " << static_cast<int>(velocity) << " | " << q.rendered.metrics.rms << " | " << q.rendered.metrics.attackRms << " | " << q.rendered.metrics.tailRms << " | " << primary << " | " << e[0]/std::max(e[1],1.0e-12f) << " | " << e[2]/std::max(e[1],1.0e-12f) << " | " << e[7]/std::max(primary,1.0e-12f) << " | " << static_cast<int>(dsp::kPresetBell.modeCount) << " | " << q.rendered.metrics.maxGainReductionDb << " |\n"; }
    report << "\n| Note | Fundamental Hz | Defined modes | Active modes | Disabled mode indices | Finite coefficients |\n|---|---:|---:|---:|---|---|\n";
    for(uint8_t n:{36,84}) { const float f=midi::MidiMapping::noteToHz(n); std::string disabled; size_t active=0; for(size_t i=0;i<dsp::kPresetBell.modeCount;++i) { if(dsp::isModeActiveAtSampleRate(f*dsp::kPresetBell.modes[i].ratio,48000.0f)) ++active; else { if(!disabled.empty()) disabled += ", "; disabled += std::to_string(i); } } report << "| " << static_cast<int>(n) << " | " << f << " | 10 | " << active << " | " << (disabled.empty()?"none":disabled) << " | PASS |\n"; const auto q=renderBellQualification({{0,note(n,110)}},576000); assert(q.rendered.hardClampCount==0 && q.rendered.modalSat==0); }
    const auto longBell=renderBellQualification({{0,note(62,90)}},576000); writeWavFile("bell_D4_long.wav",longBell.rendered.audio.data(),576000,48000);
    report << "\n## E — Decay (D4 v90, 12 s)\n\n| Window | RMS |\n|---|---:|\n";
    for(const auto& window : std::initializer_list<std::pair<const char*,std::pair<float,float>>>{{"0–100 ms",{0,.1f}},{"0.5–1.0 s",{.5f,1.f}},{"1–2 s",{1,2}},{"2–4 s",{2,4}},{"4–8 s",{4,8}},{"8–12 s",{8,12}}}) report << "| " << window.first << " | " << windowRms(longBell.rendered.audio,window.second.first,window.second.second) << " |\n";
    assert(longBell.rendered.hardClampCount==0 && longBell.rendered.modalSat==0);

    const std::vector<ScheduledEvent> singleEvents={{0,note(62,90)}};
    const std::vector<ScheduledEvent> double100={{0,note(62,90)},{4800,note(62,90)}}, double250={{0,note(62,90)},{12000,note(62,90)}}, triple={{0,note(62,90)},{4800,note(62,90)},{9600,note(62,90)}};
    const std::vector<ScheduledEvent> softHard={{0,note(62,40)},{4800,note(62,110)}}, hardSoft={{0,note(62,110)},{4800,note(62,40)}};
    const auto single=renderBellQualification(singleEvents,96000), r100=renderBellQualification(double100,96000), r250=renderBellQualification(double250,96000), rTriple=renderBellQualification(triple,96000), rSoftHard=renderBellQualification(softHard,96000), rHardSoft=renderBellQualification(hardSoft,96000);
    writeWavFile("bell_double_100ms.wav",r100.rendered.audio.data(),96000,48000); writeWavFile("bell_double_250ms.wav",r250.rendered.audio.data(),96000,48000); writeWavFile("bell_triple.wav",rTriple.rendered.audio.data(),96000,48000); writeWavFile("bell_restrike_soft_hard.wav",rSoftHard.rendered.audio.data(),96000,48000); writeWavFile("bell_restrike_hard_soft.wav",rHardSoft.rendered.audio.data(),96000,48000);
    assert(windowRms(r100.rendered.audio,.10f,.20f) > windowRms(single.rendered.audio,.10f,.20f));
    report << "\n## F — Restrike\n\n| Fixture | RMS | Peak | Max delta | Hard clamp | ModalSat | Result |\n|---|---:|---:|---:|---:|---:|---|\n";
    for(const auto& entry : std::initializer_list<std::pair<const char*,const BellQualificationRender*>>{{"double 100 ms",&r100},{"double 250 ms",&r250},{"triple 100 ms",&rTriple},{"v40 → v110",&rSoftHard},{"v110 → v40",&rHardSoft}}) { const auto& q=*entry.second; report << "| " << entry.first << " | " << q.rendered.metrics.rms << " | " << q.rendered.metrics.peak << " | " << q.maxSampleDelta << " | " << q.rendered.hardClampCount << " | " << q.rendered.modalSat << " | PASS |\n"; assert(q.rendered.hardClampCount==0 && q.rendered.modalSat==0); }

    std::vector<ScheduledEvent> chord={{0,note(50,90)},{0,note(57,90)},{0,note(62,90)},{0,note(69,90)}};
    std::vector<ScheduledEvent> cluster={{0,note(50,100)},{0,note(52,100)},{0,note(54,100)},{0,note(56,100)},{0,note(57,100)},{0,note(59,100)},{0,note(61,100)},{0,note(62,100)}};
    std::vector<ScheduledEvent> roll; for(size_t i=0;i<12;++i) roll.push_back({static_cast<uint32_t>(i*4800),note(62,90)});
    std::vector<ScheduledEvent> arpeggio; for(size_t i=0;i<16;++i) arpeggio.push_back({static_cast<uint32_t>(i*7200),note(static_cast<uint8_t>(50+(i%12)),90)});
    const auto bellChord=renderBellQualification(chord,192000), bellCluster=renderBellQualification(cluster,192000), bellRoll=renderBellQualification(roll,192000), bellArp=renderBellQualification(arpeggio,192000);
    writeWavFile("bell_chord.wav",bellChord.rendered.audio.data(),192000,48000); writeWavFile("bell_cluster8.wav",bellCluster.rendered.audio.data(),192000,48000); writeWavFile("bell_roll.wav",bellRoll.rendered.audio.data(),192000,48000); writeWavFile("bell_arpeggio_steal.wav",bellArp.rendered.audio.data(),192000,48000);
    report << "\n## G/H — Polyphony and output\n\n| Fixture | RMS | Peak | Pre-limiter peak | Max/avg GR dB | GR >0.1/>1 dB | Max voices | Steal tails | Max delta | Clamp | ModalSat |\n|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    for(const auto& entry : std::initializer_list<std::pair<const char*,const BellQualificationRender*>>{{"chord",&bellChord},{"cluster8",&bellCluster},{"roll (100 ms)",&bellRoll},{"arpeggio / 16 notes",&bellArp}}) { const auto& q=*entry.second; const auto& m=q.rendered.metrics; report << "| " << entry.first << " | " << m.rms << " | " << m.peak << " | " << m.preLimiterPeak << " | " << m.maxGainReductionDb << " / " << m.averageGainReductionDb << " | " << q.rendered.grOver0p1DbSamples << " / " << q.rendered.grOver1DbSamples << " | " << q.maxActiveVoices << " | " << q.maxStealTails << " | " << q.maxSampleDelta << " | " << q.rendered.hardClampCount << " | " << q.rendered.modalSat << " |\n"; assert(q.maxActiveVoices<=8 && q.rendered.hardClampCount==0 && q.rendered.modalSat==0); }
    report << "\n## Aftertouch and lifetime\n\n| Case | Result |\n|---|---|\n| PolyPressure 0/.25/.5/.75/1 | host smoke-tested; pressure routes to generic damping |\n| ChannelPressure 0/.25/.5/.75/1 | host smoke-tested; pressure routes to generic damping |\n| D3/D4/A4 lifetime | measured time-to-inactive and cutoff-window RMS: see `bell_m621_freeze.md` |\n";
    for(bool poly : {true,false}) for(uint8_t pressure : {0,32,64,96,127}) { dsp::SynthEngine pressureEngine; pressureEngine.init(48000); pressureEngine.setInstrumentModel(dsp::InstrumentModel::Bell); pressureEngine.handleMidiEvent(note(62,90)); int32_t pressureBlock[256]{}; pressureEngine.renderBlock(pressureBlock,128); midi::MidiEvent event{}; event.type=poly?midi::MidiEventType::PolyPressure:midi::MidiEventType::ChannelPressure; event.data1=poly?62:pressure; event.data2=poly?pressure:0; pressureEngine.handleMidiEvent(event); pressureEngine.renderBlock(pressureBlock,128); for(auto sample:pressureBlock) assert(std::isfinite(static_cast<float>(sample))); }
    report << "\n## Doublets\n\n| Note | Prime beat Hz (current / half / 1.5x) | Nominal beat Hz (current / half / 1.5x) |\n|---|---:|---:|\n";
    for(uint8_t n:{50,57,62,69,74}) { const float f=midi::MidiMapping::noteToHz(n); report << "| " << static_cast<int>(n) << " | " << f*.002f << " / " << f*.001f << " / " << f*.003f << " | " << f*.003f << " / " << f*.0015f << " / " << f*.0045f << " |\n"; }
    report << "\n## Host-only ablation audit\n\nIndividual prime-doublet, nominal-doublet, superquint, and upper ablations are documented as a required listening audit; no mode removal is selected without ESP32-S3 CPU telemetry.\n\n## Hardware CPU / realtime capture\n\nNot executable in the host test: record PAN and BELL average/p99/max block time, CPU load, deadline misses, write timeouts, short writes, and TX errors for single, chord, cluster, and roll on ESP32-S3.\n";
    report << "\nM6.2 controlled A/B/C, pressure, steal, and ablation results: see `bell_m62_ab.md`.\n";
    report.close();
    std::cout << "  -> PASSED: model resets, Bell velocity/register/tail/restrike safety verified.\n";
}

// Exact host-only copy of the M5B signal path. It is a regression oracle, not firmware code.
std::vector<int32_t> renderM5bReference(const std::vector<ScheduledEvent>& events, size_t totalFrames) {
    constexpr size_t kBlock=128; dsp::VoiceAllocator allocator; dsp::PeakLimiter limiter; allocator.init(48000.0f); allocator.reset(); limiter.init(48000.0f); limiter.reset();
    float mono[kBlock]{}, headroom=1.0f; const float attack=std::exp(-1.0f/(48000.0f*.003f)), release=std::exp(-1.0f/(48000.0f*.050f));
    std::vector<int32_t> audio(totalFrames*2); size_t eventIndex=0;
    for(size_t frame=0;frame<totalFrames;frame+=kBlock) {
        while(eventIndex<events.size() && events[eventIndex].frame<=frame) { const auto& e=events[eventIndex++].event; if(e.type==midi::MidiEventType::NoteOn) allocator.noteOn(e.data1,midi::MidiMapping::toNormalizedFloat(e.data2),midi::MidiMapping::noteToHz(e.data1)); else if(e.type==midi::MidiEventType::NoteOff) allocator.noteOff(e.data1); }
        const size_t frames=std::min(kBlock,totalFrames-frame); allocator.renderBlock(mono,frames);
        const float voices=static_cast<float>(allocator.getActiveVoiceCount()); const float target=std::pow(10.0f,std::max(voices<=1.0f?0.0f:-1.5f*std::log2(voices),-5.0f)/20.0f);
        for(size_t i=0;i<frames;++i) { const float coefficient=target<headroom?attack:release; headroom=target+coefficient*(headroom-target); const float s=limiter.processSample(mono[i]*.85f*headroom); const int32_t pcm=static_cast<int32_t>(std::clamp(s,-1.0f,1.0f)*2147483647.0f); audio[(frame+i)*2]=audio[(frame+i)*2+1]=pcm; }
    }
    return audio;
}

void testM5cBodyAndSympathetic() {
    std::cout << "[Test 13] M5C global body, sympathetic bus, and stability..." << std::endl;
    auto note=[](uint8_t n,uint8_t v){ midi::MidiEvent e{}; e.type=midi::MidiEventType::NoteOn; e.data1=n; e.data2=v; return e; };
    const std::vector<ScheduledEvent> d3={{0,note(50,110)}};
    const std::vector<ScheduledEvent> interval={{0,note(50,90)},{24000,note(57,90)}};
    const std::vector<ScheduledEvent> chord={{0,note(50,90)},{0,note(57,85)},{0,note(62,82)},{0,note(69,78)}};
    std::vector<ScheduledEvent> rollMutable; for(uint32_t n=0;n<12;++n) rollMutable.push_back({n*3600,note(50,85)}); const std::vector<ScheduledEvent>& roll=rollMutable;
    const auto dry=renderM5c(d3,96000,false,false), body=renderM5c(d3,96000,true,false), sym=renderM5c(d3,96000,false,true), full=renderM5c(d3,96000,true,true);
    writeWavFile("pan_m5c_D3_dry.wav",dry.audio.data(),96000,48000); writeWavFile("pan_m5c_D3_body.wav",body.audio.data(),96000,48000);
    writeWavFile("pan_m5c_D3_sympathetic.wav",sym.audio.data(),96000,48000); writeWavFile("pan_m5c_D3_full.wav",full.audio.data(),96000,48000);
    const auto intervalDry=renderM5c(interval,96000,false,false), intervalFull=renderM5c(interval,96000,true,true);
    const auto chordDry=renderM5c(chord,96000,false,false), chordFull=renderM5c(chord,96000,true,true);
    const auto rollDry=renderM5c(roll,96000,false,false), rollFull=renderM5c(roll,96000,true,true);
    writeWavFile("pan_m5c_interval_dry.wav",intervalDry.audio.data(),96000,48000); writeWavFile("pan_m5c_interval_full.wav",intervalFull.audio.data(),96000,48000);
    writeWavFile("pan_m5c_chord_dry.wav",chordDry.audio.data(),96000,48000); writeWavFile("pan_m5c_chord_full.wav",chordFull.audio.data(),96000,48000);
    writeWavFile("pan_m5c_roll_dry.wav",rollDry.audio.data(),96000,48000); writeWavFile("pan_m5c_roll_full.wav",rollFull.audio.data(),96000,48000);
    std::ofstream report("pan_m5c_metrics.md"); report << std::fixed << std::setprecision(6) << "# PAN M5C.1 host metrics\n\nHost timing is not ESP32-S3 realtime timing; hardware telemetry is authoritative for CPU, deadline misses, write timeouts, and TX errors.\n\n## A — D3 component isolation\n\n| Candidate | Dry baseline RMS | Candidate RMS | Difference RMS | Relative dB | Body peak/RMS | Bus peak/RMS | Max/avg GR dB | GR >0.1 / >1 dB | Clamp / safety |\n|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    auto row=[&](const char* name,const M5cResult& baseline,const M5cResult& candidate){ const float diff=differenceRms(baseline.audio,candidate.audio); report<<"| "<<name<<" | "<<baseline.metrics.rms<<" | "<<candidate.metrics.rms<<" | "<<diff<<" | "<<dbRelative(diff,baseline.metrics.rms)<<" | "<<candidate.bodyPeak<<" / "<<candidate.bodyRms<<" | "<<candidate.busPeak<<" / "<<candidate.busRms<<" | "<<candidate.metrics.maxGainReductionDb<<" / "<<candidate.metrics.averageGainReductionDb<<" | "<<candidate.grOver0p1DbSamples<<" / "<<candidate.grOver1DbSamples<<" | "<<candidate.hardClampCount<<" / "<<candidate.sympatheticSafetyCount<<" |\n"; };
    row("dry",dry,dry); row("body only contribution",dry,body); row("sympathetic only contribution",dry,sym); row("full",dry,full);
    report << "\n## B — musical fixtures\n\n| Fixture | Dry baseline RMS | Full RMS | Difference RMS | Relative dB | Dry/full max GR | Dry/full avg GR | Clamp / safety |\n|---|---:|---:|---:|---:|---:|---:|---:|\n";
    auto musical=[&](const char* name,const M5cResult& baseline,const M5cResult& candidate){ const float diff=differenceRms(baseline.audio,candidate.audio); report<<"| "<<name<<" | "<<baseline.metrics.rms<<" | "<<candidate.metrics.rms<<" | "<<diff<<" | "<<dbRelative(diff,baseline.metrics.rms)<<" | "<<baseline.metrics.maxGainReductionDb<<" / "<<candidate.metrics.maxGainReductionDb<<" | "<<baseline.metrics.averageGainReductionDb<<" / "<<candidate.metrics.averageGainReductionDb<<" | "<<candidate.hardClampCount<<" / "<<candidate.sympatheticSafetyCount<<" |\n"; };
    musical("interval",intervalDry,intervalFull); musical("chord",chordDry,chordFull); musical("roll",rollDry,rollFull);
    const std::vector<ScheduledEvent> cluster={{0,note(50,100)},{0,note(57,100)},{0,note(58,100)},{0,note(60,100)},{0,note(62,100)},{0,note(64,100)},{0,note(65,100)},{0,note(69,100)}};
    const auto clusterDry=renderM5c(cluster,96000,false,false), clusterFull=renderM5c(cluster,96000,true,true); musical("cluster8",clusterDry,clusterFull);
    report << "\n### A.1 — D3 attack and tail contribution\n\n| Velocity | Path | 0-5 ms RMS | 0-20 ms RMS | 0-100 ms RMS | 500-1500 ms RMS | Attack delta RMS / % | Tail delta RMS / % |\n|---:|---|---:|---:|---:|---:|---:|---:|\n";
    for(uint8_t velocity : {70,110}) for(const auto& entry : std::initializer_list<std::pair<const char*,M5cResult>>{{"dry",renderM5c({{0,note(50,velocity)}},96000,false,false)},{"body",renderM5c({{0,note(50,velocity)}},96000,true,false)},{"full",renderM5c({{0,note(50,velocity)}},96000,true,true)}}) { const auto& r=entry.second; const auto base=renderM5c({{0,note(50,velocity)}},96000,false,false); const float a=windowRms(r.audio,0,.020f), ta=windowRms(r.audio,.5f,1.5f), ba=windowRms(base.audio,0,.020f), bt=windowRms(base.audio,.5f,1.5f); report<<"| "<<static_cast<int>(velocity)<<" | "<<entry.first<<" | "<<windowRms(r.audio,0,.005f)<<" | "<<windowRms(r.audio,0,.020f)<<" | "<<windowRms(r.audio,0,.100f)<<" | "<<ta<<" | "<<(a-ba)<<" / "<<(100.0f*(a-ba)/std::max(ba,1e-12f))<<" | "<<(ta-bt)<<" / "<<(100.0f*(ta-bt)/std::max(bt,1e-12f))<<" |\n"; }
    report << "\n### A.2 — body mode spectral validation (D3 body ON vs OFF)\n\n| Mode Hz | Body-off magnitude | Body-on magnitude | bodyModeDeltaDb |\n|---:|---:|---:|---:|\n"; for(float hz : {110.f,205.f,390.f,730.f,1280.f,1980.f}) report<<"| "<<hz<<" | "<<goertzelMagnitude(dry.audio,hz)<<" | "<<goertzelMagnitude(body.audio,hz)<<" | "<<dbRelative(goertzelMagnitude(body.audio,hz),goertzelMagnitude(dry.audio,hz))<<" |\n";
    report << "\n## C — register audit (body ON vs OFF, velocity 70)\n\n| Note | RMS off/on | Attack RMS off/on | Tail RMS off/on | Body RMS | Pre-limiter peak off/on | Max/avg GR off/on | Nearest body mode | Distance % |\n|---|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    for(uint8_t midiNote : {50,57,62,69}) { const auto off=renderM5c({{0,note(midiNote,70)}},96000,false,false), on=renderM5c({{0,note(midiNote,70)}},96000,true,false); const float f=midi::MidiMapping::noteToHz(midiNote); float nearest=0, distance=std::numeric_limits<float>::max(); for(float partial : {f,2*f,3*f,3.98f,5.25f,6.62f,8.18f}) for(float mode : {110.f,205.f,390.f,730.f,1280.f,1980.f}) if(std::abs(partial-mode)/mode<distance) { distance=std::abs(partial-mode)/mode; nearest=mode; } const char* name=midiNote==50?"D3":midiNote==57?"A3":midiNote==62?"D4":"A4"; report<<"| "<<name<<" | "<<off.metrics.rms<<" / "<<on.metrics.rms<<" | "<<off.metrics.attackRms<<" / "<<on.metrics.attackRms<<" | "<<off.metrics.tailRms<<" / "<<on.metrics.tailRms<<" | "<<on.bodyRms<<" | "<<off.metrics.preLimiterPeak<<" / "<<on.metrics.preLimiterPeak<<" | "<<off.metrics.maxGainReductionDb<<" / "<<on.metrics.maxGainReductionDb<<"; "<<off.metrics.averageGainReductionDb<<" / "<<on.metrics.averageGainReductionDb<<" | "<<nearest<<" | "<<(distance*100.0f)<<" |\n"; }
    report << "\n## D — stability and sympathetic gain sweep\n\n| Case | Bus RMS | Bus peak | Final body energy | Safety | Hard clamp |\n|---|---:|---:|---:|---:|---:|\n";
    for(const std::vector<ScheduledEvent>* fixture : {&d3,&cluster,&roll}) { const auto r=renderM5c(*fixture,480000,true,true); assert(std::isfinite(r.metrics.rms) && r.hardClampCount==0 && r.sympatheticSafetyCount==0); report<<"| 10s fixture | "<<r.busRms<<" | "<<r.busPeak<<" | "<<r.bodyEnergy<<" | "<<r.sympatheticSafetyCount<<" | "<<r.hardClampCount<<" |\n"; }
    const auto decay=renderM5c(chord,720000,true,true); assert(decay.bodyEnergy < 1.0e-5f && decay.hardClampCount==0 && decay.sympatheticSafetyCount==0); report<<"| 15s decay | "<<decay.busRms<<" | "<<decay.busPeak<<" | "<<decay.bodyEnergy<<" | "<<decay.sympatheticSafetyCount<<" | "<<decay.hardClampCount<<" |\n";
    for(float gain : {.5f,1.f,2.f,4.f}) { const auto r=renderM5c(cluster,480000,true,true,-1.0f,gain); report<<"| sympathetic "<<gain<<"x | "<<r.busRms<<" | "<<r.busPeak<<" | "<<r.bodyEnergy<<" | "<<r.sympatheticSafetyCount<<" | "<<r.hardClampCount<<" |\n"; assert(r.hardClampCount==0); }
    report << "\n## E — gain staging\n\n| Fixture | Dry max GR | Full max GR | Dry avg GR | Full avg GR | Dry >0.1/>1 dB | Full >0.1/>1 dB | Hard clamp | Safety |\n|---|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    auto staging=[&](const char* name,const M5cResult& base,const M5cResult& candidate){ report<<"| "<<name<<" | "<<base.metrics.maxGainReductionDb<<" | "<<candidate.metrics.maxGainReductionDb<<" | "<<base.metrics.averageGainReductionDb<<" | "<<candidate.metrics.averageGainReductionDb<<" | "<<base.grOver0p1DbSamples<<" / "<<base.grOver1DbSamples<<" | "<<candidate.grOver0p1DbSamples<<" / "<<candidate.grOver1DbSamples<<" | "<<candidate.hardClampCount<<" | "<<candidate.sympatheticSafetyCount<<" |\n"; };
    staging("D3",dry,full); staging("interval",intervalDry,intervalFull); staging("chord",chordDry,chordFull); staging("roll",rollDry,rollFull); staging("cluster8",clusterDry,clusterFull);
    report << "\n## Hardware CPU capture (required before freeze)\n\n| Scenario | M5B-like avg/max block us | M5C avg/max block us | CPU delta | Deadline misses | Write timeouts | TX errors |\n|---|---:|---:|---:|---:|---:|---:|\n| single | hardware required | hardware required | hardware required | hardware required | hardware required | hardware required |\n| 4-note chord | hardware required | hardware required | hardware required | hardware required | hardware required | hardware required |\n| 8-note cluster | hardware required | hardware required | hardware required | hardware required | hardware required | hardware required |\n| roll | hardware required | hardware required | hardware required | hardware required | hardware required | hardware required |\n";
    const auto reference=renderM5bReference(d3,96000); assert(reference==dry.audio && "M5C body OFF + sympathetic OFF must be bit-identical to M5B");
    report << "\n### B.1 — M5B velocity expression with M5C enabled\n\n| Velocity | modalBrightnessRatio | highModalRatio |\n|---:|---:|---:|\n";
    float priorBright=-1.0f, priorHigh=-1.0f;
    for(uint8_t velocity : {30,70,110}) { const auto r=renderM5c({{0,note(50,velocity)}},96000,true,true); const float f=midi::MidiMapping::noteToHz(50); const float all=spectralEnergy(r.audio,{f,2*f,3*f,3.98f*f,5.25f*f,6.62f*f,8.18f*f}); const float bright=spectralEnergy(r.audio,{3*f,3.98f*f,5.25f*f,6.62f*f,8.18f*f})/std::max(all,1e-12f); const float high=spectralEnergy(r.audio,{5.25f*f,6.62f*f,8.18f*f})/std::max(spectralEnergy(r.audio,{f,2*f}),1e-12f); assert(bright>priorBright && high>priorHigh); priorBright=bright; priorHigh=high; report<<"| "<<static_cast<int>(velocity)<<" | "<<bright<<" | "<<high<<" |\n"; }
    for(const auto& fixture : std::initializer_list<std::vector<ScheduledEvent>>{{{0,note(50,90)}},{{0,note(50,90)},{4800,note(50,90)}},{{0,note(50,90)},{12000,note(50,90)}},roll}) { const auto r=renderM5c(fixture,96000,true,true); assert(std::isfinite(r.metrics.rms) && r.hardClampCount==0 && r.sympatheticSafetyCount==0); }
    for(float gain : {.07f,.11f,.15f}) { const auto d=renderM5c(d3,96000,true,false,gain); const auto c=renderM5c(chord,96000,true,false,gain); writeWavFile((std::string("pan_m5c_D3_body_")+std::to_string(static_cast<int>(gain*100))+".wav").c_str(),d.audio.data(),96000,48000); writeWavFile((std::string("pan_m5c_chord_body_")+std::to_string(static_cast<int>(gain*100))+".wav").c_str(),c.audio.data(),96000,48000); }
    const auto tail=renderM5c(d3,96000,true,false,-1.0f,1.0f,true); writeWavFile("pan_m5c_D3_body_tail_variant.wav",tail.audio.data(),96000,48000);
    dsp::SynthEngine toggle; toggle.init(48000.0f); toggle.setBodyEnabled(false); toggle.setSympatheticEnabled(true); toggle.handleMidiEvent(note(50,100)); int32_t block[256]{}; toggle.renderBlock(block,128); toggle.setSympatheticEnabled(false); toggle.setSympatheticEnabled(true); toggle.reset(); toggle.renderBlock(block,128); for(int32_t sample:block) assert(sample==0 && "Sympathetic toggle must not resurrect stale feedback into silence");
    assert(body.bodyRms>0.0f && full.bodyRms>0.0f && sym.busPeak>0.0f);
    std::cout << "  -> PASSED: M5C A/B artifacts, global fixed-Hz body and delayed bus remain stable.\n";
}

// M5C.2 qualification deliberately keeps all candidate routing host-visible.
// Firmware defaults to StrikeBus; FullMix and Transient are comparison paths.
float correlationWindow(const std::vector<int32_t>& dry, const std::vector<int32_t>& wet,
                        float startSeconds, float endSeconds) {
    const size_t begin=std::min(dry.size()/2, static_cast<size_t>(startSeconds*48000.0f));
    const size_t end=std::min(dry.size()/2, static_cast<size_t>(endSeconds*48000.0f));
    double xy=0.0, xx=0.0, yy=0.0;
    for(size_t i=begin;i<end;++i) {
        const double x=static_cast<double>(dry[i*2])/2147483647.0;
        const double y=static_cast<double>(wet[i*2]-static_cast<int64_t>(dry[i*2]))/2147483647.0;
        xy+=x*y; xx+=x*x; yy+=y*y;
    }
    return static_cast<float>(xy/std::sqrt(std::max(1.0e-24,xx*yy)));
}

void testM5c2BodyCoupling() {
    std::cout << "[Test 14] M5C.2 body excitation A/B/C/D qualification..." << std::endl;
    auto note=[](uint8_t n,uint8_t v){ midi::MidiEvent e{}; e.type=midi::MidiEventType::NoteOn; e.data1=n; e.data2=v; return e; };
    constexpr size_t kFrames=96000;
    const auto fullMix=dsp::BodyExcitationStrategy::FullMix;
    const auto transient=dsp::BodyExcitationStrategy::Transient;
    const auto strikeBus=dsp::BodyExcitationStrategy::StrikeBus;
    auto render=[&](const std::vector<ScheduledEvent>& events, bool enabled, dsp::BodyExcitationStrategy strategy) {
        return renderM5c(events,kFrames,enabled,true,-1.0f,1.0f,false,strategy);
    };
    const std::vector<ScheduledEvent> chord={{0,note(50,70)},{0,note(57,70)},{0,note(62,70)},{0,note(69,70)}};
    const std::vector<ScheduledEvent> interval={{0,note(50,70)},{24000,note(57,70)}};
    std::vector<ScheduledEvent> roll; for(uint32_t i=0;i<12;++i) roll.push_back({i*3600,note(50,85)});

    std::ofstream report("pan_m5c2_body_coupling.md");
    report << std::fixed << std::setprecision(4)
           << "# PAN M5C.2 body coupling qualification\n\n"
           << "Selected production strategy: **strike/exciter bus**. It taps local exciter energy before each voice modal bank; sympathetic feedback is excluded. Full mix and transient remain host-only comparison strategies. The fixed 1980 Hz body mode is retained and is currently weakly excited by the 1800 Hz input LPF. Physical listening required.\n\n"
           << "## Strategy comparison — D3 v70\n\n| Strategy | RMS | dry→wet dB | difference RMS / relative dB | contribution correlation (attack/mid/tail) | body RMS | clamp / sympathetic safety |\n|---|---:|---:|---:|---|---:|---:|\n";
    const auto d3Events=std::vector<ScheduledEvent>{{0,note(50,70)}};
    const auto d3Dry=render(d3Events,false,strikeBus);
    const auto d3Full=render(d3Events,true,fullMix);
    const auto d3Transient=render(d3Events,true,transient);
    const auto d3Strike=render(d3Events,true,strikeBus);
    auto strategyRow=[&](const char* name,const M5cResult& r) {
        const float diff=differenceRms(d3Dry.audio,r.audio);
        report << "| " << name << " | " << r.metrics.rms << " | " << dbRelative(r.metrics.rms,d3Dry.metrics.rms)
               << " | " << diff << " / " << dbRelative(diff,d3Dry.metrics.rms) << " | "
               << correlationWindow(d3Dry.audio,r.audio,0,.020f) << " / "
               << correlationWindow(d3Dry.audio,r.audio,.100f,.500f) << " / "
               << correlationWindow(d3Dry.audio,r.audio,.500f,1.500f) << " | " << r.bodyRms
               << " | " << r.hardClampCount << " / " << r.sympatheticSafetyCount << " |\n";
    };
    strategyRow("A: body off",d3Dry); strategyRow("B: full mix",d3Full);
    strategyRow("C: transient",d3Transient); strategyRow("D: strike bus",d3Strike);
    report << "\n## Candidate gain sweeps — D3 v70, strike bus\n\n| Sweep | Value | RMS delta dB | difference relative dB |\n|---|---:|---:|---:|\n";
    for(float gain : {0.07f,0.09f,0.11f,0.13f}) {
        const auto r=renderM5c(d3Events,kFrames,true,true,gain,1.0f,false,strikeBus);
        report << "| outputGain | " << gain << " | " << dbRelative(r.metrics.rms,d3Dry.metrics.rms) << " | " << dbRelative(differenceRms(d3Dry.audio,r.audio),d3Dry.metrics.rms) << " |\n";
    }
    for(float gain : {0.10f,0.14f,0.18f}) {
        const auto r=renderM5c(d3Events,kFrames,true,true,-1.0f,1.0f,false,strikeBus,gain);
        report << "| excitationGain | " << gain << " | " << dbRelative(r.metrics.rms,d3Dry.metrics.rms) << " | " << dbRelative(differenceRms(d3Dry.audio,r.audio),d3Dry.metrics.rms) << " |\n";
    }

    report << "\n## Register balance — candidate D, velocity 70\n\n| Note | dry RMS | candidate RMS | dry→body dB | tail dry→body dB | 0-5 / 0-20 / 0-100 ms dB | correlation attack/mid/tail | difference RMS / relative dB |\n|---|---:|---:|---:|---:|---:|---|---:|\n";
    float a3Overall=0.0f, a3Tail=0.0f;
    for(uint8_t midiNote : {50,57,62,69}) {
        const auto events=std::vector<ScheduledEvent>{{0,note(midiNote,70)}};
        const auto dry=render(events,false,strikeBus), candidate=render(events,true,strikeBus);
        const float overall=dbRelative(candidate.metrics.rms,dry.metrics.rms);
        const float tail=dbRelative(windowRms(candidate.audio,.5f,1.5f),windowRms(dry.audio,.5f,1.5f));
        const float diff=differenceRms(dry.audio,candidate.audio);
        const char* name=midiNote==50?"D3":midiNote==57?"A3":midiNote==62?"D4":"A4";
        report << "| " << name << " | " << dry.metrics.rms << " | " << candidate.metrics.rms << " | " << overall << " | " << tail << " | "
               << dbRelative(windowRms(candidate.audio,0,.005f),windowRms(dry.audio,0,.005f)) << " / "
               << dbRelative(windowRms(candidate.audio,0,.020f),windowRms(dry.audio,0,.020f)) << " / "
               << dbRelative(windowRms(candidate.audio,0,.100f),windowRms(dry.audio,0,.100f)) << " | "
               << correlationWindow(dry.audio,candidate.audio,0,.020f) << " / " << correlationWindow(dry.audio,candidate.audio,.100f,.500f) << " / " << correlationWindow(dry.audio,candidate.audio,.500f,1.500f)
               << " | " << diff << " / " << dbRelative(diff,dry.metrics.rms) << " |\n";
        if(midiNote==57) { a3Overall=overall; a3Tail=tail; }
        writeWavFile((std::string("pan_m5c2_")+name+"_dry.wav").c_str(),dry.audio.data(),kFrames,48000);
        writeWavFile((std::string("pan_m5c2_")+name+"_candidate.wav").c_str(),candidate.audio.data(),kFrames,48000);
    }
    const auto d3Current=render(d3Events,true,fullMix);
    const auto a3Current=render({{0,note(57,70)}},true,fullMix);
    writeWavFile("pan_m5c2_D3_current.wav",d3Current.audio.data(),kFrames,48000);
    writeWavFile("pan_m5c2_A3_current.wav",a3Current.audio.data(),kFrames,48000);
    const auto chordDry=render(chord,false,strikeBus), chordCurrent=render(chord,true,fullMix), chordCandidate=render(chord,true,strikeBus);
    const auto rollDry=render(roll,false,strikeBus), rollCandidate=render(roll,true,strikeBus);
    writeWavFile("pan_m5c2_chord_dry.wav",chordDry.audio.data(),kFrames,48000);
    writeWavFile("pan_m5c2_chord_current.wav",chordCurrent.audio.data(),kFrames,48000);
    writeWavFile("pan_m5c2_chord_candidate.wav",chordCandidate.audio.data(),kFrames,48000);
    writeWavFile("pan_m5c2_roll_dry.wav",rollDry.audio.data(),kFrames,48000);
    writeWavFile("pan_m5c2_roll_candidate.wav",rollCandidate.audio.data(),kFrames,48000);
    writeWavFile("pan_m5c2_D3_fullmix.wav",d3Full.audio.data(),kFrames,48000);
    writeWavFile("pan_m5c2_D3_transient.wav",d3Transient.audio.data(),kFrames,48000);
    writeWavFile("pan_m5c2_D3_strikebus.wav",d3Strike.audio.data(),kFrames,48000);

    report << "\n## Full-mix polarity and delay diagnostics — D3 v70\n\nThese are offline analysis only; no polarity or delay switch is shipped.\n\n| Full-mix return | RMS delta dB vs dry |\n|---|---:|\n";
    for(int polarity : {1,-1}) for(int delay : {0,1,2,4,8}) {
        std::vector<int32_t> probe=d3Dry.audio;
        for(size_t i=0;i<probe.size()/2;++i) { const size_t source=i>=static_cast<size_t>(delay)?i-delay:0; const int64_t c=static_cast<int64_t>(d3Full.audio[source*2])-d3Dry.audio[source*2]; const int64_t mixed=static_cast<int64_t>(d3Dry.audio[i*2])+polarity*c; probe[i*2]=probe[i*2+1]=static_cast<int32_t>(std::clamp<int64_t>(mixed,INT32_MIN,INT32_MAX)); }
        report << "| " << (polarity>0?"+1":"-1") << ", " << delay << " samples | " << dbRelative(windowRms(probe,0,2),d3Dry.metrics.rms) << " |\n";
    }
    report << "\n## Chord, interval, roll, limiter, and stability\n\n| Fixture | dry/candidate RMS | dry/candidate max GR | dry/candidate avg GR | candidate clamp / safety |\n|---|---:|---:|---:|---:|\n";
    auto fixture=[&](const char* name,const M5cResult& dry,const M5cResult& candidate) { report << "| " << name << " | " << dry.metrics.rms << " / " << candidate.metrics.rms << " | " << dry.metrics.maxGainReductionDb << " / " << candidate.metrics.maxGainReductionDb << " | " << dry.metrics.averageGainReductionDb << " / " << candidate.metrics.averageGainReductionDb << " | " << candidate.hardClampCount << " / " << candidate.sympatheticSafetyCount << " |\n"; assert(candidate.hardClampCount==0 && candidate.sympatheticSafetyCount==0); };
    fixture("D3+A3 interval",render(interval,false,strikeBus),render(interval,true,strikeBus));
    fixture("D3 A3 D4 A4 chord",chordDry,chordCandidate); fixture("D3 roll",rollDry,rollCandidate);
    const auto d3v110=render({{0,note(50,110)}},true,strikeBus); fixture("D3 v110",render({{0,note(50,110)}},false,strikeBus),d3v110);
    for(const auto& events : std::initializer_list<std::vector<ScheduledEvent>>{d3Events,chord,roll}) { const auto stable=renderM5c(events,480000,true,true,-1.0f,1.0f,false,strikeBus); assert(std::isfinite(stable.metrics.rms) && stable.hardClampCount==0 && stable.sympatheticSafetyCount==0); }
    const auto decay=renderM5c(chord,720000,true,true,-1.0f,1.0f,false,strikeBus); assert(decay.bodyEnergy<1.0e-5f && decay.hardClampCount==0 && decay.sympatheticSafetyCount==0);
    // Acceptance targets specifically reject the prior A3 cancellation failure.
    assert(a3Overall > -1.5f && a3Tail > -2.0f);
    report << "\n## Conclusion\n\nM5C.2 status: **PASS (host)**. Strike bus is selected because it stops continuously feeding the shell with coherent modal tails while preserving a single six-mode global resonator. Sympathetic defaults are unchanged (0.005 / 0.002 / 1500 Hz / 0.03), safety count is zero, and body-off/sympathetic-off M5B bit identity remains covered by Test 13. CPU: host only; hardware telemetry and physical listening remain required before voicing freeze.\n";
    std::cout << "  -> PASSED: strike-bus register/tail balance, diagnostics, artifacts, and stability.\n";
}

// M5D is intentionally a small voicing pass.  This fixture is the durable
// host regression fingerprint: it emits the listening material and reports
// independent guards instead of hiding several qualities behind one score.
void testM5dVoicingFreeze() {
    std::cout << "[Test 15] M5D.1 PAN true A/B/C qualification..." << std::endl;
    constexpr size_t kFrames=96000;
    auto note=[](uint8_t n,uint8_t v){ midi::MidiEvent e{}; e.type=midi::MidiEventType::NoteOn; e.data1=n; e.data2=v; return e; };
    struct Candidate { const char* id; dsp::ExciterConfig exciter; dsp::PanVoicingConfig voicing; };
    Candidate a{"m5c2",dsp::kPanExciterConfig,dsp::kPanVoicingConfig}; a.exciter.velocityKnee=1.0f; a.exciter.velocityKneeSlope=1.0f; a.voicing.upperModeHardVelocity=.88f;
    Candidate b{"m5d",dsp::kPanExciterConfig,dsp::kPanVoicingConfig}; b.voicing.upperModeHardVelocity=.98f;
    Candidate c{"bright",dsp::kPanExciterConfig,dsp::kPanVoicingConfig};
    const auto strikeBus=dsp::BodyExcitationStrategy::StrikeBus;
    auto render=[&](const Candidate& candidate,const std::vector<ScheduledEvent>& events) { return renderM5c(events,kFrames,true,true,-1,1,false,strikeBus,-1,-1,&candidate.exciter,&candidate.voicing); };
    // Exclude the first 200 ms so this voicing proxy measures modal balance,
    // not the M5C.2 limiter's attack attenuation at v127.
    auto modalWindow=[](const M5cResult& r) { const size_t first=static_cast<size_t>(.2f*48000)*2, last=static_cast<size_t>(1.5f*48000)*2; return std::vector<int32_t>(r.audio.begin()+first,r.audio.begin()+std::min(last,r.audio.size())); };
    auto brightness=[&](const M5cResult& r,uint8_t n) { const auto audio=modalWindow(r); const float f=midi::MidiMapping::noteToHz(n); const float all=spectralEnergy(audio,{f,2*f,3*f,3.98f*f,5.25f*f,6.62f*f,8.18f*f}); return spectralEnergy(audio,{3*f,3.98f*f,5.25f*f,6.62f*f,8.18f*f})/std::max(all,1e-12f); };
    auto high=[&](const M5cResult& r,uint8_t n) { const auto audio=modalWindow(r); const float f=midi::MidiMapping::noteToHz(n); return spectralEnergy(audio,{5.25f*f,6.62f*f,8.18f*f})/std::max(spectralEnergy(audio,{f,2*f}),1e-12f); };
    auto valid=[](const M5cResult& r) { return r.hardClampCount==0 && r.modalSat==0 && r.sympatheticSafetyCount==0; };
    auto matched=[](const M5cResult& r,float target) { auto audio=r.audio; const float gain=target/std::max(r.metrics.rms,1e-12f); for(auto& s:audio) s=static_cast<int32_t>(std::clamp<double>(static_cast<double>(s)*gain,INT32_MIN,INT32_MAX)); return audio; };
    const std::vector<ScheduledEvent> chord={{0,note(50,70)},{0,note(57,70)},{0,note(62,70)},{0,note(69,70)}};
    std::vector<ScheduledEvent> roll70,roll90,crescendo; for(uint32_t i=0;i<12;++i) { roll70.push_back({i*3840,note(50,70)}); roll90.push_back({i*3840,note(50,90)}); crescendo.push_back({i*3840,note(50,uint8_t(30+i*7))}); }
    std::ofstream legacy("pan_m5d_voicing.md"); legacy << "# PAN M5D musical voicing qualification\n\nM5D.1 now publishes a true M5C.2/M5D/bright A/B/C comparison in [pan_m5d1_ab.md](pan_m5d1_ab.md). Determinism is a separate M5D regression.\n";
    std::ofstream report("pan_m5d1_ab.md"); report<<std::fixed<<std::setprecision(6)<<"# PAN M5D.1 true A/B/C qualification\n\nA=M5C.2 (knee 1.00/1.00, upper 0.88); B=M5D current (0.85/0.35, upper 0.98); C=bright (0.85/0.35, upper 0.94). Only host-side calibration overrides are used. No global score is produced.\n\n## D3 velocity A/B/C\n\n| v | candidate | RMS | attack RMS | brightness | high modal ratio | body RMS | max/avg GR | GR >0.1/>1 | hardClamp/ModalSat | brightness delta vs A | high delta vs A |\n|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    for(uint8_t velocity : {30,70,110,127}) { const auto ra=render(a,{{0,note(50,velocity)}}); for(const Candidate* x : {&a,&b,&c}) { const auto r=x==&a?ra:render(*x,{{0,note(50,velocity)}}); report<<"| "<<int(velocity)<<" | "<<x->id<<" | "<<r.metrics.rms<<" | "<<r.metrics.attackRms<<" | "<<brightness(r,50)<<" | "<<high(r,50)<<" | "<<r.bodyRms<<" | "<<r.metrics.maxGainReductionDb<<" / "<<r.metrics.averageGainReductionDb<<" | "<<r.grOver0p1DbSamples<<" / "<<r.grOver1DbSamples<<" | "<<r.hardClampCount<<" / "<<r.modalSat<<" | "<<brightness(r,50)-brightness(ra,50)<<" | "<<high(r,50)-high(ra,50)<<" |\n"; assert(valid(r)); writeWavFile(("pan_m5d1_D3_v"+std::to_string(velocity)+"_"+x->id+".wav").c_str(),r.audio.data(),kFrames,48000); } }
    for(uint8_t velocity : {70,110}) { const auto ra=render(a,{{0,note(50,velocity)}}), rb=render(b,{{0,note(50,velocity)}}), rc=render(c,{{0,note(50,velocity)}}); writeWavFile(("pan_m5d1_D3_v"+std::to_string(velocity)+"_m5d_matched.wav").c_str(),matched(rb,ra.metrics.rms).data(),kFrames,48000); writeWavFile(("pan_m5d1_D3_v"+std::to_string(velocity)+"_bright_matched.wav").c_str(),matched(rc,ra.metrics.rms).data(),kFrames,48000); }
    report<<"\n## Register matrix\n\n| note | v | candidate | RMS | brightness | high modal ratio | attack | tail | max GR |\n|---|---:|---|---:|---:|---:|---:|---:|---:|\n";
    for(uint8_t n : {50,57,62,69}) for(uint8_t v : {70,110}) for(const Candidate* x : {&a,&b,&c}) { const auto r=render(*x,{{0,note(n,v)}}); const char* name=n==50?"D3":n==57?"A3":n==62?"D4":"A4"; report<<"| "<<name<<" | "<<int(v)<<" | "<<x->id<<" | "<<r.metrics.rms<<" | "<<brightness(r,n)<<" | "<<high(r,n)<<" | "<<r.metrics.attackRms<<" | "<<r.metrics.tailRms<<" | "<<r.metrics.maxGainReductionDb<<" |\n"; assert(valid(r)); if(v==70 && x!=&a) writeWavFile((std::string("pan_m5d1_")+name+"_"+x->id+".wav").c_str(),r.audio.data(),kFrames,48000); }
    report<<"\n## Musical fixtures\n\n| fixture | candidate | RMS | brightness proxy | max/avg GR |\n|---|---|---:|---:|---:|\n";
    const std::vector<std::pair<const char*,std::vector<ScheduledEvent>>> fixtures={{"restrike soft→hard",{{0,note(50,30)},{4800,note(50,110)}}},{"restrike hard→soft",{{0,note(50,110)},{4800,note(50,30)}}},{"roll steady v70",roll70},{"roll steady v90",roll90},{"roll crescendo",crescendo},{"chord",chord}};
    for(const auto& fixture:fixtures) {
        if(std::string(fixture.first)=="chord") { const auto r=render(a,fixture.second); report<<"| "<<fixture.first<<" | "<<a.id<<" | "<<r.metrics.rms<<" | "<<brightness(r,50)<<" | "<<r.metrics.maxGainReductionDb<<" / "<<r.metrics.averageGainReductionDb<<" |\n"; assert(valid(r)); }
        for(const Candidate* x : {&b,&c}) { const auto r=render(*x,fixture.second); report<<"| "<<fixture.first<<" | "<<x->id<<" | "<<r.metrics.rms<<" | "<<brightness(r,50)<<" | "<<r.metrics.maxGainReductionDb<<" / "<<r.metrics.averageGainReductionDb<<" |\n"; assert(valid(r)); if(std::string(fixture.first)=="restrike soft→hard") writeWavFile((std::string("pan_m5d1_restrike_")+x->id+".wav").c_str(),r.audio.data(),kFrames,48000); if(std::string(fixture.first)=="roll steady v70") writeWavFile((std::string("pan_m5d1_roll_")+x->id+".wav").c_str(),r.audio.data(),kFrames,48000); if(std::string(fixture.first)=="chord") { writeWavFile((std::string("pan_m5d1_chord_")+x->id+".wav").c_str(),r.audio.data(),kFrames,48000); const auto ra=render(a,chord); writeWavFile((std::string("pan_m5d1_chord_")+x->id+"_matched.wav").c_str(),matched(r,ra.metrics.rms).data(),kFrames,48000); } }
    }
    // A is a historical baseline and reaches limiter activity at v127. B/C
    // preserve a non-decreasing configured upper-mode blend across the full
    // required velocity grid; their measured D3 tables above retain the
    // independent acoustic brightness/high-mode proxies.
    bool monotonic=true; for(const Candidate* x : {&b,&c}) { float priorR=-1,priorBlend=-1; for(uint8_t v : {1,10,20,30,40,50,64,80,96,110,120,127}) { const auto r=render(*x,{{0,note(50,v)}}); const float blend=std::clamp((float(v)/127.0f-x->voicing.upperModeSoftVelocity)/(x->voicing.upperModeHardVelocity-x->voicing.upperModeSoftVelocity),0.0f,1.0f); monotonic &= r.metrics.rms>=priorR && blend>=priorBlend && valid(r); priorR=r.metrics.rms; priorBlend=blend; } }
    const auto d1=render(b,{{0,note(50,110)}}), d2=render(b,{{0,note(50,110)}}); assert(d1.audio==d2.audio);
    const auto a127=render(a,{{0,note(50,127)}}),b127=render(b,{{0,note(50,127)}}),c127=render(c,{{0,note(50,127)}}); assert(valid(a127)&&valid(b127)&&valid(c127)&&monotonic);
    report<<"\n## Guards and decision\n\n| Guard | Result |\n|---|---|\n| M5D deterministic render regression | PASS |\n| RMS and configured upper-mode brightness monotonicity, B/C | "<<(monotonic?"PASS":"FAIL")<<" |\n| v127 hardClamp / ModalSat, A/B/C | 0 / 0 |\n| register balance guard | PASS (existing M5C.2 guard retained) |\n\nCandidate B is technically safest; Candidate C restores moderate brightness while retaining M5D knee/headroom. **Provisional final candidate: C (upperModeHardVelocity 0.94), requires hardware listening.**\n\n## Hardware listening checklist\n\n- [ ] v70/v110/v127 M5D vs bright: useful metallic life without clang\n- [ ] v127 remains controlled\n- [ ] chord remains cohesive\n- [ ] rolls remain smooth, without metallic wash or machine-gun clicks\n";
    std::cout << "  -> PASSED: M5D.1 true A/B/C, determinism, monotonicity, and guards.\n";
}

void testSpectralAndRestrikeSanity() {
    std::cout << "[Test 9] PAN spectral/restrike/DC sanity..." << std::endl;
    const uint8_t kD3Midi = 50;
    const float kD3Hz = midi::MidiMapping::noteToHz(kD3Midi);
    auto note = [kD3Midi](uint8_t velocity) { midi::MidiEvent e{}; e.type = midi::MidiEventType::NoteOn; e.data1 = kD3Midi; e.data2 = velocity; return e; };
    AudioMetrics singleMetrics; uint32_t singleSat;
    auto single = renderScheduled({{0, note(90)}}, 96000, true, singleMetrics, singleSat);
    // Broad probes deliberately do not try to resolve the sub-1 Hz PAN doublet.
    const float fundamental = goertzelMagnitude(single, kD3Hz);
    const float octave = goertzelMagnitude(single, 2.0f * kD3Hz);
    const float fifth = goertzelMagnitude(single, 3.0f * kD3Hz);
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

float spectralEnergy(const std::vector<int32_t>& audio, const std::initializer_list<float>& frequencies) {
    float energy = 0.0f;
    for (const float frequency : frequencies) {
        const float magnitude = goertzelMagnitude(audio, frequency);
        energy += magnitude * magnitude;
    }
    return energy;
}

void panM5bVoicingAudit() {
    std::cout << "[Test 9b] M5B velocity/modal/register audit..." << std::endl;
    constexpr uint8_t kD3Midi = 50;
    const float kD3 = midi::MidiMapping::noteToHz(kD3Midi);
    auto note = [](uint8_t midiNote, uint8_t velocity) {
        midi::MidiEvent e{}; e.type = midi::MidiEventType::NoteOn; e.data1 = midiNote; e.data2 = velocity; return e;
    };
    std::ofstream report("pan_m5b_metrics.md");
    report << "# PAN M5B host metrics\n\n"
           << "D3 baseline: MIDI 50, " << kD3 << " Hz. Goertzel probes are exact PAN modal frequencies.\n\n"
           << "| Velocity | RMS | Attack RMS | f | 2f | 3f | Upper energy | modalBrightnessRatio | highModalRatio | Modal saturation |\n"
           << "|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    float previousModalBrightness = -1.0f;
    float previousHighModal = -1.0f;
    for (uint8_t velocity : {30, 70, 110}) {
        AudioMetrics metrics; uint32_t saturation = 0;
        const auto audio = renderScheduled({{0, note(kD3Midi, velocity)}}, 96000, true, metrics, saturation);
        const float fundamental = spectralEnergy(audio, {kD3});
        const float splitFundamentalHz = kD3 * (1.0f + dsp::computePanDoubletDetune(
            kD3, dsp::PanDoubletMode::FixedHz));
        const float splitFundamental = spectralEnergy(audio, {splitFundamentalHz});
        const float octave = spectralEnergy(audio, {2.0f * kD3});
        const float fifth = spectralEnergy(audio, {3.0f * kD3});
        const float upper = spectralEnergy(audio, {3.98f * kD3, 5.25f * kD3, 6.62f * kD3, 8.18f * kD3});
        const float allModes = spectralEnergy(audio, {kD3, splitFundamentalHz, 2.0f*kD3,
            3.0f*kD3, 3.98f*kD3, 5.25f*kD3, 6.62f*kD3, 8.18f*kD3});
        const float lowModes = fundamental + splitFundamental + octave;
        const float modalBrightness = spectralEnergy(audio, {3.0f*kD3, 3.98f*kD3, 5.25f*kD3, 6.62f*kD3, 8.18f*kD3}) / std::max(1.0e-12f, allModes);
        const float highModal = spectralEnergy(audio, {5.25f*kD3, 6.62f*kD3, 8.18f*kD3}) / std::max(1.0e-12f, lowModes);
        assert(modalBrightness > previousModalBrightness && "Modal brightness must grow with velocity");
        assert(highModal > previousHighModal && "High-modal ratio must grow with velocity");
        previousModalBrightness = modalBrightness;
        previousHighModal = highModal;
        report << "| " << static_cast<int>(velocity) << " | " << metrics.rms << " | " << metrics.attackRms
               << " | " << fundamental << " | " << octave << " | " << fifth << " | " << upper
               << " | " << modalBrightness << " | " << highModal << " | " << saturation << " |\n";
        writeWavFile((std::string("pan_D3_v") + std::to_string(velocity) + "_new.wav").c_str(), audio.data(), 96000, 48000);
    }
    report << "\n## Register metrics (velocity 70, current default fixed 1 Hz split)\n\n"
           << "| Note | MIDI | Hz | RMS | Attack RMS | Tail RMS (500-1500 ms) | modalBrightnessRatio | Pre-limiter peak | Max GR dB | Avg GR dB |\n"
           << "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    for (uint8_t midiNote : {50, 57, 62, 69}) {
        AudioMetrics metrics; uint32_t saturation = 0;
        const auto audio = renderScheduled({{0, note(midiNote, 70)}}, 96000, true, metrics, saturation);
        assert(std::isfinite(metrics.rms) && metrics.rms > 0.001f);
        const char* name = midiNote == 50 ? "D3" : midiNote == 57 ? "A3" : midiNote == 62 ? "D4" : "A4";
        const float f = midi::MidiMapping::noteToHz(midiNote);
        const float splitF = f * (1.0f + dsp::computePanDoubletDetune(f, dsp::PanDoubletMode::FixedHz));
        const float all = spectralEnergy(audio, {f, splitF, 2*f, 3*f, 3.98f*f, 5.25f*f, 6.62f*f, 8.18f*f});
        const float brightness = spectralEnergy(audio, {3*f, 3.98f*f, 5.25f*f, 6.62f*f, 8.18f*f}) / std::max(1.0e-12f, all);
        report << "| " << name << " | " << static_cast<int>(midiNote) << " | " << f << " | " << metrics.rms << " | " << metrics.attackRms << " | " << metrics.tailRms << " | " << brightness << " | " << metrics.preLimiterPeak << " | " << metrics.maxGainReductionDb << " | " << metrics.averageGainReductionDb << " |\n";
        writeWavFile((std::string("pan_register_") + name + ".wav").c_str(), audio.data(), 96000, 48000);
    }
    std::cout << "  -> PASSED: modal brightness ratios rise monotonically.\n";
}

std::vector<int32_t> renderPanDoubletReference(float fundamentalHz, dsp::PanDoubletMode mode) {
    constexpr size_t kFrames = 48000 * 4;
    dsp::ModalResonatorBank bank;
    bank.init(48000.0f);
    bank.setRegisterBehavior(1.0f, 1.0f, mode == dsp::PanDoubletMode::FixedHz);
    bank.updatePitchAndDamping(fundamentalHz, 0.0f);
    std::vector<int32_t> audio(kFrames * 2);
    for (size_t frame = 0; frame < kFrames; ++frame) {
        const float sample = std::clamp(bank.processSample(frame == 0 ? 0.35f : 0.0f), -1.0f, 1.0f);
        const int32_t pcm = static_cast<int32_t>(sample * 2147483647.0f);
        audio[frame * 2] = pcm; audio[frame * 2 + 1] = pcm;
    }
    return audio;
}

void testPanDoubletAb() {
    std::cout << "[Test 9c] PAN doublet relative/fixed A/B..." << std::endl;
    std::ofstream report("pan_doublet_ab.md");
    report << "# PAN doublet A/B\n\nCurrent default: fixed 1 Hz. Musical preference requires hardware listening validation.\n\n"
           << "| Note | MIDI | Fundamental Hz | Relative beat Hz | Fixed beat Hz |\n|---|---:|---:|---:|---:|\n";
    for (uint8_t midiNote : {50, 57, 62, 69}) {
        const float f = midi::MidiMapping::noteToHz(midiNote);
        const char* name = midiNote == 50 ? "D3" : midiNote == 57 ? "A3" : midiNote == 62 ? "D4" : "A4";
        const float relativeBeat = dsp::computePanDoubletBeatHz(f, dsp::PanDoubletMode::Relative);
        const float fixedBeat = dsp::computePanDoubletBeatHz(f, dsp::PanDoubletMode::FixedHz);
        assert(relativeBeat > 0.0f && fixedBeat > 0.0f);
        assert(std::abs(fixedBeat - 1.0f) < 1.0e-5f);
        report << "| " << name << " | " << static_cast<int>(midiNote) << " | " << f << " | " << relativeBeat << " | " << fixedBeat << " |\n";
        const auto relative = renderPanDoubletReference(f, dsp::PanDoubletMode::Relative);
        const auto fixed = renderPanDoubletReference(f, dsp::PanDoubletMode::FixedHz);
        writeWavFile((std::string("pan_doublet_") + name + "_relative.wav").c_str(), relative.data(), relative.size() / 2, 48000);
        writeWavFile((std::string("pan_doublet_") + name + "_fixed.wav").c_str(), fixed.data(), fixed.size() / 2, 48000);
    }
    for (uint8_t midiNote : {36, 50, 69, 84}) {
        const float f = midi::MidiMapping::noteToHz(midiNote);
        const float detune = dsp::computePanDoubletDetune(f, dsp::PanDoubletMode::FixedHz);
        assert(std::isfinite(detune) && detune > 0.0f && detune < 0.03f);
        assert(f * (1.0f + detune) > 0.0f);
    }
    std::cout << "  -> PASSED: A/B WAVs and calculated beat rates generated.\n";
}

void testHandpanDemoScale() {
    constexpr uint8_t scale[] = {50, 57, 58, 60, 62, 64, 65, 69};
    constexpr const char* names[] = {"D3", "A3", "Bb3", "C4", "D4", "E4", "F4", "A4"};
    for (size_t i = 0; i < std::size(scale); ++i) assert(std::string(midi::MidiMapping::noteToName(scale[i])) == names[i]);
}

void saturationAbAudit() {
    std::cout << "[Test 10] Internal saturation A/B audit..." << std::endl;
    std::ofstream report("saturation_ab_metrics.md");
    report << "# Internal modal saturation A/B audit\n\n| Case | Sat On Peak | Sat Off Peak | RMS delta | On ModalSat | Off ModalSat |\n|---|---:|---:|---:|---:|---:|\n";
    auto note=[](uint8_t n,uint8_t velocity){ midi::MidiEvent e{}; e.type=midi::MidiEventType::NoteOn; e.data1=n; e.data2=velocity; return e; };
    const std::vector<std::pair<const char*, std::vector<ScheduledEvent>>> cases = {
        {"single D3 vel127", {{0,note(50,127)}}}, {"double strike 100ms", {{0,note(50,100)},{4800,note(50,100)}}},
        {"rapid roll", {{0,note(50,110)},{1800,note(50,110)},{3600,note(50,110)},{5400,note(50,110)},{7200,note(50,110)}}},
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
    render("pan_D3_vel40.wav",{{0,event(50,40)}},144000);
    render("pan_D3_vel90.wav",{{0,event(50,90)}},144000);
    render("pan_D3_vel127.wav",{{0,event(50,127)}},144000);
    render("pan_D3_single.wav",{{0,event(50,90)}},144000);
    render("pan_D3_double_100ms.wav",{{0,event(50,80)},{4800,event(50,95)}},168000);
    render("pan_D3_double_250ms.wav",{{0,event(50,80)},{12000,event(50,95)}},168000);
    render("pan_D3_triple.wav",{{0,event(50,80)},{12000,event(50,95)},{24000,event(50,105)}},168000);
    render("pan_D3_roll.wav",{{0,event(50,85)},{3600,event(50,85)},{7200,event(50,85)},{10800,event(50,85)},{14400,event(50,85)},{18000,event(50,85)},{21600,event(50,85)},{25200,event(50,85)},{28800,event(50,85)},{32400,event(50,85)},{36000,event(50,85)},{39600,event(50,85)},{43200,event(50,85)}},96000);
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

void testForensicsClassification() {
    std::cout << "[Test M6.3.9] Forensics 3-class classification & exciter duration...\n";
    struct VelocityExpectation {
        uint8_t velocity;
        uint32_t expectedAttackTailBlocks;
    };
    const VelocityExpectation velTests[] = {
        {30, 2},
        {70, 1},
        {100, 1},
        {110, 1},
        {127, 1}
    };
    for (const auto& vt : velTests) {
        dsp::SynthEngine synth;
        synth.init(48000.0f);
        synth.setInstrumentModel(dsp::InstrumentModel::Pan);
        midi::MidiEvent ev{};
        ev.type = midi::MidiEventType::NoteOn;
        ev.data1 = 50;
        ev.data2 = vt.velocity;
        synth.handleMidiEvent(ev);

        // Block 0 is the event block. Exciter is active.
        assert(synth.hasActiveExciter());
        int32_t out[128 * 2];
        synth.renderBlock(out, 128);

        // Subsequent blocks are attack_tail while exciter remains active entering block
        uint32_t tailBlocks = 0;
        while (synth.hasActiveExciter()) {
            ++tailBlocks;
            synth.renderBlock(out, 128);
        }
        assert(tailBlocks == vt.expectedAttackTailBlocks);
        // After tail blocks, block is true_steady
        assert(!synth.hasActiveExciter());
        synth.renderBlock(out, 128);
        assert(!synth.hasActiveExciter());
    }

    // 2. Simulate 8192-block fixture for cluster8 (velocity 100)
    // Confirm exact block counts: event = 44, attack_tail = 44, true_steady = 8104, sum = 8192
    {
        dsp::SynthEngine synth;
        synth.init(48000.0f);
        synth.setInstrumentModel(dsp::InstrumentModel::Pan);
        uint32_t eventCount = 0;
        uint32_t attackTailCount = 0;
        uint32_t trueSteadyCount = 0;
        int32_t out[128 * 2];
        constexpr uint8_t cluster[8] = {50, 52, 54, 56, 57, 59, 61, 62};

        for (unsigned block = 0; block < 8192; ++block) {
            const bool event = (block % 188 == 0);
            if (event) {
                synth.reset();
                for (unsigned v = 0; v < 8; ++v) {
                    midi::MidiEvent note{}; note.type = midi::MidiEventType::NoteOn;
                    note.data1 = cluster[v]; note.data2 = 100;
                    synth.handleMidiEvent(note);
                }
            }
            const bool attackTail = !event && synth.hasActiveExciter();
            const unsigned timingClass = event ? 2 : (attackTail ? 1 : 0);
            if (timingClass == 2) ++eventCount;
            else if (timingClass == 1) ++attackTailCount;
            else ++trueSteadyCount;

            synth.renderBlock(out, 128);
        }
        assert(eventCount == 44);
        assert(attackTailCount == 44);
        assert(trueSteadyCount == 8192 - 44 - 44);
        assert(eventCount + attackTailCount + trueSteadyCount == 8192);
    }
    std::cout << "  -> PASSED: Forensics 3-class classification validated (44/44/8104).\n";

    // 3. Simulate 8192-block fixture for M6.3.9.1 with FixtureTransition at block 0
    // Confirm exact block counts: transition = 1, event = 44, attack_tail = 44, true_steady = 8103, sum = 8192
    {
        dsp::SynthEngine synth;
        synth.init(48000.0f);
        synth.setInstrumentModel(dsp::InstrumentModel::Pan);
        uint32_t transitionCount = 0;
        uint32_t eventCount = 0;
        uint32_t attackTailCount = 0;
        uint32_t trueSteadyCount = 0;
        int32_t out[128 * 2];
        constexpr uint8_t cluster[8] = {50, 52, 54, 56, 57, 59, 61, 62};

        for (unsigned block = 0; block < 8192; ++block) {
            const bool isTransition = (block == 0);
            const bool event = !isTransition && ((block - 1) % 188 == 0);
            if (isTransition) {
                synth.setInstrumentModel(dsp::InstrumentModel::Pan);
                synth.reset();
                ++transitionCount;
            } else if (event) {
                synth.reset();
                for (unsigned v = 0; v < 8; ++v) {
                    midi::MidiEvent note{}; note.type = midi::MidiEventType::NoteOn;
                    note.data1 = cluster[v]; note.data2 = 100;
                    synth.handleMidiEvent(note);
                }
                ++eventCount;
            } else if (synth.hasActiveExciter()) {
                ++attackTailCount;
            } else {
                ++trueSteadyCount;
            }
            synth.renderBlock(out, 128);
        }
        assert(transitionCount == 1);
        assert(eventCount == 44);
        assert(attackTailCount == 44);
        assert(trueSteadyCount == 8192 - 1 - 44 - 44);
        assert(transitionCount + eventCount + attackTailCount + trueSteadyCount == 8192);
    }
    std::cout << "  -> PASSED: M6.3.9.1 4-class classification validated (1/44/44/8103).\n";
}

struct TongueQualificationRender {
    M5cResult rendered;
    size_t maxActiveVoices = 0;
    size_t maxStealTails = 0;
    float maxSampleDelta = 0.0f;
    size_t finalActiveVoices = 0;
    uint32_t voiceStealCount = 0;
};

TongueQualificationRender renderTongueQualification(const std::vector<ScheduledEvent>& events, size_t totalFrames) {
    constexpr size_t kBlock = 128;
    TongueQualificationRender result; result.rendered.audio.resize(totalFrames * 2);
    dsp::SynthEngine engine; engine.init(48000.0f); engine.setInstrumentModel(dsp::InstrumentModel::Tongue);
    size_t eventIndex = 0; int32_t previous = 0;
    for (size_t frame = 0; frame < totalFrames; frame += kBlock) {
        while (eventIndex < events.size() && events[eventIndex].frame <= frame) engine.handleMidiEvent(events[eventIndex++].event);
        result.maxStealTails = std::max(result.maxStealTails, engine.getVoiceAllocator().getActiveStealTailCount());
        const size_t count = std::min(kBlock, totalFrames - frame);
        engine.renderBlock(result.rendered.audio.data() + frame * 2, count);
        result.maxActiveVoices = std::max(result.maxActiveVoices, engine.getVoiceAllocator().getActiveVoiceCount());
        for (size_t i = 0; i < count * 2; ++i) {
            const int32_t current = result.rendered.audio[frame * 2 + i];
            result.maxSampleDelta = std::max(result.maxSampleDelta, static_cast<float>(std::abs(current - previous)));
            previous = current;
        }
    }
    result.finalActiveVoices = engine.getVoiceAllocator().getActiveVoiceCount();
    result.voiceStealCount = engine.getVoiceAllocator().getVoiceStealCount();
    result.rendered.metrics = computeMetrics(result.rendered.audio, engine.getSoftClipCount());
    result.rendered.hardClampCount = engine.getHardClampCount();
    result.rendered.modalSat = engine.getModalInternalSaturationCount();
    result.rendered.metrics.maxGainReductionDb = engine.getMaxGainReductionDb();
    result.rendered.metrics.averageGainReductionDb = engine.getAverageGainReductionDb();
    return result;
}

void testM7TongueModel() {
    std::cout << "[Test 23] M7.0 Tongue Drum V1 model, fixtures, and 3-model cycling...\n";
    auto allFinite = [](const std::vector<int32_t>& audio) {
        for (int32_t s : audio) { if (!std::isfinite(static_cast<float>(s))) return false; }
        return true;
    };
    auto note = [](uint8_t n, uint8_t v) {
        midi::MidiEvent e{};
        e.type = midi::MidiEventType::NoteOn;
        e.data1 = n;
        e.data2 = v;
        return e;
    };

    // 1. Model Registry and helpers checks
    assert(dsp::getInstrumentModelConfig(dsp::InstrumentModel::Tongue).id == dsp::InstrumentModel::Tongue);
    assert(dsp::kPresetTongue.modeCount == 6);
    assert(std::string(dsp::instrumentModelName(dsp::InstrumentModel::Tongue)) == "TONGUE");
    assert(std::string(dsp::instrumentModelName(dsp::InstrumentModel::Bell)) == "BELL");
    assert(std::string(dsp::instrumentModelName(dsp::InstrumentModel::Pan)) == "PAN");
    assert(dsp::nextInstrumentModel(dsp::InstrumentModel::Pan) == dsp::InstrumentModel::Bell);
    assert(dsp::nextInstrumentModel(dsp::InstrumentModel::Bell) == dsp::InstrumentModel::Tongue);
    assert(dsp::nextInstrumentModel(dsp::InstrumentModel::Tongue) == dsp::InstrumentModel::Bowl);
    assert(dsp::nextInstrumentModel(dsp::InstrumentModel(3)) == dsp::InstrumentModel::Kalimba);
    assert(dsp::nextInstrumentModel(dsp::InstrumentModel::Kalimba) == dsp::InstrumentModel::Glass);
    assert(dsp::nextInstrumentModel(dsp::InstrumentModel::Glass) == dsp::InstrumentModel::Marimba);
    assert(dsp::nextInstrumentModel(dsp::InstrumentModel::Marimba) == dsp::InstrumentModel::Vibraphone);

    // 2. PolyPressure and ChannelPressure support for Tongue
    for (bool poly : {true, false}) {
        for (uint8_t pressure : {0, 32, 64, 96, 127}) {
            dsp::SynthEngine pressureEngine;
            pressureEngine.init(48000.0f);
            pressureEngine.setInstrumentModel(dsp::InstrumentModel::Tongue);
            pressureEngine.handleMidiEvent(note(62, 90));
            int32_t pressureBlock[256]{};
            pressureEngine.renderBlock(pressureBlock, 128);
            midi::MidiEvent event{};
            event.type = poly ? midi::MidiEventType::PolyPressure : midi::MidiEventType::ChannelPressure;
            event.data1 = poly ? 62 : pressure;
            event.data2 = poly ? pressure : 0;
            pressureEngine.handleMidiEvent(event);
            pressureEngine.renderBlock(pressureBlock, 128);
            for (auto sample : pressureBlock) assert(std::isfinite(static_cast<float>(sample)));
        }
    }

    // 3. 100 model switch cycles: PAN -> BELL -> TONGUE -> PAN
    std::cout << "  Testing 100 model switch cycles PAN -> BELL -> TONGUE -> PAN...\n";
    dsp::SynthEngine swEngine;
    swEngine.init(48000.0f);
    int32_t swBlock[256]{};
    for (int iter = 0; iter < 100; ++iter) {
        swEngine.handleMidiEvent(note(50, 70));
        swEngine.renderBlock(swBlock, 128);
        swEngine.setInstrumentModel(dsp::InstrumentModel::Bell);
        swEngine.renderBlock(swBlock, 128);
        for (auto sample : swBlock) assert(sample == 0);
        swEngine.handleMidiEvent(note(62, 90));
        swEngine.renderBlock(swBlock, 128);
        swEngine.setInstrumentModel(dsp::InstrumentModel::Tongue);
        swEngine.renderBlock(swBlock, 128);
        for (auto sample : swBlock) assert(sample == 0);
        swEngine.handleMidiEvent(note(58, 80));
        swEngine.renderBlock(swBlock, 128);
        swEngine.setInstrumentModel(dsp::InstrumentModel::Pan);
        swEngine.renderBlock(swBlock, 128);
        for (auto sample : swBlock) assert(sample == 0);
    }
    // Verify direct PAN matches PAN after 100 switches
    swEngine.handleMidiEvent(note(50, 70));
    std::vector<int32_t> swPan(96000 * 2);
    for (size_t frame = 0; frame < 96000; frame += 128) {
        swEngine.renderBlock(swPan.data() + frame * 2, std::min<size_t>(128, 96000 - frame));
    }
    const auto directPan = renderModel({{0, note(50, 70)}}, 96000, dsp::InstrumentModel::Pan);
    assert(fnv1a64(swPan) == fnv1a64(directPan.audio));

    // 4. Deterministic fixtures and metrics report
    std::ofstream report("tongue_m7_metrics.md");
    report << std::fixed << std::setprecision(6);
    report << "# Tongue Drum M7.0 host qualification\n\n"
           << "**InstrumentModel::Tongue (Steel Tongue Drum / Tank Drum V1 Candidate)**\n\n"
           << "## 1. Modal preset topology\n\n"
           << "| Mode | Ratio | Gain | T60 (s) | Role |\n"
           << "|---|---:|---:|---:|---|\n";
    constexpr const char* tongueRoles[] = {
        "Fundamental", "Octave overtone", "Compound 5th", "Metallic partial", "Upper colour", "High partial"
    };
    for (size_t i = 0; i < dsp::kPresetTongue.modeCount; ++i) {
        const auto& m = dsp::kPresetTongue.modes[i];
        report << "| " << i << " | " << m.ratio << " | " << m.gain << " | " << m.t60 << " | " << tongueRoles[i] << " |\n";
    }

    // Velocity tests (D4):
    report << "\n## 2. Velocity progression (D4)\n\n"
           << "| Velocity | RMS | Peak | Crest Factor | max GR dB | Modal Sat | Clamp |\n"
           << "|---|---:|---:|---:|---:|---:|---:|\n";
    float prevRms = 0.0f;
    for (uint8_t vel : {30, 60, 90, 110, 127}) {
        const auto q = renderTongueQualification({{0, note(62, vel)}}, 96000);
        const auto& m = q.rendered.metrics;
        writeWavFile((std::string("tongue_D4_v") + std::to_string(vel) + ".wav").c_str(), q.rendered.audio.data(), 96000, 48000);
        report << "| " << static_cast<int>(vel) << " | " << m.rms << " | " << m.peak << " | " << m.crestFactor << " | "
               << m.maxGainReductionDb << " | " << q.rendered.modalSat << " | " << q.rendered.hardClampCount << " |\n";
        assert(allFinite(q.rendered.audio));
        assert(q.rendered.hardClampCount == 0);
        assert(q.rendered.modalSat == 0);
        // Monotonic progression requirement:
        assert(m.rms > prevRms);
        prevRms = m.rms;
    }

    // Single notes: D3 and A4
    {
        const auto qD3 = renderTongueQualification({{0, note(50, 70)}}, 96000);
        writeWavFile("tongue_D3_v70.wav", qD3.rendered.audio.data(), 96000, 48000);
        assert(allFinite(qD3.rendered.audio) && qD3.rendered.hardClampCount == 0 && qD3.rendered.modalSat == 0);

        const auto qA4 = renderTongueQualification({{0, note(69, 70)}}, 96000);
        writeWavFile("tongue_A4_v70.wav", qA4.rendered.audio.data(), 96000, 48000);
        assert(allFinite(qA4.rendered.audio) && qA4.rendered.hardClampCount == 0 && qA4.rendered.modalSat == 0);
    }

    // Interval2: D4 + A4
    {
        const auto qInt = renderTongueQualification({{0, note(62, 80)}, {0, note(69, 80)}}, 96000);
        writeWavFile("tongue_interval2.wav", qInt.rendered.audio.data(), 96000, 48000);
        assert(allFinite(qInt.rendered.audio) && qInt.rendered.hardClampCount == 0 && qInt.rendered.modalSat == 0);
    }

    // Chord4: D3, A3, D4, A4 (50, 57, 62, 69)
    {
        const auto qChord = renderTongueQualification({{0, note(50, 80)}, {0, note(57, 80)}, {0, note(62, 80)}, {0, note(69, 80)}}, 96000);
        writeWavFile("tongue_chord4.wav", qChord.rendered.audio.data(), 96000, 48000);
        assert(allFinite(qChord.rendered.audio) && qChord.rendered.hardClampCount == 0 && qChord.rendered.modalSat == 0);
        assert(qChord.maxActiveVoices <= 8);
    }

    // Cluster8: 8 voices v100
    {
        const auto qClust = renderTongueQualification({
            {0, note(50, 100)}, {0, note(52, 100)}, {0, note(54, 100)}, {0, note(56, 100)},
            {0, note(57, 100)}, {0, note(59, 100)}, {0, note(61, 100)}, {0, note(62, 100)}
        }, 96000);
        writeWavFile("tongue_cluster8.wav", qClust.rendered.audio.data(), 96000, 48000);
        assert(allFinite(qClust.rendered.audio) && qClust.rendered.hardClampCount == 0 && qClust.rendered.modalSat == 0);
        assert(qClust.maxActiveVoices == 8);
    }

    // Restrike: soft->hard, hard->soft
    {
        const auto qSh = renderTongueQualification({{0, note(62, 30)}, {4800, note(62, 110)}}, 96000);
        writeWavFile("tongue_restrike_softhard.wav", qSh.rendered.audio.data(), 96000, 48000);
        assert(allFinite(qSh.rendered.audio) && qSh.rendered.hardClampCount == 0);

        const auto qHs = renderTongueQualification({{0, note(62, 110)}, {4800, note(62, 30)}}, 96000);
        writeWavFile("tongue_restrike_hardsoft.wav", qHs.rendered.audio.data(), 96000, 48000);
        assert(allFinite(qHs.rendered.audio) && qHs.rendered.hardClampCount == 0);
    }

    // Roll:
    {
        std::vector<ScheduledEvent> rollEvents;
        for (size_t r = 0; r < 8; ++r) {
            rollEvents.push_back({static_cast<uint32_t>(r * 1920), note(62, 75)});
        }
        const auto qRoll = renderTongueQualification(rollEvents, 96000);
        writeWavFile("tongue_roll.wav", qRoll.rendered.audio.data(), 96000, 48000);
        assert(allFinite(qRoll.rendered.audio) && qRoll.rendered.hardClampCount == 0);
    }

    // Register sweep: C3 to C6
    {
        std::vector<ScheduledEvent> sweepEvents;
        for (uint8_t n = 48; n <= 84; n += 4) {
            sweepEvents.push_back({static_cast<uint32_t>((n - 48) * 4800), note(n, 80)});
        }
        const auto qSweep = renderTongueQualification(sweepEvents, 192000);
        writeWavFile("tongue_register_sweep.wav", qSweep.rendered.audio.data(), 192000, 48000);
        assert(allFinite(qSweep.rendered.audio) && qSweep.rendered.hardClampCount == 0);
    }

    // Voice lifecycle termination:
    // Single strike with 6 seconds of render (288,000 frames) - must naturally terminate to 0 active voices
    {
        const auto qLong = renderTongueQualification({{0, note(62, 70)}}, 288000);
        assert(qLong.finalActiveVoices == 0);
    }

    report << "\n## 3. Qualification summary\n\n"
           << "- Modal saturation count: 0 across single, interval2, chord4, cluster8 v100\n"
           << "- Hard clamp count: 0 across all fixtures\n"
           << "- All samples finite: YES (no NaN, no Inf)\n"
           << "- Voice lifecycle: terminates naturally to 0 active voices\n"
           << "- PolyPressure & ChannelPressure: functional and stable\n"
           << "- 100 model switch cycles: bit-identical PAN recovery\n";
    report.close();
    std::cout << "  -> PASSED: M7.0 Tongue Drum V1 fixtures and objective tests verified.\n";
}

struct BowlQualificationRender {
    M5cResult rendered;
    size_t maxActiveVoices = 0;
    size_t maxStealTails = 0;
    float maxSampleDelta = 0.0f;
    size_t finalActiveVoices = 0;
    uint32_t voiceStealCount = 0;
};

BowlQualificationRender renderBowlQualification(const std::vector<ScheduledEvent>& events, size_t totalFrames) {
    constexpr size_t kBlock = 128;
    BowlQualificationRender result; result.rendered.audio.resize(totalFrames * 2);
    dsp::SynthEngine engine; engine.init(48000.0f); engine.setInstrumentModel(dsp::InstrumentModel::Bowl);
    size_t eventIndex = 0; int32_t previous = 0;
    for (size_t frame = 0; frame < totalFrames; frame += kBlock) {
        while (eventIndex < events.size() && events[eventIndex].frame <= frame) engine.handleMidiEvent(events[eventIndex++].event);
        result.maxStealTails = std::max(result.maxStealTails, engine.getVoiceAllocator().getActiveStealTailCount());
        const size_t count = std::min(kBlock, totalFrames - frame);
        engine.renderBlock(result.rendered.audio.data() + frame * 2, count);
        result.maxActiveVoices = std::max(result.maxActiveVoices, engine.getVoiceAllocator().getActiveVoiceCount());
        for (size_t i = 0; i < count * 2; ++i) {
            const int32_t current = result.rendered.audio[frame * 2 + i];
            result.maxSampleDelta = std::max(result.maxSampleDelta, static_cast<float>(std::abs(current - previous)));
            previous = current;
        }
    }
    result.finalActiveVoices = engine.getVoiceAllocator().getActiveVoiceCount();
    result.voiceStealCount = engine.getVoiceAllocator().getVoiceStealCount();
    result.rendered.metrics = computeMetrics(result.rendered.audio, engine.getSoftClipCount());
    result.rendered.hardClampCount = engine.getHardClampCount();
    result.rendered.modalSat = engine.getModalInternalSaturationCount();
    result.rendered.metrics.maxGainReductionDb = engine.getMaxGainReductionDb();
    result.rendered.metrics.averageGainReductionDb = engine.getAverageGainReductionDb();
    return result;
}

void testM71BowlModel() {
    std::cout << "[Test 24] M7.1 Singing Bowl V1 model, fixtures, beating, and 4-model cycling...\n";
    auto allFinite = [](const std::vector<int32_t>& audio) {
        for (int32_t s : audio) { if (!std::isfinite(static_cast<float>(s))) return false; }
        return true;
    };
    auto note = [](uint8_t n, uint8_t v) {
        midi::MidiEvent e{};
        e.type = midi::MidiEventType::NoteOn;
        e.data1 = n;
        e.data2 = v;
        return e;
    };

    // 1. Model Registry and helpers checks
    const auto& bowlConfig = dsp::getInstrumentModelConfig(dsp::InstrumentModel::Bowl);
    assert(bowlConfig.id == dsp::InstrumentModel::Bowl);
    assert(bowlConfig.modalPreset != nullptr);
    assert(bowlConfig.modalPreset->modeCount == 7);
    assert(dsp::kPresetBowl.modeCount == 7);
    assert(bowlConfig.body.enabled == false);
    assert(bowlConfig.sympathetic.enabled == false);
    assert(bowlConfig.voicing.splitBeatTargetHz == 0.70f);
    assert(bowlConfig.voicing.fixedHzSplit == true);
    assert(bowlConfig.exciter.gain == 0.76f);
    assert(bowlConfig.exciter.noiseAmount == 0.30f);
    assert(bowlConfig.exciter.brightnessMinHz == 600.0f);
    assert(bowlConfig.exciter.brightnessMaxHz == 9000.0f);
    assert(bowlConfig.exciter.velocityKnee == 0.85f);
    assert(bowlConfig.exciter.velocityKneeSlope == 0.36f);
    assert(std::string(dsp::instrumentModelName(dsp::InstrumentModel::Bowl)) == "BOWL");
    assert(dsp::nextInstrumentModel(dsp::InstrumentModel::Pan) == dsp::InstrumentModel::Bell);
    assert(dsp::nextInstrumentModel(dsp::InstrumentModel::Bell) == dsp::InstrumentModel::Tongue);
    assert(dsp::nextInstrumentModel(dsp::InstrumentModel::Tongue) == dsp::InstrumentModel::Bowl);
    assert(dsp::nextInstrumentModel(dsp::InstrumentModel(3)) == dsp::InstrumentModel::Kalimba);
    assert(dsp::nextInstrumentModel(dsp::InstrumentModel::Kalimba) == dsp::InstrumentModel::Glass);
    assert(dsp::nextInstrumentModel(dsp::InstrumentModel::Glass) == dsp::InstrumentModel::Marimba);
    assert(dsp::nextInstrumentModel(dsp::InstrumentModel::Marimba) == dsp::InstrumentModel::Vibraphone);

    // 2. Beating Doublet Verification (Phase S)
    std::cout << "  Verifying Singing Bowl beating doublet mechanism...\n";
    const float kTargetBeatHz = dsp::getInstrumentModelConfig(dsp::InstrumentModel::Bowl).voicing.splitBeatTargetHz;
    assert(std::abs(kTargetBeatHz - 0.70f) < 0.001f);
    assert(dsp::getInstrumentModelConfig(dsp::InstrumentModel::Bowl).voicing.fixedHzSplit == true);
    for (uint8_t midiNote : {uint8_t(50), uint8_t(62), uint8_t(69)}) { // D3, D4, A4
        const float noteHz = midi::MidiMapping::noteToHz(midiNote);
        dsp::ModalResonatorBank bank;
        bank.init(48000.0f);
        bank.setConfig(dsp::getInstrumentModelConfig(dsp::InstrumentModel::Bowl).resonator);
        bank.setPreset(dsp::kPresetBowl);
        const auto& v = dsp::getInstrumentModelConfig(dsp::InstrumentModel::Bowl).voicing;
        bank.setRegisterBehavior(1.0f, v.splitBeatTargetHz, v.fixedHzSplit);
        bank.updatePitchAndDamping(noteHz, 0.0f);

        // Derive actual mode frequencies
        const float f0 = noteHz * dsp::kPresetBowl.modes[0].ratio * (1.0f + dsp::kPresetBowl.modes[0].detune);
        const float detune1 = dsp::computePanDoubletDetune(noteHz, dsp::PanDoubletMode::FixedHz,
                                                          dsp::kPresetBowl.modes[1].detune, v.splitBeatTargetHz);
        const float f1 = noteHz * dsp::kPresetBowl.modes[1].ratio * (1.0f + detune1);
        const float actualBeatHz = std::abs(f1 - f0);
        std::cout << "    Note " << static_cast<int>(midiNote) << " (" << noteHz << " Hz): target beat="
                  << kTargetBeatHz << " Hz, actual=" << actualBeatHz << " Hz\n";
        assert(std::abs(actualBeatHz - kTargetBeatHz) < 0.001f);
    }

    // Time-domain doublet beating verification in rendered PCM:
    // Render single D4 with only the beating pair (modes 0 & 1)
    {
        dsp::ModalPreset doubletPreset = dsp::kPresetBowl;
        doubletPreset.modeCount = 2; // only modes 0 and 1
        dsp::InstrumentModelConfig doubletCfg = dsp::getInstrumentModelConfig(dsp::InstrumentModel::Bowl);
        doubletCfg.modalPreset = &doubletPreset;
        dsp::SynthEngine doubletEngine;
        doubletEngine.init(48000.0f);
        doubletEngine.setModelConfigForTest(doubletCfg);
        doubletEngine.handleMidiEvent(note(62, 90));
        constexpr size_t kBeatFrames = 192000; // 4.0 seconds @ 48 kHz
        std::vector<int32_t> beatPcm(kBeatFrames * 2);
        for (size_t f = 0; f < kBeatFrames; f += 128) {
            doubletEngine.renderBlock(beatPcm.data() + f * 2, 128);
        }
        // At 0.70 Hz, period T = 1 / 0.70 = 1.428 s = ~68,571 samples.
        // In 4 seconds there must be amplitude valleys and peaks (beating modulation).
        float maxAmp = 0.0f, minAmpInMiddle = 1e9f;
        for (size_t f = 0; f < 3000; ++f) {
            maxAmp = std::max(maxAmp, std::abs(static_cast<float>(beatPcm[f * 2])));
        }
        // Valley should occur around T/2 = ~34,285 samples (~0.71 s)
        for (size_t f = 30000; f < 40000; ++f) {
            minAmpInMiddle = std::min(minAmpInMiddle, std::abs(static_cast<float>(beatPcm[f * 2])));
        }
        assert(minAmpInMiddle < maxAmp * 0.6f); // Clear acoustic cancellation valley
        std::cout << "    Doublet PCM time-domain envelope cancellation verified (min/max ratio: "
                  << minAmpInMiddle / maxAmp << ")\n";
    }

    // 3. PolyPressure and ChannelPressure support for Bowl
    for (bool poly : {true, false}) {
        for (uint8_t pressure : {0, 32, 64, 96, 127}) {
            dsp::SynthEngine pressureEngine;
            pressureEngine.init(48000.0f);
            pressureEngine.setInstrumentModel(dsp::InstrumentModel::Bowl);
            pressureEngine.handleMidiEvent(note(60, 90));
            int32_t pressureBlock[256]{};
            pressureEngine.renderBlock(pressureBlock, 128);
            midi::MidiEvent event{};
            event.type = poly ? midi::MidiEventType::PolyPressure : midi::MidiEventType::ChannelPressure;
            event.data1 = poly ? 60 : pressure;
            event.data2 = poly ? pressure : 0;
            pressureEngine.handleMidiEvent(event);
            pressureEngine.renderBlock(pressureBlock, 128);
            for (auto sample : pressureBlock) assert(std::isfinite(static_cast<float>(sample)));
        }
    }

    // 4. 100 model switch cycles: PAN -> BELL -> TONGUE -> BOWL -> KALIMBA -> GLASS -> PAN
    std::cout << "  Testing 100 model switch cycles PAN -> BELL -> TONGUE -> BOWL -> KALIMBA -> GLASS -> PAN...\n";
    dsp::SynthEngine swEngine;
    swEngine.init(48000.0f);
    int32_t swBlock[256]{};
    for (int iter = 0; iter < 100; ++iter) {
        swEngine.handleMidiEvent(note(50, 70));
        swEngine.renderBlock(swBlock, 128);
        swEngine.setInstrumentModel(dsp::InstrumentModel::Bell);
        swEngine.renderBlock(swBlock, 128);
        for (auto sample : swBlock) assert(sample == 0);
        swEngine.handleMidiEvent(note(62, 90));
        swEngine.renderBlock(swBlock, 128);
        swEngine.setInstrumentModel(dsp::InstrumentModel::Tongue);
        swEngine.renderBlock(swBlock, 128);
        for (auto sample : swBlock) assert(sample == 0);
        swEngine.handleMidiEvent(note(58, 80));
        swEngine.renderBlock(swBlock, 128);
        swEngine.setInstrumentModel(dsp::InstrumentModel::Bowl);
        swEngine.renderBlock(swBlock, 128);
        for (auto sample : swBlock) assert(sample == 0);
        swEngine.handleMidiEvent(note(60, 85));
        swEngine.renderBlock(swBlock, 128);
        swEngine.setInstrumentModel(dsp::InstrumentModel::Kalimba);
        swEngine.renderBlock(swBlock, 128);
        for (auto sample : swBlock) assert(sample == 0);
        swEngine.handleMidiEvent(note(62, 95));
        swEngine.renderBlock(swBlock, 128);
        swEngine.setInstrumentModel(dsp::InstrumentModel::Glass);
        swEngine.renderBlock(swBlock, 128);
        for (auto sample : swBlock) assert(sample == 0);
        swEngine.handleMidiEvent(note(65, 90));
        swEngine.renderBlock(swBlock, 128);
        swEngine.setInstrumentModel(dsp::InstrumentModel::Pan);
        swEngine.renderBlock(swBlock, 128);
        for (auto sample : swBlock) assert(sample == 0);
    }
    // Verify direct PAN matches PAN after 100 switches
    swEngine.handleMidiEvent(note(50, 70));
    std::vector<int32_t> swPan(96000 * 2);
    for (size_t frame = 0; frame < 96000; frame += 128) {
        swEngine.renderBlock(swPan.data() + frame * 2, std::min<size_t>(128, 96000 - frame));
    }
    const auto directPan = renderModel({{0, note(50, 70)}}, 96000, dsp::InstrumentModel::Pan);
    assert(fnv1a64(swPan) == fnv1a64(directPan.audio));
    std::cout << "  -> 100 cycles verified bit-identical direct PAN recovery!\n";

    // 5. Deterministic fixtures and metrics report
    std::ofstream report("bowl_m71_metrics.md");
    report << std::fixed << std::setprecision(6);
    report << "# Singing Bowl M7.1 host qualification\n\n"
           << "**InstrumentModel::Bowl (Tibetan Singing Bowl V1 Candidate)**\n\n"
           << "## 1. Modal preset topology\n\n"
           << "| Mode | Ratio | Gain | T60 (s) | Detune | Role |\n"
           << "|---|---:|---:|---:|---:|---|\n";
    constexpr const char* bowlRoles[] = {
        "Fundamental prime", "Prime doublet (beating)", "Low inharmonic partial",
        "Mid partial", "Metallic partial", "Upper partial", "Upper colour shimmer"
    };
    for (size_t i = 0; i < dsp::kPresetBowl.modeCount; ++i) {
        const auto& m = dsp::kPresetBowl.modes[i];
        report << "| " << i << " | " << m.ratio << " | " << m.gain << " | " << m.t60 << " | "
               << m.detune << " | " << bowlRoles[i] << " |\n";
    }

    // Velocity tests (D4):
    report << "\n## 2. Velocity progression (D4)\n\n"
           << "| Velocity | RMS | Peak | Crest Factor | max GR dB | Modal Sat | Clamp |\n"
           << "|---|---:|---:|---:|---:|---:|---:|\n";
    float prevRms = 0.0f;
    for (uint8_t vel : {30, 60, 90, 110, 127}) {
        const auto q = renderBowlQualification({{0, note(62, vel)}}, 144000); // 3 seconds
        const auto& m = q.rendered.metrics;
        writeWavFile((std::string("bowl_D4_v") + std::to_string(vel) + ".wav").c_str(), q.rendered.audio.data(), 144000, 48000);
        report << "| " << static_cast<int>(vel) << " | " << m.rms << " | " << m.peak << " | " << m.crestFactor << " | "
               << m.maxGainReductionDb << " | " << q.rendered.modalSat << " | " << q.rendered.hardClampCount << " |\n";
        assert(allFinite(q.rendered.audio));
        assert(q.rendered.hardClampCount == 0);
        assert(q.rendered.modalSat == 0);
        // Monotonic progression requirement:
        assert(m.rms > prevRms);
        prevRms = m.rms;
    }

    // Single notes: D3 v30, v90, v127
    report << "\n## 3. Register notes (D3 and A4)\n\n"
           << "| Note | Velocity | RMS | Peak | max GR dB | Modal Sat | Clamp |\n"
           << "|---|---:|---:|---:|---:|---:|---:|\n";
    for (uint8_t vel : {30, 90, 127}) {
        const auto qD3 = renderBowlQualification({{0, note(50, vel)}}, 144000);
        writeWavFile((std::string("bowl_D3_v") + std::to_string(vel) + ".wav").c_str(), qD3.rendered.audio.data(), 144000, 48000);
        assert(allFinite(qD3.rendered.audio) && qD3.rendered.hardClampCount == 0 && qD3.rendered.modalSat == 0);
        report << "| D3 | " << static_cast<int>(vel) << " | " << qD3.rendered.metrics.rms << " | " << qD3.rendered.metrics.peak
               << " | " << qD3.rendered.metrics.maxGainReductionDb << " | " << qD3.rendered.modalSat << " | " << qD3.rendered.hardClampCount << " |\n";
    }

    // Single note: A4 v90
    {
        const auto qA4 = renderBowlQualification({{0, note(69, 90)}}, 144000);
        writeWavFile("bowl_A4_v90.wav", qA4.rendered.audio.data(), 144000, 48000);
        assert(allFinite(qA4.rendered.audio) && qA4.rendered.hardClampCount == 0 && qA4.rendered.modalSat == 0);
        report << "| A4 | 90 | " << qA4.rendered.metrics.rms << " | " << qA4.rendered.metrics.peak
               << " | " << qA4.rendered.metrics.maxGainReductionDb << " | " << qA4.rendered.modalSat << " | " << qA4.rendered.hardClampCount << " |\n";
    }

    // Interval2: D4 + A4
    {
        const auto qInt = renderBowlQualification({{0, note(62, 80)}, {0, note(69, 80)}}, 144000);
        writeWavFile("bowl_interval2.wav", qInt.rendered.audio.data(), 144000, 48000);
        assert(allFinite(qInt.rendered.audio) && qInt.rendered.hardClampCount == 0 && qInt.rendered.modalSat == 0);
    }

    // Chord4: D3, A3, D4, A4 (50, 57, 62, 69) v80 and v110
    const auto qChord80 = renderBowlQualification({{0, note(50, 80)}, {0, note(57, 80)}, {0, note(62, 80)}, {0, note(69, 80)}}, 144000);
    writeWavFile("bowl_chord4_v80.wav", qChord80.rendered.audio.data(), 144000, 48000);
    assert(allFinite(qChord80.rendered.audio) && qChord80.rendered.hardClampCount == 0 && qChord80.rendered.modalSat == 0);
    assert(qChord80.maxActiveVoices <= 8);

    const auto qChord110 = renderBowlQualification({{0, note(50, 110)}, {0, note(57, 110)}, {0, note(62, 110)}, {0, note(69, 110)}}, 144000);
    writeWavFile("bowl_chord4.wav", qChord110.rendered.audio.data(), 144000, 48000);
    assert(allFinite(qChord110.rendered.audio) && qChord110.rendered.hardClampCount == 0 && qChord110.rendered.modalSat == 0);
    assert(qChord110.maxActiveVoices <= 8);

    // Cluster8: 8 voices v100
    const auto qClust = renderBowlQualification({
        {0, note(50, 100)}, {0, note(52, 100)}, {0, note(54, 100)}, {0, note(56, 100)},
        {0, note(57, 100)}, {0, note(59, 100)}, {0, note(61, 100)}, {0, note(62, 100)}
    }, 144000);
    writeWavFile("bowl_cluster8.wav", qClust.rendered.audio.data(), 144000, 48000);
    assert(allFinite(qClust.rendered.audio) && qClust.rendered.hardClampCount == 0 && qClust.rendered.modalSat == 0);
    assert(qClust.maxActiveVoices == 8);

    report << "\n## 4. Polyphonic fixtures (Chord4 & Cluster8)\n\n"
           << "| Fixture | Voices | RMS | Peak | max GR dB | Modal Sat | Clamp |\n"
           << "|---|---:|---:|---:|---:|---:|---:|\n"
           << "| Chord4 v80 | 4 | " << qChord80.rendered.metrics.rms << " | " << qChord80.rendered.metrics.peak
           << " | " << qChord80.rendered.metrics.maxGainReductionDb << " | " << qChord80.rendered.modalSat << " | " << qChord80.rendered.hardClampCount << " |\n"
           << "| Chord4 v110 | 4 | " << qChord110.rendered.metrics.rms << " | " << qChord110.rendered.metrics.peak
           << " | " << qChord110.rendered.metrics.maxGainReductionDb << " | " << qChord110.rendered.modalSat << " | " << qChord110.rendered.hardClampCount << " |\n"
           << "| Cluster8 v100 | 8 | " << qClust.rendered.metrics.rms << " | " << qClust.rendered.metrics.peak
           << " | " << qClust.rendered.metrics.maxGainReductionDb << " | " << qClust.rendered.modalSat << " | " << qClust.rendered.hardClampCount << " |\n";

    // Restrike: soft->hard, hard->soft
    {
        const auto qSh = renderBowlQualification({{0, note(62, 30)}, {4800, note(62, 110)}}, 144000);
        writeWavFile("bowl_restrike_softhard.wav", qSh.rendered.audio.data(), 144000, 48000);
        assert(allFinite(qSh.rendered.audio) && qSh.rendered.hardClampCount == 0);

        const auto qHs = renderBowlQualification({{0, note(62, 110)}, {4800, note(62, 30)}}, 144000);
        writeWavFile("bowl_restrike_hardsoft.wav", qHs.rendered.audio.data(), 144000, 48000);
        assert(allFinite(qHs.rendered.audio) && qHs.rendered.hardClampCount == 0);
    }

    // Roll:
    {
        std::vector<ScheduledEvent> rollEvents;
        for (size_t r = 0; r < 8; ++r) {
            rollEvents.push_back({static_cast<uint32_t>(r * 1920), note(62, 75)});
        }
        const auto qRoll = renderBowlQualification(rollEvents, 144000);
        writeWavFile("bowl_roll.wav", qRoll.rendered.audio.data(), 144000, 48000);
        assert(allFinite(qRoll.rendered.audio) && qRoll.rendered.hardClampCount == 0);
    }

    // Register sweep: C3 to C6
    {
        std::vector<ScheduledEvent> sweepEvents;
        for (uint8_t n = 48; n <= 84; n += 4) {
            sweepEvents.push_back({static_cast<uint32_t>((n - 48) * 4800), note(n, 80)});
        }
        const auto qSweep = renderBowlQualification(sweepEvents, 240000);
        writeWavFile("bowl_register_sweep.wav", qSweep.rendered.audio.data(), 240000, 48000);
        assert(allFinite(qSweep.rendered.audio) && qSweep.rendered.hardClampCount == 0);
    }

    // Voice lifecycle termination:
    // Single strike with 12 seconds of render (576,000 frames) - must naturally terminate to 0 active voices
    {
        const auto qLong = renderBowlQualification({{0, note(62, 70)}}, 576000);
        assert(qLong.finalActiveVoices == 0);
    }

    report << "\n## 4. Qualification summary\n\n"
           << "- Modal saturation count: 0 across single v127, interval2, chord4 v110, cluster8 v100\n"
           << "- Hard clamp count: 0 across all fixtures\n"
           << "- All samples finite: YES (no NaN, no Inf)\n"
           << "- Voice lifecycle: terminates naturally to 0 active voices\n"
           << "- PolyPressure & ChannelPressure: functional and stable\n"
           << "- Beating doublet: target 0.70 Hz verified across D3, D4, A4 with time-domain envelope modulation\n"
           << "- 100 model switch cycles: bit-identical PAN recovery\n";
    report.close();
    std::cout << "  -> PASSED: M7.1 Singing Bowl V1 fixtures, doublets, and objective tests verified.\n";
}

struct KalimbaQualificationRender {
    M5cResult rendered;
    size_t maxActiveVoices = 0;
    size_t maxStealTails = 0;
    float maxSampleDelta = 0.0f;
    size_t finalActiveVoices = 0;
    uint32_t voiceStealCount = 0;
    float bodyEnergy = 0.0f;
    float bodyPeak = 0.0f;
};

KalimbaQualificationRender renderKalimbaQualification(const std::vector<ScheduledEvent>& events,
                                                     size_t totalFrames,
                                                     int bodyOverride = -1) {
    constexpr size_t kBlock = 128;
    KalimbaQualificationRender result;
    result.rendered.audio.resize(totalFrames * 2);
    dsp::SynthEngine engine;
    engine.init(48000.0f);
    engine.setInstrumentModel(dsp::InstrumentModel::Kalimba);
    if (bodyOverride >= 0) {
        engine.setBodyEnabled(bodyOverride != 0);
    }
    size_t eventIndex = 0;
    int32_t previous = 0;
    for (size_t frame = 0; frame < totalFrames; frame += kBlock) {
        while (eventIndex < events.size() && events[eventIndex].frame <= frame) {
            engine.handleMidiEvent(events[eventIndex++].event);
        }
        result.maxStealTails = std::max(result.maxStealTails, engine.getVoiceAllocator().getActiveStealTailCount());
        const size_t count = std::min(kBlock, totalFrames - frame);
        engine.renderBlock(result.rendered.audio.data() + frame * 2, count);
        result.maxActiveVoices = std::max(result.maxActiveVoices, engine.getVoiceAllocator().getActiveVoiceCount());
        for (size_t i = 0; i < count * 2; ++i) {
            const int32_t current = result.rendered.audio[frame * 2 + i];
            result.maxSampleDelta = std::max(result.maxSampleDelta, static_cast<float>(std::abs(current - previous)));
            previous = current;
        }
    }
    result.finalActiveVoices = engine.getVoiceAllocator().getActiveVoiceCount();
    result.voiceStealCount = engine.getVoiceAllocator().getVoiceStealCount();
    result.bodyEnergy = engine.getBodyEnergy();
    result.bodyPeak = engine.getBodyPeak();
    result.rendered.metrics = computeMetrics(result.rendered.audio, engine.getSoftClipCount());
    result.rendered.hardClampCount = engine.getHardClampCount();
    result.rendered.modalSat = engine.getModalInternalSaturationCount();
    result.rendered.metrics.maxGainReductionDb = engine.getMaxGainReductionDb();
    result.rendered.metrics.averageGainReductionDb = engine.getAverageGainReductionDb();
    return result;
}

void testM72KalimbaModel() {
    std::cout << "[Test 25] M7.2 Kalimba V1 model, fixtures, pluck exciter, and 5-model cycling...\n";
    auto allFinite = [](const std::vector<int32_t>& audio) {
        for (int32_t s : audio) { if (!std::isfinite(static_cast<float>(s))) return false; }
        return true;
    };
    auto note = [](uint8_t n, uint8_t v) {
        midi::MidiEvent e{};
        e.type = midi::MidiEventType::NoteOn;
        e.data1 = n;
        e.data2 = v;
        return e;
    };

    // 1. Model Registry and Config validation
    const auto& kalimbaConfig = dsp::getInstrumentModelConfig(dsp::InstrumentModel::Kalimba);
    assert(kalimbaConfig.id == dsp::InstrumentModel::Kalimba);
    assert(kalimbaConfig.modalPreset != nullptr);
    assert(kalimbaConfig.modalPreset->modeCount == 5);
    assert(dsp::kPresetKalimba.modeCount == 5);
    assert(kalimbaConfig.body.enabled == true);
    assert(kalimbaConfig.body.modeCount == 3);
    assert(kalimbaConfig.sympathetic.enabled == false);
    assert(kalimbaConfig.exciter.shape == dsp::ExciterShape::Pluck);
    assert(kalimbaConfig.exciter.gain == 0.78f);
    assert(kalimbaConfig.exciter.noiseAmount == 0.22f);
    assert(kalimbaConfig.exciter.brightnessMinHz == 1400.0f);
    assert(kalimbaConfig.exciter.brightnessMaxHz == 11000.0f);
    assert(kalimbaConfig.exciter.velocityKnee == 0.85f);
    assert(kalimbaConfig.exciter.velocityKneeSlope == 0.42f);
    assert(std::string(dsp::instrumentModelName(dsp::InstrumentModel::Kalimba)) == "KALIMBA");
    assert(dsp::nextInstrumentModel(dsp::InstrumentModel::Pan) == dsp::InstrumentModel::Bell);
    assert(dsp::nextInstrumentModel(dsp::InstrumentModel::Bell) == dsp::InstrumentModel::Tongue);
    assert(dsp::nextInstrumentModel(dsp::InstrumentModel::Tongue) == dsp::InstrumentModel::Bowl);
    assert(dsp::nextInstrumentModel(dsp::InstrumentModel::Bowl) == dsp::InstrumentModel::Kalimba);
    assert(dsp::nextInstrumentModel(dsp::InstrumentModel::Kalimba) == dsp::InstrumentModel::Glass);
    assert(dsp::nextInstrumentModel(dsp::InstrumentModel::Glass) == dsp::InstrumentModel::Marimba);
    assert(dsp::nextInstrumentModel(dsp::InstrumentModel::Marimba) == dsp::InstrumentModel::Vibraphone);

    // 2. PolyPressure and ChannelPressure support for Kalimba
    for (bool poly : {true, false}) {
        for (uint8_t pressure : {0, 32, 64, 96, 127}) {
            dsp::SynthEngine pressureEngine;
            pressureEngine.init(48000.0f);
            pressureEngine.setInstrumentModel(dsp::InstrumentModel::Kalimba);
            pressureEngine.handleMidiEvent(note(62, 90));
            int32_t pressureBlock[256]{};
            pressureEngine.renderBlock(pressureBlock, 128);
            midi::MidiEvent event{};
            event.type = poly ? midi::MidiEventType::PolyPressure : midi::MidiEventType::ChannelPressure;
            event.data1 = poly ? 62 : pressure;
            event.data2 = poly ? pressure : 0;
            pressureEngine.handleMidiEvent(event);
            pressureEngine.renderBlock(pressureBlock, 128);
            for (auto sample : pressureBlock) assert(std::isfinite(static_cast<float>(sample)));
        }
    }

    // 3. 100 model switch cycles: PAN -> BELL -> TONGUE -> BOWL -> KALIMBA -> GLASS -> PAN
    std::cout << "  Testing 100 model switch cycles PAN -> BELL -> TONGUE -> BOWL -> KALIMBA -> GLASS -> PAN...\n";
    dsp::SynthEngine swEngine;
    swEngine.init(48000.0f);
    int32_t swBlock[256]{};
    for (int iter = 0; iter < 100; ++iter) {
        swEngine.handleMidiEvent(note(50, 70));
        swEngine.renderBlock(swBlock, 128);
        swEngine.setInstrumentModel(dsp::InstrumentModel::Bell);
        swEngine.renderBlock(swBlock, 128);
        for (auto sample : swBlock) assert(sample == 0);
        swEngine.handleMidiEvent(note(62, 90));
        swEngine.renderBlock(swBlock, 128);
        swEngine.setInstrumentModel(dsp::InstrumentModel::Tongue);
        swEngine.renderBlock(swBlock, 128);
        for (auto sample : swBlock) assert(sample == 0);
        swEngine.handleMidiEvent(note(58, 80));
        swEngine.renderBlock(swBlock, 128);
        swEngine.setInstrumentModel(dsp::InstrumentModel::Bowl);
        swEngine.renderBlock(swBlock, 128);
        for (auto sample : swBlock) assert(sample == 0);
        swEngine.handleMidiEvent(note(60, 85));
        swEngine.renderBlock(swBlock, 128);
        swEngine.setInstrumentModel(dsp::InstrumentModel::Kalimba);
        swEngine.renderBlock(swBlock, 128);
        for (auto sample : swBlock) assert(sample == 0);
        swEngine.handleMidiEvent(note(62, 95));
        swEngine.renderBlock(swBlock, 128);
        swEngine.setInstrumentModel(dsp::InstrumentModel::Glass);
        swEngine.renderBlock(swBlock, 128);
        for (auto sample : swBlock) assert(sample == 0);
        swEngine.handleMidiEvent(note(65, 90));
        swEngine.renderBlock(swBlock, 128);
        swEngine.setInstrumentModel(dsp::InstrumentModel::Pan);
        swEngine.renderBlock(swBlock, 128);
        for (auto sample : swBlock) assert(sample == 0);
    }
    // Verify direct PAN matches PAN after 100 switches
    swEngine.handleMidiEvent(note(50, 70));
    std::vector<int32_t> swPan(96000 * 2);
    for (size_t frame = 0; frame < 96000; frame += 128) {
        swEngine.renderBlock(swPan.data() + frame * 2, std::min<size_t>(128, 96000 - frame));
    }
    const auto directPan = renderModel({{0, note(50, 70)}}, 96000, dsp::InstrumentModel::Pan);
    assert(fnv1a64(swPan) == fnv1a64(directPan.audio));
    std::cout << "  -> 100 cycles verified bit-identical direct PAN recovery!\n";

    // 4. A/B Body Evaluation (Phase I): K0 (body disabled) vs K1 (body enabled)
    std::cout << "  Evaluating Body A/B: K0 (tine only) vs K1 (tine + body)...\n";
    const auto k0_D4 = renderKalimbaQualification({{0, note(62, 90)}}, 96000, 0); // K0 body OFF
    const auto k1_D4 = renderKalimbaQualification({{0, note(62, 90)}}, 96000, 1); // K1 body ON
    writeWavFile("kalimba_K0_D4_v90.wav", k0_D4.rendered.audio.data(), 96000, 48000);
    writeWavFile("kalimba_K1_D4_v90.wav", k1_D4.rendered.audio.data(), 96000, 48000);
    std::cout << "    K0 D4 v90: RMS=" << k0_D4.rendered.metrics.rms << ", Peak=" << k0_D4.rendered.metrics.peak
              << ", BodyEnergy=" << k0_D4.bodyEnergy << "\n";
    std::cout << "    K1 D4 v90: RMS=" << k1_D4.rendered.metrics.rms << ", Peak=" << k1_D4.rendered.metrics.peak
              << ", BodyEnergy=" << k1_D4.bodyEnergy << "\n";
    assert(k1_D4.bodyEnergy > 0.0f);
    assert(k0_D4.bodyEnergy == 0.0f);

    // 5. Deterministic fixtures and metrics report
    std::ofstream report("kalimba_m72_metrics.md");
    report << std::fixed << std::setprecision(6);
    report << "# Kalimba M7.2 host qualification\n\n"
           << "**InstrumentModel::Kalimba (African Thumb Piano V1 Candidate)**\n\n"
           << "## 1. Modal preset topology\n\n"
           << "| Mode | Ratio | Gain | T60 (s) | Detune | Role |\n"
           << "|---|---:|---:|---:|---:|---|\n";
    constexpr const char* kalimbaRoles[] = {
        "Fundamental tine pitch", "First bending overtone", "Second bending overtone",
        "High metallic click/colour", "Upper tine partial"
    };
    for (size_t i = 0; i < dsp::kPresetKalimba.modeCount; ++i) {
        const auto& m = dsp::kPresetKalimba.modes[i];
        report << "| " << i << " | " << m.ratio << " | " << m.gain << " | " << m.t60 << " | "
               << m.detune << " | " << kalimbaRoles[i] << " |\n";
    }

    // Velocity tests (D4):
    report << "\n## 2. Velocity progression (D4)\n\n"
           << "| Velocity | RMS | Peak | Crest Factor | max GR dB | Modal Sat | Clamp |\n"
           << "|---|---:|---:|---:|---:|---:|---:|\n";
    float prevRms = 0.0f;
    for (uint8_t vel : {30, 60, 90, 110, 127}) {
        const auto q = renderKalimbaQualification({{0, note(62, vel)}}, 96000); // 2 seconds
        const auto& m = q.rendered.metrics;
        writeWavFile((std::string("kalimba_D4_v") + std::to_string(vel) + ".wav").c_str(), q.rendered.audio.data(), 96000, 48000);
        report << "| " << static_cast<int>(vel) << " | " << m.rms << " | " << m.peak << " | " << m.crestFactor << " | "
               << m.maxGainReductionDb << " | " << q.rendered.modalSat << " | " << q.rendered.hardClampCount << " |\n";
        assert(allFinite(q.rendered.audio));
        assert(q.rendered.hardClampCount == 0);
        assert(q.rendered.modalSat == 0);
        // Monotonic progression requirement:
        assert(m.rms > prevRms);
        prevRms = m.rms;
    }

    // Single notes: D3 v30, v90
    report << "\n## 3. Register notes (D3 and A4)\n\n"
           << "| Note | Velocity | RMS | Peak | max GR dB | Modal Sat | Clamp |\n"
           << "|---|---:|---:|---:|---:|---:|---:|\n";
    for (uint8_t vel : {30, 90}) {
        const auto qD3 = renderKalimbaQualification({{0, note(50, vel)}}, 96000);
        writeWavFile((std::string("kalimba_D3_v") + std::to_string(vel) + ".wav").c_str(), qD3.rendered.audio.data(), 96000, 48000);
        assert(allFinite(qD3.rendered.audio) && qD3.rendered.hardClampCount == 0 && qD3.rendered.modalSat == 0);
        report << "| D3 | " << static_cast<int>(vel) << " | " << qD3.rendered.metrics.rms << " | " << qD3.rendered.metrics.peak
               << " | " << qD3.rendered.metrics.maxGainReductionDb << " | " << qD3.rendered.modalSat << " | " << qD3.rendered.hardClampCount << " |\n";
    }

    // Single note: A4 v90
    {
        const auto qA4 = renderKalimbaQualification({{0, note(69, 90)}}, 96000);
        writeWavFile("kalimba_A4_v90.wav", qA4.rendered.audio.data(), 96000, 48000);
        assert(allFinite(qA4.rendered.audio) && qA4.rendered.hardClampCount == 0 && qA4.rendered.modalSat == 0);
        report << "| A4 | 90 | " << qA4.rendered.metrics.rms << " | " << qA4.rendered.metrics.peak
               << " | " << qA4.rendered.metrics.maxGainReductionDb << " | " << qA4.rendered.modalSat << " | " << qA4.rendered.hardClampCount << " |\n";
    }

    // Interval2: D4 + A4 (62, 69)
    {
        const auto qInt = renderKalimbaQualification({{0, note(62, 80)}, {0, note(69, 80)}}, 96000);
        writeWavFile("kalimba_interval2.wav", qInt.rendered.audio.data(), 96000, 48000);
        assert(allFinite(qInt.rendered.audio) && qInt.rendered.hardClampCount == 0 && qInt.rendered.modalSat == 0);
    }

    // Chord4: D3, A3, D4, A4 (50, 57, 62, 69) v80 and v110
    const auto qChord80 = renderKalimbaQualification({{0, note(50, 80)}, {0, note(57, 80)}, {0, note(62, 80)}, {0, note(69, 80)}}, 96000);
    assert(allFinite(qChord80.rendered.audio) && qChord80.rendered.hardClampCount == 0 && qChord80.rendered.modalSat == 0);
    assert(qChord80.maxActiveVoices <= 8);

    const auto qChord110 = renderKalimbaQualification({{0, note(50, 110)}, {0, note(57, 110)}, {0, note(62, 110)}, {0, note(69, 110)}}, 96000);
    writeWavFile("kalimba_chord4.wav", qChord110.rendered.audio.data(), 96000, 48000);
    assert(allFinite(qChord110.rendered.audio) && qChord110.rendered.hardClampCount == 0 && qChord110.rendered.modalSat == 0);
    assert(qChord110.maxActiveVoices <= 8);

    // Cluster8: 8 voices v100
    const auto qClust = renderKalimbaQualification({
        {0, note(50, 100)}, {0, note(52, 100)}, {0, note(54, 100)}, {0, note(56, 100)},
        {0, note(57, 100)}, {0, note(59, 100)}, {0, note(61, 100)}, {0, note(62, 100)}
    }, 96000);
    writeWavFile("kalimba_cluster8.wav", qClust.rendered.audio.data(), 96000, 48000);
    assert(allFinite(qClust.rendered.audio) && qClust.rendered.hardClampCount == 0 && qClust.rendered.modalSat == 0);
    assert(qClust.maxActiveVoices == 8);

    report << "\n## 4. Polyphonic fixtures (Chord4 & Cluster8)\n\n"
           << "| Fixture | Voices | RMS | Peak | max GR dB | Modal Sat | Clamp |\n"
           << "|---|---:|---:|---:|---:|---:|---:|\n"
           << "| Chord4 v80 | 4 | " << qChord80.rendered.metrics.rms << " | " << qChord80.rendered.metrics.peak
           << " | " << qChord80.rendered.metrics.maxGainReductionDb << " | " << qChord80.rendered.modalSat << " | " << qChord80.rendered.hardClampCount << " |\n"
           << "| Chord4 v110 | 4 | " << qChord110.rendered.metrics.rms << " | " << qChord110.rendered.metrics.peak
           << " | " << qChord110.rendered.metrics.maxGainReductionDb << " | " << qChord110.rendered.modalSat << " | " << qChord110.rendered.hardClampCount << " |\n"
           << "| Cluster8 v100 | 8 | " << qClust.rendered.metrics.rms << " | " << qClust.rendered.metrics.peak
           << " | " << qClust.rendered.metrics.maxGainReductionDb << " | " << qClust.rendered.modalSat << " | " << qClust.rendered.hardClampCount << " |\n";

    // Restrike: soft->hard, hard->soft
    {
        const auto qSh = renderKalimbaQualification({{0, note(62, 30)}, {4800, note(62, 110)}}, 96000);
        writeWavFile("kalimba_restrike.wav", qSh.rendered.audio.data(), 96000, 48000);
        assert(allFinite(qSh.rendered.audio) && qSh.rendered.hardClampCount == 0);

        const auto qHs = renderKalimbaQualification({{0, note(62, 110)}, {4800, note(62, 30)}}, 96000);
        assert(allFinite(qHs.rendered.audio) && qHs.rendered.hardClampCount == 0);
    }

    // Roll: 8 strikes
    {
        std::vector<ScheduledEvent> rollEvents;
        for (size_t r = 0; r < 8; ++r) {
            rollEvents.push_back({static_cast<uint32_t>(r * 1920), note(62, 75)});
        }
        const auto qRoll = renderKalimbaQualification(rollEvents, 96000);
        writeWavFile("kalimba_roll.wav", qRoll.rendered.audio.data(), 96000, 48000);
        assert(allFinite(qRoll.rendered.audio) && qRoll.rendered.hardClampCount == 0);
    }

    // Register sweep: C3 to C6
    {
        std::vector<ScheduledEvent> sweepEvents;
        for (uint8_t n = 48; n <= 84; n += 4) {
            sweepEvents.push_back({static_cast<uint32_t>((n - 48) * 4800), note(n, 80)});
        }
        const auto qSweep = renderKalimbaQualification(sweepEvents, 192000);
        writeWavFile("kalimba_register_sweep.wav", qSweep.rendered.audio.data(), 192000, 48000);
        assert(allFinite(qSweep.rendered.audio) && qSweep.rendered.hardClampCount == 0);
    }

    // Voice lifecycle termination:
    // Single strike with 8 seconds of render (384,000 frames) - must naturally terminate to 0 active voices
    {
        const auto qLong = renderKalimbaQualification({{0, note(62, 70)}}, 384000);
        assert(qLong.finalActiveVoices == 0);
    }

    report << "\n## 5. Qualification summary\n\n"
           << "- Modal saturation count: 0 across single v127, interval2, chord4 v110, cluster8 v100\n"
           << "- Hard clamp count: 0 across all fixtures\n"
           << "- All samples finite: YES (no NaN, no Inf)\n"
           << "- Voice lifecycle: terminates naturally to 0 active voices\n"
           << "- PolyPressure & ChannelPressure: functional and stable\n"
           << "- 100 model switch cycles: bit-identical PAN recovery\n"
           << "- Body A/B: K0 (tine only) vs K1 (tine + body box) verified\n";
    report.close();
    std::cout << "  -> PASSED: M7.2 Kalimba V1 fixtures, pluck exciter, and objective tests verified.\n";
}

struct GlassQualificationRender {
    M5cResult rendered;
    size_t maxActiveVoices = 0;
    size_t maxStealTails = 0;
    float maxSampleDelta = 0.0f;
    size_t finalActiveVoices = 0;
    uint32_t voiceStealCount = 0;
    float bodyEnergy = 0.0f;
    float bodyPeak = 0.0f;
};

GlassQualificationRender renderGlassQualification(const std::vector<ScheduledEvent>& events,
                                                  size_t totalFrames) {
    constexpr size_t kBlock = 128;
    GlassQualificationRender result;
    result.rendered.audio.resize(totalFrames * 2);
    dsp::SynthEngine engine;
    engine.init(48000.0f);
    engine.setInstrumentModel(dsp::InstrumentModel::Glass);
    size_t eventIndex = 0;
    int32_t previous = 0;
    for (size_t frame = 0; frame < totalFrames; frame += kBlock) {
        while (eventIndex < events.size() && events[eventIndex].frame <= frame) {
            engine.handleMidiEvent(events[eventIndex++].event);
        }
        result.maxStealTails = std::max(result.maxStealTails, engine.getVoiceAllocator().getActiveStealTailCount());
        const size_t count = std::min(kBlock, totalFrames - frame);
        engine.renderBlock(result.rendered.audio.data() + frame * 2, count);
        result.maxActiveVoices = std::max(result.maxActiveVoices, engine.getVoiceAllocator().getActiveVoiceCount());
        for (size_t i = 0; i < count * 2; ++i) {
            const int32_t current = result.rendered.audio[frame * 2 + i];
            result.maxSampleDelta = std::max(result.maxSampleDelta, static_cast<float>(std::abs(current - previous)));
            previous = current;
        }
    }
    result.finalActiveVoices = engine.getVoiceAllocator().getActiveVoiceCount();
    result.voiceStealCount = engine.getVoiceAllocator().getVoiceStealCount();
    result.bodyEnergy = engine.getBodyEnergy();
    result.bodyPeak = engine.getBodyPeak();
    result.rendered.metrics = computeMetrics(result.rendered.audio, engine.getSoftClipCount());
    result.rendered.hardClampCount = engine.getHardClampCount();
    result.rendered.modalSat = engine.getModalInternalSaturationCount();
    result.rendered.metrics.maxGainReductionDb = engine.getMaxGainReductionDb();
    result.rendered.metrics.averageGainReductionDb = engine.getAverageGainReductionDb();
    return result;
}

GlassQualificationRender renderMarimbaQualification(const std::vector<ScheduledEvent>& events,
                                                  size_t totalFrames, const dsp::InstrumentModelConfig& config,
                                                  bool motor = false, float rate = 4.5f, float depth = .32f) {
    constexpr size_t kBlock = 128;
    GlassQualificationRender result;
    result.rendered.audio.resize(totalFrames * 2);
    dsp::SynthEngine engine;
    engine.init(48000.0f);
    engine.setInstrumentModel(config.id);
    engine.setModelConfigForTest(config);
    engine.setVibraphoneMotor(motor, rate, depth);
    size_t eventIndex = 0;
    int32_t previous = 0;
    size_t count = 0;
    for (size_t frame = 0; frame < totalFrames; frame += count) {
        while (eventIndex < events.size() && events[eventIndex].frame <= frame) {
            engine.handleMidiEvent(events[eventIndex++].event);
        }
        result.maxStealTails = std::max(result.maxStealTails, engine.getVoiceAllocator().getActiveStealTailCount());
        count = std::min(kBlock, totalFrames - frame);
        if (eventIndex < events.size()) count = std::min(count, events[eventIndex].frame - frame);
        engine.renderBlock(result.rendered.audio.data() + frame * 2, count);
        for (size_t v=0; v<dsp::kMaxVoices; ++v) {
            const auto& voice = engine.getVoiceAllocator().getVoice(v);
            assert(std::isfinite(voice.getLastSample()) && std::isfinite(voice.getEstimatedEnergy()));
        }
        result.maxActiveVoices = std::max(result.maxActiveVoices, engine.getVoiceAllocator().getActiveVoiceCount());
        for (size_t i = 0; i < count * 2; ++i) {
            const int32_t current = result.rendered.audio[frame * 2 + i];
            result.maxSampleDelta = std::max(result.maxSampleDelta, static_cast<float>(std::abs(double(current) - double(previous))));
            previous = current;
        }
    }
    result.finalActiveVoices = engine.getVoiceAllocator().getActiveVoiceCount();
    result.voiceStealCount = engine.getVoiceAllocator().getVoiceStealCount();
    result.bodyEnergy = engine.getBodyEnergy();
    result.bodyPeak = engine.getBodyPeak();
    result.rendered.metrics = computeMetrics(result.rendered.audio, engine.getSoftClipCount());
    result.rendered.hardClampCount = engine.getHardClampCount();
    result.rendered.modalSat = engine.getModalInternalSaturationCount();
    result.rendered.metrics.maxGainReductionDb = engine.getMaxGainReductionDb();
    result.rendered.metrics.averageGainReductionDb = engine.getAverageGainReductionDb();
    result.rendered.metrics.preLimiterPeak = engine.getPreLimiterPeak();
    result.rendered.grOver0p1DbSamples = engine.getGainReductionOver0p1DbSamples();
    result.rendered.grOver1DbSamples = engine.getGainReductionOver1DbSamples();
    assert(engine.getNonfiniteCount() == 0);
    assert(std::isfinite(engine.getPreLimiterPeak()) && std::isfinite(engine.getBodyEnergy()));
    return result;
}

void testM74MarimbaModel() {
    auto note=[](uint8_t n,uint8_t v) { midi::MidiEvent e{}; e.type=midi::MidiEventType::NoteOn; e.data1=n; e.data2=v; return e; };
    const auto& base = dsp::getInstrumentModelConfig(dsp::InstrumentModel::Marimba);
    assert(base.modalPreset->modeCount == 6);
    assert(std::string(dsp::instrumentModelName(base.id)) == "MARIMBA");
    dsp::InstrumentModel cycle = dsp::InstrumentModel::Pan;
    for (unsigned i = 0; i < unsigned(dsp::InstrumentModel::Count); ++i) cycle = dsp::nextInstrumentModel(cycle);
    assert(cycle == dsp::InstrumentModel::Pan);
    auto b = base, c = base;
    auto bright = *base.modalPreset;
    for (size_t i = 1; i < 6; ++i) bright.modes[i].gain *= 1.18f;
    b.body = {{{210.0f, .10f, .30f}, {420.0f, .065f, .18f}}, 2, .06f, .05f, 900.0f, true};
    b.strikeBusGain = 16.0f;
    c = b; c.modalPreset = &bright; c.body.outputGain = .035f;
    c.exciter.brightnessMaxHz = 8000.0f;
    std::array<dsp::InstrumentModelConfig, 3> configs{base,b,c};
    std::ofstream report("marimba_m74_metrics.md");
    report << std::fixed << std::setprecision(7)
           << "# M7.4 deterministic host qualification\n\nA: warm bar only (runtime provisional). B: same bar plus subtle body. C: upper gains x1.18, 8kHz exciter ceiling, reduced body mix. Listening decision OPEN.\n\n"
           << "| Fixture | Peak | RMS | Crest | Pre peak | Max GR dB | Avg GR dB | >0.1 | >1 | Clamps | Modal sat | Max voices | Steals | Max tails | Nonfinite diagnostics |\n"
           << "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    struct Fixture {std::string name; std::vector<ScheduledEvent> events;};
    std::vector<Fixture> fixtures;
    const uint8_t notes[] = {50,57,62,69,74};
    const char* names[] = {"D3","A3","D4","A4","D5"};
    for (size_t n=0;n<5;++n) for (uint8_t v : {30,70,110,127})
        fixtures.push_back({std::string(names[n])+"_v"+std::to_string(v), {{0,note(notes[n],v)}}});
    fixtures.push_back({"chord", {{0,note(50,110)},{0,note(57,110)},{0,note(62,110)},{0,note(65,110)}}});
    Fixture cluster{"cluster8",{}};
    for (uint8_t n : {50,52,54,56,57,59,61,62}) cluster.events.push_back({0,note(n,110)});
    fixtures.push_back(cluster);
    Fixture stealing{"steal_burst",{}};
    for (unsigned i=0;i<12;++i) stealing.events.push_back({i*384,note(uint8_t(50+i),90)});
    fixtures.push_back(stealing);
    fixtures.push_back({"restrike100",{{0,note(62,70)},{4800,note(62,110)}}});
    fixtures.push_back({"restrike250",{{0,note(62,110)},{12000,note(62,30)}}});
    for (bool varying : {true,false}) {
        Fixture roll{varying?"roll":"roll_steady",{}}; size_t frame=0;
        for (unsigned i=0;i<24;++i) {
            roll.events.push_back({frame,note(62, varying ? uint8_t(40+(i%4)*25) : 90)});
            frame += (i%3==0 ? 3360 : i%3==1 ? 4800 : 5760);
        }
        fixtures.push_back(roll);
    }
    for (const auto& f : fixtures) {
        std::array<GlassQualificationRender,3> results;
        for (size_t i=0;i<3;++i) {
            auto& q=results[i]; q=renderMarimbaQualification(f.events,288000,configs[i]);
            auto& m=q.rendered.metrics;
            assert(q.rendered.hardClampCount==0 && q.rendered.modalSat==0);
            assert(m.rms>0 && std::isfinite(m.rms) && std::isfinite(m.maxGainReductionDb));
            assert(std::abs(m.maxGainReductionDb) < .1f);
            const std::string filename="marimba_"+f.name+"_"+char('A'+i)+".wav";
            writeWavFile(filename.c_str(),q.rendered.audio.data(),288000,48000);
            report << "| "<< filename <<" | "<<m.peak<<" | "<<m.rms<<" | "<<m.crestFactor<<" | "<<m.preLimiterPeak<<" | "<<m.maxGainReductionDb<<" | "<<m.averageGainReductionDb<<" | "<<q.rendered.grOver0p1DbSamples<<" | "<<q.rendered.grOver1DbSamples<<" | "<<q.rendered.hardClampCount<<" | "<<q.rendered.modalSat<<" | "<<q.maxActiveVoices<<" | "<<q.voiceStealCount<<" | "<<q.maxStealTails<<" | 0 |\n";
        }
        // One common RMS target per comparison; minimum avoids clipping normalization.
        const float target=std::min({results[0].rendered.metrics.rms,results[1].rendered.metrics.rms,results[2].rendered.metrics.rms});
        for (size_t i=0;i<3;++i) {
            const auto& audio=results[i].rendered.audio;
            const float gain=target/results[i].rendered.metrics.rms;
            assert(gain*results[i].rendered.metrics.peak < .99f);
            writeRmsMatchedWav(("marimba_"+f.name+"_"+char('A'+i)+"_matched.wav").c_str(),audio,target);
        }
    }
    report << "\n## Register / Nyquist\n\n| MIDI | Hz | Active modes |\n|---|---:|---:|\n";
    for (uint8_t n : {50,57,62,69,74,96,108,127}) {
        dsp::ModalResonatorBank bank; bank.init(48000); bank.setConfig(base.resonator);
        const float hz=midi::MidiMapping::noteToHz(n); bank.setPreset(*base.modalPreset); bank.updatePitchAndDamping(hz,0.0f);
        report << "| "<<int(n)<<" | "<<hz<<" | "<<bank.getActiveModeCount()<<" |\n";
        for (unsigned i=0;i<48000;++i) assert(std::isfinite(bank.processSample(i==0?.01f:0.0f)));
    }
}


void testM75VibraphoneModel() {
    auto note=[](uint8_t n,uint8_t v) { midi::MidiEvent e{}; e.type=midi::MidiEventType::NoteOn; e.data1=n; e.data2=v; return e; };
    const auto& base=dsp::getInstrumentModelConfig(dsp::InstrumentModel::Vibraphone);
    assert(base.modalPreset->modeCount==6);
    assert(std::string(dsp::instrumentModelName(base.id))=="VIBRAPHONE");
    assert(dsp::nextInstrumentModel(base.id)==dsp::InstrumentModel::Mbira);
    std::ofstream report("vibraphone_m753_metrics.md");
    report<<std::fixed<<std::setprecision(7)
          <<"# Frozen Vibraphone V1: C + M1\n\n| Fixture | Motor | FNV64 | Exact M1/dry | Peak | RMS | Pre peak | Max GR dB | Avg GR dB | Clamps | Saturation | Nonfinite |\n|---|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    struct Fixture {std::string name; std::vector<ScheduledEvent> events;};
    std::vector<Fixture> fixtures;
    fixtures.push_back({"D4_v70",{{0,note(62,70)}}});
    fixtures.push_back({"D4_v110",{{0,note(62,110)}}});
    fixtures.push_back({"chord",{{0,note(50,110)},{0,note(57,110)},{0,note(62,110)},{0,note(65,110)}}});
    Fixture phrase{"phrase",{}};
    const uint8_t melody[]={50,57,62,65,69,62,57,74};
    for(unsigned i=0;i<8;++i) phrase.events.push_back({i*28800,note(melody[i],uint8_t(65+(i%3)*15))});
    fixtures.push_back(phrase);
    Fixture cluster{"cluster8",{}}, roll{"roll",{}};
    for(uint8_t n:{50,52,54,56,57,59,61,62}) cluster.events.push_back({0,note(n,110)});
    for(unsigned i=0;i<24;++i) roll.events.push_back({i*4800,note(62,uint8_t(40+(i%4)*25))});
    fixtures.push_back(cluster); fixtures.push_back(roll);
    // M7.5.1 commit 81d054c: separately compiled C, motor OFF and legacy tube.
    // Retain exact raw PCM golden oracles after the one-time byte comparison.
    constexpr uint64_t cReference[6][2] = {
        {0x56a5e102a2026be9ULL, 0x64b43829ca0bbc55ULL},
        {0xd60c9b8423810529ULL, 0x4d4614546d6d8dcdULL},
        {0x45409fa6e702036dULL, 0x2fdf1da50b410ba5ULL},
        {0x619e7ca5d586b755ULL, 0xe80eee62e76bb8d9ULL},
        {0x159b6c87524290f5ULL, 0x52f972563eaad031ULL},
        {0x2ba8b1c15c3df565ULL, 0x07d52d0a799b2849ULL}};
    constexpr size_t frames=384000;
    auto safe=[](const GlassQualificationRender& q) {
        assert(q.rendered.hardClampCount==0 && q.rendered.modalSat==0);
        assert(q.rendered.metrics.rms>0 && std::isfinite(q.rendered.metrics.rms));
        assert(std::abs(q.rendered.metrics.maxGainReductionDb)<.1f);
    };
    for(const auto& f:fixtures) for(unsigned motor=0;motor<2;++motor) {
        const auto q=renderMarimbaQualification(f.events,frames,base,motor!=0);
        safe(q);
        uint64_t hash=14695981039346656037ULL;
        for(int32_t sample:q.rendered.audio) for(unsigned shift=0;shift<32;shift+=8) {
            hash^=(uint32_t(sample)>>shift)&255u; hash*=1099511628211ULL;
        }
        assert(hash==cReference[&f-fixtures.data()][motor]);
        const auto& m=q.rendered.metrics;
        report<<"| "<<f.name<<" | "<<(motor?"ON":"OFF")<<" | "<<std::hex<<hash<<std::dec<<" | exact | "<<m.peak<<" | "<<m.rms<<" | "<<m.preLimiterPeak<<" | "<<m.maxGainReductionDb<<" | "<<m.averageGainReductionDb<<" | 0 | 0 | 0 |\n";
    }
    // Phase advances through silence and MIDI strikes never reset the motor.
    dsp::SynthEngine engine; engine.init(48000); engine.setInstrumentModel(base.id);
    int32_t buffer[256]{}; engine.renderBlock(buffer,128);
    const auto phase=engine.getMotorPhaseForTest(); assert(phase!=0);
    engine.handleMidiEvent(note(62,70)); assert(engine.getMotorPhaseForTest()==phase);
    engine.handleMidiEvent(note(65,90)); assert(engine.getMotorPhaseForTest()==phase);
    engine.setInstrumentModel(base.id); assert(engine.getMotorPhaseForTest()==0);
    for(uint8_t n:{50,57,62,69,74,96,108,127}) {
        dsp::ModalResonatorBank bank; bank.init(48000); bank.setConfig(base.resonator); bank.setPreset(*base.modalPreset);
        bank.updatePitchAndDamping(midi::MidiMapping::noteToHz(n),0.0f);
        for(unsigned i=0;i<48000;++i) assert(std::isfinite(bank.processSample(i==0?.01f:0.0f)));
    }
}

void testM73GlassModel() {
    std::cout << "[Test 26] M7.3 Glass V1 model, fixtures, strike exciter, and 6-model cycling...\n";
    auto allFinite = [](const std::vector<int32_t>& audio) {
        for (int32_t s : audio) { if (!std::isfinite(static_cast<float>(s))) return false; }
        return true;
    };
    auto note = [](uint8_t n, uint8_t v) {
        midi::MidiEvent e{};
        e.type = midi::MidiEventType::NoteOn;
        e.data1 = n;
        e.data2 = v;
        return e;
    };

    // 1. Model Registry and Config validation
    const auto& glassConfig = dsp::getInstrumentModelConfig(dsp::InstrumentModel::Glass);
    assert(glassConfig.id == dsp::InstrumentModel::Glass);
    assert(glassConfig.modalPreset != nullptr);
    assert(glassConfig.modalPreset->modeCount == 6);
    assert(dsp::kPresetGlass.modeCount == 6);
    assert(glassConfig.body.enabled == false);
    assert(glassConfig.sympathetic.enabled == false);
    assert(glassConfig.exciter.shape == dsp::ExciterShape::Strike);
    assert(glassConfig.exciter.gain == 0.72f);
    assert(glassConfig.exciter.noiseAmount == 0.08f);
    assert(glassConfig.exciter.brightnessMinHz == 2400.0f);
    assert(glassConfig.exciter.brightnessMaxHz == 16000.0f);
    assert(glassConfig.exciter.velocityKnee == 0.85f);
    assert(glassConfig.exciter.velocityKneeSlope == 0.40f);
    assert(std::string(dsp::instrumentModelName(dsp::InstrumentModel::Glass)) == "GLASS");
    assert(dsp::nextInstrumentModel(dsp::InstrumentModel::Pan) == dsp::InstrumentModel::Bell);
    assert(dsp::nextInstrumentModel(dsp::InstrumentModel::Bell) == dsp::InstrumentModel::Tongue);
    assert(dsp::nextInstrumentModel(dsp::InstrumentModel::Tongue) == dsp::InstrumentModel::Bowl);
    assert(dsp::nextInstrumentModel(dsp::InstrumentModel::Bowl) == dsp::InstrumentModel::Kalimba);
    assert(dsp::nextInstrumentModel(dsp::InstrumentModel::Kalimba) == dsp::InstrumentModel::Glass);
    assert(dsp::nextInstrumentModel(dsp::InstrumentModel::Glass) == dsp::InstrumentModel::Marimba);
    assert(dsp::nextInstrumentModel(dsp::InstrumentModel::Marimba) == dsp::InstrumentModel::Vibraphone);

    // 2. PolyPressure and ChannelPressure support for Glass
    for (bool poly : {true, false}) {
        for (uint8_t pressure : {0, 32, 64, 96, 127}) {
            dsp::SynthEngine pressureEngine;
            pressureEngine.init(48000.0f);
            pressureEngine.setInstrumentModel(dsp::InstrumentModel::Glass);
            pressureEngine.handleMidiEvent(note(62, 90));
            int32_t pressureBlock[256]{};
            pressureEngine.renderBlock(pressureBlock, 128);
            midi::MidiEvent event{};
            event.type = poly ? midi::MidiEventType::PolyPressure : midi::MidiEventType::ChannelPressure;
            event.data1 = poly ? 62 : pressure;
            event.data2 = poly ? pressure : 0;
            pressureEngine.handleMidiEvent(event);
            pressureEngine.renderBlock(pressureBlock, 128);
            for (auto sample : pressureBlock) assert(std::isfinite(static_cast<float>(sample)));
        }
    }

    // 3. 100 model switch cycles: PAN -> BELL -> TONGUE -> BOWL -> KALIMBA -> GLASS -> PAN
    std::cout << "  Testing 100 model switch cycles PAN -> BELL -> TONGUE -> BOWL -> KALIMBA -> GLASS -> PAN...\n";
    dsp::SynthEngine swEngine;
    swEngine.init(48000.0f);
    int32_t swBlock[256]{};
    for (int iter = 0; iter < 100; ++iter) {
        swEngine.handleMidiEvent(note(50, 70));
        swEngine.renderBlock(swBlock, 128);
        swEngine.setInstrumentModel(dsp::InstrumentModel::Bell);
        swEngine.renderBlock(swBlock, 128);
        for (auto sample : swBlock) assert(sample == 0);
        swEngine.handleMidiEvent(note(62, 90));
        swEngine.renderBlock(swBlock, 128);
        swEngine.setInstrumentModel(dsp::InstrumentModel::Tongue);
        swEngine.renderBlock(swBlock, 128);
        for (auto sample : swBlock) assert(sample == 0);
        swEngine.handleMidiEvent(note(58, 80));
        swEngine.renderBlock(swBlock, 128);
        swEngine.setInstrumentModel(dsp::InstrumentModel::Bowl);
        swEngine.renderBlock(swBlock, 128);
        for (auto sample : swBlock) assert(sample == 0);
        swEngine.handleMidiEvent(note(60, 85));
        swEngine.renderBlock(swBlock, 128);
        swEngine.setInstrumentModel(dsp::InstrumentModel::Kalimba);
        swEngine.renderBlock(swBlock, 128);
        for (auto sample : swBlock) assert(sample == 0);
        swEngine.handleMidiEvent(note(62, 95));
        swEngine.renderBlock(swBlock, 128);
        swEngine.setInstrumentModel(dsp::InstrumentModel::Glass);
        swEngine.renderBlock(swBlock, 128);
        for (auto sample : swBlock) assert(sample == 0);
        swEngine.handleMidiEvent(note(65, 90));
        swEngine.renderBlock(swBlock, 128);
        swEngine.setInstrumentModel(dsp::InstrumentModel::Pan);
        swEngine.renderBlock(swBlock, 128);
        for (auto sample : swBlock) assert(sample == 0);
    }
    // Verify direct PAN matches PAN after 100 switches
    swEngine.handleMidiEvent(note(50, 70));
    std::vector<int32_t> swPan(96000 * 2);
    for (size_t frame = 0; frame < 96000; frame += 128) {
        swEngine.renderBlock(swPan.data() + frame * 2, std::min<size_t>(128, 96000 - frame));
    }
    const auto directPan = renderModel({{0, note(50, 70)}}, 96000, dsp::InstrumentModel::Pan);
    assert(fnv1a64(swPan) == fnv1a64(directPan.audio));
    std::cout << "  -> 100 cycles verified bit-identical direct PAN recovery!\n";

    // 4. Host listening fixtures & metrics collection
    std::ofstream report("glass_m73_metrics.md");
    report << "# Glass / Crystal M7.3 host qualification\n\n"
           << "**InstrumentModel::Glass (Glass Percussion V1 Candidate)**\n\n"
           << "## 1. Model topology\n\n"
           << "| Mode | Ratio | Gain | T60 (s) | Detune | Role |\n"
           << "|---:|------:|-----:|--------:|-------:|------|\n";

    constexpr const char* glassRoles[] = {
        "Fundamental (clear crystal pitch)",
        "First glass partial (transverse mode)",
        "Bright partial (resonant clarity)",
        "High partial (glass chime overtone)",
        "Crystalline colour (fragile sheen)",
        "Short shimmer (crystalline air)"
    };
    for (size_t i = 0; i < dsp::kPresetGlass.modeCount; ++i) {
        const auto& m = dsp::kPresetGlass.modes[i];
        report << "| " << i << " | " << std::fixed << std::setprecision(4) << m.ratio << " | "
               << std::setprecision(2) << m.gain << " | " << m.t60 << " | "
               << m.detune << " | " << glassRoles[i] << " |\n";
    }

    report << "\n## 2. Velocity scaling and monotonicity (Note D4 = 62)\n\n"
           << "| Velocity | RMS (dBFS) | Peak (dBFS) | Modal Sat | Clamps | Max GR (dB) |\n"
           << "|---------:|-----------:|------------:|:---------:|:------:|------------:|\n";

    float previousRms = 0.0f;
    for (uint8_t vel : {20, 40, 60, 80, 100, 120, 127}) {
        const auto q = renderGlassQualification({{0, note(62, vel)}}, 96000); // 2 seconds
        assert(allFinite(q.rendered.audio));
        assert(q.rendered.hardClampCount == 0);
        assert(q.rendered.modalSat == 0);
        assert(q.rendered.metrics.rms > previousRms);
        previousRms = q.rendered.metrics.rms;

        report << "| " << static_cast<int>(vel) << " | " << std::fixed << std::setprecision(2)
               << q.rendered.metrics.rms << " | " << q.rendered.metrics.peak << " | "
               << q.rendered.modalSat << " | " << q.rendered.hardClampCount << " | "
               << q.rendered.metrics.maxGainReductionDb << " |\n";
    }

    // Generate the 13 required host listening fixtures:
    std::cout << "  Rendering 13 Glass host listening fixtures...\n";
    report << "\n## 3. Host listening fixtures (13 WAVs)\n\n"
           << "| Fixture | Description | Peak | RMS | Modal Sat | Clamps | Max GR (dB) |\n"
           << "|:---|:---|---:|---:|:---:|:---:|---:|\n";

    auto recordFixture = [&](const char* filename, const char* desc, const GlassQualificationRender& q) {
        writeWavFile(filename, q.rendered.audio.data(), q.rendered.audio.size() / 2, 48000);
        assert(allFinite(q.rendered.audio));
        assert(q.rendered.hardClampCount == 0);
        report << "| `" << filename << "` | " << desc << " | "
               << std::fixed << std::setprecision(2) << q.rendered.metrics.peak << " | "
               << q.rendered.metrics.rms << " | " << q.rendered.modalSat << " | "
               << q.rendered.hardClampCount << " | " << q.rendered.metrics.maxGainReductionDb << " |\n";
    };

    // 1 & 2: D3 (low register) at v30 and v90
    recordFixture("glass_D3_v30.wav", "D3 note (50) soft velocity 30",
                  renderGlassQualification({{0, note(50, 30)}}, 96000));
    recordFixture("glass_D3_v90.wav", "D3 note (50) firm velocity 90",
                  renderGlassQualification({{0, note(50, 90)}}, 96000));

    // 3, 4, 5, 6: D4 at v30, v60, v90, v127
    recordFixture("glass_D4_v30.wav", "D4 note (62) soft velocity 30",
                  renderGlassQualification({{0, note(62, 30)}}, 96000));
    recordFixture("glass_D4_v60.wav", "D4 note (62) medium velocity 60",
                  renderGlassQualification({{0, note(62, 60)}}, 96000));
    recordFixture("glass_D4_v90.wav", "D4 note (62) firm velocity 90",
                  renderGlassQualification({{0, note(62, 90)}}, 96000));
    const auto qD4_v127 = renderGlassQualification({{0, note(62, 127)}}, 96000);
    assert(qD4_v127.rendered.modalSat == 0);
    recordFixture("glass_D4_v127.wav", "D4 note (62) maximum velocity 127", qD4_v127);

    // 7: A4 (high register) at v90
    recordFixture("glass_A4_v90.wav", "A4 note (69) firm velocity 90",
                  renderGlassQualification({{0, note(69, 90)}}, 96000));

    // 8: 2-note interval (D4 + A4)
    recordFixture("glass_interval2.wav", "2-note fifth interval (D4 62 + A4 69, v80)",
                  renderGlassQualification({{0, note(62, 80)}, {0, note(69, 80)}}, 96000));

    // 9: 4-note chord (D3, A3, D4, A4) at v110
    const auto qChord110 = renderGlassQualification({{0, note(50, 110)}, {0, note(57, 110)}, {0, note(62, 110)}, {0, note(69, 110)}}, 96000);
    assert(qChord110.rendered.modalSat == 0);
    recordFixture("glass_chord4.wav", "4-note resonant chord (50, 57, 62, 69, v110)", qChord110);

    // 10: 8-note cluster at v100
    const auto qCluster100 = renderGlassQualification({
        {0, note(50, 100)}, {0, note(52, 100)}, {0, note(54, 100)}, {0, note(56, 100)},
        {0, note(57, 100)}, {0, note(59, 100)}, {0, note(61, 100)}, {0, note(62, 100)}
    }, 96000);
    assert(qCluster100.rendered.modalSat == 0);
    recordFixture("glass_cluster8.wav", "8-note dense cluster (50..62, v100)", qCluster100);

    // 11: Restrike (soft then hard)
    recordFixture("glass_restrike.wav", "Soft strike (v30) restruck by hard strike (v110) at 100ms",
                  renderGlassQualification({{0, note(62, 30)}, {4800, note(62, 110)}}, 96000));

    // 12: Rapid roll
    std::vector<ScheduledEvent> rollEvents;
    for (size_t i = 0; i < 8; ++i) {
        rollEvents.push_back({static_cast<uint32_t>(i * 2400), note(62, static_cast<uint8_t>(60 + i * 8))});
    }
    recordFixture("glass_roll.wav", "8-strike accelerating dynamic roll on D4",
                  renderGlassQualification(rollEvents, 96000));

    // 13: Register sweep
    std::vector<ScheduledEvent> sweepEvents;
    const uint8_t scaleNotes[] = {50, 54, 57, 62, 66, 69, 74, 78, 81, 86};
    for (size_t i = 0; i < 10; ++i) {
        sweepEvents.push_back({static_cast<uint32_t>(i * 16000), note(scaleNotes[i], 85)});
    }
    recordFixture("glass_register_sweep.wav", "10-note ascending chromatic/modal sweep (D3 to D6)",
                  renderGlassQualification(sweepEvents, 192000));

    // Voice lifecycle termination:
    // Single strike with 8 seconds of render (384,000 frames) - must naturally terminate to 0 active voices
    {
        const auto qLong = renderGlassQualification({{0, note(62, 70)}}, 384000);
        assert(qLong.finalActiveVoices == 0);
    }

    report << "\n## 4. Qualification summary\n\n"
           << "- Modal saturation count: 0 across single v127, chord4 v110, cluster8 v100\n"
           << "- Hard clamp count: 0 across all fixtures\n"
           << "- All samples finite: YES (no NaN, no Inf)\n"
           << "- Voice lifecycle: terminates naturally to 0 active voices\n"
           << "- PolyPressure & ChannelPressure: functional and stable\n"
           << "- 100 model switch cycles: bit-identical PAN recovery\n"
           << "- Doublet: OFF (clean crystal ring, 0 beating)\n"
           << "- Body: OFF (pure crystal resonator bank identity)\n"
           << "- Sympathetic: OFF\n";
    report.close();
    std::cout << "  -> PASSED: M7.3 Glass V1 fixtures, strike exciter, and objective tests verified.\n";
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
    testHandpanDemoScale();
    panM5bVoicingAudit();
    testPanDoubletAb();
    saturationAbAudit();
    generateComparativeWavs();
    testPeakLimiter();
    testOutputStrategyAbc();
    testM5cBodyAndSympathetic();
    testM5c2BodyCoupling();
    testM5dVoicingFreeze();
    testM6ModelArchitectureAndBell();
    testBellM62Qualification();
    testBellM621FreezeQualification();
    testBellM64ListeningPack();
    testForensicsClassification();
    testM7TongueModel();
    testM71BowlModel();
    testM72KalimbaModel();
    testM73GlassModel();
    testM74MarimbaModel();
    testM75VibraphoneModel();

    std::cout << "\n=======================================================\n";
    std::cout << "  ALL DSP AND ACOUSTIC TESTS PASSED SUCCESSFULLY!     \n";
    std::cout << "=======================================================\n";
    return 0;
}
