#include "modal_voice.h"
#include "dsp_profile.h"
#include "pan_calibration.h"
#include "trigger_precompute.h"
#include <algorithm>
#include <cmath>

namespace pocketpan::dsp {

namespace {
}

void ModalVoice::init(float sampleRate) {
    sampleRate_ = (sampleRate > 1000.0f) ? sampleRate : 48000.0f;
    exciter_.init(sampleRate_);
    resonators_.init(sampleRate_);
    // The default remains PAN until SynthEngine selects its static model config.
    exciter_.setConfig(exciterConfig_);
    resonators_.setConfig(resonatorConfig_);
    resonators_.setPreset(*modalPreset_);
    // M6.3.7 Phase G: cache the register-domain logarithms (configuration
    // constants) so registerPositionFor() does one log() instead of three.
    registerLogLo_ = std::log(voicingConfig_.registerLowHz);
    registerLogHi_ = std::log(voicingConfig_.registerHighHz);

    // Smoothing coefficient for damping filter (~20 ms time constant)
    const float tcSeconds = 0.020f;
    dampingSmoothCoeff_ = 1.0f - std::exp(-1.0f / (tcSeconds * sampleRate_));

    reset();
}

void ModalVoice::setInternalSafetySaturation(bool enabled) {
    ResonatorConfig config = resonatorConfig_;
    config.internalSafetySaturation = enabled;
    resonators_.setConfig(config);
}

void ModalVoice::setModelConfig(const InstrumentModelConfig& config) {
    tubeLagDegrees_ = config.tubePhaseLagDegrees;
    vibraphoneTriggerCache_ = config.id == InstrumentModel::Vibraphone;
    modalPreset_ = config.modalPreset;
    exciterConfig_ = config.exciter;
    resonatorConfig_ = config.resonator;
    voicingConfig_ = config.voicing;
    exciter_.setConfig(exciterConfig_);
    resonators_.setConfig(resonatorConfig_);
    // No voice is active here. Trigger installs PreparedNote coefficients or
    // computes the fallback before the first sample.
    resonators_.setPreset(*modalPreset_, false);
    resonators_.resetInternalSaturationCount();
    exciter_.resetModelState();
    registerLogLo_ = std::log(voicingConfig_.registerLowHz);
    registerLogHi_ = std::log(voicingConfig_.registerHighHz);
    reset();
}

void ModalVoice::setPanConfigsForTest(const ExciterConfig& exciter, const PanVoicingConfig& voicing) {
    exciterConfig_ = exciter;
    voicingConfig_ = voicing;
    exciter_.setConfig(exciterConfig_);
    registerLogLo_ = std::log(voicingConfig_.registerLowHz);
    registerLogHi_ = std::log(voicingConfig_.registerHighHz);
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
    tubeState_ = 0.0f;

    isStealing_ = false;
    stealGain_ = 1.0f;
    stealDecr_ = 0.0f;

    exciter_.reset();
    resonators_.reset();
}

void ModalVoice::trigger(uint8_t midiNote, float fundamentalFrequencyHz, float velocity,
                         const PreparedNote* prepared) {
    {
        DSP_PROFILE_SCOPE(TriggerOther);
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
    }
    // A reused voice can retain pressure state after it becomes inactive.  In
    // that case the historical dynamic update is authoritative.  The cache is
    // intentionally new-trigger-only and never changes a damped attack.
    const bool canUsePrepared = prepared && currentDamping_ == 0.0f &&
        targetDamping_ == 0.0f &&
        prepared->matches(midiNote_, fundamentalFrequencyHz_) &&
        prepared->modeCount == resonators_.getModeCount();
    configureStrike(velocity_, canUsePrepared ? prepared : nullptr);
    tubeState_ = 0.0f;
    tubeCoefficient_ = tubeLagDegrees_ > 0.0f
        ? (canUsePrepared ? prepared->tubePhaseCoefficient
                          : tubePhaseCoefficient(fundamentalFrequencyHz_, sampleRate_, tubeLagDegrees_))
        : 0.0f;
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

    // A restrike is physically accumulative and keeps the existing resonator
    // state.  It must always recalculate at the current damping state.
    configureStrike(velocity_, nullptr);
}

float ModalVoice::registerPosition() const {
    return registerPositionFor(fundamentalFrequencyHz_);
}

float ModalVoice::registerPositionFor(float fundamentalFrequencyHz) const {
    // The register bounds are configuration constants; their logs are cached at
    // the configuration boundary (bit-identical to recomputing them here).
    const float lo = registerLogLo_;
    const float hi = registerLogHi_;
    return std::clamp((std::log(std::max(fundamentalFrequencyHz, 1.0f)) - lo) / (hi - lo), 0.0f, 1.0f);
}

bool ModalVoice::prepareNote(uint8_t midiNote, float fundamentalFrequencyHz,
                             PreparedNote& prepared) const {
    PreparedNote result{};
    result.midiNote = midiNote;

    const auto& v = voicingConfig_;
    const float reg = registerPositionFor(fundamentalFrequencyHz);
    result.tubeRegisterPosition = reg;
    const float t60Scale = v.t60LowRegisterScale +
        (v.t60HighRegisterScale - v.t60LowRegisterScale) * reg;

    // Reuse the exact normal coefficient routine rather than maintaining a
    // second exp/cos/sin implementation for the table builder.
    ModalResonatorBank scratch = resonators_;
    scratch.setRegisterBehavior(t60Scale, v.splitBeatTargetHz, v.fixedHzSplit);
    scratch.updatePitchAndDamping(fundamentalFrequencyHz, 0.0f);
    scratch.capturePreparedNote(result);
    result.tubePhaseCoefficient = tubeLagDegrees_ > 0.0f
        ? tubePhaseCoefficient(fundamentalFrequencyHz, sampleRate_, tubeLagDegrees_) : 0.0f;
    prepared = result;
    return result.modeCount == resonators_.getModeCount();
}

void ModalVoice::configureStrike(float velocity, const PreparedNote* prepared) {
    const auto& v = voicingConfig_;

    float reg;
    float gain;
    float t60Scale;
    float brightness;
    {
        DSP_PROFILE_SCOPE(TriggerRegister);
        reg = vibraphoneTriggerCache_ && prepared ? prepared->tubeRegisterPosition : registerPosition();
        gain = v.lowRegisterGain + (v.highRegisterGain - v.lowRegisterGain) * reg;
        t60Scale = v.t60LowRegisterScale + (v.t60HighRegisterScale - v.t60LowRegisterScale) * reg;
        brightness = v.lowRegisterBrightness + (v.highRegisterBrightness - v.lowRegisterBrightness) * reg;
    }

    float hardness;
    float blend;
    {
        DSP_PROFILE_SCOPE(TriggerOther);
#if POCKETPAN_VELOCITY_LUT
        // MIDI velocity is discrete, so the identical pow() result is read from
        // a shared table; any non-MIDI float keeps the historical expression.
        float powTerm;
        const int velIndex = exactMidiVelocityIndex(velocity);
        if (velIndex >= 0) {
            powTerm = velocityPow115Table()[velIndex];
        } else {
            powTerm = std::pow(std::clamp(velocity, 0.0f, 1.0f), 1.15f);
        }
        hardness = v.strikeHardnessMin + (v.strikeHardnessMax - v.strikeHardnessMin) * powTerm;
#else
        const int velIndex = vibraphoneTriggerCache_ ? exactMidiVelocityIndex(velocity) : -1;
        const float powTerm = velIndex >= 0 ? velocityPow115Table()[velIndex]
            : std::pow(std::clamp(velocity, 0.0f, 1.0f), 1.15f);
        hardness = v.strikeHardnessMin + (v.strikeHardnessMax - v.strikeHardnessMin) * powTerm;
#endif
        blend = std::clamp((velocity - v.upperModeSoftVelocity) /
            (v.upperModeHardVelocity - v.upperModeSoftVelocity), 0.0f, 1.0f);
    }

    float coupling[kMaxModesPerVoice];
    {
        DSP_PROFILE_SCOPE(TriggerCoupling);
        for (size_t i = 0; i < kMaxModesPerVoice; ++i) {
            coupling[i] = (v.softModeCoupling[i] + (v.hardModeCoupling[i] - v.softModeCoupling[i]) * blend) * gain;
        }
    }

    {
        DSP_PROFILE_SCOPE(TriggerCoefficients);
        resonators_.setRegisterBehavior(t60Scale, v.splitBeatTargetHz, v.fixedHzSplit);
        // Prepared entries are initial-damping coefficient snapshots only.
        // If a defensive validation ever rejects one, retain exact baseline
        // behavior rather than using a partially applied entry.
        if (!prepared || !resonators_.applyPreparedNote(*prepared)) {
            resonators_.updatePitchAndDamping(fundamentalFrequencyHz_, currentDamping_);
        }
    }
    {
        DSP_PROFILE_SCOPE(TriggerCoupling);
        resonators_.setExcitationCoupling(coupling, kMaxModesPerVoice);
    }
    {
        DSP_PROFILE_SCOPE(TriggerExciter);
        const float* strikePow = vibraphoneTriggerCache_ && exciterConfig_.velocityKnee == .85f &&
            exciterConfig_.velocityKneeSlope == .38f ? vibraphoneStrikePowTable() : nullptr;
        exciter_.trigger(velocity, hardness, brightness, strikePow);
    }
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

DSP_HOT float ModalVoice::processSample(float externalExcitation, float* strikeTap) {
    if (!active_) return 0.0f;

    age_++;

    { DSP_PROFILE_SCOPE(Damping);
    // 1. Damping smoothing filter (avoids zipper noise on aftertouch changes)
    if (std::abs(targetDamping_ - currentDamping_) > 0.0001f) {
        currentDamping_ += dampingSmoothCoeff_ * (targetDamping_ - currentDamping_);
        // Recalculate coefficients when damping has shifted appreciably (per-voice decimation)
        if ((++dampingUpdateCounter_ & 0x1F) == 0) {
            resonators_.updatePitchAndDamping(fundamentalFrequencyHz_, currentDamping_);
        }
    }

    }
    // 2. Generate excitation and filter through resonator bank
    float localStrike;
    { DSP_PROFILE_SCOPE(Exciter); localStrike = exciter_.processSample(); }
    if (strikeTap) *strikeTap = localStrike;
    const float exc = localStrike + externalExcitation;
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
        DSP_PROFILE_SCOPE(Energy);
        estimatedEnergy_ = resonators_.getEnergy();
        if (!exciter_.isActive() && estimatedEnergy_ < voicingConfig_.silenceThreshold) {
            active_ = false;
            estimatedEnergy_ = 0.0f;
        }
    }

    lastSample_ = output;
    return output;
}

// Fused bar/tube renderer: segment sustain at the existing lifetime checks.
// Tap z1 directly inside this hot loop; no extra recurrence or voice call.
DSP_HOT void ModalVoice::renderTubeBlock(float* bar, float* tube, size_t frames,
                                       const float* directGain, const float* phaseGain) {
    // The fused production path touches one output buffer per voice. Fan gains
    // are shared, prepared once per block; only mode 0 enters the one-state AP.
    auto accumulate = [&](size_t i, float dry, float fade) {
        const float direct = resonators_.fundamentalSample();
        const float shifted = tubeLagDegrees_ > 0.0f
            ? processTubePhase(direct, tubeCoefficient_, tubeState_) : direct;
        if (directGain) bar[i] += dry + fade * (direct * directGain[i] + shifted * phaseGain[i]);
        else { bar[i] += dry; tube[i] += shifted * fade; }
    };
    size_t done = 0;
    while (done < frames && active_) {
#if POCKETPAN_SUSTAIN_FASTPATH
        if (sustainFastPathEnabled_ && isSustainSafe()) {
            const size_t segment = std::min(size_t(128u - (age_ & 0x7fu)), frames - done);
            float last = lastSample_;
            for (size_t k=0; k<segment; ++k) {
                ++age_;
                last = resonators_.processSample(0.0f);
                accumulate(done+k, last, 1.0f);
            }
            lastSample_ = last; done += segment;
            if ((age_ & 0x7fu) == 0u) {
                estimatedEnergy_ = resonators_.getEnergy();
                if (estimatedEnergy_ < voicingConfig_.silenceThreshold) {
                    active_ = false; estimatedEnergy_ = 0.0f;
                }
            }
            continue;
        }
#endif
#if POCKETPAN_ATTACK_VOICE_FASTPATH
        if (attackFastPathEnabled_ && isAttackSafe()) {
            const size_t segment = std::min(size_t(exciter_.samplesUntilInactive()), frames - done);
            for (size_t k=0; k<segment; ++k) {
                const float dry = processSampleAttackStable();
                accumulate(done+k, dry, 1.0f);
            }
            if (segment) { done += segment; continue; }
        }
#endif
        const float fade = tubeFadeGain();
        const float dry = processSample();
        accumulate(done, dry, fade);
        ++done;
    }
}

void ModalVoice::processBlock(float* outBuffer, size_t frames) {
    for (size_t i = 0; i < frames; ++i) {
        outBuffer[i] = processSample();
    }
}

#if POCKETPAN_ATTACK_VOICE_FASTPATH
bool ModalVoice::isAttackSafe() const {
    // Same epsilon as the historical damping update branch.  While the smoothed
    // damping is within it, that branch is not taken, so currentDamping_ cannot
    // move and targetDamping_ cannot change inside a render block.  The exciter
    // must be running at the block boundary to select the attack specialization;
    // the path stays exact after it finishes because it shares the exciter's
    // exact active transition.
    return active_ && exciter_.isActive() && !isStealing_ &&
           std::abs(targetDamping_ - currentDamping_) <= 0.0001f;
}

DSP_HOT float ModalVoice::processSampleAttackStable(float externalExcitation, float* strikeTap) {
    if (!active_) return 0.0f;

    // Mirror of processSample() with the provably no-op damping-smoothing and
    // steal-crossfade branches removed.  The exciter, modal kernel, exact
    // age-aligned lifetime check and lastSample_ are unchanged.
    age_++;

    float localStrike;
    { DSP_PROFILE_SCOPE(Exciter); localStrike = exciter_.processSample(); }
    if (strikeTap) *strikeTap = localStrike;
    const float exc = localStrike + externalExcitation;
    const float output = resonators_.processSample(exc);

    if ((age_ & 0x7F) == 0) {
        estimatedEnergy_ = resonators_.getEnergy();
        if (!exciter_.isActive() && estimatedEnergy_ < voicingConfig_.silenceThreshold) {
            active_ = false;
            estimatedEnergy_ = 0.0f;
        }
    }

    lastSample_ = output;
    return output;
}

DSP_HOT void ModalVoice::renderAttackBlock(float* outBuffer, size_t frames,
                                           float externalExcitation) {
#if POCKETPAN_ATTACK_SEGMENT
    // M6.3.6 (§22): run the exact attack kernel only until the exciter reaches
    // its inactive transition, then the sustain kernel.  After the transition
    // localStrike is exactly 0 and the attack kernel's energy guard
    // (!exciter_.isActive()) is already true, so the two kernels agree
    // bit-for-bit at and after that sample.  Voice state, age, modal state and
    // the energy-check cadence are unchanged.
    size_t done = 0;
    while (done < frames) {
        if (!exciter_.isActive()) {
            for (; done < frames; ++done) outBuffer[done] += processSampleSustain(externalExcitation);
            break;
        }
        const uint32_t remaining = exciter_.samplesUntilInactive();
        if (remaining == 0) { // defensive: treated as the inactive segment
            for (; done < frames; ++done) outBuffer[done] += processSampleSustain(externalExcitation);
            break;
        }
        const size_t segment = remaining < (frames - done) ? remaining : (frames - done);
        for (size_t k = 0; k < segment; ++k) outBuffer[done + k] += processSampleAttackStable(externalExcitation);
        done += segment;
    }
#else
    for (size_t i = 0; i < frames; ++i) {
        outBuffer[i] += processSampleAttackStable(externalExcitation);
    }
#endif
}
#endif

#if POCKETPAN_SUSTAIN_FASTPATH
bool ModalVoice::isSustainSafe() const {
    // The exact epsilon used by the historical damping update branch: while the
    // smoothed damping is within it, that branch is not taken, so currentDamping_
    // cannot move and targetDamping_ cannot change inside a render block.
    return active_ && !exciter_.isActive() && !isStealing_ &&
           std::abs(targetDamping_ - currentDamping_) <= 0.0001f;
}

DSP_HOT float ModalVoice::processSampleSustain(float externalExcitation, float* strikeTap) {
    if (!active_) return 0.0f;

    // Mirror of processSample() with the provably no-op sustain branches
    // (damping smooth, exciter render, steal crossfade) removed.  The modal
    // kernel, age bookkeeping, lifetime check and lastSample_ are unchanged.
    age_++;
    if (strikeTap) *strikeTap = 0.0f; // exciter inactive: local strike is exactly 0
    const float output = resonators_.processSample(externalExcitation);

    if ((age_ & 0x7F) == 0) {
        estimatedEnergy_ = resonators_.getEnergy();
        if (estimatedEnergy_ < voicingConfig_.silenceThreshold) {
            active_ = false;
            estimatedEnergy_ = 0.0f;
        }
    }

    lastSample_ = output;
    return output;
}

DSP_HOT void ModalVoice::renderSustainBlock(float* outBuffer, size_t frames,
                                            float externalExcitation) {
    // Segment the block at the exact age-aligned lifetime check so no per-sample
    // branch is needed.  The deactivation sample and lastSample_ match the
    // general path bit-for-bit.
    size_t done = 0;
    while (done < frames) {
        const size_t until = 128u - (age_ & 0x7Fu);
        const size_t segment = std::min(until, frames - done);
        float last = lastSample_;
        for (size_t k = 0; k < segment; ++k) {
            ++age_;
            last = resonators_.processSample(externalExcitation);
            outBuffer[done + k] += last;
        }
        lastSample_ = last;
        done += segment;
        if ((age_ & 0x7Fu) == 0u) {
            estimatedEnergy_ = resonators_.getEnergy();
            if (estimatedEnergy_ < voicingConfig_.silenceThreshold) {
                active_ = false;
                estimatedEnergy_ = 0.0f;
                return;
            }
        }
    }
}
#endif

} // namespace pocketpan::dsp
