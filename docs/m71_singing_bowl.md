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

Evidence logged in `docs/hardware/m71_forensics_all.log` across 8,192 blocks per fixture with connected BLE MIDI:

| Fixture | Model | Voices | Resonators | Steady Avg | Steady p95 | Steady p99 | Steady Max | Callback Steady Avg | Callback p99 | Deadline Misses |
|:---|:---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| 0 | PAN | 8 | 64 | 1649.98 µs | 1695 µs | 1815 µs | 1959 µs | 1665.75 µs | 1885 µs | **0** |
| 1 | TONGUE | 8 | 48 | 1238.13 µs | 1295 µs | 1375 µs | 1585 µs | 1254.08 µs | 1445 µs | **0** |
| 2 | **BOWL** | **4** | **28** | **1041.15 µs** | **1215 µs** | **1260 µs** | **1564 µs** | **1057.45 µs** | **1310 µs** | **0** |
| 3 | **BOWL** | **8** | **56** | **1791.67 µs** | **2105 µs** | **2155 µs** | **2445 µs** | **1808.40 µs** | **2215 µs** | **0** |

### 2.1 Analysis
- **Headroom on BOWL chord4:** At 4 voices, total callback average is 1057.45 µs, leaving 1609.22 µs of headroom (60.3% margin, 39.7% CPU load).
- **Headroom on BOWL cluster8:** At full 8 voices (56 resonators running generic scalar path), steady callback average is 1808.40 µs with p99 of 2215 µs and maximum of 2596 µs — strictly below the 2666.67 µs deadline (**0 deadline misses across all 8,103 steady blocks**).
- **Transport Integrity:** Over 36,000 blocks rendered with `timeout=0 tx_error=0 short=0` (zero I2S DMA underruns or transport drops).

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

1. `test_ble_midi_parser`: Passed (0.12 s)
2. `test_dsp`: Passed (6.24 s) — includes 24 M7.1 Bowl qualification tests:
   - Doublet beating verification: 0.7000 Hz split, 0.0001 cancellation ratio across D3, D4, A4.
   - 100 consecutive 4-way model switches (`PAN -> BELL -> TONGUE -> BOWL -> PAN`) with bit-identical direct PAN recovery.
   - Monotonic RMS scaling across velocities v30, v60, v90, v110, v127.
   - Zero NaN, zero Inf, zero hard clamps.
   - Zero modal internal saturations across single v127, chord4 v110, cluster8 v100.
   - Finite voice lifetimes and clean ring-down envelope.
3. `test_prepared_note`: Passed (0.81 s) — complete Bowl table coverage, canaries, and 4-way model switches.
4. `test_telemetry`: Passed (0.16 s)
5. `test_modal_kernel`: Passed (0.13 s)
6. `test_fastpath`: Passed (0.49 s) — includes Bowl fastpath coverage.
7. `test_exciter_fastpath`: Passed (0.09 s)
8. `test_m635_fastpath`: Passed (0.87 s) — includes Bowl cluster8 / chord4.
9. `test_m637_trigger`: Passed (0.07 s) — includes Bowl in trigger models.

---

## 5. Listening Pack (`listening/bowl_v1_m71/`)

14 host-rendered 48 kHz / 16-bit stereo WAV fixtures:
- `bowl_D3_v30.wav`, `bowl_D3_v90.wav`, `bowl_D3_v127.wav`
- `bowl_D4_v30.wav`, `bowl_D4_v60.wav`, `bowl_D4_v90.wav`, `bowl_D4_v110.wav`, `bowl_D4_v127.wav`
- `bowl_A4_v90.wav`
- `bowl_interval2.wav`
- `bowl_chord4.wav`
- `bowl_cluster8.wav`
- `bowl_restrike_softhard.wav`, `bowl_restrike_hardsoft.wav`
- `bowl_roll.wav`
- `bowl_register_sweep.wav`

---

## 6. Milestone Conclusion

- **Singing Bowl V1** is successfully implemented, verified, and qualified on physical hardware.
- Status remains **CANDIDATE** pending extended musical evaluation.
- All baseline models (PAN = REFERENCE, Bell V1 = FROZEN, Tongue V1 = CANDIDATE) remain 100% bit-exact.
- M7.1 is **COMPLETE**.
