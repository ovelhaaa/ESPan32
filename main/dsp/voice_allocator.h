#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include "modal_voice.h"
#include "sympathetic_config.h"
#include "instrument_model.h"

namespace pocketpan::dsp {

constexpr size_t kMaxVoices = 8;

class VoiceAllocator {
public:
    VoiceAllocator() = default;

    void init(float sampleRate);
    void reset();

    // Note On: allocate voice or steal lowest energy voice
    void noteOn(uint8_t note, float velocity, float fundamentalFrequencyHz);

    // Note Off: release note gesture (natural metallic ring down continues)
    void noteOff(uint8_t note);

    // Polyphonic Key Pressure (per-note aftertouch choke)
    void setPolyPressure(uint8_t note, float pressure);

    // Channel Pressure (global aftertouch choke across all active voices)
    void setChannelPressure(float pressure);

    // Render polyphonic sum of all active voices into outBuffer
    void renderBlock(float* outBuffer, size_t frames);
    void renderBlock(float* outBuffer, size_t frames, const SympatheticConfig& config);
    // Renders the normal dry mix and, in the same voice pass, a sum of local
    // exciter taps. The latter is a body input bus, never a sympathetic bus.
    void renderBlockWithStrikeBus(float* outBuffer, float* strikeBuffer, size_t frames,
                                  const SympatheticConfig& config);
    float getSympatheticBusPeak() const { return sympatheticBusPeak_; }
    float getSympatheticBusRms() const { return sympatheticBusSamples_ ? std::sqrt(sympatheticBusSumSquares_/sympatheticBusSamples_) : 0.0f; }
    uint32_t getSympatheticSafetyCount() const { return sympatheticSafetyCount_; }
    // Audio memory only. This deliberately does not erase telemetry.
    void resetSympatheticState();
    // Telemetry only. This deliberately does not alter the delayed bus.
    void resetSympatheticDiagnostics();

    // Real-time voice metrics
    size_t getActiveVoiceCount() const;
    size_t getActiveStealTailCount() const;
    uint32_t getInternalSaturationCount() const;
    void setInternalSafetySaturation(bool enabled);
    void setModelConfig(const InstrumentModelConfig& config);
    void setPanConfigsForTest(const ExciterConfig& exciter, const PanVoicingConfig& voicing);
    const ModalVoice& getVoice(size_t index) const { return voices_[index]; }

private:
    int findVoiceToSteal() const;

    struct StealDeclickTail {
        bool active = false;
        float currentSample = 0.0f;
        float step = 0.0f;
        uint16_t samplesLeft = 0;
    };

    float sampleRate_ = 48000.0f;
    ModalVoice voices_[kMaxVoices];
    StealDeclickTail stealTails_[kMaxVoices]{};
    size_t nextStealTail_ = 0;
    float sympatheticPreviousBus_ = 0.0f, sympatheticFilterState_ = 0.0f, sympatheticLowpassCoefficient_ = 0.0f;
    float sympatheticBusPeak_ = 0.0f, sympatheticBusSumSquares_ = 0.0f; uint32_t sympatheticBusSamples_ = 0, sympatheticSafetyCount_ = 0;
};

} // namespace pocketpan::dsp
