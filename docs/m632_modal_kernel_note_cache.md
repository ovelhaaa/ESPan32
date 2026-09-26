# M6.3.2 — Xtensa modal kernel and PreparedNote cache

Baseline: M6.3.1 commit `5377b4f`, candidate 9 = D (fixed 8/10-mode kernels) + E
(bounded IRAM). Nothing from M6.3.1 was discarded. The branch is
`codex/m632-modal-note-cache`.

Two independent optimization families were implemented and hardware-qualified:

- **A — modal microkernel**: a fixed, fully-active 8/10-mode recurrence kernel
  written in C++ but shaped for Xtensa (no per-mode metadata, no virtual
  dispatch, safety variant resolved when coefficients change).
- **B — PreparedNote cache**: the deterministic, initial-undamped modal
  coefficients for each MIDI note are computed once at boot instead of during
  the realtime NoteOn path.

Candidate switch (all mutually exclusive through `POCKETPAN_DSP_CANDIDATE`):

| id | composition |
|---:|---|
| 0 | scalar historical baseline |
| 4 | D fixed kernels only |
| 5 | E bounded IRAM only |
| 9 | D + E (M6.3.1 production) |
| 11 | E + A microkernel |
| 12 | D + E + B PreparedNote |
| 13 | E + A + B (new default) |

`POCKETPAN_DSP_CANDIDATE` now defaults to 13. The 0/4/5/9 switches remain for
controlled A/B and rollback. No abandoned candidate code was left in the hot
path; the earlier out-of-line microkernel copy was replaced, not accumulated.

## 1. Baseline freeze (host, Release, assertions enabled)

`ctest` on candidates 9, 11, 12 and 13: **5/5 tests passed** each, including the
new `prepared_note` test. For every candidate: PAN **12/12 exact**, PAN → BELL →
PAN exact, **15/15 modal-bank fingerprints exact**, Bell reference WAVs
byte-identical, `hardClamp=0`, modal saturation `sat=0`. Firmware links cleanly
on ESP-IDF 5.3 (XCC 13.2, 240 MHz, performance/O2).

## 2. Part A — Xtensa modal microkernel

### 2.1 First attempt and the regression it exposed

The first C++ microkernel (`processSampleMicro8/10`) routed through a
`switch (microKernel_)` and out-of-line template copies. Its IRAM total was
**93,439 bytes**, i.e. 3,840 bytes *less* than baseline. Disassembly showed why:
the micro kernels were emitted as weak symbols at `0x4200xxxx` (flash IROM), not
IRAM, and were called once per voice per sample. The `DSP_HOT`/`IRAM_ATTR`
attribute on the out-of-class template definition was dropped for the weak
out-of-line instantiation.

Hardware confirmed the penalty (candidate 11, out-of-line):

| Fixture | sustain avg/p99/max us | deadline |
|---|---|---|
| PAN cluster8 | 2111.29 / 2775 / 3089 | 346 |
| BELL chord | 1239.45 / 1775 / 2085 | 0 |
| BELL cluster8 | 2127.82 / 2875 / 3164 | 536 |

Versus baseline this is a **+4.4% (PAN) / +2.2% (Bell cluster)** average
regression with hundreds of deadline misses — not measurement noise, since the
repeat reproduced 2111.09 and 2127.35 within 0.02%.

### 2.2 Corrected kernel

The micro templates are now declared and defined with
`__attribute__((always_inline))` plus the IRAM attribute, so both the 8- and
10-mode fixed kernels are inlined directly into the IRAM-resident
`processSample` body, exactly like the accepted D kernel. There is no
per-sample function pointer and no per-sample dispatch to flash. The mode/safety
variant is still resolved only when coefficients or configuration change
(`refreshMicroKernel`).

### 2.3 Implementation type and code size

- Implementation: **C++ (no hand assembly)**. The accepted D kernel already
  emits the optimal hardware-FPU recurrence; after the placement fix the C++
  microkernel beat D, so an assembly experiment was not justified (see §2.5).
- Modal kernel IRAM delta vs candidate 9: **+768 bytes** (`.iram0.text`
  96,015 → 96,855, `.text_end` 237 → 165; total 97,279 → 98,047).
- `processSample` instruction count (Xtensa): fixed+candidate-9 **1,377**,
  micro+candidate-13 **1,671**. The micro kernel carries four static variants
  (8/10 × safety/normal) so the recursion has no per-mode safety branch in the
  normal case.

