# ESPan32 — M7.1 Singing Bowl V1 + 4-Model Runtime

**Milestone:** M7.1  
**Target:** ESP32-S3 (QFN56 revision v0.2, 4MB Flash, 2MB PSRAM) @ 240 MHz  
**Audio Config:** 48 kHz, 128 frames/block (2666.67 µs deadline budget), 8 polyphonic voices, stereo PCM5102 I2S  
**Date:** 2026-09-29  
**Status:**  
- PAN = **REFERENCE** (bit-exact, preserved)  
- Bell V1 = **FROZEN** (bit-exact, preserved)  
- Tongue V1 = **CANDIDATE** (bit-exact, preserved)  
- Bowl V1 = **CANDIDATE** (newly qualified)  

---

## 1. Executive Summary

Milestone M7.1 introduces **Singing Bowl V1** (`InstrumentModel::Bowl`) as the fourth runtime instrument model in ESPan32, expanding the instrument palette alongside PAN, Bell, and Tongue:
1. **Physical Sound Identity:** Tibetan / Singing Bowl struck excitation with rounded mallet impulse, deep centered pitch, slow acoustic beating doublet (~0.70 Hz fixed-Hz split on fundamental prime), and long evolving sustain (up to 6.90 s in low register).
2. **7 Modes / Voice:** 56 total resonators across 8 polyphonic voices, positioned naturally between Tongue (48 resonators) and PAN (64 resonators).
3. **BOOT Cycling:** Extended hold ($\ge 800\text{ ms}$) advances through all 4 models:
   $$\text{PAN} \longrightarrow \text{BELL} \longrightarrow \text{TONGUE} \longrightarrow \text{BOWL} \longrightarrow \text{PAN}$$
   One transition per hold; extended press does not repeat cycle; short presses continue to navigate diagnostics/screens without altering model state.
4. **Zero Safety Saturation:** Saturation count is 0 across single v127, chord4 v110, and cluster8 v100.
5. **Microkernel Decision:** Singing Bowl runs on the existing generic scalar 7-mode recurrence (`processSampleReference`). In accordance with Phase O, no specialized `Process7` microkernel was added: hardware qualification shows 0 steady deadline misses and ample margin.
6. **Bit-Exact Regression Protection:** PAN, Bell V1, and Tongue V1 outputs remain 100% bit-exact across all host oracles.

---

## 2. Hardware Forensics Qualification (COM10, ESP32-S3 @ 240 MHz)

Qualification firmware built cleanly in `build-m71-qual` with production default `candidate=25`, `POCKETPAN_FORENSICS_M7=1`, and `fine_bins=5`:

```text
pocket_pan_main: [FORENSICS] candidate=25 profile=0 cpu_mhz=240 optimization_perf=1 process6=1 fine_bins=5 critical_only=0
pocket_pan_main: [CANARY] PreparedNote canaries OK=1
```

Evidence logged in `docs/hardware/m71_forensics_all.log` across 8,192 blocks per fixture with connected BLE MIDI.

### 2.1 Multi-Class Breakdown & Forensic Measurements

Nominal block period budget:
$$\text{period} = \frac{128\text{ frames}}{48000\text{ Hz}} = 2.666667\text{ ms} = 2666.7\text{ µs}$$

Under the platform's multi-class instrumentation, processing is classified into `true_steady`, `attack_tail`, and `event`:

#### Bowl Cluster8 (Fixture 3, 8 voices, 56 resonators, generic scalar path):

- **Inner DSP execution:**
  - `true_steady` ($n=8103$): avg 1791.67 µs, p95 2105 µs, p99 2155 µs, max 2445 µs, nominal deadline overruns = **0**
  - `attack_tail` ($n=44$): avg 2221.52 µs, p95 2550 µs, p99 2660 µs, max 2655 µs, nominal deadline overruns = **0**
  - `event` ($n=44$): avg 2721.34 µs, p95 3030 µs, p99 3070 µs, max 3067 µs, nominal deadline overruns = **20 / 44**

