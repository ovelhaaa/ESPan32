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


## Connected scaling and phase results (before candidate selection)

The full connected baseline is preserved in
[hardware/m631_baseline_connected.log](hardware/m631_baseline_connected.log).
[hardware/m631_measurement_summary.md](hardware/m631_measurement_summary.md)
contains all curves, incremental costs and the phase report. Every baseline
fixture had zero bad-voice blocks and zero BLE-lost blocks.

| Voices | PAN sustain avg/p99/max us | BELL sustain avg/p99/max us |
|---:|---|---|
| 0 | 351.40 / 575 / 641 | 141.01 / 225 / 335 |
| 1 | 588.69 / 925 / 1165 | 412.42 / 625 / 744 |
| 2 | 998.91 / 1500 / 1767 | 869.14 / 1325 / 1575 |
| 4 | 1450.62 / 2050 / 2259 | 1406.86 / 2000 / 2186 |
| 6 | 1906.76 / 2675 / 2898 | 1944.03 / 2725 / 2926 |
| 8 | 2347.52 / 3250 / 3574 | 2471.33 / 3425 / 3639 |

Measured fixed cost is the 0-voice row. Least-squares intercept/slope over all
six points: PAN 400.38 us + 249.60 us/voice; Bell 186.48 us + 291.71 us/voice.
The intercept differs from silence because pitch, limiter activity and excitation
change across fixtures. Do not infer a universal linear voice cost. Bell 4->8
sustain adds 1064.47 us, or 266.12 us/additional voice, confirming the hypothesis.

| Workload | Modal | Voice/allocator residual | Body | Limiter/PCM | Other |
|---|---:|---:|---:|---:|---:|
| PAN cluster8 (profile build) | 52.7% | 29.1% | 3.7% | 10.3% | 4.1% |
| Bell cluster8 (profile build) | 62.6% | 23.7% | 0.4% | 9.8% | 3.5% |

These are inclusive cycle-probe measurements partitioned without double counting.
Voice residual includes allocator/sympathetic work, exciter, lifetime checks and
nested probe bookkeeping. This establishes the modal bank as the largest phase;
precise percentages should not be interpreted as overhead-free production costs.

PAN profile modal cost is about 179 us/voice/block, Bell about 221 us/voice/block:
roughly 22 us/voice/mode/block. At 128 frames this is ~0.17 us/voice/mode/sample.
The diagnostic Bell 10->9->8 measurements were 2821.48 / 2718.63 / 2527.66 us
(total instrumented sustain), corresponding to ~12.86 and 23.87 us per removed
mode per voice. Short 1024-block windows and probe effects limit precision.
PAN body-off total was 2585.72 vs 2720.89 us, a 135.17 us difference including
its allocator strike-bus path. These variants exist only in the diagnostic build.
No Bell ablation is promoted.

### Events and spikes

PAN cluster sustain: 2347.52 / 3250 / 3574 us, 1061 deadline misses. Trigger
blocks: 3580.25 / 4725 / 4709 us, 44/44 misses. Bell cluster sustain:
2471.33 / 3425 / 3639 us, 1236 misses; trigger blocks:
3751.87 / 4725 / 4721 us, 44/44 misses. Bell chord sustain:
1410.52 / 2000 / 2150 us, zero misses; triggers:
2227.38 / 3000 / 2987 us, 6 misses.

Thus new NoteOns explain part of the maximum, while sustained DSP independently
fails. Simultaneous strikes align energy checks (once per 128 voice samples),
which occur every sustain block, not just occasional spike blocks. These fixtures
have no stealing and no aftertouch/damping changes, so neither is necessary for
the observed failures. BLE stays Ready throughout. Histograms from profile and
candidate captures retain occupied bins as `[HIST]` lines. The first baseline
capture predates histogram logging and preserves its aggregate percentiles.
No scheduler/IRQ trace was captured: attributing remaining tails specifically to
BLE radio activity or UI refresh would be speculation.

### Regression policy

