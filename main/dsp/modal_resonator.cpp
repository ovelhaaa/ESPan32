#include "modal_resonator.h"
#include "pan_doublet.h"
#include <algorithm>
#include <cmath>

namespace pocketpan::dsp {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kLn001 = -6.907755278982137f; // ln(0.001) for T60 radius calculation
constexpr float kMinDenormal = 1.0e-15f;
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

void ModalResonatorBank::setPreset(const ModalPreset& preset) {
    modeCount_ = std::min(static_cast<size_t>(preset.modeCount), kMaxModesPerVoice);
    for (size_t i = 0; i < modeCount_; ++i) {
        presetModes_[i] = preset.modes[i];
    }
    updatePitchAndDamping(fundamentalFrequencyHz_, currentDamping_);
}

void ModalResonatorBank::updatePitchAndDamping(float fundamentalFrequencyHz, float damping) {
    fundamentalFrequencyHz_ = std::clamp(fundamentalFrequencyHz, 10.0f, 15000.0f);
    currentDamping_ = std::clamp(damping, 0.0f, 1.0f);

    const float nyquistCutoff = 0.48f * sampleRate_;  // ~23.04 kHz @ 48kHz
    const float fadeStartHz   = 0.40f * sampleRate_;  // ~19.20 kHz @ 48kHz
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
        if (modeFreq >= nyquistCutoff || modeFreq <= 10.0f) {
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
}

float ModalResonatorBank::processSample(float excitation) {
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
