#pragma once

#include "dsp_config.h"
#include "modal_mode.h"
#include "body_resonator.h"
#include "sympathetic_config.h"
#include "instrument_model.h"

namespace pocketpan::dsp {

// Named home for the established PAN voicing; no sonic redesign is implied.
struct PanCalibration {
    float exciterGain = 0.80f;
    float noiseAmount = 1.0f;
    float brightnessMinHz = 700.0f;
    float brightnessMaxHz = 12000.0f;
    float velocityKnee = 0.85f;
    float velocityKneeSlope = 0.35f;
    float fundamentalGain = 1.00f;
    float splitGain = 0.38f;
    float octaveGain = 0.70f;
    float fifthGain = 0.48f;
    float masterModalGain = 1.0f;
    float dampingDepth = 0.95f;
};

inline constexpr PanCalibration kPanCalibration{};

// Musical controls for the PAN model. Values are interpolated continuously by
// fundamental frequency and strike velocity rather than scattered in DSP code.
using PanVoicingConfig = ModalVoicingConfig;
/*struct PanVoicingConfig {
    // Keep upper-mode interpolation active into hard strikes instead of
    // plateauing around v112; this makes v120–127 brighter by excitation,
    // not by forcing the output limiter.
    // M5D.1 provisional final: restores moderate upper-mode life while the
    // M5D velocity knee keeps v127 clear of M5C.2 limiter dependence.
    float upperModeHardVelocity = 0.94f;
    float t60LowRegisterScale = 1.10f;
    float t60HighRegisterScale = 0.90f;
    float splitBeatTargetHz = 1.00f;
    float registerLowHz = 146.83f;
    float registerHighHz = 440.00f;
    bool fixedHzSplit = true;
    float softModeCoupling[kMaxModesPerVoice] = {1.00f, 0.82f, 0.72f, 0.22f, 0.08f, 0.05f, 0.03f, 0.02f, 0.0f, 0.0f};
    float hardModeCoupling[kMaxModesPerVoice] = {1.00f, 0.94f, 0.88f, 0.78f, 0.70f, 0.62f, 0.54f, 0.46f, 0.0f, 0.0f};
};*/

inline constexpr PanVoicingConfig kPanVoicingConfig{};

struct PanBodyConfig {
    BodyConfig body;
    SympatheticConfig sympathetic;
    // A local exciter impulse has a much smaller sample peak than a completed
    // modal voice. Normalize only that source before it reaches the shared
    // body; outputGain remains a return-mix control.
    float strikeBusGain = 48.0f;
};
inline constexpr PanBodyConfig kPanBodyConfig{{
    {{110.0f,0.18f,1.00f},{205.0f,0.14f,0.82f},{390.0f,0.11f,0.65f},{730.0f,0.08f,0.48f},{1280.0f,0.05f,0.32f},{1980.0f,0.035f,0.22f}},
    6, 0.18f, 0.11f, 1800.0f, true
}, {true, 0.005f, 0.002f, 1500.0f, 0.03f}, 48.0f};

// PAN is a model selection, not a set of implicit DSP defaults. Future models
// provide their own preset plus these two generic DSP configurations.
inline constexpr ExciterConfig kPanExciterConfig{
    kPanCalibration.exciterGain,
    kPanCalibration.noiseAmount,
    kPanCalibration.brightnessMinHz,
    kPanCalibration.brightnessMaxHz,
    kPanCalibration.velocityKnee,
    kPanCalibration.velocityKneeSlope,
};

inline constexpr ResonatorConfig kPanResonatorConfig{
    kPanCalibration.masterModalGain,
    kPanCalibration.dampingDepth,
    true,
};
} // namespace pocketpan::dsp
