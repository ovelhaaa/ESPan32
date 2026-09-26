#pragma once

#include <cmath>
#include <cstdint>
#include <cstddef>
#include "modal_mode.h"
#include "modal_preset.h"
#include "dsp_config.h"

namespace pocketpan::dsp {

constexpr float nyquistModeCutoff(float sampleRate) { return 0.48f * sampleRate; }
constexpr float nyquistModeFadeStart(float sampleRate) { return 0.40f * sampleRate; }
inline bool isModeActiveAtSampleRate(float frequencyHz, float sampleRate) {
    return frequencyHz > 10.0f && frequencyHz < nyquistModeCutoff(sampleRate);
}

class ModalResonatorBank {
public:
    ModalResonatorBank() = default;

    void init(float sampleRate);
    void reset();

    void setPreset(const ModalPreset& preset);
    void setConfig(const ResonatorConfig& config) { config_ = config; }
    void updatePitchAndDamping(float fundamentalFrequencyHz, float damping);
    void setExcitationCoupling(const float* coupling, size_t count);
    void setRegisterBehavior(float t60Scale, float splitBeatTargetHz, bool fixedHzSplit);

    // Process a single sample through the resonator bank
    float processSample(float excitation);

    // Process an entire block of samples in place or from input to output buffer
    void processBlock(const float* inExcitation, float* outSignal, size_t frames);

    // Current estimated instantaneous energy of the resonant modes
    float getEnergy() const;

    size_t getModeCount() const { return modeCount_; }
    size_t getActiveModeCount() const { return activeModeCount_; }
    uint32_t getInternalSaturationCount() const { return internalSaturationCount_; }
    void resetInternalSaturationCount() { internalSaturationCount_ = 0; }

private:
    float sampleRate_ = 48000.0f;
    float fundamentalFrequencyHz_ = 220.0f;
    float currentDamping_ = 0.0f;

    size_t modeCount_ = 0;
    size_t activeModeCount_ = 0;
    ModalModeState modes_[kMaxModesPerVoice];
    ModalModeDefinition presetModes_[kMaxModesPerVoice];
    uint32_t internalSaturationCount_ = 0;
    float excitationCoupling_[kMaxModesPerVoice] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    float t60RegisterScale_ = 1.0f;
    float splitBeatTargetHz_ = 0.0f;
    bool fixedHzSplit_ = false;
    ResonatorConfig config_{};
};

} // namespace pocketpan::dsp
