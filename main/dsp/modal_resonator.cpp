#include "modal_resonator.h"
#include "dsp_profile.h"
#include "pan_doublet.h"
#include <algorithm>
#include <cmath>

namespace pocketpan::dsp {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kLn001 = -6.907755278982137f; // ln(0.001) for T60 radius calculation
constexpr float kMinDenormal = 1.0e-15f;

#if POCKETPAN_MODAL_MICROKERNEL
// The expression and the state write ordering deliberately match the accepted
// D+E recurrence.  The only change in the microkernel is how fixed, fully
// active mode state is presented to the compiler.
template<bool Safety>
inline float processMicroMode(ModalModeState& m, float excitation,
                              uint32_t& internalSaturationCount) {
    float y = m.excitationGain * excitation + m.a1 * m.z1 + m.a2 * m.z2;

    if constexpr (Safety) {
        if (std::abs(y) > 2.0f) {
            ++internalSaturationCount;
            y = (y > 0.0f) ? (2.0f + 0.5f * std::tanh(y - 2.0f))
                           : (-2.0f + 0.5f * std::tanh(y + 2.0f));
        }
    }

    if (std::abs(y) < kMinDenormal) {
        y = 0.0f;
    }

    m.z2 = m.z1;
    m.z1 = y;
    return y;
}
#endif
}

void ModalResonatorBank::init(float sampleRate) {
    sampleRate_ = (sampleRate > 1000.0f) ? sampleRate : 48000.0f;
    internalSaturationCount_ = 0;
    reset();
    resetInternalSaturationCount();
    setPreset(kPresetPan);
}

void ModalResonatorBank::reset() {
    for (size_t i = 0; i < kMaxModesPerVoice; ++i) {
        modes_[i].z1 = 0.0f;
        modes_[i].z2 = 0.0f;
    }
}

void ModalResonatorBank::setConfig(const ResonatorConfig& config) {
    config_ = config;
#if POCKETPAN_MODAL_MICROKERNEL
    // Safety is part of the selected fixed kernel, so resolve it only when
    // configuration changes, never in the sample recurrence.
    refreshMicroKernel();
#endif
}

void ModalResonatorBank::setExcitationCoupling(const float* coupling, size_t count) {
    for (size_t i = 0; i < modeCount_; ++i) {
        excitationCoupling_[i] = (coupling && i < count) ? std::max(0.0f, coupling[i]) : 1.0f;
        // z1/z2 are intentionally untouched: a restrike changes only newly
        // injected energy, never an already ringing tail.
        modes_[i].excitationGain = modes_[i].modalAmplitude * excitationCoupling_[i];
    }
}

void ModalResonatorBank::setRegisterBehavior(float t60Scale, float splitBeatTargetHz, bool fixedHzSplit) {
    t60RegisterScale_ = std::clamp(t60Scale, 0.5f, 1.5f);
    splitBeatTargetHz_ = std::max(0.0f, splitBeatTargetHz);
    fixedHzSplit_ = fixedHzSplit;
}

void ModalResonatorBank::setPreset(const ModalPreset& preset, bool updateCoefficients) {
    modeCount_ = std::min(static_cast<size_t>(preset.modeCount), kMaxModesPerVoice);
    for (size_t i = 0; i < modeCount_; ++i) {
        presetModes_[i] = preset.modes[i];
    }
    if (updateCoefficients) updatePitchAndDamping(fundamentalFrequencyHz_, currentDamping_);
}

