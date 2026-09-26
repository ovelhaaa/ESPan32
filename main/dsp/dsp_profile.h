#pragma once
#ifndef POCKETPAN_DSP_CANDIDATE
#define POCKETPAN_DSP_CANDIDATE 0
#endif
#ifndef POCKETPAN_FORENSICS_CRITICAL_ONLY
#define POCKETPAN_FORENSICS_CRITICAL_ONLY 0
#endif

// With profiling disabled, all probes disappear at preprocessing time.
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#ifdef CONFIG_POCKETPAN_DSP_PROFILE
#include "esp_cpu.h"
#include <cstdint>
namespace pocketpan::dsp::profile {
enum Phase { Allocator, Modal, Exciter, Damping, Energy, Body, Mix, Limiter, Pcm, Count };
inline uint64_t cycles[Count]{}; // Audio-core owned; copied only at block boundaries.
inline bool enabled = false;
class Scope {
    Phase phase_;
    uint32_t start_;
    bool enabled_;
public:
    explicit Scope(Phase phase) : phase_(phase), start_(esp_cpu_get_cycle_count()), enabled_(enabled) {}
    ~Scope() { if (enabled_) cycles[phase_] += uint32_t(esp_cpu_get_cycle_count() - start_); }
};
}
#define DSP_PROFILE_SCOPE(phase) pocketpan::dsp::profile::Scope dspProfileScope(pocketpan::dsp::profile::phase)
#else
#define DSP_PROFILE_SCOPE(phase)
#endif
