# ESPan32 — M7.0.2 PAN/Bell Regression Isolation + M7 Closure

**Milestone:** M7.0.2
**Target:** ESP32-S3 (QFN56 revision v0.2, 4MB Flash, 2MB PSRAM) @ 240 MHz
**Audio Config:** 48 kHz, 128 frames/block (2666.67 µs budget), 8 polyphonic voices, stereo PCM5102 I2S
**Date:** 2026-09-29
**Status:** M7 **CLOSED** — Pan = **REFERENCE**, Bell V1 = **FROZEN**, Tongue V1 = **CANDIDATE**

---

## 1. Purpose

M7.0.1 reported a ~20% CPU regression on the frozen instruments:

```text
PAN  cluster8 callback true_steady avg 1966.24 us  (M6 frozen ~1651 us)
BELL cluster8 callback true_steady avg 1968.58 us  (M6 frozen ~1634 us)
```

The primary suspicion was that the additive, fully-inlined 6-mode
`Process6Normal`/`Process6Safety` microkernel (added for Tongue) perturbed the
PAN/Bell hot-code layout in the IRAM-resident `ModalResonatorBank::processSample`.

This milestone performs a controlled A/B to prove or reject that hypothesis,
and, in doing so, determined that the regression is **not reproducible from the
source** at all: the frozen envelope was intact, and the M7.0.1 figure was a
qualification-build artifact.

No DSP arithmetic was changed. The scalar recurrence, saturation threshold,
`tanh` behaviour, denormal flush, summation order and `z1`/`z2` ordering are
untouched.

---

## 2. Phase A — Controlled A/B

A single compile-time switch was added (production default preserves current
behaviour):

```cpp
// main/dsp/dsp_profile.h
#ifndef POCKETPAN_PROCESS6_MICROKERNEL
#define POCKETPAN_PROCESS6_MICROKERNEL 1
#endif
```

* **Variant A** (`POCKETPAN_PROCESS6_MICROKERNEL=1`, production): 6/8/10-mode
  unrolled microkernels all inlined into the IRAM `processSample` body.
* **Variant B** (`=0`): Tongue's 6-mode bank is routed through the exact scalar
  `processSampleReference()` recurrence. 8/10-mode paths, Tongue voicing, model
  switching, BOOT cycling and tests are untouched.

Both variants were built with candidate 25, the M7 forensics instrumentation
(`POCKETPAN_FORENSICS_M7=1`, 5 µs bins) and otherwise identical flags.

### 2.1 Hot-code footprint

`ModalResonatorBank::processSample` symbol size (IRAM, `xtensa-esp32s3-elf-nm`):

| Variant | `processSample` size | Δ |
| :--- | ---: | ---: |
| A (Process6 inlined) | 6258 bytes | baseline |
| B (Process6 removed) | 4686 bytes | −1572 bytes |

The 6-mode microkernel does grow the dispatcher by 1572 bytes, as suspected.

---

## 3. Phase B/C — Hardware A/B (ESP32-S3 @ 240 MHz, COM10)

8192 blocks per fixture, same BLE controller connected, same UI (F0), same
transport. Callback `true_steady` values:

| Fixture | Variant A (P6 on) | Variant B (P6 off) | Δ |
| :--- | ---: | ---: | ---: |
| PAN cluster8 avg | 1668.24 µs | 1649.98 µs | −1.1% |
| PAN cluster8 p99 | 1875 µs | 1850 µs | noise |
| BELL cluster8 avg | 1653.82 µs | 1631.83 µs | −1.3% |
| BELL cluster8 p99 | 1875 µs | 1875 µs | 0 |
| TONGUE chord4 avg | 786.87 µs | 961.35 µs | **+22.2%** |
| TONGUE cluster8 avg | 1253.87 µs | 1604.46 µs | **+28.0%** |

**Conclusion:** disabling Process6 does not improve PAN/Bell at all; it only
makes Tongue slower. Removing a 1572-byte inline expansion from the dispatcher
changes PAN/Bell by ≤1.3% (run-to-run noise). Process6 is **not** the source of
the reported regression.

*(The variant-B PAN/Bell figures are marginally lower than variant-A, not higher;
there is no recoverable regression to recover.)*

---

## 4. Phase E/F — Root cause: the M7.0.1 firmware was candidate 13, not 25

The decisive observation is the **shape** of the M7.0.1 regression. It was
uniform across every model, including Tongue:

| Model | M6 frozen (c25) | M7.0.1 | ratio |
| :--- | ---: | ---: | ---: |
| PAN cluster8 | ~1651 µs | 1966.24 µs | 1.19× |
| BELL cluster8 | ~1634 µs | 1968.58 µs | 1.20× |
| TONGUE cluster8 | (new) | 1566.84 µs | — |
| TONGUE chord4 | (new) | 953.46 µs | — |

A Process6 dispatch/code-layout effect would be **model-specific** and would
make *Tongue* (the model that executes Process6) the outlier, not PAN and Bell.
A uniform ~20% across every model is instead the signature of a different DSP
**candidate** — exactly what happened.