void ModalResonatorBank::updatePitchAndDamping(float fundamentalFrequencyHz, float damping) {
    fundamentalFrequencyHz_ = std::clamp(fundamentalFrequencyHz, 10.0f, 15000.0f);
    currentDamping_ = std::clamp(damping, 0.0f, 1.0f);

    const float nyquistCutoff = nyquistModeCutoff(sampleRate_);  // ~23.04 kHz @ 48kHz
    const float fadeStartHz   = nyquistModeFadeStart(sampleRate_);  // ~19.20 kHz @ 48kHz
    const float fadeRange     = nyquistCutoff - fadeStartHz;

    // Damping factor: scales T60 down smoothly as damping increases (choke / palm mute)
    // At damping = 0: 100% of nominal T60
    // At damping = 1: 5% of nominal T60 (fast physical decay)
    const float dampingScale = 1.0f - config_.dampingDepth * currentDamping_;

    // Bank normalization: energy-based scaling across defined modes
    // Prevents multi-mode summation from exploding headroom
    float sumGainSq = 0.0f;
    for (size_t i = 0; i < modeCount_; ++i) {
        sumGainSq += (presetModes_[i].gain * presetModes_[i].gain);
    }
    const float bankNorm = (sumGainSq > 0.001f) ? (1.0f / std::sqrt(sumGainSq)) : 1.0f;

    activeModeCount_ = 0;

    for (size_t i = 0; i < modeCount_; ++i) {
        const auto& def = presetModes_[i];

        // Mode frequency with detune splitting (detune applied strictly once)
        float detune = def.detune;
        if (i == 1 && def.ratio == 1.0f && def.detune != 0.0f) {
            detune = computePanDoubletDetune(fundamentalFrequencyHz_,
                fixedHzSplit_ ? PanDoubletMode::FixedHz : PanDoubletMode::Relative,
                def.detune, splitBeatTargetHz_);
        }
        const float modeFreq = fundamentalFrequencyHz_ * def.ratio * (1.0f + detune);

        // Nyquist handling: do NOT clamp to 0.48*fs!
        // Disable or softly fade out modes exceeding audible / sampling boundaries
        if (!isModeActiveAtSampleRate(modeFreq, sampleRate_)) {
            modes_[i].active = false;
            modes_[i].modalAmplitude = 0.0f;
            modes_[i].excitationGain = 0.0f;
            modes_[i].a1 = 0.0f;
            modes_[i].a2 = 0.0f;
            continue;
        }

        modes_[i].active = true;
        activeModeCount_++;

        float modeGain = def.gain;
        if (modeFreq > fadeStartHz) {
            float fadeFactor = (nyquistCutoff - modeFreq) / fadeRange;
            modeGain *= std::clamp(fadeFactor, 0.0f, 1.0f);
        }

        // Calculate angular frequency w and pole radius r
        const float w = (kTwoPi * modeFreq) / sampleRate_;
        const float effectiveT60 = std::max(0.005f, def.t60 * dampingScale * t60RegisterScale_);

        // r = exp(ln(0.001) / (T60 * fs))
        float r = std::exp(kLn001 / (effectiveT60 * sampleRate_));
        if (r > 0.99999f) r = 0.99999f; // Ensure strict unconditional stability
        if (r < 0.00001f) r = 0.00001f;

        // Precalculated 2nd order IIR coefficients:
        // y[n] = gain * x[n] + a1 * y[n-1] + a2 * y[n-2]
        modes_[i].a1 = 2.0f * r * std::cos(w);
        modes_[i].a2 = -r * r;

        // Filter normalization:
        // Impulse response of 2-pole resonator: h[n] = (b0 / sin(w)) * r^n * sin(w*(n+1))
        // Setting b0 = sin(w) * modeGain * bankNorm normalizes the attack envelope peak to
        // exactly modeGain * bankNorm, INDEPENDENT of frequency w and INDEPENDENT of T60 decay!
        const float filterNorm = std::sin(w);
        modes_[i].modalAmplitude = modeGain * filterNorm * bankNorm * config_.masterGain;
        modes_[i].excitationGain = modes_[i].modalAmplitude * excitationCoupling_[i];
    }

#if POCKETPAN_MODAL_MICROKERNEL
    // A pitch/damping change can create a sparse Nyquist-pruned set.  Resolve
    // eligibility here so the sample path never reinterprets holes as a
    // shorter prefix.
    refreshMicroKernel();
#endif
}

void ModalResonatorBank::capturePreparedNote(PreparedNote& note) const {
    note.modeCount = static_cast<uint8_t>(modeCount_);
    note.fundamentalFrequencyHz = fundamentalFrequencyHz_;
    note.activeMask = 0;
    for (size_t i = 0; i < modeCount_; ++i) {
        const auto& mode = modes_[i];
        if (mode.active) note.activeMask |= static_cast<uint16_t>(1u << i);
        note.a1[i] = mode.a1;
        note.a2[i] = mode.a2;
        note.modalAmplitude[i] = mode.modalAmplitude;
    }
}

