# Milestone M7.3 — Glass / Crystal V1 + 6-Model Runtime

**Branch:** `codex/m632-modal-note-cache`  
**Base Commit:** `d3b7184264fa84b3a6849641d862d589f01c855c`  
**Target Platform:** ESP32-S3 @ 240 MHz, 48 kHz, 128 frames/block, 8 voices  
**Status:** M7.3 **COMPLETE**  
**Instruments:**
- PAN: **REFERENCE**
- Bell V1: **FROZEN**
- Tongue V1: **FROZEN** (Human listening: COMPLETE, User verdict: accepted)
- Bowl V1: **FROZEN** (Human listening: COMPLETE, User verdict: accepted)
- Kalimba V1: **FROZEN** (Human listening: COMPLETE, User verdict: accepted)
- Glass V1: **CANDIDATE**

---

## 1. Summary of Changes

Milestone M7.3 adds `InstrumentModel::Glass` as the sixth instrument in ESPan32, establishing a 6-model unified runtime.

### 1.1 Core Components
1. **Freeze Kalimba V1 & Pluck Documentation Correction:**
   - Kalimba V1 accepted and frozen (`docs/kalimba_v1_baseline.md` and `docs/m72_kalimba.md`).
   - Corrected Pluck exciter documentation to accurately describe the 3–8 sample asymmetric quadratic-decay pulse ($p = 1.0 - i/N$, output $+=\text{amp}\cdot p^2 \cdot \text{norm}$) and one-pole lowpass noise filter. Zero DSP changes to Pluck or Kalimba.
2. **Model Extension to 6 Instruments:**
   - Extended `InstrumentModel` enum to `{ Pan = 0, Bell, Tongue, Bowl, Kalimba, Glass, Count = 6 }`.
   - Updated `nextInstrumentModel()`, `instrumentModelName()`, model registry, prepared note cache, canary checks, UI label, and soak tests.
   - Long-press BOOT cycling extended to 6 models:
     `PAN -> BELL -> TONGUE -> BOWL -> KALIMBA -> GLASS -> PAN`.
3. **Modal Topology:**
   - Exactly 6 modes per voice (48 resonators across 8 voices, identical resonator budget to Tongue).
   - Glass ratios: 1.0000, 2.3200, 3.8500, 5.5500, 7.7500, 10.4000.
   - Initial modal gains: 1.00, 0.42, 0.30, 0.18, 0.09, 0.04.
   - T60 profile: 4.80 s, 3.80 s, 2.80 s, 2.00 s, 1.20 s, 0.70 s.
4. **Exciter Configuration:**
   - Reused `ExciterShape::Strike` with clean crystal settings: gain 0.72, noise 0.08, brightness 2.4–16 kHz.
   - Hardness range: 0.60–0.98.
5. **Acoustic Shell:**
   - Body: OFF, Sympathetic: OFF, Doublets: OFF.
6. **Process6 Microkernel Acceleration:**
   - Automatically leverages the hand-unrolled `Process6Normal` microkernel for 6 modes on ESP32-S3.
7. **Memory Footprint:**
   - Total prepared note storage across 6 tables = 56,088 bytes.
   - `sizeof(SynthEngine) = 66,536 bytes`.
   - Canary checks verify all 6 tables in-place.
8. **Output Safety:**
   - 0 NaN, 0 Inf, 0 hard clamps, 0 modal saturation.

---

## 2. Host Test Suite Results

All 9 host test suites pass with 100% success on both standard build and microkernel-disabled fallback:

### 2.1 Standard Build (`build-host`, Process6 ON)
```text
1/9 Test #1: ble_midi_parser ................. Passed   0.03 sec
2/9 Test #2: dsp ............................. Passed   5.88 sec
3/9 Test #3: prepared_note ................... Passed   0.78 sec
4/9 Test #4: telemetry ....................... Passed   0.06 sec
5/9 Test #5: modal_kernel .................... Passed   0.05 sec
6/9 Test #6: fastpath ........................ Passed   0.56 sec
7/9 Test #7: exciter_fastpath ................ Passed   0.01 sec
8/9 Test #8: m635_fastpath ................... Passed   1.01 sec
9/9 Test #9: m637_trigger .................... Passed   0.01 sec
100% tests passed, 0 tests failed out of 9
```

### 2.2 Process6 Fallback Build (`build-host-p6off`, `POCKETPAN_PROCESS6_MICROKERNEL=0`)
```text
100% tests passed, 0 tests failed out of 9
```

### 2.3 Bit-Exact Regression Protection (FNV-1a Hashes)
Historical FNV-1a hashes across the 24..96 note grid remained 100% identical:
- PAN: `0x9e9801244165101d`
- BELL: `0x7c2dc6f37fb99f4b`
- TONGUE: `0x83f75567fb47ebaf`
- BOWL: `0x14772092e35bc4a7`
- KALIMBA: `0xe9bcabdd0b6e3bd2`
- GLASS: `0x14e0dd6ba566e1cf`

