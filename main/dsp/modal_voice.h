#pragma once

#include <cstdint>
#include <cstddef>
#include "exciter.h"
#include "modal_resonator.h"
#include "pan_calibration.h"
#include "instrument_model.h"

namespace pocketpan::dsp {

class ModalVoice {
public:
    ModalVoice() = default;

    void init(float sampleRate);
    void reset();

    // Trigger strike with MIDI note and velocity [0.0, 1.0] (resets filter states for clean attack)
    void trigger(uint8_t midiNote, float fundamentalFrequencyHz, float velocity,
                 const PreparedNote* prepared = nullptr);

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
    // strikeTap, when supplied, receives only the local exciter signal before
    // it enters the modal bank.  External sympathetic feedback is deliberately
    // excluded: it is not a physical strike on the shared shell.
    float processSample(float externalExcitation = 0.0f, float* strikeTap = nullptr);

    // Process an entire block
    void processBlock(float* outBuffer, size_t frames);

    // M6.3.3 stable-sustain fast path.  A voice is sustain-safe when it is
    // active, the exciter has finished, no damping transition is in flight and
    // no steal crossfade is running.  In that state the historical per-sample
    // damping, exciter and steal checks are provably no-ops, so the sample path
    // only advances age, runs the same modal kernel and performs the exact
    // lifetime/energy check.  The decision is taken once per voice per block.
    bool isSustainSafe() const;
    float processSampleSustain(float externalExcitation = 0.0f, float* strikeTap = nullptr);
    void renderSustainBlock(float* outBuffer, size_t frames, float externalExcitation = 0.0f);

#if POCKETPAN_ATTACK_VOICE_FASTPATH
    // M6.3.5 Phase B attack-voice fast path.  A voice is attack-safe when it is
    // active, its exciter is still running, no steal crossfade is in flight and
    // the smoothed damping has already settled.  Under those conditions the
    // historical damping and steal branches are provably no-ops for the whole
    // block, so the sample path can be specialized without any musical change.
    bool isAttackSafe() const;
    float processSampleAttackStable(float externalExcitation = 0.0f, float* strikeTap = nullptr);
    void renderAttackBlock(float* outBuffer, size_t frames, float externalExcitation = 0.0f);
#if POCKETPAN_ATTACK_SEGMENT
    // M6.3.6 (§22): exact sample at which the exciter body ends, so the attack
    // block can switch to the sustain kernel immediately.  It is the same
    // transition the per-sample path performs.
    uint32_t samplesUntilExciterInactive() const { return exciter_.samplesUntilInactive(); }
#endif
#endif

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
    void setModelConfig(const InstrumentModelConfig& config);
    // Host qualification only. Defaults remain the frozen PAN calibration.
    void setPanConfigsForTest(const ExciterConfig& exciter, const PanVoicingConfig& voicing);

    // Used only while prebuilding the fixed NoteOn tables before audio starts.
    // It delegates all modal math to the established bank update routine.
    bool prepareNote(uint8_t midiNote, float fundamentalFrequencyHz,
                     PreparedNote& prepared) const;

#ifdef POCKETPAN_TRIGGER_DIFFERENTIAL_TEST
    // Host-only probe for the M6.3.7 register-precompute differential test.
    float registerPositionForTest(float fundamentalFrequencyHz) const {
        return registerPositionFor(fundamentalFrequencyHz);
    }
#endif

private:
    void configureStrike(float velocity, const PreparedNote* prepared = nullptr);
    float registerPosition() const;
    float registerPositionFor(float fundamentalFrequencyHz) const;

    float sampleRate_ = 48000.0f;
    // Cached model-voicing logarithms for registerPositionFor().  They are the
    // log() of constant configuration values, so caching them is bit-identical
    // to recomputing them on every trigger.
    float registerLogLo_ = 0.0f;
    float registerLogHi_ = 0.0f;
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
    ExciterConfig exciterConfig_ = kPanExciterConfig;
    ResonatorConfig resonatorConfig_ = kPanResonatorConfig;
    ModalVoicingConfig voicingConfig_ = kPanVoicingConfig;
    const ModalPreset* modalPreset_ = &kPresetPan;
#if POCKETPAN_SUSTAIN_FASTPATH
    // Host A/B only; firmware leaves this at the compiled default.
    bool sustainFastPathEnabled_ = true;
public:
    void setSustainFastPathEnabledForTest(bool enabled) { sustainFastPathEnabled_ = enabled; }
    bool isSustainFastPathEnabledForTest() const { return sustainFastPathEnabled_; }
private:
#endif
#if POCKETPAN_ATTACK_VOICE_FASTPATH
    // Host A/B only; firmware leaves this at the compiled default.
    bool attackFastPathEnabled_ = true;
public:
    void setAttackFastPathEnabledForTest(bool enabled) { attackFastPathEnabled_ = enabled; }
    bool isAttackFastPathEnabledForTest() const { return attackFastPathEnabled_; }
private:
#endif
};

} // namespace pocketpan::dsp