bool ModalResonatorBank::applyPreparedNote(const PreparedNote& note) {
    if (note.modeCount != modeCount_) return false;

    fundamentalFrequencyHz_ = std::clamp(note.fundamentalFrequencyHz, 10.0f, 15000.0f);
    currentDamping_ = 0.0f;
    activeModeCount_ = 0;

    for (size_t i = 0; i < modeCount_; ++i) {
        auto& mode = modes_[i];
        const bool active = (note.activeMask & static_cast<uint16_t>(1u << i)) != 0;
        mode.active = active;
        mode.modalAmplitude = active ? note.modalAmplitude[i] : 0.0f;
        mode.excitationGain = 0.0f; // restored by the dynamic coupling step.
        mode.a1 = active ? note.a1[i] : 0.0f;
        mode.a2 = active ? note.a2[i] : 0.0f;
        if (active) ++activeModeCount_;
    }

#if POCKETPAN_MODAL_MICROKERNEL
    refreshMicroKernel();
#endif
    return true;
}

#if POCKETPAN_FIXED_MODAL_KERNEL
template<size_t N>
DSP_HOT float ModalResonatorBank::processSampleFixed(float excitation) {
    float outSample = 0.0f;

    #pragma GCC unroll 10
    for (size_t i = 0; i < N; ++i) {

        auto& m = modes_[i];
        // 2nd-order direct form IIR resonant filter
        float y = m.excitationGain * excitation + m.a1 * m.z1 + m.a2 * m.z2;

        // Physical displacement compression on large amplitudes (prevents runaway on rapid strikes)
        if (config_.internalSafetySaturation && std::abs(y) > 2.0f) {
            ++internalSaturationCount_;
            y = (y > 0.0f) ? (2.0f + 0.5f * std::tanh(y - 2.0f)) : (-2.0f + 0.5f * std::tanh(y + 2.0f));
        }

        // Denormal flushing
        if (std::abs(y) < kMinDenormal) {
            y = 0.0f;
        }

        m.z2 = m.z1;
        m.z1 = y;
        outSample += y;
    }

    return outSample;
}

#endif

#if POCKETPAN_MODAL_MICROKERNEL
void ModalResonatorBank::refreshMicroKernel() {
    microKernel_ = MicroKernel::Generic;
    if (activeModeCount_ != modeCount_) return;

#if POCKETPAN_PROCESS6_MICROKERNEL
    if (modeCount_ == 6) {
        microKernel_ = config_.internalSafetySaturation
            ? MicroKernel::Process6Safety : MicroKernel::Process6Normal;
    } else
#endif
    if (modeCount_ == 8) {
        microKernel_ = config_.internalSafetySaturation
            ? MicroKernel::Process8Safety : MicroKernel::Process8Normal;
    } else if (modeCount_ == 10) {
        microKernel_ = config_.internalSafetySaturation
            ? MicroKernel::Process10Safety : MicroKernel::Process10Normal;
    }
}

#if POCKETPAN_PROCESS6_MICROKERNEL
template<bool Safety>
__attribute__((always_inline)) DSP_HOT float ModalResonatorBank::processSampleMicro6(float excitation) {
    ModalModeState* __restrict const hot = modes_;
    float outSample = 0.0f;
    outSample += processMicroMode<Safety>(hot[0], excitation, internalSaturationCount_);
    outSample += processMicroMode<Safety>(hot[1], excitation, internalSaturationCount_);
    outSample += processMicroMode<Safety>(hot[2], excitation, internalSaturationCount_);
    outSample += processMicroMode<Safety>(hot[3], excitation, internalSaturationCount_);
    outSample += processMicroMode<Safety>(hot[4], excitation, internalSaturationCount_);
    outSample += processMicroMode<Safety>(hot[5], excitation, internalSaturationCount_);
    return outSample;
}
#endif

template<bool Safety>
__attribute__((always_inline)) DSP_HOT float ModalResonatorBank::processSampleMicro8(float excitation) {
    ModalModeState* __restrict const hot = modes_;
    float outSample = 0.0f;
    outSample += processMicroMode<Safety>(hot[0], excitation, internalSaturationCount_);
    outSample += processMicroMode<Safety>(hot[1], excitation, internalSaturationCount_);
    outSample += processMicroMode<Safety>(hot[2], excitation, internalSaturationCount_);
    outSample += processMicroMode<Safety>(hot[3], excitation, internalSaturationCount_);
    outSample += processMicroMode<Safety>(hot[4], excitation, internalSaturationCount_);
    outSample += processMicroMode<Safety>(hot[5], excitation, internalSaturationCount_);
    outSample += processMicroMode<Safety>(hot[6], excitation, internalSaturationCount_);
    outSample += processMicroMode<Safety>(hot[7], excitation, internalSaturationCount_);
    return outSample;
}