### 2.4 Generated Host Listening Fixtures (13 WAVs)
The following 13 audio fixtures were rendered by `test_dsp` under `tests/fixtures/audio/`:
- `glass_D3_v30.wav`
- `glass_D3_v90.wav`
- `glass_D4_v30.wav`
- `glass_D4_v60.wav`
- `glass_D4_v90.wav`
- `glass_D4_v127.wav`
- `glass_A4_v90.wav`
- `glass_interval2.wav`
- `glass_chord4.wav`
- `glass_cluster8.wav`
- `glass_restrike.wav`
- `glass_roll.wav`
- `glass_register_sweep.wav`

---

## 3. Hardware Qualification Forensics (ESP32-S3 @ 240 MHz)

Captured directly from physical hardware on `COM10` with Candidate 25, 5 µs histogram bins, BLE-MIDI connected:

| Fixture | Voices | Class | Inner Avg (µs) | Callback Avg (µs) | p95 (µs) | p99 (µs) | Max (µs) | Deadline Misses | Hard Clamps | Modal Sat |
|:---|:---:|:---|---:|---:|---:|---:|---:|:---:|:---:|:---:|
| **PAN cluster8** | 8 | `true_steady` | 1649.70 | 1664.80 | 1730 | 1905 | 2078 | 0 | 0 | 0 |
| **PAN cluster8** | 8 | `attack_tail` | 2044.99 | 2089.66 | 2315 | 2340 | 2335 | 0 | 0 | 0 |
| **PAN cluster8** | 8 | `event` | 2656.29 | 2741.32 | 2825 | 2980 | 2977 | 44 | 0 | 0 |
| **GLASS chord4** | 4 | `true_steady` | 766.91 | 781.75 | 830 | 990 | 1847 | **0** | 0 | 0 |
| **GLASS chord4** | 4 | `attack_tail` | 931.68 | 971.95 | 1040 | 1320 | 1315 | **0** | 0 | 0 |
| **GLASS chord4** | 4 | `event` | 1349.40 | 1430.73 | 1595 | 1785 | 1782 | **0** | 0 | 0 |
| **GLASS cluster8** | 8 | `true_steady` | 1235.86 | 1250.44 | 1305 | 1450 | 1648 | **0** | 0 | 0 |
| **GLASS cluster8** | 8 | `attack_tail` | 1523.27 | 1560.34 | 1595 | 1735 | 1731 | **0** | 0 | 0 |
| **GLASS cluster8** | 8 | `event` | 2157.02 | 2233.39 | 2295 | 2330 | 2329 | **0** | 0 | 0 |
| **KALIMBA cluster8** | 8 | `true_steady` | 1718.40 | 1734.17 | 1985 | 2050 | 2483 | 0 | 0 | 0 |
| **KALIMBA cluster8** | 8 | `event` | 2398.10 | 2489.73 | 2785 | 3105 | 3101 | 6 | 0 | 0 |

### Key Hardware Observations:
- **Zero Deadline Misses for Glass:** Both 4-voice chord and 8-voice cluster scenarios achieved **0 deadline misses across all classes (steady, attack, and event)**.
- **Timing Headroom:** Glass 8-voice steady average of 1235.86 µs is well inside the 1733 µs physical gate (occupying just 46.3% of the 2666.7 µs block duration).
- **Process6 Microkernel Efficiency:** 6 unrolled modes per voice allow 8 simultaneous voices to render faster than PAN (8 modes) and Bowl (7 modes).

---

## 4. BOOT Button Qualification (6-Model Runtime)

Hardware automated qualification with `POCKETPAN_BUTTON_QUAL=1` verified:
- **3 Short Presses:** UI navigation cycled `Status -> MidiDiagnostic -> AudioDiagnostic` without model drift.
- **11 Long-Press Transitions (Cycling 6 Models):**
  - Cycle 1: `PAN -> BELL`
  - Cycle 2: `BELL -> TONGUE`
  - Cycle 3: `TONGUE -> BOWL`
  - Cycle 4: `BOWL -> KALIMBA`
  - Cycle 5: `KALIMBA -> GLASS`
  - Cycle 6: `GLASS -> PAN`
  - Extended hold test (2.5 s hold on PAN): advanced to BELL without runaway re-triggers.
  - Cycle 8: `BELL -> TONGUE`
  - Cycle 9: `TONGUE -> BOWL`
  - Cycle 10: `BOWL -> KALIMBA`
  - Cycle 11: `KALIMBA -> GLASS`
  - Cycle 12: `GLASS -> PAN`
- **Heap Stability:** Free internal SRAM remained exactly `105,135 bytes` before and after all 11 transitions (0 byte leak).
- **Canary Validation:** `PreparedNote canaries OK=1` maintained continuously.
