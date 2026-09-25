#pragma once

#include <cstdint>
#include <cstddef>
#include "exciter.h"
#include "modal_resonator.h"

namespace pocketpan::dsp {

class ModalVoice {
public:
    ModalVoice() = default;

    void init(float sampleRate);
    void reset();

    // Trigger strike with MIDI note and velocity [0.0, 1.0] (resets filter states for clean attack)
    void trigger(uint8_t midiNote, float fundamentalFrequencyHz, float velocity);

    // Retrigger same physical note: adds energy to existing vibration without zeroing resonator states
    void restrike(float velocity);

    // Note Off gesture release (does NOT silence natural metal ring-down)
    void release();

    // Immediate silence / choke (used for hard reset)
    void kill();

    // Set damping / aftertouch choke [0.0 = open, 1.0 = heavy choke]
    void setDamping(float damping);

    // Prepare voice for stealing with quick crossfade
    void prepareSteal(float fadeDurationMs = 2.5f);

    // Process a single audio sample
    float processSample(float externalExcitation = 0.0f);

    // Process an entire block
    void processBlock(float* outBuffer, size_t frames);

    // Query state for voice allocator
    bool isActive() const { return active_; }
    bool isReleased() const { return released_; }
    uint8_t getMidiNote() const { return midiNote_; }
    float getFundamentalFrequencyHz() const { return fundamentalFrequencyHz_; }
    float getVelocity() const { return velocity_; }
    uint32_t getAge() const { return age_; }
    float getEstimatedEnergy() const { return estimatedEnergy_; }
    float getLastSample() const { return lastSample_; }
    uint32_t getInternalSaturationCount() const { return resonators_.getInternalSaturationCount(); }
    void setInternalSafetySaturation(bool enabled);

private:
    void configureStrike(float velocity);
    float registerPosition() const;

    float sampleRate_ = 48000.0f;
    bool active_ = false;
    bool released_ = false;

    uint8_t midiNote_ = 60;
    float fundamentalFrequencyHz_ = 261.63f; // Explicitly named per specification
    float velocity_ = 0.0f;
    uint32_t age_ = 0;
    float estimatedEnergy_ = 0.0f;
    float lastSample_ = 0.0f;

    // Smooth damping (avoids zipper noise)
    float targetDamping_ = 0.0f;
    float currentDamping_ = 0.0f;
    float dampingSmoothCoeff_ = 0.01f;
    uint32_t dampingUpdateCounter_ = 0; // Per-voice decimation counter (avoids static state sharing)

    // Voice stealing crossfade envelope
    bool isStealing_ = false;
    float stealGain_ = 1.0f;
    float stealDecr_ = 0.0f;

    Exciter exciter_;
    ModalResonatorBank resonators_;
};

} // namespace pocketpan::dsp
