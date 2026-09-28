#pragma once
#ifndef POCKETPAN_DSP_CANDIDATE
#define POCKETPAN_DSP_CANDIDATE 25
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
     POCKETPAN_DSP_CANDIDATE == 16 || POCKETPAN_TAIL_BASE)
#define POCKETPAN_PREPARED_NOTE_CACHE \
    (POCKETPAN_DSP_CANDIDATE == 12 || POCKETPAN_DSP_CANDIDATE == 13 || \
     POCKETPAN_DSP_CANDIDATE == 14 || POCKETPAN_DSP_CANDIDATE == 15 || \
     POCKETPAN_DSP_CANDIDATE == 16 || POCKETPAN_TAIL_BASE)
#define POCKETPAN_BOUNDED_IRAM \
    (POCKETPAN_DSP_CANDIDATE == 5 || POCKETPAN_DSP_CANDIDATE == 9 || \
     POCKETPAN_DSP_CANDIDATE == 11 || POCKETPAN_DSP_CANDIDATE == 12 || \
     POCKETPAN_DSP_CANDIDATE == 13 || POCKETPAN_DSP_CANDIDATE == 14 || \
     POCKETPAN_DSP_CANDIDATE == 15 || POCKETPAN_DSP_CANDIDATE == 16 || \
     (POCKETPAN_DSP_CANDIDATE >= 17 && POCKETPAN_DSP_CANDIDATE <= 29))

// M6.3.4 candidate matrix.  Candidate 16 = 13 + stable sustain is the
// production-qualified baseline (Phases A).  Candidates 17-19 move one tail
// function to IRAM at a time, 20 is the accepted combination and 21 adds the
// exact attack/exciter fast path:
//   17 = 16 + SynthEngine::renderBlock in IRAM          (T1)
//   18 = 16 + PeakLimiter::processSample in IRAM        (T2)
//   19 = 16 + BodyResonator::processSample in IRAM      (T3)
//   20 = 16 + accepted combination                      (T4)
//   21 = 20 + exact attack/exciter fast path            (final)
#define POCKETPAN_TAIL_BASE \
    (POCKETPAN_DSP_CANDIDATE >= 17 && POCKETPAN_DSP_CANDIDATE <= 29)
#define POCKETPAN_IRAM_RENDERBLOCK \
    (POCKETPAN_DSP_CANDIDATE == 17 || POCKETPAN_DSP_CANDIDATE == 20 || \
     POCKETPAN_DSP_CANDIDATE == 21 || \
     (POCKETPAN_DSP_CANDIDATE >= 22 && POCKETPAN_DSP_CANDIDATE <= 29))
#define POCKETPAN_IRAM_LIMITER \
    (POCKETPAN_DSP_CANDIDATE == 18 || POCKETPAN_DSP_CANDIDATE == 20 || \
     POCKETPAN_DSP_CANDIDATE == 21 || \
     (POCKETPAN_DSP_CANDIDATE >= 22 && POCKETPAN_DSP_CANDIDATE <= 29))
#define POCKETPAN_IRAM_BODY \
    (POCKETPAN_DSP_CANDIDATE == 19 || POCKETPAN_DSP_CANDIDATE == 20 || \
     POCKETPAN_DSP_CANDIDATE == 21 || \
     (POCKETPAN_DSP_CANDIDATE >= 22 && POCKETPAN_DSP_CANDIDATE <= 29))

// M6.3.3 additive fast paths.  Candidate 13 is the measured M6.3.2 production
// baseline and must stay bit-identical: 14 adds the stable-sustain voice path,
// 15/16 keep the historical sustain composition.  The production-qualified
// baseline is candidate 16 = 13 + stable sustain.  Candidates 17-25 are
// additive to 16, so they also carry the sustain fast path.
#define POCKETPAN_SUSTAIN_FASTPATH \
    (POCKETPAN_DSP_CANDIDATE == 14 || POCKETPAN_DSP_CANDIDATE == 16 || \
     POCKETPAN_TAIL_BASE)