- **Full Audio Callback (DSP + telemetry + overhead):**
  - `true_steady` ($n=8103$): avg 1808.40 µs, p95 2130 µs, p99 2215 µs, max 2596 µs, nominal deadline overruns = **0**
  - `attack_tail` ($n=44$): avg 2261.50 µs, p95 2605 µs, p99 2730 µs, max 2729 µs, nominal deadline overruns = **1 / 44**
  - `event` ($n=44$): avg 2805.39 µs, p95 3120 µs, p99 3160 µs, max 3155 µs, nominal deadline overruns = **43 / 44**

#### Baseline Comparison Across Fixtures (True Steady):

| Fixture | Model | Voices | Resonators | Steady Avg | Steady p95 | Steady p99 | Steady Max | Callback Steady Avg | Callback p99 | Steady Deadline Misses |
|:---|:---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| 0 | PAN | 8 | 64 | 1649.98 µs | 1695 µs | 1815 µs | 1959 µs | 1665.75 µs | 1885 µs | **0** |
| 1 | TONGUE | 8 | 48 | 1238.13 µs | 1295 µs | 1375 µs | 1585 µs | 1254.08 µs | 1445 µs | **0** |
| 2 | **BOWL** | **4** | **28** | **1041.15 µs** | **1215 µs** | **1260 µs** | **1564 µs** | **1057.45 µs** | **1310 µs** | **0** |
| 3 | **BOWL** | **8** | **56** | **1791.67 µs** | **2105 µs** | **2155 µs** | **2445 µs** | **1808.40 µs** | **2215 µs** | **0** |

### 2.2 Buffered Real-Time Contract & Timing Semantics

Bowl cluster8 must **not** be characterized simply as having "0 deadline misses".

> **Performance Semantics:** Bowl cluster8 has zero true-steady deadline misses, but high-polyphony trigger events frequently exceed the nominal 2.667 ms block period. The production engine is already qualified under a buffered real-time contract (`docs/performance_contract_v1.md`), so these transient overruns are evaluated at the system level rather than treated as transport failure by themselves.

- **Nominal Block Period:** 2666.7 µs. A callback duration exceeding 2666.7 µs constitutes a nominal-period overrun, not an I2S transport failure.
- **Transport Verification:** Across 36,000+ blocks executed under live BLE MIDI on hardware:
  - `I2S timeout = 0`
  - `TX error = 0`
  - `short write = 0`
  - `BLE lost = 0`
  - `bad_voices = 0`, `hard clamps = 0`, `saturation count = 0`
- **Streak / Overlap Note:** Raw log analysis (`docs/hardware/m71_forensics_all.log`) reveals at least one case where an event block exceeding the nominal period is immediately followed by an attack-tail block also exceeding the nominal period:
  - `seq=31281 callback_us=2736 inner_us=2659 class=event`
  - `seq=31282 callback_us=2729 inner_us=2655 class=attack_tail`
  Consequently, it cannot be claimed that all Bowl overruns are strictly isolated single-block instances. The 6-descriptor DMA buffer absorption capacity absorbed this 2-block transient sequence without DMA underflow.
- **Process7 Microkernel Decision:** The generic scalar 7-mode kernel achieves steady callback average of ~1.81 ms with 0 steady deadline misses. A specialized 7-mode kernel is not justified solely to improve benchmark numbers while current steady-state operation is safe and the instrument has not yet undergone human listening.

---

## 3. BOOT State Machine Qualification

Automated state-machine qualification (`POCKETPAN_BUTTON_QUAL=1`) executed live on ESP32-S3 hardware (logged in `docs/hardware/m71_boot_cycle.log`):

