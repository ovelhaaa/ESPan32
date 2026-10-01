# M7.5.1 — Vibraphone resonator coupling refinement

Historical milestone snapshot. Production is now [frozen VIBRAPHONE V1: BAR=C, MOTOR/TUBE=M1](m753_vibraphone_v1_freeze.md).

Implemented as a provisional listening candidate. VIBRAPHONE is not frozen; the perceptual verdict remains a listening decision.

The previous motor multiplied the completed bar/body mix by `1 - depth * shutter`. Every partial rose and fell together, producing conventional VCA tremolo. The new signal flow keeps the dry modal bar continuously present:

```text
existing modal bank sum -----------------------------------> dry bar bus --+
existing mode 0 z1 -> summed fundamental bus -> tube coupling -------------+-> master/headroom -> limiter -> PCM
                                                      ^
                                      shared motor LUT / uint32 phase
```

`ModalResonatorBank::fundamentalSample()` reads mode 0's current `z1` immediately after the existing recurrence, returning zero for absent/inactive mode 0. No recurrence or oscillator is duplicated. `modal_resonator.cpp`, including Process6 arithmetic and addition order, is unchanged. Extraction works with Process6 and the scalar/Nyquist-pruned fallback.

`ModalVoice::renderTubeBlock` fuses bar summation and the fundamental tap in a hot block loop. Sustain is segmented at the existing 128-sample lifetime checks; attack uses the existing exciter/recurrence and transitions to fused sustain. Pressure/reference paths and fades remain available. `VoiceAllocator::renderVibraphoneBlock` retains voice order and adds existing captured steal tails only to the dry bus. Killed/reset voices clear their modal tap. The tube buffer is overwritten each enabled render, so no prior-model tube energy survives selection.

Motor OFF or depth zero uses the previous dry renderer and contributes no tube energy. Motor ON uses:

```cpp
openness = 1 - interpolatedShutter;
fanResponse = .10f + .90f * openness;
output = barBus + fundamentalBus * tubeCoupling * (depth / .32f) * fanResponse;
```

B's coupling .90 and unchanged depth .32 give added tube gain .09–.90 while the dry bar remains continuous. Coupling supplies the new effect; motor depth was not increased. One global uint32 phase advances through silence and motor OFF. NoteOn/restrike preserves phase; reset/model selection clears it. The 2–8 Hz clamp, default 4.5 Hz, startup LUT and interpolation remain. No per-sample trig, new coefficient calculation, heap allocation, realtime logging, chorus or delay was added.

This first experiment uses the fundamental directly. Its frequency follows the note through existing PreparedNote coefficients. There is no fixed 146.83/293.66 Hz body, additional coloration/filter state, or pitch motion. A physical tube's separate decay and phase are not modeled. Listening must establish whether this simple coupling is convincing before adding an inexpensive phase treatment.

## A/B/C voicings

Runtime is **B classic/balanced**, retaining the previous dry bar configuration and adding tube coupling .90. A/C are host listening variants. All use 4.5 Hz/.32; modes 16x/22.4x/29x retain their original weak gains and short decays.

| Control | A mellow | B balanced | C bright |
|---|---:|---:|---:|
| 1x / 4x / 10x preset gain | 1 / .2356 / .060 | 1 / .62 / .20 | 1 / 1.147 / .50 |
| 4x / 10x T60 before register scaling, seconds | 3 / 1.25 | 3 / 1.25 | 2.16 / .8125 |
| Exciter maximum brightness, Hz | 6000 | 9500 | 13500 |
| Maximum mallet hardness | .42 | .62 | .85 |
| Soft 4x / 10x coupling | .55 / .24 | .55 / .24 | .80 / .42 |
| Tube coupling | .55 | .90 | .75 |

Preset gains are subsequently bank-normalized and shaped by velocity/strike coupling. D4 v70 attack-band energy relative to the fundamental: 4x is -19.87 / -12.56 / -6.96 dB for A/B/C; 10x is -45.11 / -35.18 / -28.96 dB. The rendered fundamental remains dominant, including C. This establishes spectral separation, not reliable listener identification or acoustic realism.

## Focused listening pack

[Open the 21-file pack](listening/m751/README.md). It contains exactly the requested five A/B/C groups and two OFF–OLD–NEW motor groups. All seven groups are RMS-matched after PCM conversion within 2e-6 linear RMS. [Manifest](qualification/m751/listening_manifest.json) records SHA256, RMS, peak and representative modal-band energy. Reproduce with `tests/collect_m751.py` after running `test_dsp` in a host build directory.

OLD uses the same B bar, original shutter law, rate/depth and pre-limiter placement. It is compiled only into `test_dsp` under `POCKETPAN_M751_LISTENING_REFERENCE`; the production target contains no old-motor branch/state. No large matrix was generated.

