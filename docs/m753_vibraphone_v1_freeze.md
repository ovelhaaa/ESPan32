# M7.5.3 — Vibraphone V1 freeze

```text
VIBRAPHONE V1
BAR = C
MOTOR/TUBE = M1
```

The listening decision is final. Production uses the selected C bar and the exact M7.5.1-style fundamental tube coupling. Tonal exploration is closed. M2/M3 survive as historical commits and listening files; their implementation, phase state, coefficients and runtime branches have been removed.

## Frozen parameters

| Modal ratio | Preset gain | T60 before register scaling, seconds |
|---|---:|---:|
| 1 | 1 | 5.5 |
| 4 | .62 × 1.85 = 1.147 | 3 × .72 = 2.16 |
| 10 | .20 × 2.50 = .50 | 1.25 × .65 = .8125 |
| 16 | .065 | .65 |
| 22.4 | .025 | .32 |
| 29 | .010 | .16 |

The original float multiplication expressions are retained to reproduce C exactly. All mode detunes are zero; the bank retains energy normalization and internal safety saturation. Silence threshold is 1e-8.

Exciter: Strike shape, gain .16, noise amount .075, brightness 800–13500 Hz, velocity knee .85 and slope .38. Strike energy uses the unchanged floor .15 and `pow(energyVelocity,1.25)`. Hardness is .10–.85 using `pow(v,1.15)`. Soft mode coupling is `[1,.80,.42,.08,.025,.008]`; hard coupling is `[1,.95,.72,.40,.20,.08]`, blended over velocities .20–.90. Register gain is 1–.85, brightness scaling 1.02–.94, T60 scaling 1.20–.80 over 146.83–587.33 Hz. Resonator gain is 1, damping depth .95. Body and sympathetic paths remain disabled.

The motor defaults to **4.5 Hz**, depth **.32**, coupling **.75**. Its interpolated cosine shutter yields `openness=1-shutter` and `response=.10+.90*openness`. Closed/open added fundamental gain at default depth is .075/.75. Depth zero and motor OFF are exactly dry.

```cpp
output = drySum
       + ((fundamentalSum * .75f) * (depth / .32f))
         * (.10f + .90f * openness);
```

One global uint32 motor phase and the existing 257-point LUT drive all notes. Phase advances through silence and motor OFF; NoteOn and restrike never reset it. Model selection/reset remain deterministic. There is no tube allpass, phase modulation, per-note LFO, vibrato, chorus or delay.

## Production path and simplification

The M7.5.2 sample-first renderer is retained, specialized to **drySum + fundamentalSum**. Mode 0 is tapped immediately after the single existing modal recurrence. Voices are summed in slot order 0–7; the global fan gain is applied once per sample. There is no per-voice dual-buffer accumulation. Multiplications retain the original M1 left association rather than precombining gains, preserving exact PCM.

The stable renderer uses the accepted Process6 safety entry when every participating bank qualifies, and the original dispatch for pruned/scalar banks. Exciter and lifetime transitions are segmented at their exact sample boundaries. Age and last-sample updates occur once per segment, with energy checks at the original 128-sample boundaries. Segments with all eight voices active specialize outside the sample loop, eliminating repeated active checks; all-eight sustain segments also eliminate repeated exciter checks. Partial or mixed segments retain the same arithmetic and slot order. Pressure, damping transitions and stealing use the reference sample-first path. Captured steal tails enter the dry sum before tube mixing.

Removed: tube phase state/coefficient/lag, PreparedNote tube phase coefficient, phase configuration and nonlinear aperture selector, phaseSum, phase-mix gains and branches, the allpass header, the old listening-reference compile definition and motor reference branch. The remaining 128-frame buffer stores shared fan response, not a tube audio bus. Exact MIDI frequency/velocity lookup tables and PreparedNote register position are retained; startup preparation and realtime model-switch behavior are unchanged.

## Exact sonic regression and frozen hashes

[Host fixture report](qualification/m753/host_metrics.md) asserts all twelve retained M7.5.1/M7.5.2 dry/M1 FNV64 fixtures: D4 v70, D4 v110, chord4, cluster8, sparse phrase and roll. **All are exact. No PCM tolerance or numerical difference is needed.** No new listening pack was generated.

[Process6 ON](qualification/m753/ctest.log) and [Process6 OFF](qualification/m753/p6off_ctest.log) each pass all ten CTest suites. Independent sample oracles cover attack-to-sustain rendering and modal state after tube bypass. Cached/reference triggers, out-of-table notes, pressure, restrikes, fast/reference rendering, all 64 model boundaries, motor partitioning, depth-zero and realtime allocation checks remain covered.

