#include "exciter.h"
#include <algorithm>
#include <cmath>

namespace pocketpan::dsp {

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

#if POCKETPAN_ATTACK_FASTPATH
float Exciter::windowTable_[kExciterMaxImpulseSamples + 1][kExciterMaxImpulseSamples];
bool Exciter::windowTableReady_ = false;

void Exciter::buildWindowTable() {
    // Same float expression as the historical per-sample std::sin call, so on
    // any single platform the table lookup returns the identical bit pattern.
    for (uint32_t n = 3; n <= kExciterMaxImpulseSamples; ++n) {
        const float denom = static_cast<float>(n);
        for (uint32_t i = 0; i < n; ++i) {
            windowTable_[n][i] = std::sin((kExciterPi * (i + 0.5f)) / denom);
        }
    }
    windowTableReady_ = true;
}
#endif

void Exciter::init(float sampleRate) {
    sampleRate_ = (sampleRate > 1000.0f) ? sampleRate : 48000.0f;
#if POCKETPAN_ATTACK_FASTPATH
    if (!windowTableReady_) buildWindowTable();
#endif
#if POCKETPAN_VELOCITY_LUT
    buildStrikePow();
#endif
    // init is a model-boundary reset; make a selected model start from the
    // same deterministic noise sequence as a cold boot.
    rngState_ = 123456789U;
    reset();
}

void Exciter::reset() {
    active_ = false;
    sampleIndex_ = 0;
    filterState_ = 0.0f;
}

#if POCKETPAN_VELOCITY_LUT
void Exciter::buildStrikePow() {
    // Same clamps and expression as trigger(); only the 128 discrete MIDI
    // velocities are materialized.
    const float knee = std::clamp(config_.velocityKnee, 0.01f, 1.0f);
    const float slope = std::clamp(config_.velocityKneeSlope, 0.01f, 1.0f);
    for (int i = 0; i < kMidiVelocityCount; ++i) {
        const float v = std::clamp(static_cast<float>(i) / 127.0f, 0.01f, 1.0f);
        const float energyVelocity = v <= knee ? v : knee + (v - knee) * slope;
        strikePowLut_[i] = std::pow(energyVelocity, 1.25f);
    }
    strikePowReady_ = true;
}
#endif

void Exciter::trigger(float velocity, float hardness, float brightnessScale, const float* exactStrikePow) {
    const float v = std::clamp(velocity, 0.01f, 1.0f);
    const float h = std::clamp(hardness, 0.0f, 1.0f);

    // 1. Strike amplitude: calibrated velocity curve
    // Vel 20 (~0.15) -> ~0.24, Vel 80 (~0.63) -> ~0.63, Vel 127 (1.0) -> 1.0
    const float minStrikeGain = 0.15f;
    const float knee = std::clamp(config_.velocityKnee, 0.01f, 1.0f);
    const float slope = std::clamp(config_.velocityKneeSlope, 0.01f, 1.0f);
    // A continuous knee keeps v120–127 expressive through hardness and modal
    // content without making the output limiter a timbre processor.
    const float energyVelocity = v <= knee ? v : knee + (v - knee) * slope;
#if POCKETPAN_VELOCITY_LUT
    float strikePow;
    const int velIndex = velocityLutEnabled_ ? exactMidiVelocityIndex(velocity) : -1;
    if (velIndex >= 0 && strikePowReady_) {
        strikePow = strikePowLut_[velIndex];
    } else {
        strikePow = std::pow(energyVelocity, 1.25f);
    }
    const float strikeGain = minStrikeGain + (1.0f - minStrikeGain) * strikePow;
#else
    const int index = exactStrikePow ? exactMidiVelocityIndex(velocity) : -1;
    const float strikePow = index >= 0 ? exactStrikePow[index] : std::pow(energyVelocity, 1.25f);
    const float strikeGain = minStrikeGain + (1.0f - minStrikeGain) * strikePow;
#endif
    strikeAmplitude_ = strikeGain * config_.gain;

    // 2. Transient hardness: high velocity yields a shorter, sharper impulse
    if (config_.shape == ExciterShape::Pluck) {
        // Pluck: very short release snap (3 to 8 samples)
        const float durationSamples = 8.0f - 5.0f * h;
        impulseSamples_ = std::max<uint32_t>(3U, static_cast<uint32_t>(durationSamples));

        // Noise click: very short slip burst ~1.0 ms to 2.5 ms
        const float burstSeconds = 0.0010f + 0.0015f * (1.0f - h);
        noiseSamples_ = static_cast<uint32_t>(burstSeconds * sampleRate_);

        // Brightness / Cutoff frequency
        const float cutoffHz = std::clamp((config_.brightnessMinHz +
            (config_.brightnessMaxHz - config_.brightnessMinHz) * (h * h)) * brightnessScale,
            config_.brightnessMinHz * 0.65f, config_.brightnessMaxHz);
        const float w = (2.0f * kPi * cutoffHz) / sampleRate_;
        filterCoeff_ = std::clamp(1.0f - std::exp(-w), 0.01f, 0.99f);

        noiseGain_ = strikeAmplitude_ * (0.020f + 0.035f * h) * config_.noiseAmount;

        // Normalization for sum of (1 - i/N)^2 over i=0..N-1
        float sumWeight = 0.0f;
        const float invN = 1.0f / static_cast<float>(impulseSamples_);
        for (uint32_t i = 0; i < impulseSamples_; ++i) {
            const float p = 1.0f - static_cast<float>(i) * invN;
            sumWeight += p * p;
        }
        impulseNorm_ = (sumWeight > 0.0f) ? (1.0f / sumWeight) : 1.0f;
    } else {
        // Soft strike (low v): ~14 samples (soft finger pad / rounded mallet)
        // Hard strike (high v): ~3 samples (hard strike / knuckle)
        const float durationSamples = 14.0f - 11.0f * h;
        impulseSamples_ = std::max<uint32_t>(3U, static_cast<uint32_t>(durationSamples));

        // 3. Noise burst: duration ~3 ms to 8 ms
        const float burstSeconds = 0.003f + 0.005f * (1.0f - h);
        noiseSamples_ = static_cast<uint32_t>(burstSeconds * sampleRate_);

        // 4. Brightness / Cutoff frequency:
        // Low velocity: ~700 Hz (warm, rounded thud)
        // High velocity: ~12,000 Hz (bright, crisp acoustic strike)
        const float cutoffHz = std::clamp((config_.brightnessMinHz +
            (config_.brightnessMaxHz - config_.brightnessMinHz) * (h * h)) * brightnessScale,
            config_.brightnessMinHz * 0.65f, config_.brightnessMaxHz);
        const float w = (2.0f * kPi * cutoffHz) / sampleRate_;
        filterCoeff_ = std::clamp(1.0f - std::exp(-w), 0.01f, 0.99f);

        // Hardness adds texture, but high velocity is primarily modal coupling.
        noiseGain_ = strikeAmplitude_ * (0.035f + 0.045f * h) * config_.noiseAmount;

        // Hoisted impulse normalisation.  The multiply association used by the
        // sample loop remains (strikeAmplitude_ * window) * norm.
        impulseNorm_ = (kExciterPi * 0.5f) / static_cast<float>(impulseSamples_);
    }

    sampleIndex_ = 0;
    filterState_ = 0.0f;
    active_ = true;
}

#if !POCKETPAN_ATTACK_FASTPATH
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

