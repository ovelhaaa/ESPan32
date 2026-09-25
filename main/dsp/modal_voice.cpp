#include "modal_voice.h"
#include "pan_calibration.h"
#include <algorithm>
#include <cmath>

namespace pocketpan::dsp {

namespace {
constexpr float kSilenceThreshold = 1.0e-7f;
}

void ModalVoice::init(float sampleRate) {
    sampleRate_ = (sampleRate > 1000.0f) ? sampleRate : 48000.0f;
    exciter_.init(sampleRate_);
    resonators_.init(sampleRate_);
    // Explicit PAN wiring. The exciter/resonator primitives remain model-neutral.
    exciter_.setConfig(kPanExciterConfig);
    resonators_.setConfig(kPanResonatorConfig);

    // Smoothing coefficient for damping filter (~20 ms time constant)
    const float tcSeconds = 0.020f;
    dampingSmoothCoeff_ = 1.0f - std::exp(-1.0f / (tcSeconds * sampleRate_));

    reset();
}

void ModalVoice::setInternalSafetySaturation(bool enabled) {
    ResonatorConfig config = kPanResonatorConfig;
    config.internalSafetySaturation = enabled;
    resonators_.setConfig(config);
}

void ModalVoice::reset() {
    active_ = false;
    released_ = true;
    midiNote_ = 0;
    fundamentalFrequencyHz_ = 220.0f;
    velocity_ = 0.0f;
    age_ = 0;
    estimatedEnergy_ = 0.0f;

    targetDamping_ = 0.0f;
    currentDamping_ = 0.0f;
    dampingUpdateCounter_ = 0;
    lastSample_ = 0.0f;

    isStealing_ = false;
    stealGain_ = 1.0f;
    stealDecr_ = 0.0f;

    exciter_.reset();
    resonators_.reset();
}

void ModalVoice::trigger(uint8_t midiNote, float fundamentalFrequencyHz, float velocity) {
    midiNote_ = midiNote;
    fundamentalFrequencyHz_ = fundamentalFrequencyHz;
    velocity_ = velocity;
    age_ = 0;
    released_ = false;
    active_ = true;
    // A newly allocated voice has not rendered this strike yet. Do not expose
    // the final sample from its previous lifetime to a subsequent steal.
    lastSample_ = 0.0f;

    isStealing_ = false;
    stealGain_ = 1.0f;

    // Reset filter states for clean attack
    resonators_.reset();
    configureStrike(velocity_);
}

void ModalVoice::restrike(float velocity) {
    // Physical restrike on the same vibrating metal note:
    // Retrigger exciter with new velocity WITHOUT zeroing modal resonator energy!
    velocity_ = velocity;
    age_ = 0;
    released_ = false;
    active_ = true;

    isStealing_ = false;
    stealGain_ = 1.0f;

    configureStrike(velocity_);
}

float ModalVoice::registerPosition() const {
    const auto& v = kPanVoicingConfig;
    const float lo = std::log(v.registerLowHz);
    const float hi = std::log(v.registerHighHz);
    return std::clamp((std::log(std::max(fundamentalFrequencyHz_, 1.0f)) - lo) / (hi - lo), 0.0f, 1.0f);
}

void ModalVoice::configureStrike(float velocity) {
    const auto& v = kPanVoicingConfig;
    const float reg = registerPosition();
    const float hardness = v.strikeHardnessMin + (v.strikeHardnessMax - v.strikeHardnessMin) *
        std::pow(std::clamp(velocity, 0.0f, 1.0f), 1.15f);
    const float blend = std::clamp((velocity - v.upperModeSoftVelocity) /
        (v.upperModeHardVelocity - v.upperModeSoftVelocity), 0.0f, 1.0f);
    const float gain = v.lowRegisterGain + (v.highRegisterGain - v.lowRegisterGain) * reg;
    float coupling[kMaxModesPerVoice];
    for (size_t i = 0; i < kMaxModesPerVoice; ++i) {
        coupling[i] = (v.softModeCoupling[i] + (v.hardModeCoupling[i] - v.softModeCoupling[i]) * blend) * gain;
    }
    const float t60Scale = v.t60LowRegisterScale + (v.t60HighRegisterScale - v.t60LowRegisterScale) * reg;
    const float brightness = v.lowRegisterBrightness + (v.highRegisterBrightness - v.lowRegisterBrightness) * reg;
    resonators_.setRegisterBehavior(t60Scale, v.splitBeatTargetHz, v.fixedHzSplit);
    resonators_.updatePitchAndDamping(fundamentalFrequencyHz_, currentDamping_);
    resonators_.setExcitationCoupling(coupling, kMaxModesPerVoice);
    exciter_.trigger(velocity, hardness, brightness);
}

void ModalVoice::release() {
    // Metal notes naturally sustain and ring out after release!
    released_ = true;
}

void ModalVoice::kill() {
    reset();
}

void ModalVoice::setDamping(float damping) {
    targetDamping_ = std::clamp(damping, 0.0f, 1.0f);
}

void ModalVoice::prepareSteal(float fadeDurationMs) {
    if (!active_) return;
    isStealing_ = true;
    const float fadeFrames = std::max(16.0f, (fadeDurationMs * 0.001f) * sampleRate_);
    stealDecr_ = 1.0f / fadeFrames;
}

float ModalVoice::processSample(float externalExcitation) {
    if (!active_) return 0.0f;

    age_++;

    // 1. Damping smoothing filter (avoids zipper noise on aftertouch changes)
    if (std::abs(targetDamping_ - currentDamping_) > 0.0001f) {
        currentDamping_ += dampingSmoothCoeff_ * (targetDamping_ - currentDamping_);
        // Recalculate coefficients when damping has shifted appreciably (per-voice decimation)
        if ((++dampingUpdateCounter_ & 0x1F) == 0) {
            resonators_.updatePitchAndDamping(fundamentalFrequencyHz_, currentDamping_);
        }
    }

    // 2. Generate excitation and filter through resonator bank
    const float exc = exciter_.processSample() + externalExcitation;
    float output = resonators_.processSample(exc);

    // 3. Handle voice stealing micro-fade
    if (isStealing_) {
        output *= stealGain_;
        stealGain_ -= stealDecr_;
        if (stealGain_ <= 0.0f) {
            kill();
            lastSample_ = 0.0f;
            return 0.0f;
        }
    }

    // 4. Energy estimation and automatic voice release
    if ((age_ & 0x7F) == 0) { // Check periodically
        estimatedEnergy_ = resonators_.getEnergy();
        if (!exciter_.isActive() && estimatedEnergy_ < kSilenceThreshold) {
            active_ = false;
            estimatedEnergy_ = 0.0f;
        }
    }

    lastSample_ = output;
    return output;
}

void ModalVoice::processBlock(float* outBuffer, size_t frames) {
    for (size_t i = 0; i < frames; ++i) {
        outBuffer[i] = processSample();
    }
}

} // namespace pocketpan::dsp
