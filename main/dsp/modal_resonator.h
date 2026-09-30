#pragma once

#include <cmath>
#include <cstdint>
#include <cstddef>
#include "modal_mode.h"
#include "dsp_profile.h"
#include "modal_preset.h"
#include "dsp_config.h"
#include "prepared_note.h"

namespace pocketpan::dsp {

constexpr float nyquistModeCutoff(float sampleRate) { return 0.48f * sampleRate; }
constexpr float nyquistModeFadeStart(float sampleRate) { return 0.40f * sampleRate; }
inline bool isModeActiveAtSampleRate(float frequencyHz, float sampleRate) {
    return frequencyHz > 10.0f && frequencyHz < nyquistModeCutoff(sampleRate);
}

class ModalResonatorBank {
public:
    ModalResonatorBank() = default;

    void init(float sampleRate);
    void reset();

    void setPreset(const ModalPreset& preset, bool updateCoefficients = true);
    void setConfig(const ResonatorConfig& config);
    void updatePitchAndDamping(float fundamentalFrequencyHz, float damping);
    void setExcitationCoupling(const float* coupling, size_t count);
    void setRegisterBehavior(float t60Scale, float splitBeatTargetHz, bool fixedHzSplit);

    // Snapshot/apply only the deterministic, initial-undamped portion of a
    // NoteOn coefficient update.  State history and velocity coupling remain
    // outside this object and are never cached.
    void capturePreparedNote(PreparedNote& note) const;
    bool applyPreparedNote(const PreparedNote& note);

    // Process a single sample through the resonator bank
    float processSample(float excitation);

    // Exact scalar recurrence accepted in M6.3.1, independent of the selected
    // fast kernel.  It is the definition of correctness for the fast paths and
    // is compiled unconditionally so production and host tests share one code
    // path; only its public differential-test hook is gated.
    float processSampleReference(float excitation);

    // Process an entire block of samples in place or from input to output buffer
    void processBlock(const float* inExcitation, float* outSignal, size_t frames);

#ifdef POCKETPAN_MODAL_DIFFERENTIAL_TEST
    // Host-only kernel equivalence harness.  These never exist in firmware.
    struct ModeDebug {
        bool active = false;
        float excitationGain = 0.0f, a1 = 0.0f, a2 = 0.0f, z1 = 0.0f, z2 = 0.0f;
    };
    size_t modeCountForTest() const { return modeCount_; }
    ModeDebug modeDebugForTest(size_t i) const {
        ModeDebug d;
        if (i < kMaxModesPerVoice) {
            const auto& m = modes_[i];
            d.active = m.active; d.excitationGain = m.excitationGain;
            d.a1 = m.a1; d.a2 = m.a2; d.z1 = m.z1; d.z2 = m.z2;
        }
        return d;
    }
#endif

    // Current estimated instantaneous energy of the resonant modes
    float getEnergy() const;

    size_t getModeCount() const { return modeCount_; }
    size_t getActiveModeCount() const { return activeModeCount_; }
    uint32_t getInternalSaturationCount() const { return internalSaturationCount_; }
    void resetInternalSaturationCount() { internalSaturationCount_ = 0; }

private:
    float sampleRate_ = 48000.0f;
    float fundamentalFrequencyHz_ = 220.0f;
    float currentDamping_ = 0.0f;

    size_t modeCount_ = 0;
    size_t activeModeCount_ = 0;
#if POCKETPAN_FIXED_MODAL_KERNEL
    template<size_t N> float processSampleFixed(float excitation);
#endif
#if POCKETPAN_MODAL_MICROKERNEL
    enum class MicroKernel : uint8_t {
        Generic,
#if POCKETPAN_PROCESS6_MICROKERNEL
        Process6Safety,
        Process6Normal,
#endif
        Process8Safety,
        Process8Normal,
        Process10Safety,
        Process10Normal,
    };
    void refreshMicroKernel();
    // The fixed 8/10-mode kernels must live in the same IRAM-resident
    // processSample body; out-of-line weak copies were measured in flash and
    // are the source of the candidate-11 regression.
#if POCKETPAN_PROCESS6_MICROKERNEL
    template<bool Safety> DSP_HOT __attribute__((always_inline)) float processSampleMicro6(float excitation);
#endif
    template<bool Safety> DSP_HOT __attribute__((always_inline)) float processSampleMicro8(float excitation);
    template<bool Safety> DSP_HOT __attribute__((always_inline)) float processSampleMicro10(float excitation);
    MicroKernel microKernel_ = MicroKernel::Generic;
#endif
    ModalModeState modes_[kMaxModesPerVoice];
    ModalModeDefinition presetModes_[kMaxModesPerVoice];
    uint32_t internalSaturationCount_ = 0;
    float excitationCoupling_[kMaxModesPerVoice] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    float t60RegisterScale_ = 1.0f;
    float splitBeatTargetHz_ = 0.0f;
    bool fixedHzSplit_ = false;
    ResonatorConfig config_{};
};

} // namespace pocketpan::dsp
