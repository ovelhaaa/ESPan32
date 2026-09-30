#pragma once

namespace pocketpan::dsp {

enum class ExciterShape : uint8_t {
    Strike = 0,
    Pluck = 1,
};

// Model-neutral controls.  A voice/model selects these; DSP primitives do not
// know which instrument calibration supplied them.
struct ExciterConfig {
    float gain = 0.80f;
    float noiseAmount = 1.0f;
    float brightnessMinHz = 700.0f;
    float brightnessMaxHz = 12000.0f;
    // Smooth top-end energy knee.  Hardness and modal coupling still rise to
    // v127; only strike energy is gently compressed before the limiter.
    float velocityKnee = 1.0f;
    float velocityKneeSlope = 1.0f;
    ExciterShape shape = ExciterShape::Strike;
};

struct ResonatorConfig {
    float masterGain = 1.0f;
    float dampingDepth = 0.95f;
    bool internalSafetySaturation = true;
};

} // namespace pocketpan::dsp
