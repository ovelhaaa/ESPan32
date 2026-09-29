# ESPan32 — M7.0.1 Tongue Documentation Sync + Hardware Qualification Closure

**Milestone:** M7.0.1  
**Target:** ESP32-S3 (QFN56 revision v0.2, 4MB Flash, 2MB PSRAM) @ 240 MHz  
**Audio Config:** 48 kHz, 128 frames/block (2666.67 µs budget), 8 polyphonic voices, stereo PCM5102 I2S  
**Date:** 2026-09-29  
**Status:** M7.0 CLOSED / Tongue V1 = **CANDIDATE** (Bell V1 = **FROZEN**, Pan = **REFERENCE**)

> **M7.0.2 correction (see [m702_regression_isolation.md](m702_regression_isolation.md)):**
> the `1966/1968 µs` PAN/Bell figures in this document came from a qualification
> firmware built as **DSP candidate 13** (a stale `build/` CMake cache), not
> candidate 25 as the M6 baseline was. Candidate 13 lacks the tail-IRAM,
> sustain, PAN stable-8 and attack-voice fast paths and is ~19% slower on every
> model. A controlled A/B on the committed source with candidate 25 reproduces
> the M6 frozen envelope (PAN 1668 µs, Bell 1654 µs) and proves the additive
> 6-mode `Process6` microkernel is not responsible.

---

## 1. Executive Summary

Milestone M7.0.1 was executed to resolve documentation discrepancies and complete physical hardware qualification for `InstrumentModel::Tongue` (Steel Tongue Drum / Tank Drum) on physical ESP32-S3 silicon without altering DSP tuning or audio behavior.