Each candidate is mutually exclusive via `POCKETPAN_DSP_CANDIDATE`; baseline is 0.
Host Release tests retain `-UNDEBUG`, check PAN 12/12 and model-switch PCM, Bell
velocity/stability, and newly recorded 15 modal-bank fingerprints. The new
fingerprints were captured from candidate 0, without replacing any existing
PAN golden. They exercise sparse/unsorted presets, Nyquist pruning, pitch/damping
updates, saturation and denormal handling. Bell WAVs are additionally compared
byte-for-byte with the fresh baseline outputs. Existing local reports stay intact.


## Isolated optimization decisions

[Complete candidate comparison](hardware/m631_candidate_comparison.md) follows the
requested candidate/three-fixture/deadline/PAN/IRAM format, with separate trigger
rows. Candidate manifests retain firmware SHA256, regression result and linked
IRAM section sizes. All accepted comparisons are profiling OFF, BLE Ready.
The first incomplete B capture is retained and excluded; its complete repeat is
used. A transient IDF configure failure was retried successfully before B was
measured. No incomplete build or capture is presented as PASS.

| Candidate | Experiment | Decision |
|---|---|---|
| A | block-level disabled-body dispatch | rejected: <1% effect, no clear gate improvement |
| B | cache nine target-libm headroom gains at init | rejected: <1% cluster average gain; literal constexpr table not promoted |
| C | ascending active-index traversal, including holes | rejected: PAN +2.7%, Bell +3.6% average cost |
| D | fully active fixed 8/10 kernels, ordered sum, generic fallback | retained: PAN -9.2%, Bell -11.5% average |
| E | bank, voice and allocator render functions in IRAM | retained: PAN/Bell p99 -20.8%/-21.2%; +1792 linked bytes |
| F | compact AoS, 40->28 bytes/mode | rejected: saved 960 RAM bytes, no useful CPU gain |
| G | cached safety flag and reordered guard | rejected for production: ~3% average improvement, limited tail benefit |
| H | denormal guard audit | kept original policy; existing compare/conditional move, no isolated guard benchmark or alternative promoted |
| O3 | DSP-only -O3, no fast-math | rejected: no useful cluster improvement |
| grouped | independent recurrences in groups of four, ordered sums | rejected: no useful tail improvement over D |
| D+E | fixed kernels and bounded IRAM | selected: PAN average -14.3%, p99 -27.7%; Bell average -16.0%, p99 -30.7% |

Combined critical-window sustain results: PAN cluster 2012.65/2350/2697 us,
Bell chord 1205.69/1475/1557 us, Bell cluster 2076.52/2375/2623 us.
Sustain deadline counts: 1/0/0. Trigger max: 3831/2574/3954 us, and trigger
misses: 44/0/44. This **does not meet M6.3.1 acceptance**. CPU averages for the
clusters remain above the 65% hard stop. A reduction of ~14�16% in average and
~28�31% in p99 is not labelled success or PARTIAL because PAN still fails.

Combined IRAM uses 97,279 linked bytes vs 91,647 for the scalar baseline,
including vectors and alignment sections: **+5632 bytes**. Only the modal bank
(including its fixed kernels), ModalVoice sample function and allocator render
functions are moved. Body, limiter and the whole SynthEngine are not moved.
The linker accepts the budget; the diagnostic heap remains adequate and I/O
errors stay zero. SRAM state layout remains the original 40-byte AoS.

The scalar control repeat with the same diagnostic harness returned PAN cluster
2347.33/3275/3587, Bell chord 1418.36/2025/2197, Bell cluster 2479.57/3425/3675 us,
confirming the original connected curve. Small 1�3% deltas are treated cautiously.
IRAM's tail change and D's average change are clear relative to that variability.

Production defaults to candidate 9 (D+E); 0/4/5 remain for baseline/fixed/IRAM A/B.
Rejected experiment code is removed from current source, while its individual
commits and hardware evidence remain available. No preset, gain, T60, doublet,
velocity curve, sympathetic coupling, limiter configuration, polyphony, sample
rate, or block size changed. No fast-math or new floating-point reassociation
flags were introduced. Existing PAN goldens were never updated.

