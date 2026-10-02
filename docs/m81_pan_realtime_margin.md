# M8.1 — PAN realtime margin recovery

**Preferred physical gate passed.** Fresh control maximum: 2642 us. Worst selected maximum across three connected captures: **2318 us**, giving **348.67 us** margin at 48 kHz / 128 frames. Zero deadline misses. All ten frozen hashes remain exact. Normal firmware was rebuilt and flashed. Real SMK25V2 MIDI reception and display updates were verified, with zero audio/I2S faults.

## 1. Exact bottleneck

The near-deadline class was the eight-voice event/attack callback, not a steady fallback. Control true steady averaged 1667.62 us, attack tail 2064.32 us, event 2508.77 us (maximum 2642 us). MIDI dispatch alone averaged 144.47 us, maximum 151 us; dispatch optimization alone cannot recover enough margin.

Measured PAN cluster profile: modal work about 1063 us in every class; exciter zero in sustain, 344 us in attack tail, 613 us in an event block. Limiter approximately 250–282 us; body 98 us; sympathetic bus 25–38 us; energy checks 5.3–5.5 us; PCM conversion 17 us. Largest sampled event: allocator 2133.56 us, modal 1062.40, exciter 684.22, energy 5.33, sympathetic 37.59, body 97.60, mix 140.65, limiter 314.63, PCM 17.07.

These are diagnostic cycle measurements divided by 240, with probe overhead. Allocator contains modal/exciter/energy/sympathetic; mix contains body. Do not sum parent and child timings. Profiles are not production gate timing. Full PAN single/chord4/cluster8/rapid/release breakdowns, comparison instruments, maximum-block phases and trigger subphases are in the [phase appendix](qualification/m81/phase_appendix.md).

## 2. Fast-path hit rates

PAN cluster8 hit stable8 in all 4051 steady blocks. Its 44 event/attack callbacks used the existing attack path: 352 voice-blocks. Sustain hits: 32408 voice-blocks. Every sampled modal call used Process8: 128000 steady, 22528 attack-tail, 22528 event; scalar/Process6/Process10 counts were zero. Another Process8 implementation would duplicate the existing unrolled recurrence.

The corrected final hardware capture reports **shared_noise=44**: all 44 eligible attack callbacks used the selected optimization. Singles and chords report zero, as intended. Early selected-repeat reports incorrectly sampled this new counter only in the startup branch; regular rendering was then instrumented and PAN recaptured. Existing stable8/attack/sustain counts were valid throughout.

M8.1 also fixes historical counters that retained only the last fixture reset, samples every active-exciter block, records largest sampled class phases, and prevents legacy profile-only IDs 16–18 from editing M8's overlapping UDU fixtures. Normal production has no new diagnostic storage or counter work.

## 3. Isolated candidates tested

| Candidate | Average us | p95 us | p99 us | Maximum us | Margin us | Decision |
|---|---:|---:|---:|---:|---:|---|
| Fresh canonical control | 1678 | 1730 | 2025 | 2642 | 24.67 | Reference |
| Exact held-gain limiter log reuse | 1676 | 1730 | 2025 | 2635 | 31.67 | Reject |
| Age-bounded PAN stable8 renderer | 1568 | 1620 | 1900 | 2546 | 120.67 | Reject |
| Shared exact envelope, first run | 1670 | 1725 | 1850 | 2238 | 428.67 | Select |
| Selected 20-fixture repeat | 1669 | 1725 | 1850 | 2231 | 435.67 | Pass |
| Final explicit hit verification | 1674 | 1725 | 1850 | 2318 | 348.67 | Pass |

[Control](qualification/m81/control/hardware_manifest.json), [limiter](qualification/m81/candidate/hardware_manifest.json), [segmented renderer](qualification/m81/segments/hardware_manifest.json), [first selected run](qualification/m81/common/hardware_manifest.json), [20-fixture repeat](qualification/m81/final/hardware_manifest.json), [final PAN verification](qualification/m81/verification/hardware_manifest.json).

## 4. Rejections and historical constraints

Held-gain log reuse was exact in a 432000-sample differential test but did not materially improve PAN's maximum; its limiter frame also grew from 48 to 64 bytes. The segmented renderer improved sustain but missed the maximum gate and cost 5244 bytes of IRAM. Its first template version landed in flash; the map caught that before physical timing. Forced inlining corrected placement, but the final cost/benefit remained insufficient. Both implementations, flags and the rejected limiter test were removed; evidence remains archived.

The historically unsuccessful attack-to-sustain split was not reintroduced. The selected change processes the original attack kernel through the block and retains exact inactive transitions and age-aligned energy checks.

## 5. Selected optimization

For PAN only, after the existing eight-voice attack eligibility check, compare every exciter's sample index and noise duration. Compute `(1 - float(index) / float(noiseSamples))²` once per sample and reuse that identical float across the eight voices. This eliminates seven repeated divisions/envelope calculations while noise is active.

