# ESPan32 — M7.0.1 Tongue Documentation Sync + Hardware Qualification Closure

**Milestone:** M7.0.1  
**Target:** ESP32-S3 (QFN56 revision v0.2, 4MB Flash, 2MB PSRAM) @ 240 MHz  
**Audio Config:** 48 kHz, 128 frames/block (2666.67 µs budget), 8 polyphonic voices, stereo PCM5102 I2S  
**Date:** 2026-09-29  
**Status:** M7.0 CLOSED / Tongue V1 = **CANDIDATE** (Bell V1 = **FROZEN**, Pan = **REFERENCE**)

---

## 1. Executive Summary

Milestone M7.0.1 was executed to resolve documentation discrepancies and complete physical hardware qualification for `InstrumentModel::Tongue` (Steel Tongue Drum / Tank Drum) on physical ESP32-S3 silicon without altering DSP tuning or audio behavior.

All six milestone goals have been successfully fulfilled:
1. **Full Documentation Synchronization:** Synchronized [docs/tongue_v1_baseline.md](file:///c:/progs/ESPan/docs/tongue_v1_baseline.md) and [docs/m70_tongue_drum.md](file:///c:/progs/ESPan/docs/m70_tongue_drum.md) with exact source code parameters from `kTongueVoicing`, `kPresetTongue`, and `kTongueModelConfig`.
2. **PAN Mode Count Correction:** Corrected historical documentation misstatement claiming PAN had 7 modes (56 resonators). PAN operates with 8 modes/voice (64 resonators total). Tongue's 6 modes/voice (48 resonators total) represents a **25.0% reduction** vs PAN and **40.0% reduction** vs Bell (10 modes/voice, 80 resonators total).
3. **Exciter Impulse Duration Correction:** Removed non-existent struct member claims (`impulseWidthMin`/`impulseWidthMax`). Documented the generic exciter model where impulse duration is universally calculated as $\text{durationSamples} = 14.0f - 11.0f \cdot h$ (min 3 samples), and documented the real physical/DSP causes of Tongue's mellow, warm mallet strike.
4. **Physical Hardware Forensics Qualification:** Qualified 8192-block hardware runs with 5 µs bins on ESP32-S3 COM10 across PAN cluster8, BELL cluster8, TONGUE chord4, and TONGUE cluster8. Tongue cluster8 callback average is **1566.84 µs** (vs PAN **1966.24 µs** and BELL **1968.58 µs**), achieving a **20.3% CPU headroom reduction** (~400 µs headroom gain) and **0 deadline misses**.
5. **Physical BOOT Button Qualification:** Successfully verified 3 cycles of `PAN -> BELL -> TONGUE -> PAN` (6 transitions total), plus 3 short presses (UI navigation regression test) and an extended 2500 ms hold test. Exactly 1 transition per hold, 0 repeated cyclings while held, instant voice cutoff on model switch, and 0 crashes.
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
The `ExciterConfig` struct defines:
```cpp
struct ExciterConfig {
    float hammerHardnessMin;
    float hammerHardnessMax;
    float strikePosition;
    float noiseAmount;
    float brightnessBandwidthMin;
    float brightnessBandwidthMax;
    float velocityKnee;
    float velocitySlopeAboveKnee;
};
```
There are no fields named `impulseWidthMin` or `impulseWidthMax`. In `main/dsp/modal_voice.cpp`, the contact duration is determined uniformly across all instrument models as:
$$\text{durationSamples} = \max\left(3, \; \text{round}(14.0f - 11.0f \cdot h)\right)$$
where $h \in [0, 1]$ is the effective strike hardness.

Tongue V1 achieves its signature mellow, woody mallet character through:
- **Hardness Envelope:** $[0.25, 0.95]$ (strikes at low velocity produce duration $\approx 11\text{--}12$ samples).
- **Subdued Noise Floor:** `noiseAmount = 0.42` (lower than Pan `0.65` and Bell `0.58`).
- **Warm Brightness Bandwidth:** $[750.0\text{ Hz}, 8500.0\text{ Hz}]$ filtering transient spike energy.
- **Velocity Compression Knee:** `velocityKnee = 0.85` and `velocitySlopeAboveKnee = 0.38`.
- **Direct Modal Gain Profile:** Ratios $[1.000, 2.000, 3.000, 4.000, 5.250, 7.150]$ with steep higher-mode attenuation ($g_0 = 1.00$, $g_5 = 0.08$).

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

## 4. Physical BOOT Button Qualification

Tested live on ESP32-S3 over COM10:
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
- [docs/hardware/m701_boot_cycle.log](file:///c:/progs/ESPan/docs/hardware/m701_boot_cycle.log): Physical BOOT button cycle verification log.
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