### 2.4 Hardware result — microkernel only (candidate 11)

Connected, 8192 blocks/fixture, BLE Ready, profiling OFF. Repeat run in
parentheses.

| Fixture | sustain avg/p99/max us | event avg/p99/max us | sustain/event deadlines |
|---|---|---|---|
| PAN cluster8 | 1939.30/2200/2509 (1940.83/2225/2405) | 3105.30/3900/3883 | 0/44 |
| BELL chord | 1132.67/1375/1483 (1133.80/1375/1580) | 1891.17/2075/2066 | 0/0 |
| BELL cluster8 | 1923.41/2200/2485 (1924.75/2200/2482) | 3098.90/3675/3667 | 0/44 |

Against candidate 9:

| Fixture | sustain avg delta | sustain p99 delta | sustain max delta |
|---|---|---|---|
| PAN cluster8 | **-4.1%** | -5.4% | -3.4% |
| BELL chord | **-6.3%** | -5.2% | -14.0% |
| BELL cluster8 | **-7.6%** | -7.4% | -5.7% |

The kernel is exact and strictly better than D, but the standalone
`>= 8%` acceptance gate is not met (PAN -4.1%, Bell cluster -7.6%). It is
retained as the kernel component of the combined candidate, where the measured
Bell-cluster gain reaches -9.0%.

### 2.5 Why no hand assembly was promoted

The disassembly audit (§5) shows the recurrence already compiles to one `mul.s`
plus two `madd.s` on the hardware FPU with the same addition association as the
source, and the dominant fixed cost is memory traffic for the per-mode state,
not the arithmetic. The correction that mattered was **placement**, not
instruction selection. A hand-written AE32 recurrence would still have to
reproduce `tanh` saturation, the denormal comparison and the `madd.s`
contraction bit-for-bit, so the risk was not justified by the measured
headroom.

## 3. Part B — PreparedNote cache

### 3.1 Design

`PreparedNote` (128 bytes) stores only what is deterministic for one model,
one MIDI note, 48 kHz and the initial undamped NoteOn state: `midiNote`,
`modeCount`, `activeMask`, `fundamentalFrequencyHz`, and `a1/a2/modalAmplitude`
per mode. Velocity, hardness, brightness, soft/hard coupling, register gains and
aftertouch are deliberately **not** cached. Two shared tables (PAN, BELL) are
built once in `SynthEngine::init`, before I2S starts, by reusing the normal
`ModalResonatorBank::updatePitchAndDamping` routine on a scratch bank, so there
is no second coefficient implementation and no quantisation. A model switch in
the audio callback only swaps a pointer. Restrikes and damped/reused voices
ignore the cache and take the historical dynamic path. Notes outside MIDI 24–96
and non-canonical frequencies fall back to the normal computation; the host test
asserts the boundaries.

### 3.2 Trigger-path profile (diagnostic builds, 1024 blocks, profiling ON)

Total trigger probes and the dominant phase, per event block:

| Model / fixture | cand 9 trigger / modal-coeff us | cand 12 trigger / modal-coeff us |
|---|---|---|
| PAN cluster8 | 571.66 / 270.25 | 273.80 / 29.25 |
| BELL chord | 528.25 / 259.57 | 259.11 / 25.94 |
| BELL cluster8 | 575.68 / 326.98 | 292.39 / 31.25 |

The modal-coefficient phase falls by **~89–90%**, and the whole trigger probe
set by **~49–52%**. Exciter setup (~89–108 us), register/pitch (~35–46 us),
coupling (~16–22 us), allocation (~12–14 us) and MIDI mapping are unchanged, as
expected. `prepared lookup` is a new ~13 us line (two 73-entry lookups plus the
match/branch).

### 3.3 Hardware result — PreparedNote only (candidate 12)

| Fixture | sustain avg/p99/max us | event avg/p99/max us | sustain/event deadlines |
|---|---|---|---|
| PAN cluster8 | 1997.94/2275/2523 | 2956.22/3350/3346 | 0/44 |
| BELL chord | 1189.47/1425/1705 | 1803.44/2250/2244 | 0/0 |
| BELL cluster8 | 2059.04/2300/2547 | 3046.86/3550/3533 | 0/44 |

Against candidate 9: event average **-7.2% (PAN) / -10.7% (Bell chord) /
-8.2% (Bell cluster)**; sustain within ±1.6%. There is no sustain regression.
The full event block improves less than the trigger probes because the 128-frame
render dominates the event block.