Each voice retains its own PRNG, filter, impulse envelope, modal recurrence, age, energy, last sample and active state. Multiplication/summation order, voice order, strike taps, sympathetic feedback and safety handling are unchanged. An active exciter cannot stop advancing before its noise ends; afterward the noise envelope is zero, so different impulse/lifetime endings remain safe. Unequal timing/duration takes the canonical path. No production fields or tables were added.

`POCKETPAN_COMMON_NOISE=1` is the default; zero retains the canonical A/B build. The mirror exciter/voice body keeps an independent reference and bounded IRAM placement. No fast-math, reciprocal approximation, musical changes, mode/voice reduction or feature disabling.

## 6. Before/after callback timing

4096 blocks per fixture, ESP32-S3 240 MHz, 48 kHz / 128 frames. Whole-fixture p99 has 25 us bins; class distributions and p95 have 5 us bins. The after columns use the final 20-fixture repeat; the additional verification maximum is explicitly included above and below.

| PAN fixture | Average us before / after | p95 us before / after | p99 us before / after | Maximum us before / after | CPU % before / after |
|---|---:|---:|---:|---:|---:|
| Single | 515 / 514 | 665 / 660 | 700 / 700 | 925 / 1091 | 19.31 / 19.27 |
| Chord4 | 1159 / 1157 | 1210 / 1210 | 1375 / 1375 | 1745 / 1860 | 43.46 / 43.38 |
| Cluster8 | 1678 / 1669 | 1730 / 1725 | 2025 / 1850 | 2642 / 2231 | 62.92 / 62.58 |
| Rapid | 728 / 728 | 895 / 910 | 975 / 1000 | 1331 / 1380 | 27.30 / 27.30 |
| Cyclic switches | 520 / 519 | 545 / 545 | 600 / 600 | 1088 / 1104 | 19.50 / 19.46 |

Latest counter verification: single average 512/max 927 us; chord4 average 1156/max 2081 us; cluster8 average 1674/max 2318 us. Single/chord/rapid averages and p99 remain broadly stable; isolated maxima vary between connected runs. Only about 1.1% of cluster callbacks are eligible attacks, so mean CPU changes little despite the large maximum improvement.

## 7. Preferred acceptance gate

Deadline: 2666.667 us. Control margin: 24.667 us. Worst selected margin: 348.667 us, recovering **324 us**. All three selected captures pass maximum <=2400 us, margin >=250 us and zero misses. Final p99: 1850 us. [Final explicit gate](qualification/m81/verification/gate.json).

## 8. Event timing and overhead

The selected code changes rendering, not dispatch. First selected cluster event callback average: 2182.50 us versus control 2508.77; attack tail 1869.27 versus 2064.32; steady 1666.05 versus 1667.62. Final event-only verification averaged 148.81 us, maximum 221 us; control averaged 144.47, maximum 151. The event-only outlier is retained rather than hidden.

The first selected maximum, 2238 us, was an eight-voice event with active exciter: inner work 2124 us, telemetry publication enabled, no queue consumption, model/reset request. UI correlation marked all 22 callbacks above 2000 us without overlapping draw/LCD markers. This does not prove a BLE/UI scheduling cause; the preferred gate is met without that fallback argument. `[OVERRUN]` entries below the deadline are records at the diagnostic 2200 us capture threshold, not deadline misses.

## 9. Target memory, IRAM, flash and heap

Production ESP32-S3 ELF/DWARF, not host sizeof:

| Measurement | Control bytes | Selected bytes | Delta |
|---|---:|---:|---:|
| IRAM text | 130971 | 132519 | +1548 |
| Internal data | 17664 | 17664 | 0 |
| Internal BSS | 96016 | 96016 | 0 |
| Flash text | 410876 | 410876 | 0 |
| Flash rodata | 132532 | 132532 | 0 |
| PSRAM BSS | 0 | 0 | 0 |
| SynthEngine | 81668 | 81668 | 0 |
| Nine packed PreparedNote owners | 60552 | 60552 | 0 |
| Internal free heap, normal connected capture | 58531 | 56995 | -1536 |
| Largest internal free block | 31744 | 31744 | 0 |

The bounded instruction growth consumes internal SRAM; M8's packed ownership/layout savings remain intact. Target map confirms the new attack voice routine and strike-bus renderer in IRAM. [Control memory](qualification/m81/control/production/memory.json), [selected memory](qualification/m81/final/production/memory.json).

## 10. Stack-frame delta

`-Werror=frame-larger-than=2048` and `-fstack-usage` remain active, extended to modal resonator and peak limiter compilation. No large automatic buffers.

