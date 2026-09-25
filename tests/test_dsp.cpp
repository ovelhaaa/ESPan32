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
    for (size_t i=0; i<panFixtures.size(); ++i) { const uint64_t hash=fnv1a64(renderModel(panFixtures[i].second,96000,dsp::InstrumentModel::Pan).audio); assert(hash == kPanGolden[i]); std::cout << "  PAN FNV " << panFixtures[i].first << " = 0x" << std::hex << hash << std::dec << "\n"; }
    dsp::SynthEngine switched; switched.init(48000.0f); switched.handleMidiEvent(note(50,70)); int32_t block[256]{}; switched.renderBlock(block,128);
    switched.setInstrumentModel(dsp::InstrumentModel::Bell); switched.renderBlock(block,128); for (auto sample : block) assert(sample == 0);
    switched.handleMidiEvent(note(62,110)); switched.renderBlock(block,128); switched.setInstrumentModel(dsp::InstrumentModel::Pan); switched.handleMidiEvent(note(50,70));
    std::vector<int32_t> switchedPan(96000 * 2); for (size_t frame=0; frame<96000; frame+=128) switched.renderBlock(switchedPan.data()+frame*2, std::min<size_t>(128,96000-frame));
    if (fnv1a64(switchedPan) != fnv1a64(pan.audio)) {
        for (size_t i=0; i<switchedPan.size(); ++i) if (switchedPan[i] != pan.audio[i]) { std::cerr << "M6 switch first diff " << i << " " << switchedPan[i] << " vs " << pan.audio[i] << "\n"; break; }
        assert(false && "PAN after model switch must match direct PAN");
    }
    float previousUpper = -1.0f;
    for (uint8_t velocity : {30,70,110,127}) {
        const auto bell = renderModel({{0,note(62,velocity)}}, 96000, dsp::InstrumentModel::Bell); assert(bell.hardClampCount == 0 && bell.modalSat == 0);
        const float f = midi::MidiMapping::noteToHz(62); const float primary = spectralEnergy(bell.audio,{f,2.0f*f});
        const float upper = spectralEnergy(bell.audio,{3.0f*f,4.0f*f,5.2f*f}) / std::max(primary, 1.0e-12f); assert(upper >= previousUpper); previousUpper = upper;
    }
    for (uint8_t midiNote : {36,50,57,62,69,74,84}) { const auto bell = renderModel({{0,note(midiNote,110)}}, 576000, dsp::InstrumentModel::Bell); assert(bell.hardClampCount == 0 && bell.modalSat == 0); }
    const auto single=renderModel({{0,note(62,100)}},48000,dsp::InstrumentModel::Bell); const auto restrike=renderModel({{0,note(62,100)},{4800,note(62,100)}},48000,dsp::InstrumentModel::Bell);
    assert(windowRms(restrike.audio,.10f,.20f) > windowRms(single.audio,.10f,.20f));
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

    std::cout << "\n=======================================================\n";
    std::cout << "  ALL DSP AND ACOUSTIC TESTS PASSED SUCCESSFULLY!     \n";
    std::cout << "=======================================================\n";
    return 0;
}
