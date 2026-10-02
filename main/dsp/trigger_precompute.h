#pragma once

#include <cmath>
#include <algorithm>
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

// Canonical C exciter knee/slope. Eagerly warmed before audio starts.
inline const float* vibraphoneStrikePowTable() {
    static float table[kMidiVelocityCount];
    static bool ready=false;
    if (!ready) {
        for(int i=0;i<kMidiVelocityCount;++i) {
            const float v=std::clamp(float(i)/127.0f,.01f,1.0f);
            const float energy=v<=.85f ? v : .85f+(v-.85f)*.38f;
            table[i]=std::pow(energy,1.25f);
        }
        ready=true;
    }
    return table;
}
// M7.6 canonical Mbira knee/slope; shared once, never per note/model table.
inline const float* mbiraStrikePowTable() {
    static float table[kMidiVelocityCount];
    static bool ready=false;
    if (!ready) {
        for(int i=0;i<kMidiVelocityCount;++i) {
            const float v=std::clamp(float(i)/127.0f,.01f,1.0f);
            const float energy=v<=.90f ? v : .90f+(v-.90f)*.55f;
            table[i]=std::pow(energy,1.25f);
        }
        ready=true;
    }
    return table;
}
// M8: one shared canonical PAN/BELL strike curve, never one LUT per voice.
// Initialize explicitly before audio; lookup cannot generate a table.
struct CanonicalStrikePowCache {
    float table[kMidiVelocityCount]{};
    float knee=0, slope=0;
    bool ready=false;
};
inline CanonicalStrikePowCache& canonicalStrikePowCache() {
    static CanonicalStrikePowCache cache;
    return cache;
}
inline void prepareCanonicalStrikePow(float configKnee,float configSlope) {
    auto& cache=canonicalStrikePowCache();
    cache.ready=false;cache.knee=configKnee;cache.slope=configSlope;
    const float knee=std::clamp(configKnee,.01f,1.0f);
    const float slope=std::clamp(configSlope,.01f,1.0f);
    for(int i=0;i<kMidiVelocityCount;++i) {
        const float v=std::clamp(float(i)/127.0f,.01f,1.0f);
        const float energy=v<=knee ? v : knee+(v-knee)*slope;
        cache.table[i]=std::pow(energy,1.25f);
    }
    cache.ready=true;
}
inline const float* canonicalStrikePowTable(float knee,float slope) {
    const auto& cache=canonicalStrikePowCache();
    return cache.ready && sameFloatBits(knee,cache.knee) && sameFloatBits(slope,cache.slope)
        ? cache.table : nullptr;
}
} // namespace pocketpan::dsp
