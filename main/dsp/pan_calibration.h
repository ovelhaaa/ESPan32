#pragma once

#include "dsp_config.h"

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
