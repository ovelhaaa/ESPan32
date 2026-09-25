#pragma once

#include <cstdint>
#include <cstddef>

namespace pocketpan::dsp {

constexpr size_t kMaxModesPerVoice = 10;

// Immutable mode definition in a preset
struct ModalModeDefinition {
    float ratio;    // Frequency ratio relative to fundamental (e.g. 1.0, 2.0, 3.0)
    float gain;     // Relative mode amplitude (0.0 to 1.0)
    float t60;      // Decay time in seconds for this mode
    float detune;   // Detune fraction (e.g. 0.003 for mode splitting / metallic shimmer)
};

// Runtime state for an active resonator mode
struct ModalModeState {
    float ratio = 1.0f;
    // Static modal identity versus new-strike injection. Updating the latter
    // never rescales the energy already stored in z1/z2.
    float modalAmplitude = 1.0f;
    float excitationGain = 1.0f;
    float t60 = 1.0f;
    float detune = 0.0f;

    // Pre-calculated IIR coefficients (recalculated only on pitch/damping/preset update)
    // y[n] = gain * x[n] + a1 * y[n-1] + a2 * y[n-2]
    float a1 = 0.0f;
    float a2 = 0.0f;

    // Filter delay states
    float z1 = 0.0f;
    float z2 = 0.0f;

    bool active = true;
};

} // namespace pocketpan::dsp
