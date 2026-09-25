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
    // Clears reported statistics without touching delay or envelope state.
    void resetDiagnostics();
    void setConfig(const LimiterConfig& cfg);
    float processSample(float x);

    float getCurrentGainReductionDb() const;
    float getMaxGainReductionDb() const;
    uint32_t getActiveSampleCount() const { return activeSamples_; }
    uint32_t getGainReductionOver0p1DbSamples() const { return gainReductionOver0p1DbSamples_; }
    uint32_t getGainReductionOver1DbSamples() const { return gainReductionOver1DbSamples_; }
    float getAverageGainReductionDb() const;

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
    uint32_t gainReductionOver0p1DbSamples_ = 0;
    uint32_t gainReductionOver1DbSamples_ = 0;
    uint32_t processedSamples_ = 0;
    float gainReductionDbSum_ = 0.0f;
};

} // namespace pocketpan::dsp
