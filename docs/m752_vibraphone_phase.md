# M7.5.2 — Vibraphone tube phase and modulation refinement

VIBRAPHONE BAR = C. The bar selection is final; the motor/tube model remains provisional pending the [16-file listening comparison](listening/m752/README.md). The qualified cluster8 maximum is 2382 us, giving **284.7 us worst-case margin**, compared with M7.5.1's 2658 us/8.7 us margin: **276 us recovered**. Cluster average is 1248 us versus 1210 us historically; average CPU is slightly higher despite the improved measured maximum. All six final connected fixtures passed with zero deadline misses, I2S timeouts/errors/short writes, hard clamps, modal saturation, invalid voice counts or BLE event loss. Hardware acceptance and measurements are recorded in [the complete report](qualification/m752/hardware_report.md), including unsuccessful development captures.

## Selected C production values

These reproduce the original M7.5.1 C operations, including float multiplication rather than rounding the resulting decimal constants.

| Ratio | Preset gain | T60 before register scaling, seconds |
|---|---:|---:|
| 1x | 1 | 5.5 |
| 4x | .62 × 1.85 = 1.147 | 3 × .72 = 2.16 |
| 10x | .20 × 2.50 = .50 | 1.25 × .65 = .8125 |
| 16x | .065 | .65 |
| 22.4x | .025 | .32 |
| 29x | .010 | .16 |

Exciter: gain .16, noise .075, brightness 800–13500 Hz, velocity knee .85/slope .38, Strike shape. Hardness .10–.85 with unchanged `pow(v,1.15)` shaping. Soft coupling `[1,.80,.42,.08,.025,.008]`; hard coupling `[1,.95,.72,.40,.20,.08]`, unchanged velocity blend .20–.90. Register gain 1–.85, brightness 1.02–.94, T60 scale 1.20–.80 across 146.83–587.33 Hz. Bank normalization, safety saturation, silence threshold 1e-8, modal ratios and all other C strike behavior are retained.

## Tube signal path

```text
existing modal recurrence -> dry bar sum --------------------------+
existing mode-0 tap ------> direct fundamental sum ---- fan gain --+-> headroom/master -> limiter -> PCM
                      +--> one-state allpass -> phase sum -- fan --+
```

Only mode 0 enters the tube. No extra oscillator or duplicate Process6, body resonance, delay buffer, pitch LFO, chorus, per-voice motor, heap allocation or per-sample trigonometry is introduced.

The transposed first-order allpass is `y=a*x+s; s=x-a*y`, with `H(z)=(a+z^-1)/(1+a*z^-1)` and one state per voice. For note angular frequency `w`, `a=(tan(w/2)-tan(lag/2))/(tan(w/2)+tan(lag/2))`, yielding the specified negative lag at that note and unity magnitude. Host sinusoidal oracles independently check 30/60/110° over low through high registers. The implementation bounds the normalized coefficient-design frequency below Nyquist, with `abs(a)<1`.

Production provisionally uses 60°; M3 uses 110°. Three fixed phase experiments and two monotonic aperture curves are safety-tested, without another bar voicing matrix. There is no automatic perceptual choice.

Coefficients are generated in PreparedNote tables at startup. Out-of-table/noncanonical frequencies use the same formula at NoteOn. Model selection does not generate tube coefficients. A new voice clears its tube state; restriking retains it along with the ringing bar, and reset/kill/model switching clears it.

With shared openness `o`, added coupling `g=.75*(depth/.32)*o²`, the output is `dry + directSum*g*(1-o) + phaseSum*g*o`. Closed fans contribute zero; opening increases resonance and moves its apparent phase toward the treated signal. Thus openness changes **amplitude and phase contribution**, while the dry oscillator pitch stays fixed. M1 reproduces the old `.10+.90*o` response exactly in the host-only listening reference. All ON states retain 4.5 Hz/.32. One global motor phase advances through silence and motor OFF; NoteOn does not reset it.

Motor OFF and depth zero use the established dry renderer and contribute no tube energy. The allpass state is not advanced while bypassed. An enable after a long bypass may therefore need further listening for a tube startup transient.

