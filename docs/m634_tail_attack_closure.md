# M6.3.4 — Tail latency + attack path closure

Branch: `codex/m632-modal-note-cache`. Baseline at entry: `a119560`.
Production-qualified baseline at entry: **candidate 13** (microkernel +
PreparedNote). Entry experimental candidate: **candidate 16** = 13 + stable
sustain fast path.

Hardware: ESP32-S3 (TENSTAR TS-ESP32-S3), 240 MHz, ESP-IDF 5.3, **BLE
connected** to a real BLE-MIDI central (state=8, interval 11.25 ms) for every
measurement in this pass. Forensics harness, 8192 blocks/fixture, DSP profiling
**OFF**. Raw captures under `docs/hardware/m634_*`.

---

## Commit(s)

Working tree only (no commit made; see "Integrity"). Files changed:

```text
main/dsp/dsp_profile.h        candidate matrix 16-21, tail-IRAM attributes
main/dsp/exciter.h/.cpp       exact attack/exciter fast path (candidate 21)
main/dsp/synth_engine.cpp     T1: renderBlock IRAM attribute
main/dsp/peak_limiter.cpp     T2: processSample IRAM attribute
main/dsp/body_resonator.cpp   T3: processSample IRAM attribute
main/CMakeLists.txt           default 13 -> 20; candidate list 17-21
tests/CMakeLists.txt          exciter_fastpath target; default -> 20
tests/test_exciter_fastpath.cpp  new exact differential harness
docs/m634_tail_attack_closure.md
docs/hardware/m634_*             connected evidence
```

---

## M6.3.4: PARTIAL

- Phase A (connected validation of 16) **PASS** — candidate 16 promoted.
- Phase B (tail-latency / IRAM placement) **PARTIAL** — T4 retains a reproducible
  ~6–10% steady p99 gain at ~-0.8% average and +1.2 KB IRAM, but the 8-voice
  cluster gates remain red.
- Phase C (exact attack/exciter fast path) **exact but below retention gate** —
  ~5–8% event gain, gate required >= 15%.
- Phase D (Bell mode-major) not performed (optional).

---

## Candidate 16 connected validation

Connected, 8192 blocks/fixture, BLE Ready, profiling OFF. Candidate 16 measured
once in Phase A and repeated for the tail comparison; candidate 13 measured once
connected for the true connected delta.

| Fixture | c13 connected avg/p99/max | c16 connected avg/p99/max |
|---|---|---|
| PAN cluster8 steady | 1919.21 / 2175 / 2596 | 1767.52 / 2025 / 2575 |
| BELL chord4 steady | 1123.43 / 1350 / 1683 | 974.43 / 1225 / 1385 |
| BELL cluster8 steady | 1925.21 / 2200 / 2543 | 1628.43 / 1900 / 2478 |

Connected c16 vs c13:

| Fixture | Δavg | Δp99 | Δmax |
|---|---:|---:|---:|
| PAN cluster8 | **-7.90%** | -6.90% | -0.81% |
| BELL chord4 | **-13.26%** | -9.26% | -17.71% |
| BELL cluster8 | **-15.42%** | -13.64% | -2.56% |

### Connected vs disconnected

Disconnected c16 (M6.3.3): PAN 1770/2050/2574, BELL chord 974/1225/1420,
BELL cluster 1628/1875/2444.

| Fixture | Δavg (conn-disc) | Δp99 | Δmax |
|---|---:|---:|---:|
| PAN cluster8 | -2.5 us | -25 us | +1 us |
| BELL chord4 | +0.4 us | 0 us | -35 us |
| BELL cluster8 | +0.4 us | +25 us | +34 us |

The difference is at or below the +/-25 us histogram bin, i.e. **BLE connected
does not measurably change render cost**. No attribution to BLE IRQ/cache/UI is
made without evidence; the histogram shows no connected-specific low-frequency
cluster.

### Candidate 16 promotion gate

```text
PAN exact                12/12              PASS (host)
modal in-process exact   15/15              PASS (host)
Bell reference WAV       byte-identical     PASS (host, no WAV drift)
I/O 0/0/0                timeout/short/tx   PASS
connected sustain gain   >=5% PAN/Bell cl.  PASS (-7.90% / -15.42%)
```

