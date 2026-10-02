# M7.7.1 — UDU Dynamic Hole Morph

**UDU V1 FROZEN** in [M7.7.2](m772_udu_v1_freeze.md): Character **B**,
**Centered Dynamic Opening**, **R1 Restrike**. The listening decision is final.
This document retains the M7.7.1 implementation and historical comparison evidence.

## Architecture and velocity formula

The existing UDU-specific 49-pot cache keeps the original six-mode coefficient
sets. Two additional 24-byte air-only endpoint sets per pot hold CLOSED and
OPEN. PARTIAL reuses the original coefficients exactly. At NoteOn, six
coefficient values are interpolated into the voice's two-air-mode state.
Four ceramic modes continue using the original shared cache coefficients.

Normalize MIDI velocity as `v = velocity / 127`. For the frozen centered
curve:

```cpp
if (v <= .3) opening = .5 * v;
else if (v <= .6) opening = .15 + (v - .3) * (.35 / .3);
else opening = .5 + (v - .6) * 1.25;
```

This passes through (0,0), (.3,.15), (.6,.5), (.8,.75), (1,1).
The retained host candidates are linear `opening=v` and smoothstep
`opening=v*v*(3-2*v)`. There is no UI, CC, aftertouch, MPE or persisted opening
control. Pressure still uses the original damping path.

## Cavity interpolation

Endpoint coefficients use frequency multipliers .72 / 1 / 1.18 and T60
multipliers 1.30 / 1 / .72 for CLOSED / PARTIAL / OPEN. Each of `a1`, `a2` and
`injection` is interpolated between CLOSED→PARTIAL for opening below .5 and
PARTIAL→OPEN above .5. The segment fraction is `2*opening` or
`2*(opening-.5)`. Exact knots return their stored values directly.

Coefficient interpolation gives a slightly curved frequency/decay trajectory,
rather than claiming literal linear Hz or T60 interpolation. The reported
effective poles are measured from `r=sqrt(-a2)`,
`Hz=acos(a1/(2*r))*fs/(2*pi)`, `T60=-ln(1000)/(fs*ln(r))` in the host analyzer.
The analyzer's trig/log operations never run in firmware events or rendering.
All 49 pot sizes × 128 velocities × both air modes pass strict increasing
frequency, decreasing T60 and second-order stability assertions.

| MIDI velocity | Opening | Effective frequency × base | Effective T60 × base |
|---:|---:|---:|---:|
| 30 | .118110 | .795023 | 1.213991 |
| 70 | .443045 | .972128 | 1.026937 |
| 110 | .832677 | 1.123095 | .794494 |
| 127 | 1.000000 | 1.179912 | .720040 |

Values above are from the MIDI 60 main cavity's float recurrence coefficients;
small endpoint differences are float precision, including the original PARTIAL
pole. Base B remains 105 Hz / .42 s, secondary ×2.31 / ×.29. Shell
frequencies, weights and T60, hand pulse, output gain, velocity drive laws,
compressed MIDI 36–84 size mapping and out-of-range clamps remain unchanged.

## Strike and restrike state

A newly allocated or cleared voice accepts velocity-derived opening. R1 keeps
its current cavity coefficients on a live restrike until the sum of squared
states of the two cavity modes is below 1e-9. This uses the established silence
threshold, checked only on a strike. A quiet cavity accepts the new opening
without clearing its negligible retained state. Excitation velocity always
updates, including cavity drive, shell drive and the transient.

R2 is retained as a host qualification candidate: update the poles immediately
while preserving resonator state. It passes nonfinite, clamp, limiter and
partition checks, but an abrupt change in a ringing cavity's frequency remains
an audible risk requiring listening. R1 is the conservative default, with the
explicit tradeoff that rapid same-pot gestures can inherit an earlier opening.
There is no automatic selection of R2 based on its numerical results.

All endpoint trig/exp work occurs in startup/cache preparation. Neither NoteOn
nor the sample loop calls trig/exp or allocates. The six sample recurrences keep
their original summation order; dynamic interpolation has no per-sample branch.
Model switching selects ready state, and allocator/steal integration is unchanged.

## Memory

