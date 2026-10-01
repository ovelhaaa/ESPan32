#pragma once
#include <algorithm>
#include <cmath>

namespace pocketpan::dsp {
// H(z)=(a+z^-1)/(1+a*z^-1). One transposed state, unity magnitude.
// Choose a so phase at the note is -lag: tan(lag/2)=
// ((1-a)/(1+a))*tan(pi*f/fs). Called only at preparation/NoteOn.
inline float tubePhaseCoefficient(float hz, float sampleRate, float lagDegrees) {
    constexpr float pi = 3.14159265358979323846f;
    const float t = std::tan(pi * std::clamp(hz / sampleRate, .00001f, .48f));
    const float p = std::tan(lagDegrees * pi / 360.0f);
    return (t - p) / (t + p);
}
inline float processTubePhase(float input, float coefficient, float& state) {
    const float output = coefficient * input + state;
    state = input - coefficient * output;
    return output;
}
} // namespace pocketpan::dsp