### 3.4 Cache size (internal RAM)

| Quantity | bytes |
|---|---:|
| `PreparedNote` / note | 128 |
| `PreparedNoteTable` / model (73 notes + flag) | 9,348 |
| both models | 18,696 |
| measured `.dram0.bss` delta (cand 9 → 13) | 18,736 |

Tables are generated once during `SynthEngine::init` (outside the audio
callback, no heap, fixed arrays). Cost is +~18.7 KiB internal DRAM; the internal
free heap in production smoke falls 177,387 → 158,715 bytes, still ample.

### 3.5 Acceptance

Measured on the trigger probes the cache exceeds the `>= 25%` target by a wide
margin (~50%); measured on the whole event block it delivers 7–15%, and the
event **max** gates are not met for PAN/Bell cluster. It is retained because it
is bit-exact, has no sustain regression, and its cost is a bounded, statically
sized internal table. A future iteration could shrink the table (smaller note
range) or regenerate coefficients for non-48 kHz rates; neither was required
here.

## 4. Part C — voice sustain fast path

Not implemented in this pass, and therefore not promoted. A/B profiling was
completed first, as required, and it shows the remaining gap is still dominated
by the modal bank (48–57% of a cluster block) with a 27–32% voice/allocator
residual that includes the exciter and lifetime checks. The exciter-off
`processActiveSample` fast path and the fully-active allocator path from
sections 38–41 are the natural next generic step, but they are separate code
changes that must be qualified on their own. Implementing them now would mix
two unqualified changes into the final matrix.

## 5. Xtensa recurrence audit

See `docs/hardware/m632_xtensa_recurrence_audit.json`. The accepted M6.3.1
audit already covers D. For the new kernel, disassembling
`ModalResonatorBank::processSample` in candidate 13 shows that each fixed mode
is computed as

```
f0 = a1 * z1          ; mul.s
f0 = excitationGain * excitation + f0   ; madd.s   (commutative addend swap only)
f0 = a2 * z2 + f0                        ; madd.s
z2 = z1 ; z1 = f0
```

- Hardware FPU `mul.s` / `madd.s`, no scalar libgcc float calls in the loop.
- The only difference from the source association `(eg*x + a1*z1) + a2*z2` is
  that the compiler emits `(a1*z1 + eg*x) + a2*z2`; IEEE additions are
  commutative, so the result is bit-identical.
- No reassociation between modes: the running sum is accumulated strictly in
  ascending mode order (0…7 for PAN, 0…9 for Bell) with `add.s`.
- Denormal comparison and saturation are preserved; the safety variant is
  instantiated statically, so the normal variant has no saturation branch.

The host qualification (`test_dsp`, `test_modal_kernel`) is the authoritative
bit-exactness check across all 8- and 10-mode paths, including sparse/Nyquist
pruning, pitch/damping changes, saturation and denormals.

## 6. Combined candidate (A + B) and final comparison

Connected, 8192 blocks/fixture, BLE Ready, profiling OFF. All fixtures report
`hard=0`, `sat=0`, `bad_voices=0`, `ble_lost=0`, I/O timeout/short/TX = 0/0/0.

| Candidate | PAN cluster sustain avg/p99/max | PAN cluster event avg/p99/max | Bell chord sustain / event | Bell cluster sustain / event | IRAM delta |
|---|---|---|---|---|---|
| 9 D+E | 2021.68/2325/2597 | 3186.29/3850/3834 | 1209.03/1450/1724 • 2020.12/2475/2470 | 2082.37/2375/2635 • 3320.57/3975/3950 | 0 |
| 11 A | 1939.30/2200/2509 | 3105.30/3900/3883 | 1132.67/1375/1483 • 1891.17/2075/2066 | 1923.41/2200/2485 • 3098.90/3675/3667 | +768 |
| 12 B | 1997.94/2275/2523 | 2956.22/3350/3346 | 1189.47/1425/1705 • 1803.44/2250/2244 | 2059.04/2300/2547 • 3046.86/3550/3533 | 0 |
| **13 A+B** | **1908.28/2150/2410** | **2873.27/3275/3260** | **1106.00/1325/1449 • 1736.21/2150/2130** | **1894.87/2125/2604 • 2824.07/3200/3188** | +768 |

Sustain deadline misses are 0 for every candidate; event deadline misses are 44
for PAN/Bell cluster (one per reset block) and 0 for Bell chord.