**Candidate 16 promoted** from candidate 13 to the connected-validated
baseline. It does not pass the final performance gates and is not the final
default (see Phase B).

---

## Tail-latency investigation

### Code placement (candidate 20 map, `pocket_pan.map`)

| Function | Section | Address | Class |
|---|---|---|---|
| `SynthEngine::renderBlock` | `.iram0.text` | 0x40377bec | IRAM |
| `VoiceAllocator::renderBlock` | `.iram0.text` | 0x403774d4 | IRAM |
| `VoiceAllocator::renderBlock(SympatheticConfig)` | `.iram0.text` | 0x403775e0 | IRAM |
| `VoiceAllocator::renderBlockWithStrikeBus` | `.iram0.text` | 0x403777e4 | IRAM |
| `ModalVoice::renderSustainBlock` | `.iram0.text` | 0x40377f9c | IRAM |
| `ModalVoice::processSampleSustain` | `.iram0.text` | 0x40377f4c | IRAM |
| `ModalVoice::processSample` | `.iram0.text` | 0x40377e20 | IRAM |
| `ModalResonatorBank::processSample` | `.iram0.text` | 0x40378028 | IRAM |
| `PeakLimiter::processSample` | `.iram0.text` | 0x40377a38 | IRAM |
| `BodyResonator::processSample` | `.iram0.text` | 0x40377434 | IRAM |
| `ModalResonatorBank::processSampleReference` | `.flash.text` | 0x4200xxxx | IROM (reference only) |
| modal micro8/10 kernels | inlined into `processSample` | — | inlined |

In candidate 16 (before Phase B) `SynthEngine::renderBlock`,
`PeakLimiter::processSample` and `BodyResonator::processSample` were emitted at
`0x4200xxxx` (flash IROM). The modal kernel, voices and allocator were already
IRAM.

### IRAM / internal memory

| Build | `.iram0.text` | `.dram0.data` | `.dram0.bss` | free internal heap | largest block |
|---|---:|---:|---:|---:|---:|
| c16 (forensics) | 97,115 | 17,664 | 118,584 | 146,531 | 43,008 |
| c20 (forensics) | 98,351 | 17,664 | 118,616 | 145,219 | 43,008 |
| c20 (production) | 98,351 | 17,664 | 39,552 | 157,115 | 53,248 |
| c21 (forensics) | 98,627 | 17,664 | 119,456 | 144,123 | 40,960 |

T4 costs **+1,236 bytes IRAM** and +32 bytes `.dram0.bss` over candidate 16.
Internal heap margin is not guessed: the production smoke reports 157,115 bytes
free with a 53,248-byte largest block.

### IRAM experiments (connected, 8192 blocks, steady)

| Candidate | placement added | PAN cl. avg/p99/max | BELL chord avg/p99/max | BELL cl. avg/p99/max |
|---|---|---|---|---|
| 16 baseline | — | 1767.52/2025/2575 | 974.43/1225/1385 | 1628.43/1900/2478 |
| 17 T1 | renderBlock | 1765.23/2000/2272 | 971.26/1175/1627 | 1625.12/1850/2315 |
| 18 T2 | limiter | 1763.02/2000/2478 | 968.70/1175/1464 | 1622.86/1825/2391 |
| 19 T3 | body | 1770.89/2050/2608 | 977.57/1250/1732 | 1630.95/1900/2333 |
| **20 T4** | all three | **1755.75/1900/2372** | **963.91/1125/1388** | **1617.53/1775/2426** |
| 20 T4 repeat | all three | 1756.02/1925/2425 | 963.88/1125/1476 | 1617.47/1775/2290 |
| 16 repeat | — | 1771.32/2100/2284 | 977.17/1250/1379 | 1630.46/1900/2308 |

T1/T2/T3 individually are within the +/-25 us bin noise (BELL chord `max` even
regresses on T1/T3; body is disabled for Bell, confirming that column is noise).
T1 shows a PAN `max` reduction but no >=5% `p99`.

**Best candidate: T4 (candidate 20).**

