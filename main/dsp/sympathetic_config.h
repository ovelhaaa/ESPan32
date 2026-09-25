#pragma once
namespace pocketpan::dsp {
struct SympatheticConfig {
    bool enabled = false;
    float inputGain = 0.0f;
    float feedbackGain = 0.0f;
    float lowpassHz = 0.0f;
    float maxBusLevel = 0.0f;
};
} // namespace pocketpan::dsp
