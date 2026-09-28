#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include "dsp_config.h"
#include "dsp_profile.h"
#include "trigger_precompute.h"

namespace pocketpan::dsp {

// The impulse window is a half-sine with at most 14 samples (hardest strike).
// The angle is the same float expression used by the historical per-sample
// path, so the table is bit-identical to the runtime std::sin result on the
// platform that built it (see tests/test_exciter_fastpath.cpp).
constexpr uint32_t kExciterMaxImpulseSamples = 14;
constexpr float kExciterPi = 3.14159265358979323846f;

class Exciter {
public:
    Exciter() = default;

    void init(float sampleRate);
    void reset();
    void setConfig(const ExciterConfig& config) {
        config_ = config;
#if POCKETPAN_VELOCITY_LUT
        // Built eagerly at the configuration boundary so the realtime event
        // block never pays for 128 pow() calls.
        buildStrikePow();
#endif
    }

    // Trigger strike with MIDI normalized velocity (0.0 to 1.0)
    void trigger(float velocity, float hardness = 1.0f, float brightnessScale = 1.0f);

    // Render single sample of excitation signal.  With the attack fast path
    // (candidate 21) the body is inlined so ModalVoice::processSample can fold
    // the exciter into the IRAM-resident voice kernel; the sample stream and
    // active_ transition are bit-identical to the reference.
    float processSample();

    // Exact scalar reference: the historical body, compiled only when the
    // differential harness requests it.  It is the definition of correctness
    // for the fast path and advances identical state.
    float processSampleReference();

    bool isActive() const { return active_; }

    // M6.3.6 (§22).  Number of further processSample() calls that will still run
    // the active body before the exact inactive transition.  While active_ is
    // true, sampleIndex_ is guaranteed below this end index, so the result is at
    // least 1.  Used only to segment an attack block at the exciter boundary;
    // the transition condition itself is unchanged.
    uint32_t samplesUntilInactive() const {
        if (!active_) return 0;
        const uint32_t end = noiseSamples_ > impulseSamples_ ? noiseSamples_ : impulseSamples_;
        return sampleIndex_ < end ? end - sampleIndex_ : 0;
    }

#ifdef POCKETPAN_EXCITER_DIFFERENTIAL_TEST
    // Host-only exactness probes.  Never present in firmware.
    uint32_t rngStateForTest() const { return rngState_; }
    float filterStateForTest() const { return filterState_; }
    uint32_t sampleIndexForTest() const { return sampleIndex_; }
    uint32_t impulseSamplesForTest() const { return impulseSamples_; }
    uint32_t noiseSamplesForTest() const { return noiseSamples_; }
    bool activeForTest() const { return active_; }
#endif

private:
#if POCKETPAN_ATTACK_FASTPATH
    // Inlined so the PRNG sequence stays register-resident inside the voice
    // kernel.  It is the identical xorshift/convert sequence.
    float nextNoise() {
        rngState_ ^= (rngState_ << 13);
        rngState_ ^= (rngState_ >> 17);
        rngState_ ^= (rngState_ << 5);
        return (static_cast<float>(static_cast<int32_t>(rngState_)) / 2147483648.0f);
    }
#else
    float nextNoise();
#endif

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

    // Hoisted impulse normalisation (constant for a strike; the multiply
    // association strikeAmplitude_ * window * impulseNorm_ is unchanged).
    float impulseNorm_ = 0.0f;

    // Fast 32-bit xorshift PRNG (deterministic, zero allocation)
    uint32_t rngState_ = 123456789U;
    ExciterConfig config_{};

#if POCKETPAN_VELOCITY_LUT
    // M6.3.7 Phase G: config-dependent pow(energyVelocity,1.25) over the 128
    // MIDI velocities, built lazily from the identical expression in trigger().
    float strikePowLut_[kMidiVelocityCount]{};
    bool strikePowReady_ = false;
    bool velocityLutEnabled_ = true;
    void buildStrikePow();
public:
    // Host A/B only.  Firmware leaves this at the compiled default.
    void setVelocityLutEnabledForTest(bool enabled) { velocityLutEnabled_ = enabled; }
private:
#endif

#if POCKETPAN_ATTACK_FASTPATH
    // Half-sine window table, built once during init from the same expression
    // as the reference (window only; window * norm is never precomputed).
    static float windowTable_[kExciterMaxImpulseSamples + 1][kExciterMaxImpulseSamples];
    static bool windowTableReady_;
    static void buildWindowTable();
#endif
};

#if POCKETPAN_ATTACK_FASTPATH
inline float Exciter::processSample() {
    if (!active_) return 0.0f;

    float output = 0.0f;

    // 1. Shaped impulse component.  The segment below runs only for the first
    // impulseSamples_ samples; window is a table lookup, norm is hoisted.  The
    // multiplication order (strikeAmplitude_ * window) * norm is preserved.
    if (sampleIndex_ < impulseSamples_) {
        const float window = (impulseSamples_ >= 3u && impulseSamples_ <= kExciterMaxImpulseSamples)
            ? windowTable_[impulseSamples_][sampleIndex_]
            : std::sin((kExciterPi * (sampleIndex_ + 0.5f)) / static_cast<float>(impulseSamples_));
        output += strikeAmplitude_ * window * impulseNorm_;
    }

    // 2. Filtered noise burst component with decay.  The PRNG is inlined and
    // the envelope expression is unchanged (no iterative subtraction).
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