| Fixture | Δavg | Δp99 | Δmax | §10 keep |
|---|---:|---:|---:|---|
| PAN cluster8 | -0.67% | **-6.17%** | -7.88% | p99 >= 5% → keep |
| BELL chord4 | -1.08% | **-8.16%** | +0.22% | p99 >= 5% → keep |
| BELL cluster8 | -0.67% | **-6.58%** | -2.10% | p99 >= 5% → keep |

All three exceed the >= 5% p99 keep gate with average regression <= 2%, and T4
repeats within one histogram bin. T1/T2/T3 alone do not meet §10/§11 and are not
individually retained; T4 is the accepted combination.

### Histogram analysis (§12)

PAN cluster8 steady, 8148 samples, 25 us bins:

| bin | c16 n | c20 n |
|---:|---:|---:|
| 1725 | 5291 | 5236 |
| 1750 | 1437 | 1716 |
| 1775 | 293 | 255 |
| 1800 | 110 | 395 |
| 1825 | 35 | 311 |
| 1850 | 53 | 109 |
| 1875 | 153 | 45 |
| 1900 | 232 | 24 |
| 1925 | 209 | 10 |
| 1950 | 149 | 3 |
| 1975 | 81 | 0 |
| 2000 | 35 | 0 |
| 2025 | 12 | 0 |
| 2050 | 1 | 0 |
| 2100 | 4 | 0 |
| 2150 | 15 | 13 |
| 2175 | 21 | 16 |
| 2375 | 1 | 0 |
| 2475 | 2 | 1 |
| 2575 | 1 | 0 |

The improvement is **both** a whole-distribution shift (the 1850–2050 mass moves
down into 1750–1850) **and** rare-tail clipping (the isolated 2375–2575 spikes
disappear). A residual secondary cluster at 2150–2350 remains in both, which is
the ~10 Hz cache/scheduler tail described in §13 and is not addressed by IRAM
placement.

### Scheduler / IRQ evidence (§13)

The residual 2150–2350 us cluster (≈0.2% of blocks) survives on every candidate,
including the fully-IRAM c20. It is not correlated with the modal bank (which is
already IRAM), so the most likely sources are BLE controller bursts on core 1,
display refresh and FreeRTOS scheduling. This pass did **not** add permanent
production instrumentation; the histogram/`[CURVE]` forensics build is the
diagnostic, and no profiling build was used for acceptance (§14: every gate
below is production-equivalent firmware, profiling OFF).

---

## Attack profiling

Corrected candidate matrix (all additive to candidate 13):

```text
13  microkernel + PreparedNote (M6.3.2 baseline)
14  13 + sustain fast path
15  13 + sustain (historical; identical to 14/16 in this pass)
16  13 + stable sustain fast path            <- Phase A production baseline
17  16 + SynthEngine::renderBlock IRAM       (T1)
18  16 + PeakLimiter::processSample IRAM     (T2)
19  16 + BodyResonator::processSample IRAM   (T3)
20  16 + accepted T4 tail-IRAM combination   <- new default
21  20 + exact attack/exciter fast path
```

Event blocks, connected, 8192 blocks (steady sustain shown for reference):

| Fixture | c20 event avg/p99/max | c21 event avg/p99/max | c21 vs c20 |
|---|---|---|---|
| PAN cluster8 | 2869.36 / 3150 / 3132 | 2687.45 / 2950 / 2925 | -5.8% / -4.8% / -5.0% |
| BELL chord4 | 1731.53 / 2050 / 2034 | 1594.85 / 1900 / 1888 | -7.6% / -7.3% / -7.2% |
| BELL cluster8 | 2828.45 / 3150 / 3140 | 2694.56 / 3000 / 2991 | -4.9% / -4.8% / -4.6% |

Attack vs stable sustain budget (c20): PAN cluster event 2869 - steady 1756 =
**1113 us** attack-specific; BELL chord 1732 - 964 = 768 us; BELL cluster 2828 -
1618 = 1210 us. The exciter fast path recovers ~160–210 us of that, i.e. the
attack budget is **not** dominated by the exciter per-sample math.

---

## Exciter fast path

### Implementation (candidate 21, `POCKETPAN_ATTACK_FASTPATH`)

- `Exciter::processSample()` is inlined into the IRAM-resident
  `ModalVoice::processSample`, folding the PRNG, one-pole filter and envelope
  into the voice kernel.