The frozen production hash is the existing aggregate FNV64 scheme over MIDI 24–96 × velocities 30/70/110/127 (292 cases, default production motor ON). All eight aggregates are now mandatory assertions in `test_prepared_note`.

| Frozen instrument | Aggregate FNV64 |
|---|---|
| PAN | 9e9801244165101d |
| BELL | 7c2dc6f37fb99f4b |
| TONGUE | 83f75567fb47ebaf |
| BOWL | 14772092e35bc4a7 |
| KALIMBA | e9bcabdd0b6e3bd2 |
| GLASS | 14e0dd6ba566e1cf |
| MARIMBA | 236d684f05f5f1d2 |
| **VIBRAPHONE V1** | **3cea893644aa0283** |

[Complete hash grid and memory log](qualification/m753/regression_hashes.log). All host fixtures have zero nonfinite samples, hard clamps and modal saturation. Maximum and average limiter gain reduction are zero for the frozen fixtures.

## Hardware qualification and memory

[Full callback measurements and class timings](qualification/m753/hardware_report.md), [raw-evidence manifest](qualification/m753/hardware_manifest.json). Physical ESP32-S3 on COM10, 240 MHz, 48 kHz/128 frames, UI/I2S active, BLE MIDI connected at 11.25 ms. Six 8192-block fixtures: single OFF, single ON, chord4 ON, cluster8 ON, phrase ON and repeated model switching. Event and transition blocks remain included. The report compares the final maximum against M7.5.2's 2382 us / 284.7 us margin and retains the initial development capture.

| Fixture | Average us | p95 us | p99 us | Maximum us | CPU % |
|---|---:|---:|---:|---:|---:|
| Single OFF | 263 | 295 | 350 | 690 | 9.86 |
| Single ON | 312 | 340 | 425 | 823 | 11.70 |
| Chord4 ON | 680 | 735 | 925 | 1626 | 25.50 |
| Cluster8 ON | 1121 | 1160 | 1550 | 2212 | 42.03 |
| Phrase ON | 560 | 720 | 875 | 1059 | 21.00 |
| Model switching | 312 | 335 | 425 | 1117 | 11.70 |

All six fixtures: **zero deadline misses, I2S timeouts/errors/short writes, hard clamps, modal saturation and BLE losses**. Cluster8 margin is **454.7 us**, improving the captured M7.5.2 margin by **170 us**. Average CPU falls from 46.79% to 42.03%. These are separate physical captures and include scheduling variability. p95 uses 5 us callback-histogram upper edges; p99 uses AudioStats 75 us bins. The two earlier development versions failed the required worst-case margin (2436 and 2490 us); both logs and binaries remain available. Final evidence uses the specialized stable-eight implementation.

Host `sizeof(SynthEngine)` is **90992 bytes**, down **2408 bytes** from M7.5.2's 93400. On ESP32-S3 the engine's BSS symbol is **90744 bytes**. PreparedNote is **132 bytes**, down from 136; each 73-note table is **9640 bytes**, down from 9932, saving **2336 bytes** across eight tables. Shared MIDI/velocity lookup storage remains 1536 bytes outside the engine. There is no per-voice tube memory and no realtime heap allocation. Device free/largest internal heap is recorded in the raw logs.

The specialized renderer trades code size for timing margin: its IRAM function is **21248 bytes**. State savings do not imply an overall SRAM reduction. Qualification steady internal heap is **54175–54195 bytes**, with largest block **31744 bytes**; restored normal firmware reports **56363 bytes free**, largest **31744 bytes**. The framebuffer stays in PSRAM and DMA scratch in internal RAM. [Production link-size report](qualification/m753/production_size.json): shared DIRAM data 17664, BSS 104056, text 109799, remaining 110241 bytes before runtime allocations. Source, flashed-firmware and serial-log SHA256 values accompany the evidence manifest.

## Normal firmware

Normal firmware boots PAN and retains long-BOOT cycling through all eight models, including frozen VIBRAPHONE V1. Forensic/smoke/test flags are disabled; there is no compiled M2/M3 or listening-reference branch. Restoration and boot evidence are recorded in [production manifest](qualification/m753/production_manifest.json) and [production smoke log](qualification/m753/production_smoke.log).

Restoration completed on COM10. The 20-second normal boot capture contains four PAN audio reports, BLE MIDI connected at 11.25 ms, and zero deadline/I2S faults. Long-BOOT cycling remains unchanged at `main/app/app_main.cpp:365`; the complete eight-model registry and model-boundary tests pass.