    if (config_.shape == ExciterShape::Pluck) {
        if (sampleIndex_ < impulseSamples_) {
            const float p = 1.0f - (static_cast<float>(sampleIndex_) / static_cast<float>(impulseSamples_));
            output += strikeAmplitude_ * (p * p) * impulseNorm_;
        }
    } else {
        // 1. Shaped impulse component (half-sine bell window normalized to unit integral)
        // Ensures physical momentum conservation across different mallet hardnesses
        if (sampleIndex_ < impulseSamples_) {
            const float phase = (kPi * (sampleIndex_ + 0.5f)) / static_cast<float>(impulseSamples_);
            const float window = std::sin(phase);
            const float norm = (kPi * 0.5f) / static_cast<float>(impulseSamples_);
            output += strikeAmplitude_ * window * norm;
        }
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
#endif

#if POCKETPAN_ATTACK_FASTPATH || defined(POCKETPAN_EXCITER_DIFFERENTIAL_TEST)
float Exciter::processSampleReference() {
    if (!active_) return 0.0f;

    float output = 0.0f;

    if (config_.shape == ExciterShape::Pluck) {
        if (sampleIndex_ < impulseSamples_) {
            const float p = 1.0f - (static_cast<float>(sampleIndex_) / static_cast<float>(impulseSamples_));
            output += strikeAmplitude_ * (p * p) * impulseNorm_;
        }
    } else {
        if (sampleIndex_ < impulseSamples_) {
            const float phase = (kPi * (sampleIndex_ + 0.5f)) / static_cast<float>(impulseSamples_);
            const float window = std::sin(phase);
            const float norm = (kPi * 0.5f) / static_cast<float>(impulseSamples_);
            output += strikeAmplitude_ * window * norm;
        }
    }

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
#endif

} // namespace pocketpan::dsp