- The half-sine window is a per-strike-length lookup table built once at `init`
  from the **same float expression** as the reference (`kExciterPi`), so it is
  bit-identical; only `window` is precomputed, never `window * norm` (§22).
- `impulseNorm_` is hoisted; the association stays
  `(strikeAmplitude_ * window) * impulseNorm_` (§17).
- The noise envelope is unchanged: `1.0f - i / noiseSamples_`, and the product
  stays `(filterState_ * noiseGain_) * (env * env)` (§18/§24).
- The PRNG is the identical xorshift/convert sequence with no skipped calls.
- The `active_` transition sample is unchanged (`sampleIndex_ >= noiseSamples_
  && sampleIndex_ >= impulseSamples_`), so the age-aligned lifetime check reads
  the exact `exciter_.isActive()` value (§21).
- Segmentation is structural rather than an explicit mode variable: the impulse
  window is bounded to <= 14 samples and the whole body is inlined and hoisted,
  so the two historical range tests are folded into the caller. No generic
  per-sample dispatch remains, and the state advanced per sample is identical.

Candidate 16/20 stay binary-identical: the attack block is compiled only for
candidate 21, and the tail macros are empty on host.

### Exactness

`tests/test_exciter_fastpath.cpp` (new) renders the fast path and the historical
reference on independent state copies and requires:

```text
sample-for-sample float bit equality over the whole strike
active_ trace equality
rngState_, filterState_, sampleIndex_ equality at the end
```

Coverage: velocities 30/70/110/127, hardness {0, .25, .5, .75, 1} plus a dense
0..1 sweep, and all 12 supported impulse lengths (3..14). Result: **exact**.

Full engine: `test_dsp` PAN `kPanGolden` **12/12 exact** with candidate 21, and
the generated WAV set (including every Bell reference) is **byte-identical**
(no `git status` WAV drift). `test_modal_kernel` 15/15.

### PRNG exactness (§25)

After the full strike the reference and fast `rngState_` are equal for every
velocity/hardness combination. No PRNG call is skipped or reordered.

### Event delta

| Model/fixture | c20 event avg | c21 event avg | Δavg | §28 gate |
|---|---:|---:|---:|---|
| PAN 8-note | 2852 | 2687 | **-5.8%** | 15% — not met |
| BELL 4-note | 1726 | 1595 | -7.6% | 15% — not met |
| BELL 8-note | 2834 | 2695 | **-4.9%** | 15% — not met |

The exactness is complete, but the >= 15% retention gate is not met, so the
attack fast path is **not promoted for performance**. It is left in place behind
candidate 21 as an exact, shippable building block. It also reduces the
fixture's >2666 us event-block count from 44 to 20 (PAN) and 44 to 18 (BELL
cluster).

---

## Bell mode-major

```text
tested / not needed:  not performed (optional)
```

Phase D was authorized to test (16 accepted and Bell cluster p99 fails), but it
is explicitly optional and only touches Bell. PAN cluster remains the blocking
fixture, so a Bell-only change cannot change the M6.3.4 outcome. No Bell mode
ablation or reordering was attempted.

---

## Final qualification (candidate 20, default)

Connected, BLE Ready, forensics, 8192 blocks, profiling OFF.

### Final PAN cluster

```text
steady  avg 1755.75  p95 ~1850  p99 1900  max 2372
event   avg 2869.36  p99 3150  max 3132
CPU     ~61% (whole-callback forensics window; <=65% hard)
```

### Final Bell chord

```text
steady  avg 963.91  p95 ~1050  p99 1125  max 1388
event   avg 1731.53 p99 2050  max 2034
```

### Final Bell cluster

```text
steady  avg 1617.53  p95 ~1700  p99 1775  max 2426
event   avg 2828.45  p99 3150  max 3140
```

### Whole callback

Production firmware (candidate 20, forensics OFF, BLE connected, idle):

```text
avg 352 us  p99 425 us  max 588 us  cpu 13.2%  deadline 0
```

All six probe phases report `hard=0`, `sat=0`, `bad_voices=0`, `ble_lost=0`. The
forensics `[AUDIO]` deadline counter in the fixture build accumulates the
diagnostic reset blocks; the production-equivalent idle measurement is
`deadline = 0`.

### I/O

```text
timeout 0   short 0   tx_error 0   (all candidates, all captures)
```