## CPU and focused hardware qualification

[Complete hardware timings and faults](qualification/m751/hardware_report.md), [checksummed raw-evidence manifest](qualification/m751/hardware_manifest.json). Actual ESP32-S3 on COM10, 240 MHz, 48 kHz/128 frames, candidate 25, Process6 ON, phase profiling OFF, UI/I2S active, BLE connected at 11.25 ms. Two three-fixture batches, 8192 blocks each: single OFF/chord4 ON/cluster8 ON; phrase ON/paired model switching/single ON. Only 4.5 Hz/.32.

All six final fixtures have zero callback/inner deadline misses, I2S timeouts/errors/short writes, hard clamps, modal saturation, bad voice counts and BLE event loss. Full callback results:

| Fixture | Avg us | p95 us | p99 us | Max us | CPU % |
|---|---:|---:|---:|---:|---:|
| single OFF | 272 | 305 | 375 | 929 | 10.20 |
| single ON | 338 | 370 | 450 | 1366 | 12.67 |
| chord4 ON | 717 | 765 | 1050 | 1606 | 26.88 |
| phrase ON | 703 | 755 | 925 | 1440 | 26.36 |
| cluster8 ON | 1210 | 1250 | 1875 | 2658 | 45.37 |
| model switching | 336 | 365 | 475 | 1205 | 12.60 |

p95 uses combined callback histograms with 5 us bin upper edges; p99 uses existing AudioStats 75 us bins. The class snapshots cover 8191 callbacks, excluding the final callback; averages/maxima/CPU use full AudioStats. Per-class inner and callback tables are in the linked report.

Single steady inner rendering: 252.16 us OFF / 313.38 us ON, **61.22 us/block added**. Historical M7.5 connected motor delta: 11.69 us; incremental historical comparison: approximately 49.53 us. Separate fixtures include scheduling variability, so this is not a paired old-firmware measurement. Chord4/cluster8 steady increases are approximately 83.00/107.25 us against historical dry measurements. The preferred <50 us additional cost is not established for polyphony.

Cluster8 maximum **2658 us** leaves only about 9 us beneath the 2666.7 us deadline, a remaining performance concern. The initial sample-call renderer produced one 3094 us cluster callback miss; `batch0_raw.log` preserves it. Fusing the block renderer reduced cluster steady cost from 1386.97 to 1174.36 us and passed the repeated focused run. Initial failed results are excluded from final qualification totals.

The M7.5 selection architecture remains: canonical tables/body templates are built at startup; selection chooses existing tables/templates and clears state. All 64 source/target host boundaries equal cold selection. Repeated double-selection event callback max is **1205 us**, inner max 1041 us, zero misses. Selection prepares no new tube coefficients. Eight PreparedNote tables remain 74784 bytes; actual SynthEngine is 88376 bytes (+516 versus M7.5: 512-byte tube bus plus config/layout padding). There is no new per-voice tube state.

Host paired median: OFF 2.53014 / ON 2.95204 us/block, delta .421898 us (seven reversed-order trials, 12800 blocks each). Host timing is not a hardware estimate. No realtime heap allocation was observed.

## Regression and restoration

All 10 CTest suites pass with Process6 ON and OFF: [ON log](qualification/m751/ctest.log), [OFF log](qualification/m751/p6off_ctest.log). [Hash grid](qualification/m751/regression_hashes.log), [host cost](qualification/m751/host_timing.log). MIDI 24–96 x velocities 30/70/110/127 yields 292 exact cached/reference cases per model; fallback notes, pressure, restrikes and all model boundaries also pass. Fast/reference PCM, motor partitioning, depth-zero dry equality, fundamental extraction against an independently excited mode-0 bank, and dry-bar equality through pressure/steals pass.

| Frozen instrument | Asserted aggregate FNV64 |
|---|---|
| PAN | 9e9801244165101d |
| BELL | 7c2dc6f37fb99f4b |
| TONGUE | 83f75567fb47ebaf |
| BOWL | 14772092e35bc4a7 |
| KALIMBA | e9bcabdd0b6e3bd2 |
| GLASS | 14e0dd6ba566e1cf |
| MARIMBA | 236d684f05f5f1d2 |

VIBRAPHONE intentionally changes; current grid hash 124f1d18573243ad is not frozen. Normal firmware is rebuilt/restored with PAN default and forensic/smoke flags OFF; [production boot evidence](qualification/m751/production_smoke.log) and production manifest are retained.

The remaining musical question is: **does NEW sound like the tube breathes beneath a continuously ringing bar rather than whole-signal tremolo?** No subjective listening verdict is claimed. B remains provisional until the listening comparison is accepted.
