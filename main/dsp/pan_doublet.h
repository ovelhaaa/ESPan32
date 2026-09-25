#pragma once

#include <algorithm>
#include <cmath>

namespace pocketpan::dsp {

// Kept with the PAN model: this is a musical doublet policy, not generic DSP.
enum class PanDoubletMode { Relative, FixedHz };

inline float computePanDoubletDetune(float fundamentalHz, PanDoubletMode mode,
                                     float relativeDetune = 0.0032f,
                                     float fixedBeatHz = 1.0f) {
    const float safeFundamental = std::max(fundamentalHz, 1.0f);
    return mode == PanDoubletMode::FixedHz ? fixedBeatHz / safeFundamental : relativeDetune;
}

inline float computePanDoubletBeatHz(float fundamentalHz, PanDoubletMode mode,
                                     float relativeDetune = 0.0032f,
                                     float fixedBeatHz = 1.0f) {
    return std::abs(fundamentalHz * computePanDoubletDetune(fundamentalHz, mode,
                                                             relativeDetune, fixedBeatHz));
}

} // namespace pocketpan::dsp
