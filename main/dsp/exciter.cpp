#include "exciter.h"
#include <algorithm>
#include <cmath>

namespace pocketpan::dsp {

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

void Exciter::init(float sampleRate) {
    sampleRate_ = (sampleRate > 1000.0f) ? sampleRate : 48000.0f;
    reset();
}

void Exciter::reset() {
    active_ = false;
    sampleIndex_ = 0;
    filterState_ = 0.0f;
}

void Exciter::trigger(float velocity) {
    const float v = std::clamp(velocity, 0.01f, 1.0f);

    // 1. Strike amplitude: calibrated velocity curve
    // Vel 20 (~0.15) -> ~0.24, Vel 80 (~0.63) -> ~0.63, Vel 127 (1.0) -> 1.0
    const float minStrikeGain = 0.15f;
    const float strikeGain = minStrikeGain + (1.0f - minStrikeGain) * std::pow(v, 1.25f);
    strikeAmplitude_ = strikeGain * 0.80f;

    // 2. Transient hardness: high velocity yields a shorter, sharper impulse
    // Soft strike (low v): ~14 samples (soft finger pad / rounded mallet)
    // Hard strike (high v): ~3 samples (hard strike / knuckle)
    const float durationSamples = 14.0f - 11.0f * v;
    impulseSamples_ = std::max<uint32_t>(3U, static_cast<uint32_t>(durationSamples));

    // 3. Noise burst: duration ~3 ms to 8 ms
    const float burstSeconds = 0.003f + 0.005f * (1.0f - v);
    noiseSamples_ = static_cast<uint32_t>(burstSeconds * sampleRate_);

    // 4. Brightness / Cutoff frequency:
    // Low velocity: ~700 Hz (warm, rounded thud)
    // High velocity: ~12,000 Hz (bright, crisp acoustic strike)
    const float cutoffHz = 700.0f + 11300.0f * (v * v);
    const float w = (2.0f * kPi * cutoffHz) / sampleRate_;
    filterCoeff_ = std::clamp(1.0f - std::exp(-w), 0.01f, 0.99f);

    noiseGain_ = strikeAmplitude_ * (0.04f + 0.06f * v);

    sampleIndex_ = 0;
    filterState_ = 0.0f;
    active_ = true;
}

float Exciter::nextNoise() {
    // 32-bit xorshift PRNG
    rngState_ ^= (rngState_ << 13);
    rngState_ ^= (rngState_ >> 17);
    rngState_ ^= (rngState_ << 5);
    // Convert to [-1.0f, 1.0f]
    return (static_cast<float>(static_cast<int32_t>(rngState_)) / 2147483648.0f);
}

float Exciter::processSample() {
    if (!active_) return 0.0f;

    float output = 0.0f;

    // 1. Shaped impulse component (half-sine bell window normalized to unit integral)
    // Ensures physical momentum conservation across different mallet hardnesses
    if (sampleIndex_ < impulseSamples_) {
        const float phase = (kPi * (sampleIndex_ + 0.5f)) / static_cast<float>(impulseSamples_);
        const float window = std::sin(phase);
        const float norm = (kPi * 0.5f) / static_cast<float>(impulseSamples_);
        output += strikeAmplitude_ * window * norm;
    }

    // 2. Filtered noise burst component with decay
    if (sampleIndex_ < noiseSamples_) {
        const float rawNoise = nextNoise();
        filterState_ += filterCoeff_ * (rawNoise - filterState_);

        const float env = 1.0f - (static_cast<float>(sampleIndex_) / static_cast<float>(noiseSamples_));
        output += filterState_ * noiseGain_ * (env * env);
    }

    sampleIndex_++;
    if (sampleIndex_ >= noiseSamples_ && sampleIndex_ >= impulseSamples_) {
        active_ = false;
    }

    return output;
}

} // namespace pocketpan::dsp
