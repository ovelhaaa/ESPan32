#pragma once

#include <cstddef>
#include <cstdint>

namespace pocketpan::dsp {

constexpr size_t kMaxBodyModes = 6;

struct BodyMode {
    float frequencyHz;
    float gain;
    float t60Seconds;
};

struct BodyConfig {
    BodyMode modes[kMaxBodyModes]{};
    uint8_t modeCount = 0;
    float excitationGain = 0.0f;
    float outputGain = 0.0f;
    float lowpassHz = 0.0f;
    bool enabled = false;
};

// Fixed-frequency, allocation-free instrument shell. It deliberately has no
// knowledge of notes or PAN voicing: its modes represent one shared object.
class BodyResonator {
public:
    void init(float sampleRate);
    void reset();
    void setConfig(const BodyConfig& config);
    float processSample(float excitation);
    float getEnergy() const;

private:
    struct ModeState { float a1=0, a2=0, gain=0, z1=0, z2=0; };
    void updateCoefficients();
    float sampleRate_ = 48000.0f;
    BodyConfig config_{};
    ModeState modes_[kMaxBodyModes]{};
    float lowpassState_ = 0.0f;
    float lowpassCoefficient_ = 0.0f;
};

} // namespace pocketpan::dsp