## CPU changes

The Vibraphone renderer uses a sample-first stable-voice path, unrolled over voice slots 0–7. It sums dry, direct and phase-treated signals locally and applies the common fan gains once per sample. It writes one output buffer, instead of traversing bar/tube output buffers for every voice. Existing scratch buffers hold shared fan gains. Attack operations are inlined; lifetime checks and attack-to-sustain transitions retain the original sample positions. Pressure, damping transitions and stealing use a sample-first reference fallback with the same summation topology.

Fully active six-mode safety banks can bypass the general modal dispatch and invoke the **existing** Process6 safety microkernel. Its recurrence, safety handling, denormal handling and addition order are unchanged. Nyquist-pruned banks and Process6 OFF keep the original dispatch/scalar path. Optimization/reference PCM and modal state checks are exact.

Vibraphone MIDI frequency values and exact velocity powers are prepared before audio starts. PreparedNote also retains note register position. Arbitrary normalized velocities, noncanonical exciter knees and uncached notes retain the original math. Prior instruments keep their existing trigger and render behavior. No tables are rebuilt on realtime selection. PreparedNote grows from 128 to 136 bytes, adding 4672 bytes across eight 73-note tables; shared MIDI/velocity lookup storage adds 1536 bytes. Voice state/coefficient/selector storage also grows slightly. Host `sizeof(SynthEngine)` is 93400 bytes, versus 88376 in M7.5.1 (+5024); table and engine sizes are in the regression log. Shared lookup storage sits outside the engine.

## Verification and references

[Host metrics](qualification/m752/host_metrics.md), [Process6 ON CTest](qualification/m752/ctest.log), [Process6 OFF CTest](qualification/m752/p6off_ctest.log), [regression hash grid](qualification/m752/regression_hashes.log), [independent C PCM comparison](qualification/m752/reference_pcm_hashes.log).

The M7.5.1 reference was separately compiled from commit `81d054cd375117b7a8a30d98bd8f280a76e68b74`, with the historical C overrides and identical fixtures. Raw PCM is byte-identical for C motor OFF and legacy M1 in D4 v70/v110, chord4, cluster8, sparse phrase and roll. Twelve FNV64 golden oracles now assert those results before normalization. Host allpass phase/unit-magnitude checks, cached/reference triggers, fallback frequencies, pressure, retriggers, steals, all 64 model boundaries, motor partitioning/depth-zero and realtime allocation checks are retained.

All four modulation states and the phase/aperture safety experiments require zero NaN/Inf, hard clamps and modal saturation, and <.1 dB maximum limiter gain reduction. Actual limiter activity is reported, rather than using limiter GR to conceal tube reinforcement.

| Frozen instrument | Asserted aggregate FNV64 |
|---|---|
| PAN | 9e9801244165101d |
| BELL | 7c2dc6f37fb99f4b |
| TONGUE | 83f75567fb47ebaf |
| BOWL | 14772092e35bc4a7 |
| KALIMBA | e9bcabdd0b6e3bd2 |
| GLASS | 14e0dd6ba566e1cf |
| MARIMBA | 236d684f05f5f1d2 |

## Remaining perceptual question

The allpass supplies note-tracking memory and phase, but does not reproduce a tube's complete acoustic impedance or independent resonant decay. At steady state a sinusoidal fundamental plus its phase-treated counterpart still reduces mathematically to a changing fundamental amplitude/phase. Whether the onset memory and fan-dependent vector blend produce a convincing acoustic bloom requires listening. Check chord clarity and M3 cancellation; do not infer a winner from RMS, peaks or spectra. The milestone is not perceptually accepted until the listener confirms the requested bloom.

Normal PAN-default firmware was rebuilt and restored on COM10 with forensic and smoke flags OFF. [Production boot evidence](qualification/m752/production_smoke.log) and [manifest](qualification/m752/production_manifest.json) verify zero deadline/I2S faults during the restored boot. [Final source/firmware checksums](qualification/m752/source_manifest.json) bind the retained artifacts to this implementation.