## Reproduction

Use an ESP-IDF environment and a host GCC/Ninja environment. The local host
build directory is `build-host-m631`; all build directories stay ignored.

```sh
cmake -S tests -B build-host-m631 -G Ninja -DCMAKE_BUILD_TYPE=Release -DPOCKETPAN_DSP_CANDIDATE=9
cmake --build build-host-m631 --parallel
ctest --test-dir build-host-m631 --output-on-failure
```

For automatic connected forensics, set `CONFIG_POCKETPAN_POLYPHONY_FORENSICS=y`,
`CONFIG_POCKETPAN_FORENSICS_BLOCKS=8192`, qualification log on, DSP profile off.
`CONFIG_POCKETPAN_DSP_PROFILE=y` with 1024 blocks is the short diagnostic phase
run, including PAN body-off and Bell 9/8-mode measurements. Production keeps both
forensics and DSP profile OFF. Current default candidate is 9; use 0 for scalar.

```sh
idf.py -DPOCKETPAN_DSP_CANDIDATE=9 -DPOCKETPAN_FORENSICS_CRITICAL_ONLY=0 -DPOCKETPAN_FORENSICS_DISCONNECTED=0 build
idf.py -p COM10 flash
python tests/capture_forensics.py docs/hardware/new_capture.log --port COM10 --rows 32 --seconds 420
python tests/summarize_forensics.py docs/hardware/new_capture.log
```

Start capture immediately after flash. Files are opened exclusively, so an
existing log cannot be overwritten; incomplete captures return a nonzero status.
Critical-only mode uses three fixtures and six curve rows. Disconnected mode sets
`POCKETPAN_FORENSICS_DISCONNECTED=1`: the NimBLE host/radio still scan, but
advertisement matching does not establish a connection. This measures an active
BLE scan scenario, not a radio-disabled scenario. External controller power need
not change. Restore this cache flag to 0 before building normal firmware.

Historical rejected candidates use their respective commits: A `c0ff4b5`,
B `ea0694e`, C `a6ec26e`, D `180d206`, E `08993ba`, G `d03a36d`, F `91728f7`,
O3 `01699bc`, grouped `e1108a5`. Their numeric cache selections are 1/2/3/4/5/6/7/8/10,
respectively. Current code retains only 0/4/5/9. Host-only fingerprints establish
host PCM exactness; these logs do not constitute an I2S PCM null comparison on
the target or the pending human A/C listening decision.

## Final qualification and disposition

**M6.3.1 status: FAIL. Bell ablation authorized: NO. Next: further generic optimization.**
The accepted source is commit `5377b4f`, candidate 9 (D+E). The final connected
capture has all 16 fixtures / 32 event-and-sustain rows, 8192 blocks per fixture.
See [complete matrix](hardware/m631_final_matrix.md),
[raw capture](hardware/m631_candidate_final_connected.log), and
[merged distributions](hardware/m631_final_overall.json).

### Final connected results, including attacks

| Fixture | Avg us | P99 us | Max us | Deadline misses | CPU average |
|---|---:|---:|---:|---:|---:|
| PAN cluster8 | 2019.52 | 2350 | 3800 | 44 | 75.7% |
| BELL chord | 1210.51 | 1500 | 2473 | 0 | 45.4% |
| BELL cluster8 | 2082.92 | 2375 | 3515 | 44 | 78.1% |

These combine event and sustain histograms; p99 is the conservative upper edge
of a 25 us bin. Average is weighted from rounded class averages. Acceptance
requires p99 <=1733 us, max <=2133 us, zero deadlines and CPU <=65% (50%
preferred). Both clusters fail; Bell chord fails max despite passing its p99,
CPU and deadline gates. PAN cluster also fails the average <=1733 us gate.

