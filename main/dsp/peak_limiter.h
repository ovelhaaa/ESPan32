#pragma once

#include <cstdint>

namespace pocketpan::dsp {

struct LimiterConfig {
    float thresholdDb = -3.0f;       // Detector activation threshold.
    float ceilingDb = -0.5f;         // Absolute output ceiling.
    float attackMs = 0.0f;           // Lookahead makes the attack instantaneous.
    float releaseMs = 80.0f;
    uint32_t lookaheadSamples = 32;
};

// Mono peak limiter. The fixed delay line supplies lookahead without allocating
// from the audio thread. kMaxLookahead is deliberately small for the ESP32.
class PeakLimiter {
public:
    void init(float sampleRate);
    void reset();
    void setConfig(const LimiterConfig& cfg);
    float processSample(float x);

    float getCurrentGainReductionDb() const;
    float getMaxGainReductionDb() const;
    uint32_t getActiveSampleCount() const { return activeSamples_; }

private:
    static constexpr uint32_t kMaxLookahead = 64;
    float sampleRate_ = 48000.0f;
    LimiterConfig config_{};
    float delay_[kMaxLookahead]{};
    float requiredGain_[kMaxLookahead]{};
    uint32_t writeIndex_ = 0;
    float gain_ = 1.0f;
    float releaseCoefficient_ = 0.0f;
    float thresholdLinear_ = 1.0f;
    float ceilingLinear_ = 1.0f;
    uint32_t holdSamples_ = 0;
    float currentGrDb_ = 0.0f;
    float maxGrDb_ = 0.0f;
    uint32_t activeSamples_ = 0;
};

} // namespace pocketpan::dsp
