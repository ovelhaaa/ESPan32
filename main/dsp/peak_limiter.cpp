#include "peak_limiter.h"

#include <algorithm>
#include <cmath>

namespace pocketpan::dsp {
namespace {
constexpr float dbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }
}

void PeakLimiter::init(float sampleRate) {
    sampleRate_ = sampleRate;
    setConfig(config_);
    reset();
}

void PeakLimiter::reset() {
    for (uint32_t i = 0; i < kMaxLookahead; ++i) { delay_[i] = 0.0f; requiredGain_[i] = 1.0f; }
    writeIndex_ = 0;
    gain_ = 1.0f;
    holdSamples_ = 0;
    currentGrDb_ = 0.0f;
    maxGrDb_ = 0.0f;
    activeSamples_ = 0;
}

void PeakLimiter::setConfig(const LimiterConfig& cfg) {
    config_ = cfg;
    config_.lookaheadSamples = std::clamp(config_.lookaheadSamples, 1U, kMaxLookahead);
    config_.releaseMs = std::max(config_.releaseMs, 1.0f);
    thresholdLinear_ = dbToLinear(config_.thresholdDb);
    ceilingLinear_ = dbToLinear(config_.ceilingDb);
    const float releaseSamples = config_.releaseMs * sampleRate_ / 1000.0f;
    releaseCoefficient_ = std::exp(-1.0f / releaseSamples);
}

float PeakLimiter::processSample(float x) {
    if (!std::isfinite(x)) x = 0.0f;

    // The sample is delayed before it is emitted. Its peak is therefore known
    // a full lookahead window before it reaches the output.
    const uint32_t readIndex = (writeIndex_ + kMaxLookahead - config_.lookaheadSamples) % kMaxLookahead;
    const float delayed = delay_[readIndex];
    const float requiredForDelayedSample = requiredGain_[readIndex];
    const float peak = std::abs(x);
    // Ceiling, rather than a waveshaper, determines the required attenuation.
    // Threshold remains an explicit detector/activity boundary for diagnostics.
    const float desiredGain = peak > thresholdLinear_
        ? std::min(1.0f, ceilingLinear_ / std::max(peak, 1.0e-20f))
        : 1.0f;
    delay_[writeIndex_] = x;
    requiredGain_[writeIndex_] = desiredGain;
    writeIndex_ = (writeIndex_ + 1) % kMaxLookahead;
    if (desiredGain < gain_) {
        gain_ = desiredGain; // instantaneous attack
        holdSamples_ = config_.lookaheadSamples;
    } else if (holdSamples_ > 0) {
        // Do not begin release until the detected peak has left the delay line.
        // This makes the configured ceiling a hard contract, not an average.
        --holdSamples_;
    } else {
        gain_ = desiredGain + releaseCoefficient_ * (gain_ - desiredGain);
    }
    // The scheduled value is an exact per-sample ceiling guarantee. The normal
    // envelope has had the full lookahead interval to reach it; this final min
    // only protects pathological peak sequences from a release overshoot.
    gain_ = std::min(gain_, requiredForDelayedSample);

    currentGrDb_ = gain_ < 1.0f ? 20.0f * std::log10(gain_) : 0.0f;
    maxGrDb_ = std::min(maxGrDb_, currentGrDb_);
    if (gain_ < 0.99999f) ++activeSamples_;
    return delayed * gain_;
}

float PeakLimiter::getCurrentGainReductionDb() const { return currentGrDb_; }
float PeakLimiter::getMaxGainReductionDb() const { return maxGrDb_; }

} // namespace pocketpan::dsp