| Structure | M7.7 | M7.7.1 | Increase |
|---|---:|---:|---:|
| UduCache | 3576 | 5928 | 2352 bytes |
| UduVoice, host | 104 | 136 | 32 bytes |
| UduVoice, device | 96 | 124 | 28 bytes |
| SynthEngine, host | 105624 | 108232 | 2608 bytes |
| SynthEngine, device | 105272 | 107848 | 2576 bytes |
| PreparedNote | 132 | 132 | 0 |
| Nine PreparedNote tables | 86760 | 86760 | 0 |

The device delta is 2352 cache bytes + 8 × 28 voice bytes = **2576 bytes**.
The two new config enums occupy existing padding. No global PreparedNote
enlargement or heap allocation is introduced. Diagnostic event timing adds
24 bytes per fixture record; those records exist only in forensic firmware.
Connected production memory and the direct M7.7 comparison are retained in
[completion.json](qualification/m771/completion.json).

## Host safety and exact regressions

Both Process6 configurations pass all **13 suites**. Sixty-one focused raw
fixtures cover the three curves, both restrike policies, MIDI boundaries,
v30/70/110/127, simultaneous chord4/cluster8, 20 ms alternating retriggers,
pressure, steals, partitioning, fast/reference paths and RT allocation guards.
Every fixture has zero NaN/Inf, hard clamps, resonator faults and limiter GR.
Maximum raw peak is .3357663155; maximum absolute phrase DC is 3.079632e-5.
Per-fixture values are in [host_metrics.csv](listening/m771/host_metrics.csv).

| MIDI 60 velocity | Raw peak | Raw RMS | Crest | DC |
|---:|---:|---:|---:|---:|
| 30 | .0177017022 | .0008460402 | 20.9230 | 6.17e-12 |
| 70 | .0424466319 | .0017226297 | 24.6406 | 3.20e-12 |
| 110 | .0745202824 | .0023761689 | 31.3615 | -2.23e-13 |
| 127 | .0964745060 | .0026886010 | 35.8828 | -8.02e-12 |

The fixed B/PARTIAL groove has raw PCM FNV64 **1a768c0057636a2d**, asserted
in the host test. With the original M7.7 opening-group gain it matches the
existing PARTIAL WAV byte for byte, SHA256
`41eab006973fad719f9960a110d45621526ffc7c2684c6120762432b53b12507`.
The new fixed/dynamic comparison pair uses a newly matched common gain.

All nine frozen model aggregate hashes remain exact: PAN, BELL, TONGUE, BOWL,
KALIMBA, GLASS, MARIMBA, VIBRAPHONE and MBIRA. All seven frozen MBIRA B + Buzz ON
fixture hashes also remain exact. MBIRA source and settings were not modified.
See [regression hashes](qualification/m771/regression_hashes.log),
[MBIRA fixtures](qualification/m771/mbira_fixtures.log),
[Process6 ON](qualification/m771/host_p6on.log), and
[Process6 OFF](qualification/m771/host_p6off.log).

## Listening pack and remaining decision

The [focused pack](listening/m771/README.md) contains the nine requested WAVs,
RMS-matched in three independent groups, plus two focused R1/R2 restrike probes
in their own group. It preserves the exact fixed-PARTIAL signal path and the
original M7.7 reference. No UDU A/B/C pack was regenerated.

The technical evidence establishes deeper/longer cavity poles on soft fresh
strikes and higher/shorter poles on hard strikes. It does not decide whether
ghosts sound natural, accents project, the groove feels more physical, motion
resembles hand/hole technique, hard hits sound overly tuned, or medium hits
retain the desired B character. The user finalized that listening decision in
M7.7.2: B + CENTERED + R1. The frozen hash and fixtures are recorded there.

## Physical qualification

Final physical results, event timing, fixed-PARTIAL timing control and restored
production memory are generated from retained ESP32-S3 / COM10 logs in
[completion.json](qualification/m771/completion.json). The final report is
appended below after checking every fixture and restoring production.

The initial `development_batch0` capture is retained separately. It had zero
deadline/I2S/nonfinite/clamp faults but an outdated diagnostic voice-count
assertion expected all OPEN tails to remain alive for the original fixed-PARTIAL
interval. The final oracle allows natural UDU expiry in steady tails and still
requires exact counts on event/attack blocks. No allocator or DSP lifetime
threshold was changed to satisfy that diagnostic check.

<!-- M771_PHYSICAL_REPORT -->

