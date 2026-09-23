#pragma once

#include <cstdint>
#include <cstddef>

namespace pocketpan::dsp {

class Exciter {
public:
    Exciter() = default;

    void init(float sampleRate);
    void reset();

    // Trigger strike with MIDI normalized velocity (0.0 to 1.0)
    void trigger(float velocity);

    // Render single sample of excitation signal
    float processSample();

    bool isActive() const { return active_; }

private:
    float nextNoise();

    float sampleRate_ = 48000.0f;
    bool active_ = false;

    // Strike trajectory
    uint32_t sampleIndex_ = 0;
    uint32_t impulseSamples_ = 6;
    uint32_t noiseSamples_ = 240; // ~5 ms burst

    float strikeAmplitude_ = 0.0f;
    float noiseGain_ = 0.0f;
    float filterCoeff_ = 0.1f;
    float filterState_ = 0.0f;

    // Fast 32-bit xorshift PRNG (deterministic, zero allocation)
    uint32_t rngState_ = 123456789U;
};

} // namespace pocketpan::dsp