template<bool Safety>
__attribute__((always_inline)) DSP_HOT float ModalResonatorBank::processSampleMicro10(float excitation) {
    ModalModeState* __restrict const hot = modes_;
    float outSample = 0.0f;
    outSample += processMicroMode<Safety>(hot[0], excitation, internalSaturationCount_);
    outSample += processMicroMode<Safety>(hot[1], excitation, internalSaturationCount_);
    outSample += processMicroMode<Safety>(hot[2], excitation, internalSaturationCount_);
    outSample += processMicroMode<Safety>(hot[3], excitation, internalSaturationCount_);
    outSample += processMicroMode<Safety>(hot[4], excitation, internalSaturationCount_);
    outSample += processMicroMode<Safety>(hot[5], excitation, internalSaturationCount_);
    outSample += processMicroMode<Safety>(hot[6], excitation, internalSaturationCount_);
    outSample += processMicroMode<Safety>(hot[7], excitation, internalSaturationCount_);
    outSample += processMicroMode<Safety>(hot[8], excitation, internalSaturationCount_);
    outSample += processMicroMode<Safety>(hot[9], excitation, internalSaturationCount_);
    return outSample;
}
#endif

DSP_HOT float ModalResonatorBank::processSample(float excitation) {
    DSP_PROFILE_SCOPE(Modal);
#if POCKETPAN_MODAL_MICROKERNEL
    // The mode/safety tag is resolved by coefficient/configuration updates,
    // outside this recurrence.  Sparse and Nyquist-pruned banks are always
    // routed to the original indexed fallback below.
    switch (microKernel_) {
#if POCKETPAN_PROCESS6_MICROKERNEL
        case MicroKernel::Process6Safety: return processSampleMicro6<true>(excitation);
        case MicroKernel::Process6Normal: return processSampleMicro6<false>(excitation);
#endif
        case MicroKernel::Process8Safety: return processSampleMicro8<true>(excitation);
        case MicroKernel::Process8Normal: return processSampleMicro8<false>(excitation);
        case MicroKernel::Process10Safety: return processSampleMicro10<true>(excitation);
        case MicroKernel::Process10Normal: return processSampleMicro10<false>(excitation);
        case MicroKernel::Generic: break;
    }
#elif POCKETPAN_FIXED_MODAL_KERNEL
    // Only a completely active bank can use the branch-free fixed kernel.
    // Arbitrary presets, sparse active sets and Nyquist pruning fall back.
    if (activeModeCount_ == modeCount_) {
        if (modeCount_ == 6) return processSampleFixed<6>(excitation);
        if (modeCount_ == 8) return processSampleFixed<8>(excitation);
        if (modeCount_ == 10) return processSampleFixed<10>(excitation);
    }
#endif
    return processSampleReference(excitation);
}

float ModalResonatorBank::processSampleReference(float excitation) {
    float outSample = 0.0f;

    for (size_t i = 0; i < modeCount_; ++i) {
        if (!modes_[i].active) continue;

        auto& m = modes_[i];
        // 2nd-order direct form IIR resonant filter
        float y = m.excitationGain * excitation + m.a1 * m.z1 + m.a2 * m.z2;

        // Physical displacement compression on large amplitudes (prevents runaway on rapid strikes)
        if (config_.internalSafetySaturation && std::abs(y) > 2.0f) {
            ++internalSaturationCount_;
            y = (y > 0.0f) ? (2.0f + 0.5f * std::tanh(y - 2.0f)) : (-2.0f + 0.5f * std::tanh(y + 2.0f));
        }

        // Denormal flushing
        if (std::abs(y) < kMinDenormal) {
            y = 0.0f;
        }

        m.z2 = m.z1;
        m.z1 = y;
        outSample += y;
    }

    return outSample;
}

void ModalResonatorBank::processBlock(const float* inExcitation, float* outSignal, size_t frames) {
    for (size_t n = 0; n < frames; ++n) {
        outSignal[n] = processSample(inExcitation[n]);
    }
}

float ModalResonatorBank::getEnergy() const {
    float energy = 0.0f;
    for (size_t i = 0; i < modeCount_; ++i) {
        if (modes_[i].active) {
            energy += (modes_[i].z1 * modes_[i].z1) + (modes_[i].z2 * modes_[i].z2);
        }
    }
    return energy;
}

} // namespace pocketpan::dsp
