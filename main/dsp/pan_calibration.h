#pragma once

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
} // namespace pocketpan::dsp
