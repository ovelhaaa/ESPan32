#pragma once
#ifndef POCKETPAN_DSP_CANDIDATE
#define POCKETPAN_DSP_CANDIDATE 9
#endif
#ifndef POCKETPAN_FORENSICS_CRITICAL_ONLY
#define POCKETPAN_FORENSICS_CRITICAL_ONLY 0
#endif

// M6.3.2 candidates are deliberately additive to the accepted M6.3.1 D+E
// baseline.  That keeps a cache-only or microkernel-only comparison from
// accidentally measuring a change in IRAM placement as well.
#define POCKETPAN_FIXED_MODAL_KERNEL \
    (POCKETPAN_DSP_CANDIDATE == 4 || POCKETPAN_DSP_CANDIDATE == 9 || \
     POCKETPAN_DSP_CANDIDATE == 12)
#define POCKETPAN_MODAL_MICROKERNEL \
    (POCKETPAN_DSP_CANDIDATE == 11 || POCKETPAN_DSP_CANDIDATE == 13 || \
     POCKETPAN_DSP_CANDIDATE == 14 || POCKETPAN_DSP_CANDIDATE == 15 || \
     POCKETPAN_DSP_CANDIDATE == 16)
#define POCKETPAN_PREPARED_NOTE_CACHE \
    (POCKETPAN_DSP_CANDIDATE == 12 || POCKETPAN_DSP_CANDIDATE == 13 || \
     POCKETPAN_DSP_CANDIDATE == 14 || POCKETPAN_DSP_CANDIDATE == 15 || \
     POCKETPAN_DSP_CANDIDATE == 16)
#define POCKETPAN_BOUNDED_IRAM \
    (POCKETPAN_DSP_CANDIDATE == 5 || POCKETPAN_DSP_CANDIDATE == 9 || \
     POCKETPAN_DSP_CANDIDATE == 11 || POCKETPAN_DSP_CANDIDATE == 12 || \
     POCKETPAN_DSP_CANDIDATE == 13 || POCKETPAN_DSP_CANDIDATE == 14 || \
     POCKETPAN_DSP_CANDIDATE == 15 || POCKETPAN_DSP_CANDIDATE == 16)

// M6.3.3 additive fast paths.  Candidate 13 is the measured M6.3.2 production
// baseline and must stay bit-identical: 14 adds the stable-sustain voice path,
// 15 adds the attack/exciter segment path, 16 combines both.
#define POCKETPAN_SUSTAIN_FASTPATH \
    (POCKETPAN_DSP_CANDIDATE == 14 || POCKETPAN_DSP_CANDIDATE == 16)
#define POCKETPAN_ATTACK_FASTPATH \
    (POCKETPAN_DSP_CANDIDATE == 15 || POCKETPAN_DSP_CANDIDATE == 16)

#if defined(ESP_PLATFORM) && POCKETPAN_BOUNDED_IRAM
#include "esp_attr.h"
#define DSP_HOT IRAM_ATTR
#else
#define DSP_HOT
#endif

// With profiling disabled, all probes disappear at preprocessing time.
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#ifdef CONFIG_POCKETPAN_DSP_PROFILE
#include "esp_cpu.h"
#include <cstdint>
namespace pocketpan::dsp::profile {
// The first nine phases retain their M6.3.1 numbering so historical reports
// remain readable.  The trigger phases exist only in diagnostic/profile
// builds and are used to partition NoteOn work independently from sustain.
enum Phase {
    Allocator, Modal, Exciter, Damping, Energy, Body, Mix, Limiter, Pcm,
    MidiDispatch, VoiceAllocation, TriggerRegister, TriggerCoefficients,
    TriggerCoupling, TriggerExciter, PreparedLookup, TriggerOther, Count
};
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