All six milestone goals have been successfully fulfilled:
1. **Full Documentation Synchronization:** Synchronized [docs/tongue_v1_baseline.md](file:///c:/progs/ESPan/docs/tongue_v1_baseline.md) and [docs/m70_tongue_drum.md](file:///c:/progs/ESPan/docs/m70_tongue_drum.md) with exact source code parameters from `kTongueVoicing`, `kPresetTongue`, and `kTongueModelConfig`.
2. **PAN Mode Count Correction:** Corrected historical documentation misstatement claiming PAN had 7 modes (56 resonators). PAN operates with 8 modes/voice (64 resonators total). Tongue's 6 modes/voice (48 resonators total) represents a **25.0% reduction** vs PAN and **40.0% reduction** vs Bell (10 modes/voice, 80 resonators total).
3. **Exciter Impulse Duration Correction:** Removed non-existent struct member claims (`impulseWidthMin`/`impulseWidthMax`). Documented the generic exciter model where impulse duration is universally calculated as $\text{durationSamples} = 14.0f - 11.0f \cdot h$ (min 3 samples), and documented the real physical/DSP causes of Tongue's mellow, warm mallet strike.
4. **Physical Hardware Forensics Qualification:** Qualified 8192-block hardware runs with 5 µs bins on ESP32-S3 COM10 across PAN cluster8, BELL cluster8, TONGUE chord4, and TONGUE cluster8. Tongue cluster8 callback average is **1566.84 µs** (vs PAN **1966.24 µs** and BELL **1968.58 µs**), achieving a **20.3% CPU headroom reduction** (~400 µs headroom gain) and **0 deadline misses**.
5. **BOOT Button Qualification:** An automated, hardware-resident BOOT state-machine test (`POCKETPAN_BUTTON_QUAL=1`) verified 3 cycles of `PAN -> BELL -> TONGUE -> PAN` (6 transitions total), plus 3 short presses (UI navigation regression test) and an extended 2500 ms hold test: exactly 1 transition per hold, 0 repeated cyclings while held, instant voice cutoff on model switch, and 0 crashes. The **user separately confirmed the physical BOOT button works on real hardware** (`PAN -> BELL -> TONGUE -> PAN`).
6. **Active Musical Smoke Soak:** Successfully executed 15,029 consecutive audio blocks of active musical soak on Tongue Drum (covering single strikes, velocity dynamic sweeps $30 \to 127$, restrikes, rolls, 4-voice chords, 8-voice clusters, PolyPressure, ChannelPressure, and idle decay) with **0 deadline misses**, **0 I2S underruns**, **0 MIDI drops**, and completely stable internal heap.

---

## 2. Documentation Corrections & Alignment

### 2.1 Mode and Resonator Count Hierarchy
Prior documentation mistakenly stated PAN had 7 modes per voice (56 resonators). The compiled source in `main/dsp/instrument_model.cpp` specifies:

| Instrument Model | Modes / Voice | Total Resonators (8 Voices) | Resonator Delta vs Tongue |
| :--- | :--- | :--- | :--- |
| **Tongue V1** | **6** | **48** | Baseline |
| **Pan** | **8** | **64** | Tongue is **25.0% smaller** (-16 resonators) |
| **Bell V1** | **10** | **80** | Tongue is **40.0% smaller** (-32 resonators) |

### 2.2 Exciter Model & Attack Softness Mechanism
The compiled `main/dsp/dsp_config.h` defines the exciter config exactly as:
```cpp
struct ExciterConfig {
    float gain;
    float noiseAmount;
    float brightnessMinHz;
    float brightnessMaxHz;
    float velocityKnee;
    float velocityKneeSlope;
};
```
There are **no** fields named `hammerHardnessMin`, `hammerHardnessMax`, `strikePosition`, `brightnessBandwidthMin`, `brightnessBandwidthMax`, `velocitySlopeAboveKnee`, `impulseWidthMin` or `impulseWidthMax`. In `main/dsp/exciter.cpp` the contact duration is computed uniformly across all instrument models from the effective strike hardness `h`:
```cpp
const float durationSamples = 14.0f - 11.0f * h;
impulseSamples_ = std::max<uint32_t>(3U, static_cast<uint32_t>(durationSamples));
```
The conversion to `uint32_t` **truncates toward zero** for the positive `durationSamples` (there is no `round()`), and the minimum impulse length is clamped to **3 samples**.

Verified exciter configurations (`main/dsp/dsp_config.h`, `main/dsp/instrument_model.cpp`, `main/dsp/pan_calibration.h`):

| Field | Tongue | Bell | Pan |
| :--- | ---: | ---: | ---: |
| `gain` | 0.78 | 0.80 | 0.80 |
| `noiseAmount` | 0.42 | 0.70 | 1.00 |
| `brightnessMinHz` | 750 | 900 | 700 |
| `brightnessMaxHz` | 8500 | 14000 | 12000 |
| `velocityKnee` | 0.85 | 0.85 | 0.85 |
| `velocityKneeSlope` | 0.38 | 0.35 | 0.35 |

Tongue V1 achieves its signature mellow, woody mallet character through:
- **Hardness Envelope:** `strikeHardnessMin = 0.25`, `strikeHardnessMax = 0.95` (soft strikes at low velocity produce a shorter contact window than Pan/Bell).
- **Subdued Noise Floor:** `noiseAmount = 0.42` (lower than Bell `0.70` and Pan `1.00`).
- **Warm Brightness Bandwidth:** `[750.0 Hz, 8500.0 Hz]`, filtering transient spike energy.
- **Velocity Compression Knee:** `velocityKnee = 0.85` and `velocityKneeSlope = 0.38` (the actual `ExciterConfig` member is `velocityKneeSlope`).
- **Direct Modal Gain Profile (`main/dsp/modal_preset.h`, `kPresetTongue`):** ratios $[1.0000, 2.0000, 2.9850, 4.0600, 5.3800, 6.7200]$ with gains $[1.00, 0.48, 0.26, 0.14, 0.07, 0.03]$.

---

## 3. Physical Hardware Forensics Qualification (ESP32-S3 @ 240 MHz)

Measurements captured live on `ESP32-S3 (QFN56 revision v0.2)` over serial COM10. Each fixture ran for 8192 audio blocks (21.85 seconds each) using 5 µs histogram binning. Total forensics budget: 2666.67 µs per block.

### 3.1 Steady-State Audio Callback Metrics

| Fixture ID | Model | Note Scenario | Voices | Resonators | Steady Avg Callback (µs) | Steady p95 (µs) | Steady p99 (µs) | Steady Max (µs) | Steady Deadline Misses | Event Max (µs) |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **0** | **PAN** | cluster8 | 8 | 64 | **1966.24** | 2080 | 2310 | 2522 | **0** | 3471 |
| **1** | **BELL** | cluster8 | 8 | 80 | **1968.58** | 2100 | 2265 | 2566 | **0** | 3276 |
| **2** | **TONGUE** | chord4 | 4 | 24 | **953.46** | 1080 | 1245 | 1934 | **0** | 1920 |
| **3** | **TONGUE** | cluster8 | 8 | 48 | **1566.84** | 1695 | 1855 | 2164 | **0** | 2496 |

### 3.2 Key Hardware Findings
1. **Measured CPU Hierarchy:**
   $$\text{TONGUE chord4 (953 µs)} < \text{TONGUE cluster8 (1567 µs)} < \text{PAN cluster8 (1966 µs)} \approx \text{BELL cluster8 (1969 µs)}$$
2. **Headroom Improvement:** Tongue cluster8 is **20.3% faster** than PAN cluster8 and Bell cluster8 in steady state, yielding approximately **400 µs of extra CPU margin** per 128-frame audio block.
3. **No Steady Deadline Misses:** Across all 32,768 forensics blocks evaluated, steady-state deadline misses remained strictly **0**.
4. **Clean Chord Execution:** Tongue chord4 runs in under 1 ms average (953.46 µs), consuming only 35.8% of the block budget.

---

## 4. BOOT Button Qualification

This qualification was an **automated, hardware-resident BOOT state-machine test**
(`POCKETPAN_BUTTON_QUAL=1`), not a capture of literal GPIO presses: on real
ESP32-S3 silicon over COM10 the firmware drove the same button flag the GPIO path
uses and logged the resulting transitions. Separately, the **user manually
confirmed on physical hardware that the real BOOT button cycles
`PAN -> BELL -> TONGUE -> PAN`**. The `m701_boot_cycle.log` artifact records the
automated state-machine run only and does not record physical button presses.

Automated state-machine results (ESP32-S3 over COM10):
- **Short Press Test:** 3 short presses ($<800$ ms) executed sequentially. Result: UI screen mode advanced (`Status -> Presets -> Bluetooth -> Status`), active model remained `PAN`. Short-press regression: **PASS**.
- **Model Cycling Test:** 6 long-press transitions executed ($\ge 800$ ms hold each):
  1. `PAN -> BELL` (preset label: `'BELL'`)
  2. `BELL -> TONGUE` (preset label: `'TONGUE'`)
  3. `TONGUE -> PAN` (preset label: `'PAN'`)
  4. Extended hold test: 2500 ms hold on BOOT button. Result: transitioned `PAN -> BELL` once; **0 repeated cycling** while held.
  5. `BELL -> TONGUE` (preset label: `'TONGUE'`)
  6. `TONGUE -> PAN` (preset label: `'PAN'`)
- **Audio Integrity:** Active voices were silenced cleanly on each model transition via synchronous block-boundary reset; 0 audio pops, 0 panics, 0 crashes.

---

## 5. Active Musical Smoke Soak Test

Ran continuous musical sequence on Tongue V1 model for 15,029 audio blocks (~40.1 seconds):
- **Workload Coverage:**
  - Soft strikes ($v=30$) and forte strikes ($v=127$)
  - Single notes and intervals
  - 4-voice chords and full 8-voice sustained clusters
  - Note restrikes and 66 ms rapid rolls
  - Polyphonic Aftertouch (PolyPressure $50, 75$)
  - Channel Aftertouch (ChannelPressure $64, 80$)
  - Complete ring-down into silence
- **Observed Metrics:**
  - `blocks`: 15,029
  - `deadlineMisses`: **0**
  - `writeTimeouts`: **0**
  - `txErrors`: **0**
  - `shortWrites`: **0**
  - `midiDrops`: **0**
  - `internal_free`: **138,087 bytes** (constant throughout test)
  - `largest_internal`: **34,816 bytes** (no fragmentation)

---

## 6. Raw Hardware Qualification Logs

The following raw log artifacts are preserved in `docs/hardware/`:
- [docs/hardware/m701_pan_cluster8.log](file:///c:/progs/ESPan/docs/hardware/m701_pan_cluster8.log): PAN cluster8 forensics (8192 blocks, 5 µs bins).
- [docs/hardware/m701_bell_cluster8.log](file:///c:/progs/ESPan/docs/hardware/m701_bell_cluster8.log): BELL cluster8 forensics (8192 blocks, 5 µs bins).
- [docs/hardware/m701_tongue_chord4.log](file:///c:/progs/ESPan/docs/hardware/m701_tongue_chord4.log): TONGUE chord4 forensics (8192 blocks, 5 µs bins).
- [docs/hardware/m701_tongue_cluster8.log](file:///c:/progs/ESPan/docs/hardware/m701_tongue_cluster8.log): TONGUE cluster8 forensics (8192 blocks, 5 µs bins).
- [docs/hardware/m701_forensics_all.log](file:///c:/progs/ESPan/docs/hardware/m701_forensics_all.log): Complete 4-fixture automated forensics execution log.
- [docs/hardware/m701_boot_cycle.log](file:///c:/progs/ESPan/docs/hardware/m701_boot_cycle.log): Automated BOOT state-machine cycle verification log (not a physical button capture; physical BOOT was confirmed manually by the user).
- [docs/hardware/m701_tongue_smoke.log](file:///c:/progs/ESPan/docs/hardware/m701_tongue_smoke.log): Continuous musical smoke & soak log (15,029 blocks).

---

## 7. Model Status & Closure Verdict

| Instrument Model | Status | Resonators / Voice | Role |
| :--- | :--- | :---: | :--- |
| **Pan** | **REFERENCE** | 8 | Core handpan reference model |
| **Bell V1** | **FROZEN** | 10 | Tubular / tuned bell model |
| **Tongue V1** | **CANDIDATE** | 6 | Steel tongue drum / tank drum candidate |

**Milestone Verdict:**
- Documentation synchronization: **COMPLETE**
- Hardware qualification: **PASSED**
- Regression testing: **9/9 host tests passed (100%)**
- Clean production firmware: **COMPILED & FLASHED**
- M7.0 / M7.0.1 is hereby formally **CLOSED**.
