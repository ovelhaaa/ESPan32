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

// Steel tongue drum (tank drum) acoustic model:
// 6 modes:
// Mode 0: Fundamental (1.0000) - dominant pitch and sustained body
// Mode 1: Octave overtone (2.0000) - tuned octave slit mode
// Mode 2: Compound fifth overtone (2.9850) - transverse tongue overtone
// Mode 3: Metallic partial (4.0600) - higher slit resonance
// Mode 4: Upper colour mode (5.3800) - boundary rim overtone
// Mode 5: Short high partial (6.7200) - transient steel attack colour
inline constexpr ModalPreset kPresetTongue = {
    "TONGUE",
    6,
    {
        // ratio,   gain,  t60(s), detune
        { 1.0000f, 1.00f, 3.80f, 0.0000f }, // Mode 0: Fundamental
        { 2.0000f, 0.48f, 2.40f, 0.0000f }, // Mode 1: Octave
        { 2.9850f, 0.26f, 1.60f, 0.0000f }, // Mode 2: Compound Fifth
        { 4.0600f, 0.14f, 1.00f, 0.0000f }, // Mode 3: Metallic Partial
        { 5.3800f, 0.07f, 0.65f, 0.0000f }, // Mode 4: Upper Colour
        { 6.7200f, 0.03f, 0.40f, 0.0000f }, // Mode 5: High Partial
        { 0.0000f, 0.00f, 0.00f, 0.0000f },
        { 0.0000f, 0.00f, 0.00f, 0.0000f },
        { 0.0000f, 0.00f, 0.00f, 0.0000f },
        { 0.0000f, 0.00f, 0.00f, 0.0000f }
    }
};

// Singing Bowl (Tibetan Bowl) acoustic model:
// 7 modes:
// Mode 0: Fundamental / Prime (1.0000) - deep stable pitch center and long sustain
// Mode 1: Prime split doublet (1.0000) - slow acoustic beating doublet (~0.7 Hz)
// Mode 2: Low inharmonic partial (2.3200) - warm lower metallic resonance
// Mode 3: Mid partial (2.9600) - singing body resonance
// Mode 4: Metallic partial (4.1500) - overtone shimmer
// Mode 5: Upper partial (5.4500) - gentle high metallic colour
// Mode 6: Upper colour shimmer (6.8000) - transient attack sheen
inline constexpr ModalPreset kPresetBowl = {
    "BOWL",
    7,
    {
        // ratio,   gain,  t60(s), detune
        { 1.0000f, 1.00f, 6.00f, 0.0000f }, // Mode 0: Prime fundamental
        { 1.0000f, 0.48f, 5.20f, 0.0032f }, // Mode 1: Prime doublet (beating)
        { 2.3200f, 0.38f, 4.20f, 0.0000f }, // Mode 2: Low inharmonic partial
        { 2.9600f, 0.26f, 3.20f, 0.0000f }, // Mode 3: Mid partial
        { 4.1500f, 0.16f, 2.20f, 0.0000f }, // Mode 4: Metallic partial
        { 5.4500f, 0.08f, 1.40f, 0.0000f }, // Mode 5: Upper partial
        { 6.8000f, 0.04f, 0.80f, 0.0000f }, // Mode 6: High shimmer
        { 0.0000f, 0.00f, 0.00f, 0.0000f },
        { 0.0000f, 0.00f, 0.00f, 0.0000f },
        { 0.0000f, 0.00f, 0.00f, 0.0000f }
    }
};

// African thumb piano / Kalimba acoustic model:
// 5 modes:
// Mode 0: Fundamental tine pitch (1.0000) - dominant pitch and sustained tine body
// Mode 1: First tine bending overtone (2.7000) - characteristic inharmonic tine mode
// Mode 2: Second bending region (5.4000) - mid metallic tine partial
// Mode 3: High metallic click/colour (8.9000) - fast transient chime
// Mode 4: Upper tine partial (13.0000) - short metallic attack sheen
inline constexpr ModalPreset kPresetKalimba = {
    "KALIMBA",
    5,
    {
        // ratio,   gain,  t60(s), detune
        { 1.0000f, 1.00f, 2.20f, 0.0000f }, // Mode 0: Fundamental
        { 2.7000f, 0.32f, 1.10f, 0.0000f }, // Mode 1: First bending overtone
        { 5.4000f, 0.15f, 0.55f, 0.0000f }, // Mode 2: Second bending overtone
        { 8.9000f, 0.06f, 0.25f, 0.0000f }, // Mode 3: High metallic click
        { 13.0000f, 0.02f, 0.12f, 0.0000f }, // Mode 4: Upper tine partial
        { 0.0000f, 0.00f, 0.00f, 0.0000f },
        { 0.0000f, 0.00f, 0.00f, 0.0000f },
        { 0.0000f, 0.00f, 0.00f, 0.0000f },
        { 0.0000f, 0.00f, 0.00f, 0.0000f },
        { 0.0000f, 0.00f, 0.00f, 0.0000f }
    }
};

// Glass / Crystal percussion acoustic model:
// 6 modes:
// Mode 0: Fundamental (1.0000) - clean transparent pitch center
// Mode 1: First glass partial (2.3200) - characteristic transverse glass mode
// Mode 2: Bright partial (3.8500) - clear crystalline resonance
// Mode 3: High partial (5.5500) - high glass chime overtone
// Mode 4: Crystalline colour (7.7500) - fragile upper sheen
// Mode 5: Short shimmer (10.4000) - transient crystalline air
inline constexpr ModalPreset kPresetGlass = {
    "GLASS",
    6,
    {
        // ratio,   gain,  t60(s), detune
        { 1.0000f, 1.00f, 4.80f, 0.0000f }, // Mode 0: Fundamental
        { 2.3200f, 0.42f, 3.80f, 0.0000f }, // Mode 1: First glass partial
        { 3.8500f, 0.30f, 2.80f, 0.0000f }, // Mode 2: Bright partial
        { 5.5500f, 0.18f, 2.00f, 0.0000f }, // Mode 3: High partial
        { 7.7500f, 0.09f, 1.20f, 0.0000f }, // Mode 4: Crystalline colour
        { 10.4000f, 0.04f, 0.70f, 0.0000f }, // Mode 5: Short shimmer
        { 0.0000f, 0.00f, 0.00f, 0.0000f },
        { 0.0000f, 0.00f, 0.00f, 0.0000f },
        { 0.0000f, 0.00f, 0.00f, 0.0000f },
        { 0.0000f, 0.00f, 0.00f, 0.0000f }
    }
};

} // namespace pocketpan::dsp