// M6.3.5 candidate matrix (additive to the validated candidate 20):
//   22 = 20 + cached sympathetic LPF coefficient        (Phase A1)
//   23 = 22 + PAN stable-8 sample-outer sustain path     (Phase A2)
//   24 = 20 + exact attack-voice fast path + exciter     (Phase B)
//   25 = 23 + attack-voice fast path                     (best combination)
#define POCKETPAN_SYMPATHETIC_COEFF_CACHE \
    (POCKETPAN_DSP_CANDIDATE >= 22 && POCKETPAN_DSP_CANDIDATE <= 29)
#define POCKETPAN_PAN_STABLE8_FASTPATH \
    (POCKETPAN_DSP_CANDIDATE == 23 || POCKETPAN_DSP_CANDIDATE == 25 || \
     POCKETPAN_DSP_CANDIDATE == 26 || \
     (POCKETPAN_DSP_CANDIDATE >= 27 && POCKETPAN_DSP_CANDIDATE <= 29))
#define POCKETPAN_ATTACK_VOICE_FASTPATH \
    (POCKETPAN_DSP_CANDIDATE == 24 || POCKETPAN_DSP_CANDIDATE == 25 || \
     POCKETPAN_DSP_CANDIDATE == 26 || \
     (POCKETPAN_DSP_CANDIDATE >= 27 && POCKETPAN_DSP_CANDIDATE <= 29))

// M6.3.7 Phase G trigger precompute (additive to the production candidate 25):
//   27/28/29 = 25 + exact MIDI-velocity lookup tables (hardness, exciter
//               strike/noise) + cached model register logarithms.
//
// The per-note register snapshot stored inside PreparedNote was measured on
// hardware to cause a reproducible boot panic (interrupt watchdog) whenever the
// struct grew, independent of memory pressure (an equal .bss pad booted).  That
// approach is therefore dropped and the register win is limited to caching the
// two configuration logarithms per voice; 28/29 are kept as aliases of 27.
#define POCKETPAN_VELOCITY_LUT \
    (POCKETPAN_DSP_CANDIDATE == 27 || POCKETPAN_DSP_CANDIDATE == 28 || \
     POCKETPAN_DSP_CANDIDATE == 29)

// M6.3.6 candidate 26 (additive to the connected-qualified candidate 25):
//   * headroom target lookup table: the nine integer voice counts 0..8 are
//     precomputed at init with the identical float expression (§28).  Host
//     differential test proves the table is bit-exact.
//
// The attack->sustain segmentation experiment (§22) is retained in the tree but
// DISABLED: the M6.3.6 connected run measured it *regressing* event average by
// ~3-4% (PAN 2490 -> 2568 us, BELL 2500 -> 2607 us) and increasing deadline
// misses (BELL 1 -> 7).  The extra per-sample branch and the doubled hot-loop
// body cost more than the exciter samples they save, and the event block itself
// never reaches the exciter end (noise burst >= 144 samples > 128-frame block).
// It is kept behind this opt-in macro only as a documented negative result.
#define POCKETPAN_HEADROOM_TABLE (POCKETPAN_DSP_CANDIDATE == 26)
#define POCKETPAN_ATTACK_SEGMENT 0
// The exact attack/exciter segment path.  Candidate 21 introduced it and it is
// reused verbatim by the attack-voice candidates, so candidate 16 stays
// binary-identical to its validated build.
#define POCKETPAN_ATTACK_FASTPATH \
    (POCKETPAN_DSP_CANDIDATE == 21 || POCKETPAN_ATTACK_VOICE_FASTPATH)

#if defined(ESP_PLATFORM) && POCKETPAN_BOUNDED_IRAM
#include "esp_attr.h"
#define DSP_HOT IRAM_ATTR
#else
#define DSP_HOT
#endif

// Per-function tail placement.  Defined empty on host so the experiment is a
// no-op outside firmware and the host binary equals the baseline.
#if defined(ESP_PLATFORM) && POCKETPAN_IRAM_RENDERBLOCK
#define DSP_IRAM_RENDERBLOCK IRAM_ATTR
#else
#define DSP_IRAM_RENDERBLOCK
#endif
#if defined(ESP_PLATFORM) && POCKETPAN_IRAM_LIMITER
#define DSP_IRAM_LIMITER IRAM_ATTR
#else
#define DSP_IRAM_LIMITER
#endif
#if defined(ESP_PLATFORM) && POCKETPAN_IRAM_BODY
#define DSP_IRAM_BODY IRAM_ATTR
#else
#define DSP_IRAM_BODY
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
