# M6.3.1 — Polyphony hot-path forensics

## Baseline consolidation

M6.3 is committed locally as `918e039` (parent `974cadd`), including the
audio-core reset of `deadlineMisses`. No existing hardware report was replaced.
The requested host command encountered an existing NMake cache without nmake;
the same Release suite was configured in `build-host-m631` with Ninja/GCC 14.2.
All 3 tests passed with assertions enabled. PAN is 12/12 exact and
PAN → BELL → PAN is exact (`0x625f551231401141`).
`idf.py set-target esp32s3` followed by `idf.py build` passed on ESP-IDF 5.3,
GCC 13.2. Effective sdkconfig: performance optimization enabled, CPU 240 MHz.

## Previously captured hardware

| Fixture | BLE | Avg us | P99 us | Max us | Deadline | IO timeout/short/TX |
|---|---|---:|---:|---:|---:|---|
| PAN cluster8 | connected | 2342 | 3150 | 4109 | 1461 | 0/0/0 |
| BELL chord | disconnected | 1431 | 2000 | 2800 | 1 | 0/0/0 |
| BELL cluster8 | disconnected | 2495 | >=3175 | 4534 | 1015 | 0/0/0 |

Source: `hardware_m63_metrics.md`. The 153 us Bell-minus-PAN difference is
suggestive, but comes from different BLE scenarios and is not a controlled A/B.
PAN already fails: investigate the generic engine before Bell mode ablation.
The original histogram's last bin is an overflow bin, not an exact percentile.

## Measurement protocol

`CONFIG_POCKETPAN_POLYPHONY_FORENSICS` defaults off. It waits for BLE Ready,
then owns MIDI synthesis while the ordinary UI, BLE stack and I2S remain active.
External MIDI is drained for telemetry without influencing the diagnostic fixture.
The fixture runs on the existing audio core, with 48 kHz / 128 frames / 8 voices.
PAN/Bell voicing, safety saturation, denormal guard and limiter are unchanged.

For each model, fixtures 0–5 use 0/1/2/4/6/8 distinct notes. Fixture 6 is the
host chord D3/A3/D4/A4 v90; fixture 7 is D4 v90 roll, every 38 blocks (~101 ms).
Cluster pitches match the host fixture: 50/52/54/56/57/59/61/62 v100.
Single is D4 v90. Other scaling points are prefixes of that cluster.
This estimates workload scaling, not a pitch-independent per-voice constant.

8192 blocks (~21.85 s) are measured per fixture. Scaling/chord fixtures reset
every 188 blocks (~501 ms), then trigger distinct notes. This keeps the requested
number of voices ringing. All measured blocks check actual active count and BLE
readiness. Reset/model-switch cost is excluded from trigger timing; the NoteOns
and render are included. Sustain blocks contain no injected MIDI events.
Roll is not reset within its fixture. `bad_voices` and `ble_lost` must both be 0.

The outer timer and internal probes use `esp_cpu_get_cycle_count()`; unsigned
32-bit subtraction handles counter wrap for intervals shorter than ~17.9 s.
Conversion uses the verified fixed 240 MHz clock outside sample loops.
New percentiles use 512 bins of 25 us and report the conservative upper edge.
The last bin is overflow (>=12775 us); inspect max before interpreting it.
Counts, averages, maxima and deadline misses are separate for event and sustain.
`[AUDIO]` additionally retains the existing whole-callback / transport counters.

`CONFIG_POCKETPAN_DSP_PROFILE` defaults off and its probes preprocess away.
When on, every 32nd block is microinstrumented. This perturbs measured timing;
profile builds are diagnostic, never used to accept performance gates. Phase IDs:

| ID | Phase | Inclusion |
|---:|---|---|
| 0 | allocator | voice rendering, sympathetic and steal tails |
| 1 | modal bank | nested within allocator |
| 2 | exciter | nested within allocator |
| 3 | damping smoothing | nested within allocator |
| 4 | energy/lifetime check | nested within allocator |
| 5 | body | nested within mixing |
| 6 | mixing/headroom | body plus headroom smoothing and mix |
| 7 | limiter/output guards | finite checks, peaks and limiter |
| 8 | PCM | clamp, conversion and stereo duplication |

Subtract nested phases before calculating percentages; never sum inclusive
allocator and modal times. Probe overhead and interrupt time remain in these
diagnostic cycle counts. Compare production candidates with profiling OFF.

## Source and compiler audit

The modal loop maintains ascending preset index, skips inactive modes, preserves
the recurrence and sum order, and checks saturation and denormals per mode.
PAN has 8 modes; Bell has 10. Active modes can be noncontiguous for future
presets, so a fast path must have a general fallback rather than infer a prefix
from mode count alone. Pitch/damping changes can change the active set.

`ModalModeState` currently occupies 40 bytes (9 floats and a bool plus padding).
Only a1/a2/excitationGain/z1/z2 and active are read in the sample loop.
ratio/t60/detune are redundant runtime metadata; modalAmplitude is used during
restrikes. A structural layout experiment is deferred until safer candidates
have been measured.

ESP32-S3 disassembly confirms hardware floating-point recurrence instructions,
active and safety branches, and a conditional denormal move. The compiler's
existing default contraction emits `madd.s`; this milestone does not change
contraction flags, reassociate sums, add fast-math, or update golden hashes.

## Gates and authorization

All relevant connected fixtures require p99 <=1733 us, max <=2133 us,
deadline=0, and timeout/short/TX=0. Average target <=50% CPU, hard stop <=65%.
PAN cluster average must also be <=1733 us. No gate is relaxed.
Bell ablation remains unauthorized until PAN cluster passes with generic
optimization while Bell cluster still fails. Listening A/C and Bell freeze remain
separate pending decisions.
