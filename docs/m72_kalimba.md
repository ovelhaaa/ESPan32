# Milestone M7.2 — Kalimba / Thumb Piano V1

**Branch:** `codex/m632-modal-note-cache`  
**Base Commit:** `324d873543b69ab51016e3fcceab8be2f0cfb6f6`  
**Target:** ESP32-S3 @ 240 MHz, 48 kHz, 128 frames/block, 8 voices  
**Status:** M7.2 **COMPLETE**  
**Instruments:**
- PAN: **REFERENCE**
- Bell V1: **FROZEN**
- Tongue V1: **FROZEN** (Human listening: COMPLETE, User verdict: accepted)
- Bowl V1: **FROZEN** (Human listening: COMPLETE, User verdict: accepted)
- Kalimba V1: **CANDIDATE** (Ready for human listening)

---

## 1. Summary of Changes

Milestone M7.2 adds `InstrumentModel::Kalimba` as the fifth instrument in ESPan32.

### 1.1 Core Components
1. **Model Extension:**
   - Extended `InstrumentModel` enum to `{ Pan = 0, Bell, Tongue, Bowl, Kalimba, Count = 5 }`.
   - Updated `nextInstrumentModel()`, `instrumentModelName()`, model registry, prepared note cache, canary checks, UI label, and soak tests.
   - Long-press BOOT cycling extended: `PAN -> BELL -> TONGUE -> BOWL -> KALIMBA -> PAN`.
2. **Pluck Exciter Architecture:**
   - Added `ExciterShape` enum with `Strike` and `Pluck` modes.
   - Implemented `ExciterShape::Pluck` using an asymmetric displacement pulse release combined with a highpass/bandpass noise click.
   - Strict bit-exact arithmetic maintained: historical `Strike` calculation for PAN, Bell, Tongue, and Bowl is 100% bit-identical.
3. **Modal Topology:**
   - 5 modes per voice (40 resonators across 8 voices).
   - Tine ratios: 1.0000, 2.7000, 5.4000, 8.9000, 13.0000.
   - Initial modal gains: 1.00, 0.32, 0.15, 0.06, 0.02.
   - T60 profile: 2.20 s, 1.10 s, 0.55 s, 0.25 s, 0.12 s.
4. **Body Resonator:**
   - Lightweight 3-mode wooden box resonator (210 Hz, 420 Hz, 820 Hz) excited via `StrikeBus`.
   - A/B evaluation (K0 tine-only vs K1 tine + box) selected K1 for warm acoustic resonance.
5. **Memory Footprint:**
   - Prepared note cache extended for Kalimba (`kalimbaPreparedNotes_`).
   - SPSC / internal SRAM layout preserved with 0 fragmentation and canary checks intact.
6. **Output Safety:**
   - 0 NaN, 0 Inf, 0 hard clamps, 0 modal saturation across single v127, chord4 v110, cluster8 v100.

---

## 2. Host Test Suite Results

All 9 host test suites pass with 100% success (`ctest --test-dir build-host`):
1. `ble_midi_parser`: Passed (0.03 s)
2. `dsp`: Passed (5.76 s) — including M7.2 Kalimba registry, 100 model switch cycles, K0 vs K1 A/B, velocity monotonicity, polyphonic fixtures, and 13 WAV listening fixtures.
3. `prepared_note`: Passed (0.77 s) — including Kalimba prepared note cache, canary validation, fallback, restrike, and pressure.
4. `telemetry`: Passed (0.07 s)
5. `modal_kernel`: Passed (0.05 s)
6. `fastpath`: Passed (0.56 s)
7. `exciter_fastpath`: Passed (0.01 s) — including Pluck vs Strike differential across velocities.
8. `m635_fastpath`: Passed (0.99 s)
9. `m637_trigger`: Passed (0.01 s)

### 2.1 Generated Host Listening Fixtures (13 WAVs)
The following 13 audio fixtures were rendered by `test_dsp` under `tests/fixtures/audio/`:
- `kalimba_D3_v30.wav`
- `kalimba_D3_v90.wav`
- `kalimba_D4_v30.wav`
- `kalimba_D4_v60.wav`
- `kalimba_D4_v90.wav`
- `kalimba_D4_v127.wav`
- `kalimba_A4_v90.wav`
- `kalimba_interval2.wav`
- `kalimba_chord4.wav`
- `kalimba_cluster8.wav`
- `kalimba_restrike.wav`
- `kalimba_roll.wav`
- `kalimba_register_sweep.wav`

Exact reproduction command:
```bash
cmake --build build-host --target test_dsp && ./build-host/tests/test_dsp
```

---

## 3. Hardware Qualification Forensics (ESP32-S3 @ 240 MHz)

Captured directly from physical hardware on `COM10` with Candidate 25, 5 µs histogram bins, BLE-MIDI connected:

| Fixture | Voices | Class | Avg (µs) | p95 (µs) | p99 (µs) | Max (µs) | Deadlines | Clamps | Saturation |
|:---|:---:|:---|---:|---:|---:|---:|:---:|:---:|:---:|
| **PAN cluster8** | 8 | `true_steady` | 1650.87 | 1695 | 1835 | 2089 | 0 | 0 | 0 |
| **PAN cluster8** | 8 | callback steady | 1666.70 | 1730 | 1910 | 2193 | 0 | 0 | 0 |
| **BOWL cluster8** | 8 | `true_steady` | 1784.85 | 2095 | 2155 | 2425 | 0 | 0 | 0 |
| **BOWL cluster8** | 8 | callback steady | 1801.16 | 2120 | 2230 | 2538 | 0 | 0 | 0 |
| **KALIMBA chord4** | 4 | `true_steady` | 1091.15 | 1235 | 1270 | 1535 | 0 | 0 | 0 |
| **KALIMBA chord4** | 4 | callback steady | 1107.68 | 1260 | 1335 | 1750 | 0 | 0 | 0 |
| **KALIMBA chord4** | 4 | callback event | 1697.84 | 1845 | 1985 | 1980 | 0 | 0 | 0 |
| **KALIMBA cluster8** | 8 | `true_steady` | 1720.22 | 1970 | 2005 | 2279 | 0 | 0 | 0 |
| **KALIMBA cluster8** | 8 | callback steady | 1736.73 | 1990 | 2040 | 2410 | 0 | 0 | 0 |
| **KALIMBA cluster8** | 8 | callback event | 2418.80 | 2720 | 2860 | 2855 | 4 | 0 | 0 |

### 3.1 BOOT Button Automation
Hardware qualification with `POCKETPAN_BUTTON_QUAL=1` verified:
- 3 short presses without model drift
- 9 long-press transitions across all 5 models:
  `PAN -> BELL -> TONGUE -> BOWL -> KALIMBA -> PAN`
- 0 audio crashes, 0 memory leaks, 0 assertion failures.
