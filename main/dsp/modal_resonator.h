#pragma once

#include <cmath>
#include <cstdint>
#include <cstddef>
#include "modal_mode.h"
#include "modal_preset.h"

namespace pocketpan::dsp {

class ModalResonatorBank {
public:
    ModalResonatorBank() = default;

    void init(float sampleRate);
    void reset();

    void setPreset(const ModalPreset& preset);
    void updatePitchAndDamping(float fundamentalFrequencyHz, float damping);

    // Process a single sample through the resonator bank
    float processSample(float excitation);

    // Process an entire block of samples in place or from input to output buffer
    void processBlock(const float* inExcitation, float* outSignal, size_t frames);

    // Current estimated instantaneous energy of the resonant modes
    float getEnergy() const;

    size_t getModeCount() const { return modeCount_; }
    size_t getActiveModeCount() const { return activeModeCount_; }

private:
    float sampleRate_ = 48000.0f;
    float fundamentalFrequencyHz_ = 220.0f;
    float currentDamping_ = 0.0f;

    size_t modeCount_ = 0;
    size_t activeModeCount_ = 0;
    ModalModeState modes_[kMaxModesPerVoice];
    ModalModeDefinition presetModes_[kMaxModesPerVoice];
};

} // namespace pocketpan::dsp