Combined candidate 13 gain versus candidate 9:

| Fixture | sustain avg | sustain p99 | sustain max | event avg |
|---|---|---|---|---|
| PAN cluster8 | -5.6% | -7.5% | -7.2% | -9.8% |
| BELL chord | -8.5% | -8.6% | -16.0% | -14.1% |
| BELL cluster8 | -9.0% | -10.5% | -1.2% | -14.9% |

Disconnected-scan diagnostic (candidate 13): PAN cluster
1913.93/2175/2525, Bell chord 1110.13/1350/1503, Bell cluster
1899.52/2150/2351; no deadline misses, I/O = 0/0/0.

Whole-callback CPU load during PAN cluster8 falls from ~79% (cand 9) to ~71%
(cand 13); Bell chord is ~42%. Both clusters remain above the 65% hard limit.

## 7. Acceptance gates

| Gate | Required | Candidate 13 |
|---|---|---|
| PAN cluster sustain p99 | <= 1733 us | 2150 us — **fail** |
| PAN cluster sustain max | <= 2133 us | 2410 us — **fail** |
| PAN cluster sustain avg | <= 1733 us | 1908 us — **fail** |
| BELL chord sustain p99/max | <= 1733 / 2133 us | 1325 / 1449 us — **pass** |
| BELL cluster sustain p99 | <= 1733 us | 2125 us — **fail** |
| BELL cluster sustain max | <= 2133 us | 2604 us — **fail** |
| PAN event max | <= 2133 us | 3260 us — **fail** |
| BELL chord event max | <= 2133 us | 2130 us — **pass** (p99 2150 > 1733 preferred) |
| BELL cluster event max | <= 2133 us | 3188 us — **fail** |
| Deadline misses (sustain) | 0 | 0 — pass |
| Deadline misses (whole callback) | 0 | 44/88 — **fail** |
| CPU cluster (whole callback) | <= 65% | ~71% (PAN), ~72% (Bell) — **fail** |
| I/O timeout/short/TX | 0/0/0 | 0/0/0 — pass |
| PAN exact / fingerprints / Bell ref | all exact | pass |

Target engineering range before freeze (avg 1500–1650 us) is not reached.
M6.3.2 status: **FAIL** (generic optimization must continue). Because PAN
cluster still fails, **Bell ablation is NOT authorized** (section 68).

## 8. Integrity

- No golden hash was updated or relaxed, no preset, gain, T60, doublet,
  velocity curve, sympathetic coupling, limiter setting, polyphony, sample rate
  or block size changed.
- No `-ffast-math`, no new reassociation flags, no new reciprocal
  approximations. The existing `madd.s` contraction is unchanged.
- The accepted kernel is C++ (no hand assembly), preserving the exact
  recurrence, state write order, summation order and saturation/denormal
  semantics.
- Host suite 5/5 for 9/11/12/13; PAN 12/12; 15/15 fingerprints; Bell reference
  WAVs byte-identical; the PreparedNote grid checks 73 notes × 4 velocities × 2
  models of cached-vs-uncached PCM for exact equality.

## 9. Reproduction

```sh
cmake -S tests -B build-host-m632 -G Ninja -DCMAKE_BUILD_TYPE=Release -DPOCKETPAN_DSP_CANDIDATE=13
cmake --build build-host-m632 --parallel && ctest --test-dir build-host-m632 --output-on-failure
```

```sh
# Forensics builds: set CONFIG_POCKETPAN_POLYPHONY_FORENSICS=y and
# CONFIG_POCKETPAN_FORENSICS_BLOCKS=8192 in sdkconfig, then
idf.py -B build-m632-qual13 -DPOCKETPAN_DSP_CANDIDATE=13 -DPOCKETPAN_FORENSICS_CRITICAL_ONLY=1 build
idf.py -B build-m632-qual13 -p COM10 flash
python tests/capture_forensics.py docs/hardware/m632_combined_connected.log --port COM10 --rows 6 --seconds 120
```

## 10. Next

1. Part C voice sustain fast path (exciter-off `processActiveSample`, branch-light
   fully-active allocator) to attack the 27–32% voice residual.
2. Re-check whether the +18.7 KiB PreparedNote table can be reduced (narrower
   note range) if internal RAM becomes constrained.
3. Bell ablation remains **not authorized**: PAN cluster still fails.
4. Listening A/C selection and Bell freeze remain separate pending decisions.