### Gates

| Gate | Required | c20 | Result |
|---|---|---|---|
| PAN cluster sustain avg | <= 1733 | 1755.75 | **FAIL** |
| PAN cluster sustain p99 | <= 1733 | 1900 | **FAIL** |
| PAN cluster sustain max | <= 2133 | 2372 | **FAIL** |
| BELL chord sustain avg/p99/max | <= 1733/1733/2133 | 964/1125/1388 | PASS |
| BELL cluster sustain avg | <= 1733 | 1617.53 | PASS |
| BELL cluster sustain p99 | <= 1733 | 1775 | **FAIL** |
| BELL cluster sustain max | <= 2133 | 2426 | **FAIL** |
| CPU (whole callback) | <= 65% | ~61% | PASS |
| deadlineMisses (production idle) | 0 | 0 | PASS |
| I/O | 0/0/0 | 0/0/0 | PASS |
| PAN event max | <= 2133 | 3132 | **FAIL** |
| BELL chord event max | <= 2133 | 2034 | PASS |

Engineering-margin preference (avg <= 1650): PAN cluster 1755.75, Bell cluster
1617.53 (Bell passes its average margin, PAN narrowly misses).

---

## Regression gates

```text
Host CI        PASS  7/7 (all candidates tested)
ESP-IDF        PASS  host + production + all 6 forensics builds link
PAN            12/12 exact
Modal kernel   15/15 in-process exact
Bell reference byte-identical (no WAV drift in git status)
Model switch   PAN -> BELL -> PAN exact
PreparedNote   grid exact (unchanged)
Sustain fast   fast-on vs fast-off exact (test_fastpath)
Attack fast    optimized vs reference exact (test_exciter_fastpath)
```

---

## Production candidate

Candidate **20** is the new default (`13 -> 16` promoted in Phase A, then
`16 -> 20` in Phase B). It is exact, connected-verified, I/O 0/0/0, and keeps a
reproducible ~6–10% steady p99 improvement over candidate 16 at ~-0.8% average
and +1.2 KB IRAM. Candidate 16 remains selectable as the Phase A
connected-validated baseline; candidate 21 (attack) remains selectable as an
exact but performance-unqualified path.

## Bell ablation authorized: NO

PAN cluster still fails (§54 requires PAN PASS and Bell FAIL). No Bell ablation
was performed.

## Next

```text
further generic optimization   <- PAN cluster is the only remaining blocker
Bell freeze                    (listening decision, unchanged)
Bell ablation                  NOT authorized
```

The tail-latency avenue is exhausted for placement: the residual cluster
p99/max is the ~10 Hz system tail (2150–2350 us), and every DSP hot function
already lives in IRAM. The next generic step should target the modal-bank /
allocator steady cost that still puts PAN cluster average 23 us above the gate,
not more IRAM.

---

## Integrity

- No preset, modal ratio, gain, T60, doublet, velocity curve, sympathetic gain,
  body voicing, limiter setting, polyphony (8), sample rate (48 kHz) or block
  size (128) changed.
- No `-ffast-math`, no golden regeneration, no unsafe reassociation, no Bell
  mode ablation, no adaptive quality, no reduced polyphony, no sample-rate or
  block-size change.
- Candidate 16 output is unchanged; candidate 13 remains selectable and
  unchanged. The new attack path is bit-exact by the differential harness and
  byte-identical at engine level.

## Reproduction

```sh
cmake -S tests -B build-host-m634 -G Ninja -DCMAKE_BUILD_TYPE=Release -DPOCKETPAN_DSP_CANDIDATE=20
cmake --build build-host-m634 --parallel && ctest --test-dir build-host-m634 --output-on-failure

# Firmware (BLE connected, forensics critical fixtures, profiling OFF):
idf.py -B build-m634-tail -DSDKCONFIG=build-m634-tail/sdkconfig \
  -DPOCKETPAN_DSP_CANDIDATE=20 -DPOCKETPAN_FORENSICS_CRITICAL_ONLY=1 build
idf.py -B build-m634-tail -p COM10 flash
python tests/capture_forensics.py docs/hardware/m634_c20_tailT4_connected.log \
  --port COM10 --rows 6 --seconds 180
```
