#pragma once

#include "modal_mode.h"

namespace pocketpan::dsp {

struct ModalPreset {
    const char* name;
    uint8_t modeCount;
    ModalModeDefinition modes[kMaxModesPerVoice];
};

// Handpan (Pantam) acoustic model:
// Predominant modes:
// 1. Fundamental (f)
// 2. Fundamental split doublet (beating)
// 3. Octave (2f - longitudinal ding mode)
// 4. Compound 5th (3f - transverse ding mode)
// Coupled with high metal partials and boundary modes.
inline constexpr ModalPreset kPresetPan = {
    "PAN",
    8,
    {
        // ratio,   gain,  t60(s), detune
        { 1.0000f, 1.00f, 2.40f, 0.0000f }, // Mode 0: Fundamental (f)
        { 1.0000f, 0.38f, 2.10f, 0.0032f }, // Mode 1: Split doublet
        { 2.0000f, 0.70f, 1.80f, 0.0000f }, // Mode 2: Octave
        { 3.0000f, 0.48f, 1.30f, 0.0000f }, // Mode 3: Compound Fifth
        { 3.9800f, 0.22f, 0.85f, 0.0000f }, // Mode 4: Upper harmonic mode (~4f)
        { 5.2500f, 0.14f, 0.60f, 0.0000f }, // Mode 5: Higher boundary mode (~5.25f)
        { 6.6200f, 0.08f, 0.40f, 0.0000f }, // Mode 6: Metallic ring mode (~6.6f)
        { 8.1800f, 0.04f, 0.25f, 0.0000f }, // Mode 7: High metal shimmer mode (~8.2f)
        { 0.0000f, 0.00f, 0.00f, 0.0000f },
        { 0.0000f, 0.00f, 0.00f, 0.0000f }
    }
};

// MIDI pitch is the Bell prime reference: ratio 1.0 is the audible prime.
inline constexpr ModalPreset kPresetBell = {"BELL", 10, {
    {.5000f,.28f,5.5f,0.0f}, {1.0000f,1.00f,5.0f,0.0f},
    {1.0020f,.20f,4.6f,0.0f}, {1.2000f,.65f,3.8f,0.0f},
    {1.5000f,.22f,2.6f,0.0f}, {2.0000f,.85f,4.2f,0.0f},
    {2.0015f,.16f,3.9f,0.0f}, {3.0000f,.50f,2.8f,0.0f},
    {4.0000f,.32f,2.2f,0.0f}, {5.2000f,.15f,1.4f,0.0f},
}};

} // namespace pocketpan::dsp