| Fixture | Sustain avg/p99/max us | Event avg/p99/max us | Sustain/event deadlines |
|---|---|---|---|
| PAN cluster8 | 2013.02/2325/2602 | 3223.99/3825/3800 | 0/44 |
| BELL chord | 1206.22/1475/1784 | 2004.31/2475/2473 | 0/0 |
| BELL cluster8 | 2076.65/2375/2648 | 3243.82/3525/3515 | 0/44 |

### Final scaling

Sustain averages in us/block; this is the same connected fixture protocol as
the controlled baseline. Per-voice slopes are descriptive fits, not a guarantee
of linear behavior near voice activation transitions.

| Voices | PAN baseline | PAN final | BELL baseline | BELL final |
|---:|---:|---:|---:|---:|
| 0 | 351.40 | 342.71 | 141.01 | 142.44 |
| 1 | 588.69 | 534.78 | 412.42 | 360.28 |
| 2 | 998.91 | 901.11 | 869.14 | 764.72 |
| 4 | 1450.62 | 1272.22 | 1406.86 | 1202.95 |
| 6 | 1906.76 | 1643.32 | 1944.03 | 1640.79 |
| 8 | 2347.52 | 2013.02 | 2471.33 | 2076.65 |

Measured fixed cost: PAN 342.71 us, Bell 142.44 us. Fitted final per-voice
cost: PAN 208.73 us, Bell 242.15 us (baseline 249.60/291.71 us).
Final fit intercepts: 387.29/183.79 us. Diagnostic baseline modal-bank cost was
about 22 us/voice/mode/block; do not interpret that as final production cost.
The phase percentages and per-mode ablations above are probe-enabled estimates.

### Disconnected scan cross-check

[Disconnected capture](hardware/m631_final_disconnected_scan.log) keeps the
BLE host/radio scanning and suppresses connection matching. Sustain
PAN cluster: 2016.12/2325/2694 us, deadlines 1; Bell chord:
1208.09/1450/1616 us, deadlines 0; Bell cluster: 2079.40/2350/2586 us,
deadlines 0. Event maxima are 3643/2497/3703 us, event deadlines 44/0/44.
This scenario also fails. These measurements do not identify BLE/UI or IRQ as
the source of individual tails; no scheduler/IRQ trace was collected.

### Integrity, I/O, and firmware restoration

Final host Release suite: **4/4 PASS**, PAN **12/12 exact**, model switch exact,
all 15 modal fingerprints exact and Bell reference WAVs byte-identical. All
fixtures report hard/saturation counts zero, expected voice counts and no BLE
connection loss. [Xtensa recurrence audit](hardware/m631_xtensa_recurrence_audit.json)
confirms all 18 fixed-mode recurrences retain the baseline first multiply order.
This is not a target I2S PCM null comparison or a human listening approval.

Timeout / short write / TX error: **0 / 0 / 0** throughout final hardware runs.
The complete callback telemetry includes setup/reset, queue and publication
work excluded from isolated fixture NoteOn+render timing: its lifetime max is
**4084 us and deadline count 121**. These are real callback misses and must not
be hidden by the fixture's event/sustain partition.

The normal candidate-9 firmware was built and flashed on **COM10** with
forensics and DSP profiling OFF, disconnected override 0, CPU 240 MHz and
performance/O2 optimization. Production smoke reconnected BLE Ready at
11.25 ms interval and showed idle PAN avg 347–348 us, p99 450–500 us,
max 640 us, deadlines 0, I/O 0/0/0, internal free heap 177387 bytes and largest
block 73728 bytes. See [production smoke](hardware/m631_production_smoke.log).

D+E is retained because it preserves sound and gives substantial generic CPU
and tail reductions, but no success, PARTIAL acceptance, Bell freeze or Bell
ablation is declared. PAN still requires about 14% average and 26% p99 reduction
from the final overall numbers; its attack maximum needs about 44% reduction.
Continue generic modal/voice and event-path optimization with independent
hardware validation before reconsidering Bell-specific musical changes.
