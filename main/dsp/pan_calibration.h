#pragma once

#include "dsp_config.h"
#include "modal_mode.h"

namespace pocketpan::dsp {

// Named home for the established PAN voicing; no sonic redesign is implied.
struct PanCalibration {
    float exciterGain = 0.80f;
    float noiseAmount = 1.0f;
    float brightnessMinHz = 700.0f;
    float brightnessMaxHz = 12000.0f;
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
struct PanVoicingConfig {
    float strikeHardnessMin = 0.18f;
    float strikeHardnessMax = 1.00f;
    float lowRegisterGain = 1.04f;
    float highRegisterGain = 0.94f;
    float lowRegisterBrightness = 1.00f;
    float highRegisterBrightness = 0.82f;
    float upperModeSoftVelocity = 0.18f;
    float upperModeHardVelocity = 0.88f;
    float t60LowRegisterScale = 1.10f;
    float t60HighRegisterScale = 0.90f;
    float splitBeatTargetHz = 1.00f;
    float registerLowHz = 146.83f;
    float registerHighHz = 440.00f;
    bool fixedHzSplit = true;
    float softModeCoupling[kMaxModesPerVoice] = {1.00f, 0.82f, 0.72f, 0.22f, 0.08f, 0.05f, 0.03f, 0.02f, 0.0f, 0.0f};
    float hardModeCoupling[kMaxModesPerVoice] = {1.00f, 0.94f, 0.88f, 0.78f, 0.70f, 0.62f, 0.54f, 0.46f, 0.0f, 0.0f};
};

inline constexpr PanVoicingConfig kPanVoicingConfig{};

// PAN is a model selection, not a set of implicit DSP defaults. Future models
// provide their own preset plus these two generic DSP configurations.
inline constexpr ExciterConfig kPanExciterConfig{
    kPanCalibration.exciterGain,
    kPanCalibration.noiseAmount,
    kPanCalibration.brightnessMinHz,
    kPanCalibration.brightnessMaxHz,
};

inline constexpr ResonatorConfig kPanResonatorConfig{
    kPanCalibration.masterModalGain,
    kPanCalibration.dampingDepth,
    true,
};
} // namespace pocketpan::dsp
