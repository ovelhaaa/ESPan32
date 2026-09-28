#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

namespace pocketpan::dsp {

// M6.3.7 Phase G.  MIDI velocity is a discrete 7-bit domain, so values that
// depend only on velocity can be precomputed exactly.  The tables are built
// from the *identical* expressions used by the realtime trigger path; any
// float that is not exactly one of the 128 normalized MIDI velocities falls
// back to the historical libm expression, so exactness is unconditional.

constexpr int kMidiVelocityCount = 128;

inline bool sameFloatBits(float a, float b) {
    uint32_t ua = 0, ub = 0;
    std::memcpy(&ua, &a, sizeof(ua));
    std::memcpy(&ub, &b, sizeof(ub));
    return ua == ub;
}

// Index in [0,127] when velocity bit-equals i/127.0f (the exact value
// MidiMapping::toNormalizedFloat produces), otherwise -1.
inline int exactMidiVelocityIndex(float velocity) {
    if (!(velocity >= 0.0f && velocity <= 1.0f)) return -1;
    int i = static_cast<int>(velocity * 127.0f + 0.5f);
    if (i < 0) i = 0;
    if (i > 127) i = 127;
    const float key = static_cast<float>(i) / 127.0f;
    return sameFloatBits(key, velocity) ? i : -1;
}

// Config-independent pow(v, 1.15f) over the 128 MIDI velocities.  Shared by all
// voices; the expression matches ModalVoice::configureStrike exactly.
inline const float* velocityPow115Table() {
    static float table[kMidiVelocityCount];
    static bool ready = false;
    if (!ready) {
        for (int i = 0; i < kMidiVelocityCount; ++i) {
            const float v = static_cast<float>(i) / 127.0f;
            table[i] = std::pow(v, 1.15f);
        }
        ready = true;
    }
    return table;
}

} // namespace pocketpan::dsp