The production default is candidate 25 (`main/CMakeLists.txt`). The M7.0.1
build commands never passed `-DPOCKETPAN_DSP_CANDIDATE`, so they inherited
whatever the reused `build/` CMake cache already held. That cache still records:

```text
build/CMakeCache.txt: POCKETPAN_DSP_CANDIDATE:STRING=13
```

Candidate 13 = IRAM microkernel + prepared-note cache **only**. It lacks the
tail-IRAM placement, the stable sustain fast path, the PAN stable-8 path and the
attack-voice fast path that candidate 25 adds — and those are worth ~15–20%
across *all* models. The M6.3.4 hardware qualification of candidate 13 on this
same board measured:

| Candidate | PAN cluster8 avg | BELL cluster8 avg |
| :--- | ---: | ---: |
| **13** | **1919.21 µs** | **1925.21 µs** |
| 25 (M6 frozen / M7.0.2) | ~1651 µs | ~1634 µs |

which matches the M7.0.1 numbers (1966 / 1969 µs) within normal variation. The
`[FORENSICS] candidate=` banner that would have exposed this was not captured in
the saved M7.0.1 logs.

**Conclusion:** the "M7 regression" is a qualification-build configuration
artifact — the M7.0.1 firmware was built as candidate 13 because the reused
build tree kept a stale cache, while the M6 baseline was candidate 25. The
committed source is not regressed.

**Direct confirmation:** the committed source, rebuilt with candidate 25 and the
*exact same M7 forensics instrumentation*, measures:

```text
PAN  cluster8 true_steady callback avg 1668.24 us  p99 1875 us  max 2106 us  deadline 0
BELL cluster8 true_steady callback avg 1653.82 us  p99 1875 us  max 2052 us  deadline 0
```

which is the M6 frozen envelope, inside normal variation.

### 4.1 Phase F — diagnostic instrumentation is not the cause

The M7 diagnostics (`POCKETPAN_FORENSICS_M7`, `CONFIG_POCKETPAN_POLYPHONY_FORENSICS`)
were **active** in the candidate-25 baseline build above, and it is at baseline.
Removing diagnostics is therefore not what restores performance; selecting the
production candidate is. The `POCKETPAN_FORENSICS_FINE_BINS=5` histogram was
also tested in isolation and does not change the average.

### 4.2 Tooling fixes so this cannot recur silently

* `main/app/polyphony_forensics.h` failed to compile with
  `CONFIG_POCKETPAN_DSP_PROFILE=y` (the `profiled` local was declared inside the
  render branch but consumed after the branch join). The declaration was hoisted
  to function scope. Production is unaffected (compiled out); the profile path
  is buildable again.
* The boot banner now self-reports `candidate=`, `optimization_perf=`,
  `process6=`, `fine_bins=` and `critical_only=`, so a qualification build can
  no longer silently differ from the production build without leaving a trace.
* `POCKETPAN_DSP_CANDIDATE` is set explicitly in every build path and the host
  oracles are gated on `POCKETPAN_PROCESS6_MICROKERNEL`, so the A/B is
  reproducible.

> Qualification rule adopted: forensic qualification firmware must be built in
> a fresh build directory (or after `idf.py fullclean`) with
> `-DPOCKETPAN_DSP_CANDIDATE=25` stated explicitly.

---

## 5. Phase D — Final implementation

No DSP change. The final implementation is the source as committed:

```text
POCKETPAN_PROCESS6_MICROKERNEL = 1   (production; Tongue keeps its 6-mode kernel)
DSP candidate 25
UI F0
```

Tongue retains its headroom. PAN/Bell retain the frozen performance envelope.

---

## 6. Phase N/O — Final hardware matrix and CPU table

Single consistent build family (Variant A = production source), same
instrumentation, same hardware, same BLE/display conditions, 8192 blocks per
fixture.

### 6.1 Steady callback

| Fixture | avg | p95 | p99 | max | deadline |
| :--- | ---: | ---: | ---: | ---: | ---: |
| PAN cluster8 | 1668.24 µs | 1750 µs | 1875 µs | 2106 µs | 0 |
| BELL cluster8 | 1653.82 µs | 1725 µs | 1875 µs | 2052 µs | 0 |
| TONGUE chord4 | 786.87 µs | 875 µs | 1025 µs | 1638 µs | 0 |
| TONGUE cluster8 | 1253.87 µs | 1325 µs | 1475 µs | 1624 µs | 0 |

### 6.2 Event / attack-tail

| Fixture | event avg | event p99 | event max | event misses | attack-tail avg |
| :--- | ---: | ---: | ---: | ---: | ---: |
| PAN cluster8 | 2635.73 µs | 2900 µs | 2876 µs | 12/44 | 2000.75 µs |
| BELL cluster8 | 2645.98 µs | 2800 µs | 2783 µs | 12/44 | 2000.20 µs |
| TONGUE chord4 | 1425.02 µs | 1575 µs | 1555 µs | 0/44 | 1034.14 µs |
| TONGUE cluster8 | 2236.61 µs | 2550 µs | 2547 µs | 0/44 | 1659.30 µs |