| Function / maximum | Control bytes | Selected bytes |
|---|---:|---:|
| renderBlockWithStrikeBus | 112 | 128 |
| Existing attack voice routine | 48 | 48 |
| New shared-envelope attack voice routine | — | 48 |
| Modal bank processSample | 64 | 64 |
| Peak limiter processSample | 48 | 48 |
| SynthEngine renderBlock | 48 | 48 |
| Largest callback function, Vibraphone renderer | 304 | 304 |
| Largest guarded DSP frame including boot preparation | 1328 | 1328 |

The 1328-byte frame is startup `preparePackedNotes`; `ModalVoice::prepareNote` is 832 bytes. These are per-function frames, not total call-chain bounds. All five `.su` files are archived with the production ELF.

## 11. All ten frozen hashes

| Model | Aggregate FNV64 |
|---|---|
| PAN | 9e9801244165101d |
| BELL | 7c2dc6f37fb99f4b |
| TONGUE | 83f75567fb47ebaf |
| BOWL | 14772092e35bc4a7 |
| KALIMBA | e9bcabdd0b6e3bd2 |
| GLASS | 14e0dd6ba566e1cf |
| MARIMBA | 236d684f05f5f1d2 |
| VIBRAPHONE | 3cea893644aa0283 |
| MBIRA | f5ee8755af2fa270 |
| UDU | 552d8d59691b008e |

All 292 note/velocity cases per model and aggregates remain exact. [Frozen qualification](qualification/m81/final_frozen.log).

## 12. Special fixture regressions

Current-source Release/O3 host build with assertions: **16/16 passed**. Existing Vibraphone runtime, Mbira, Udu, Udu dynamic/frozen, modal/exciter, prepared-cache, parser, telemetry and fast-path regressions passed.

New differential suite: ten models × nine fixtures × three partitions, **270 cases**, comparing complete PCM and voice active/age/energy/last-sample bits. Covers single/chord4/cluster8/rapid, restrikes, pressure, NoteOff/tails, stealing and staggered attacks. Partitions: 128, 37, irregular 1/79/13/128/37. Tail/staggered cases run 576000 samples. A test-only counter proves the optimized PAN cluster executes while the reference, with attack/sustain paths disabled, records no hits. [Clean-source tests](qualification/m81/clean_host_tests.log).

## 13. Physical qualification

COM10; BLE MIDI Ready, state 8, interval 11.25 ms, latency 0. Normal redraws and LCD DMA transfers active. All **20** fixtures for PAN/VIBRAPHONE/MBIRA/UDU passed: single/chord4/cluster8/rapid/cyclic instrument switching. Zero deadlines, I2S timeout/TX/short-write faults, hard clamps, modal saturation, nonfinite samples or BLE loss. Switching visits all ten instruments and checks packed canaries. [Complete manifest](qualification/m81/final/hardware_manifest.json).

| Cluster8 model | Average us | p99 us | Maximum us | Misses |
|---|---:|---:|---:|---:|
| PAN | 1669 | 1850 | 2231 | 0 |
| VIBRAPHONE | 1117 | 1525 | 2142 | 0 |
| MBIRA | 1381 | 1475 | 1865 | 0 |
| UDU | 1048 | 1575 | 1766 | 0 |

Natural UDU deactivation remains unchanged; its steady population does not imply eight voices stay active for the whole fixture. The release profile also retains PAN's natural lifetime transitions. Final corrected-counter PAN single/chord/cluster capture passed independently.

## 14. Normal production restoration

Normal build and flash completed after the final verification. Packed caches and selected optimization enabled; phase probes, forensics, release probe, UI/audio correlation, smoke/soak experiments disabled. Original BLE, UI/display and I2S behavior, sample rate and block size retained.

Final 180-second live SMK25V2 capture (36 audio/BLE/MIDI snapshots): **291 events pushed and consumed, zero drops**, BLE Ready with no reconnects. Normal PAN audio maximum **2278 us**, zero deadlines/timeouts/TX/short writes. The user confirmed that the display updated note names and showed no deadline alert. UI urgent and telemetry redraw counters advanced. [Live MIDI capture](qualification/m81/verification/production/live_midi.log), [normal restored boot](qualification/m81/verification/production/raw.log).

Reproduce with `tests/qualify_m81.ps1`. `tests/check_m81_gate.py` checks actual callback evidence against the preferred gate. Binary SHA256, actual CMake/sdkconfig flags, raw captures, production ELF/map and stack reports are retained. The first directory called `profile` was an incomplete uninstrumented initial control, not phase evidence. Counter-report corrections are documented above; only the selected optimization remains in production code.

## 15. Remaining realtime limitation

The optimization targets synchronized eight-voice PAN attacks. Unequal timing/duration safely falls back to the canonical path. The observed preferred gate is not a universal bound for every possible MIDI stream or arbitrary scheduling stall. Modal and limiter processing remain the largest steady costs. Required connected physical fixtures retain comfortable margin, exact frozen sound and all musical/architectural constraints.
