#include "modal_voice.h"
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

    // Smoothing coefficient for damping filter (~20 ms time constant)
    const float tcSeconds = 0.020f;
    dampingSmoothCoeff_ = 1.0f - std::exp(-1.0f / (tcSeconds * sampleRate_));

    reset();
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

    isStealing_ = false;
    stealGain_ = 1.0f;

    // Reset filter states for clean attack
    resonators_.reset();
    resonators_.updatePitchAndDamping(fundamentalFrequencyHz_, currentDamping_);
    exciter_.trigger(velocity_);
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

float ModalVoice::processSample() {
    if (!active_) return 0.0f;

    age_++;

    // 1. Damping smoothing filter (avoids zipper noise on aftertouch changes)
    if (std::abs(targetDamping_ - currentDamping_) > 0.0001f) {
        currentDamping_ += dampingSmoothCoeff_ * (targetDamping_ - currentDamping_);
        // Recalculate coefficients when damping has shifted appreciably
        static uint32_t decimate = 0;
        if ((++decimate & 0x1F) == 0) {
            resonators_.updatePitchAndDamping(fundamentalFrequencyHz_, currentDamping_);
        }
    }

    // 2. Generate excitation and filter through resonator bank
    const float exc = exciter_.processSample();
    float output = resonators_.processSample(exc);

    // 3. Handle voice stealing micro-fade
    if (isStealing_) {
        output *= stealGain_;
        stealGain_ -= stealDecr_;
        if (stealGain_ <= 0.0f) {
            kill();
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

    return output;
}

void ModalVoice::processBlock(float* outBuffer, size_t frames) {
    for (size_t i = 0; i < frames; ++i) {
        outBuffer[i] = processSample();
    }
}

} // namespace pocketpan::dsp