```text
I (1157) pocket_pan_main: [CANARY] PreparedNote canaries OK=1
I (4217) pocket_pan_main: [BOOT_QUAL] SCREEN_MODE: 0 -> 1 (model=PAN rel_tick=6)
I (4637) pocket_pan_main: [BOOT_QUAL] SCREEN_MODE: 1 -> 2 (model=PAN rel_tick=20)
I (6387) pocket_pan_main: [BOOT_QUAL] TRANSITION: PAN -> BELL (label='BELL' mode=0 rel_tick=77)
I (7927) pocket_pan_main: [BOOT_QUAL] TRANSITION: BELL -> TONGUE (label='TONGUE' mode=0 rel_tick=128)
I (9427) pocket_pan_main: [BOOT_QUAL] TRANSITION: TONGUE -> BOWL (label='BOWL' mode=0 rel_tick=178)
I (10937) pocket_pan_main: [BOOT_QUAL] TRANSITION: BOWL -> PAN (label='PAN' mode=0 rel_tick=227)
I (12467) pocket_pan_main: [BOOT_QUAL] TRANSITION: PAN -> BELL (label='BELL' mode=0 rel_tick=278)
I (15167) pocket_pan_main: [BOOT_QUAL] TRANSITION: BELL -> TONGUE (label='TONGUE' mode=0 rel_tick=368)
I (16667) pocket_pan_main: [BOOT_QUAL] TRANSITION: TONGUE -> BOWL (label='BOWL' mode=0 rel_tick=417)
I (18197) pocket_pan_main: [BOOT_QUAL] TRANSITION: BOWL -> PAN (label='PAN' mode=0 rel_tick=468)
I (19157) pocket_pan_main: [BOOT_QUAL] COMPLETE: 3 short presses + 7 long cycles verified
I (25857) pocket_pan_main: [AUDIO] model=PAN blocks=9372 avg_us=341 p99_us=375 max_us=2004 cpu_load=12.8 deadline=0 timeout=0 tx_error=0 short=0
```

- Verified 3 short presses: screen modes cycle without changing model.
- Verified 7 long holds ($\ge 800\text{ ms}$): 4-model cyclic rotation completes smoothly.
- Verified extended hold (2500 ms): single model advancement, no repeat trigger.
- Verified audio transport: `timeout=0 tx_error=0 short=0 deadline=0`.

---

## 4. Host Test Suite Results

Host test suite executed with CTest (9/9 passed, 100%):

1. `test_ble_midi_parser`: Passed
2. `test_dsp`: Passed — includes doublet beating verification (0.7000 Hz split, 0.0001 cancellation ratio across D3, D4, A4), 100 consecutive 4-way model switches (`PAN -> BELL -> TONGUE -> BOWL -> PAN`) with bit-identical direct PAN recovery, monotonic RMS scaling, zero NaN/Inf/clamps/saturation, and canonical source consistency assertions (`modeCount=7`, `splitBeatTargetHz=0.70`, `fixedHzSplit=true`, `body.enabled=false`, `sympathetic.enabled=false`, exciter parameters).
3. `test_prepared_note`: Passed — complete Bowl table coverage, canaries, memory footprint verification (128 bytes/note, 9,348 bytes/table, 37,392 bytes total storage across 4 tables), and 4-way model switches.
4. `test_telemetry`: Passed
5. `test_modal_kernel`: Passed
6. `test_fastpath`: Passed — includes Bowl fastpath coverage.
7. `test_exciter_fastpath`: Passed
8. `test_m635_fastpath`: Passed — includes Bowl cluster8 / chord4.
9. `test_m637_trigger`: Passed — includes Bowl in trigger models.

---

## 5. Listening Pack (`listening/bowl_v1_m71/`)

These fixtures are generated by host qualification (`tests/test_dsp.cpp`, command: `ctest --test-dir build-host -R dsp` or `./build-host/test_dsp.exe`).
Per repository policy (`.gitignore`), WAV binaries are intentionally unversioned.

Exactly **17 host-rendered 48 kHz / 16-bit stereo WAV fixtures** are generated:
1. `bowl_D3_v30.wav`
2. `bowl_D3_v90.wav`
3. `bowl_D3_v127.wav`
4. `bowl_D4_v30.wav`
5. `bowl_D4_v60.wav`
6. `bowl_D4_v90.wav`
7. `bowl_D4_v110.wav`
8. `bowl_D4_v127.wav`
9. `bowl_A4_v90.wav`
10. `bowl_interval2.wav`
11. `bowl_chord4_v80.wav`
12. `bowl_chord4.wav`
13. `bowl_cluster8.wav`
14. `bowl_restrike_softhard.wav`
15. `bowl_restrike_hardsoft.wav`
16. `bowl_roll.wav`
17. `bowl_register_sweep.wav`

---

## 6. Milestone Conclusion

- **Singing Bowl V1** implementation remains untouched and qualified on physical hardware.
- Status remains **CANDIDATE** pending human critical listening.
- All baseline models (PAN = REFERENCE, Bell V1 = FROZEN, Tongue V1 = CANDIDATE) remain 100% bit-exact.
- M7.1 is **PASS** (with M7.1.1 documentation sync and qualification closure complete).
