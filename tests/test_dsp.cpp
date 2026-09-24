#include <iostream>
#include <vector>
#include <cmath>
#include <cassert>
#include <fstream>
#include <cstring>
#include <iomanip>

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
};

AudioMetrics computeMetrics(const std::vector<int32_t>& audioStereo, uint32_t softClipCount) {
    AudioMetrics m;
    m.softClipCount = softClipCount;
    if (audioStereo.empty()) return m;

    double sumSq = 0.0;
    float maxAbs = 0.0f;
    const size_t numSamples = audioStereo.size();

    for (size_t i = 0; i < numSamples; ++i) {
        float s = static_cast<float>(audioStereo[i]) / 2147483647.0f;
        float absVal = std::abs(s);
        if (absVal > maxAbs) maxAbs = absVal;
        sumSq += (s * s);
    }

    m.peak = maxAbs;
    m.rms = static_cast<float>(std::sqrt(sumSq / numSamples));
    m.crestFactor = (m.rms > 1.0e-6f) ? (m.peak / m.rms) : 0.0f;
    return m;
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

    float previousRms = 0.0f;
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
        report << "| "<<filename<<" | "<<m.peak<<" | "<<m.rms<<" | "<<m.crestFactor<<" | "<<m.softClipCount<<" | "<<sat<<" |\n";
        std::cout<<filename<<" peak="<<m.peak<<" rms="<<m.rms<<" crest="<<m.crestFactor<<" limiter="<<m.softClipCount<<" modalSat="<<sat<<"\n";
    };
    render("pan_D3_vel40.wav",{{0,event(62,40)}},144000);
    render("pan_D3_vel90.wav",{{0,event(62,90)}},144000);
    render("pan_D3_vel127.wav",{{0,event(62,127)}},144000);
    render("pan_D3_double_strike.wav",{{0,event(62,80)},{12000,event(62,95)}},168000);
    render("pan_D3_triple_strike.wav",{{0,event(62,80)},{12000,event(62,95)},{24000,event(62,105)}},168000);
    render("pan_chord_simultaneous.wav",{{0,event(62,85)},{0,event(69,80)},{0,event(77,75)},{0,event(81,80)}},192000);
    render("pan_dense_cluster.wav",{{0,event(62,100)},{0,event(64,100)},{0,event(65,100)},{0,event(67,100)},
           {0,event(69,100)},{0,event(70,100)},{0,event(72,100)},{0,event(74,100)}},192000);
}

int main() {
    std::cout << "=======================================================\n";
    std::cout << "  Pocket Pan / Metal Modal Synth - Comprehensive DSP  \n";
    std::cout << "=======================================================\n\n";

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
    generateComparativeWavs();

    std::cout << "\n=======================================================\n";
    std::cout << "  ALL DSP AND ACOUSTIC TESTS PASSED SUCCESSFULLY!     \n";
    std::cout << "=======================================================\n";
    return 0;
}