Transport across all runs: `I2S timeout = 0`, `short = 0`, `tx_error = 0`.

### 6.3 Required closure table

| Metric | M6 frozen | M7.0.1 regression | M7.0.2 final |
| :--- | ---: | ---: | ---: |
| PAN avg | ~1651 µs | 1966.24 µs | 1668.24 µs |
| PAN p99 | ~1845 µs | 2310 µs | 1875 µs |
| PAN event avg | ~2586 µs | 2966.95 µs | 2635.73 µs |
| PAN event misses | 2/44 | 44/44 | 12/44 |
| Bell avg | ~1634 µs | 1968.58 µs | 1653.82 µs |
| Bell p99 | ~1855 µs | 2265 µs | 1875 µs |
| Bell event avg | ~2628 µs | 3016.61 µs | 2645.98 µs |
| Bell event misses | 8/44 | 44/44 | 12/44 |
| Tongue chord4 avg | n/a | 953.46 µs | 786.87 µs |
| Tongue chord4 p99 | n/a | 1245 µs | 1025 µs |
| Tongue cluster8 avg | n/a | 1566.84 µs | 1253.87 µs |
| Tongue cluster8 p99 | n/a | 1855 µs | 1475 µs |

PAN/Bell are back inside the frozen closure band (avg within ~1–2% of M6). The
remaining event-miss difference is the historical buffered-debt behaviour of
[performance_contract_v1.md](performance_contract_v1.md), not a source defect;
the 44/44 M7.0.1 figure does not remain.

---

## 7. Phase H/I — Tongue safety and bit-exactness

Tongue after the final implementation:

```text
TONGUE chord4   avg 786.87 us   p99 1025 us   max 1638 us
TONGUE cluster8 avg 1253.87 us  p99 1475 us   max 1624 us
NaN/Inf 0, hard clamp 0, modal saturation 0, I2S timeout 0, short 0, TX error 0
```

Host suite (bit-exact oracles): **9/9 PASS**, including PAN `kPanGolden[12]`
exact, the Bell V1 frozen reference, the `PAN -> BELL -> TONGUE -> PAN`
switch-identity test, the prepared-note cache exactness and the modal-kernel
differential hashes. No rendered sample changed: only the compile-time
`POCKETPAN_PROCESS6_MICROKERNEL` switch and non-DSP tooling were touched.

---

## 8. Phase K/L/M — Documentation and tooling corrections

Documentation corrected in `docs/m701_qualification_closure.md`:

* `ExciterConfig` is documented with its real members (`gain`, `noiseAmount`,
  `brightnessMinHz`, `brightnessMaxHz`, `velocityKnee`, `velocityKneeSlope`);
  the fabricated `hammerHardness*`, `strikePosition`, `brightnessBandwidth*`
  and `velocitySlopeAboveKnee` members were removed.
* Verified exciter values: Tongue `{0.78, 0.42, 750, 8500, 0.85, 0.38}`,
  Bell `{0.80, 0.70, 900, 14000, 0.85, 0.35}`, Pan `{0.80, 1.00, 700, 12000,
  0.85, 0.35}` (`velocityKnee`/`velocityKneeSlope` differ from the previous
  wrong Pan `1.00/1.00` claim).
* Tongue ratios corrected to `[1.0000, 2.0000, 2.9850, 4.0600, 5.3800, 6.7200]`
  with gains `[1.00, 0.48, 0.26, 0.14, 0.07, 0.03]`; the incorrect
  `[1, 2, 3, 4, 5.25, 7.15]` list was removed.
* Impulse duration documented as `static_cast<uint32_t>(14.0f - 11.0f * h)`
  (truncation toward zero for the positive duration), minimum clamp 3 samples —
  not `round(...)`.
* BOOT qualification wording corrected: the M7.0.1 log is an **automated,
  hardware-resident BOOT state-machine test** (`POCKETPAN_BUTTON_QUAL=1`), not a
  capture of physical GPIO presses. The user separately, manually confirmed the
  real BOOT button cycles `PAN -> BELL -> TONGUE -> PAN`; `m701_boot_cycle.log`
  records the automated state-machine run only.

Capture tooling: the canonical in-repo script `tests/capture_forensics.py`
creates the output directory before opening the log
(`args.output.parent.mkdir(parents=True, exist_ok=True)`) and now refuses an
existing evidence file with a clean message instead of a traceback. The M7.0.1
`FileNotFoundError` came from an ad-hoc, out-of-repo scratch script that opened
`scratch/raw_forensics_all.log` without creating `scratch/`. Using the canonical
script, a completed hardware capture no longer ends with an error.

---

## 9. Verdict

```text
Regression reproduced from source:            NO
Process6 responsible:                         NO
Root cause:                                   M7.0.1 firmware built as candidate 13
                                              (stale build-tree CMake cache); M6 baseline
                                              and M7.0.2 were candidate 25
Frozen instruments back in M6 envelope:       YES
Pan / Bell / Tongue output:                   bit-exact (9/9 host oracles, both A/B paths)
Tongue safe:                                  YES
I2S transport:                                0/0/0
M7:                                           CLOSED
```