Physical evidence: ESP32-S3 on COM10, BLE MIDI ready at 11.25 ms throughout, 240 MHz, 48 kHz / 128 frames, live UI/I2S. Each fixture has 8192 blocks. Event-only timing includes dispatch/allocator/strike and model switching, and excludes render; full callbacks include all audio callback work. Means combine all four recorded callback classes (8191–8192 samples per fixture); the existing recorder may credit a boundary callback to the following fixture. Max/deadline/I2S counters cover all 8192. CPU % is this whole-fixture mean divided by the 2666.7 µs budget. The original FIXTURE_IO avg/cpu fields are the final 100-block window, preserved separately in completion.json and the raw manifest.

| Fixture | Avg µs | p95 µs | p99 µs | Max µs | CPU % | Event avg / max µs |
|---|---:|---:|---:|---:|---:|---|
| single v127 | 270.89 | 315 | 400 | 782 | 10.16 | 112.94 / 124 |
| chord4 | 629.3 | 750 | 950 | 1568 | 23.6 | 128.58 / 262 |
| cluster8 | 1092.68 | 1295 | 1575 | 2088 | 40.98 | 140.04 / 234 |
| dynamic groove | 443.66 | 570 | 800 | 1305 | 16.64 | 121.29 / 258 |
| model switching | 278.79 | 330 | 425 | 1028 | 10.45 | 561.41 / 604 |
| rapid retrigger / restrike | 310.84 | 375 | 450 | 779 | 11.66 | 42.07 / 144 |

All six dynamic fixtures: deadline misses = 0; I2S timeout/error/short = 0/0/0; hard clamps = 0; nonfinite = 0; resonator faults = 0; BLE losses = 0; invalid event/attack voice counts = 0. p95 uses the upper edge of the 5 µs full-callback histogram; p99 uses the existing 75 µs AudioStats histogram.
Cluster8 margin is 578.7 µs. Rapid retrigger repeats MIDI 60 every eight blocks (21.33 ms), alternating v30/v127, exercising retained cavity state and new excitation.

| Fixture | Fixed PARTIAL avg µs | Dynamic avg µs | Delta µs |
|---|---:|---:|---:|
| single v127 | 272.02 | 270.89 | -1.13 |
| chord4 | 627.87 | 629.30 | +1.43 |
| cluster8 | 1094.35 | 1092.68 | -1.67 |
| dynamic groove | 442.32 | 443.66 | +1.34 |
| model switching | 277.74 | 278.79 | +1.06 |
| rapid retrigger / restrike | 308.38 | 310.84 | +2.45 |

This fresh control uses the same implementation with opening fixed at PARTIAL during startup. The render loop adds zero arithmetic and no interpolation, coefficient motion, trig or exp. Aggregate fixture cost also depends on the chosen cavity decay: CLOSED tails can keep voices active longer than fixed PARTIAL. The host mirror of the repeating device groove counts 2,542,080 dynamic voice-frames versus 2,501,376 fixed (+1.63%); this is host occupancy evidence, not simulated hardware timing. The table also includes physical scheduling variance. A structural zero sample-loop cost does not imply identical aggregate groove CPU.
The literal <10 µs average delta across all six fixtures is met; this target is reported separately from the required zero-fault/deadline gates. The comparative model/groove results and remaining listening choice must be considered before declaring the entire musical milestone accepted. In particular, the final 100-block groove window reads 467 versus 430 µs, while the complete callback mean is 443.66 versus 442.32 µs. The window difference is not a whole-fixture average regression. The normal-render inner class means also differ only +3.00 µs steady / -1.12 µs attack for this groove; event-only costs are reported independently above.

Restored normal production boots PAN and reconnects BLE MIDI. Minimum internal free SRAM is **37715 bytes** (M7.7: 40303, delta -2588); largest free internal block is **20480 bytes** (M7.7: 23552, delta -3072). Both clear the strict 32 KiB / 16 KiB guardrails. Forensics, BOOT injection and the fixed timing control are disabled. The retained production binary matches the flashed build by SHA256; the final connected production samples have zero deadline and I2S faults.

[Dynamic hardware manifest](qualification/m771/hardware_manifest.json), [fixed control](qualification/m771/fixed_control/hardware_manifest.json), [production log](qualification/m771/production_raw.log), [completion and memory](qualification/m771/completion.json).
