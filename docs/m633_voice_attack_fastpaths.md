# M6.3.3 — Exactness recovery + voice sustain fast path

Branch: `codex/m632-modal-note-cache`. Baseline at entry: `dad07a6`.
Performance baseline retained: **candidate 13** (modal microkernel + PreparedNote
+ accepted IRAM strategy). Candidate 9 was not revisited.

This pass delivers **Phase 0 (exactness recovery)** and **Phase 1 (stable-sustain
voice fast path)** with full host bit-exactness. **Phase 2 (attack/exciter fast
path) is designed but deliberately not promoted**, and hardware qualification is
still required. See "Status" at the end.

---

## Phase 0 — modal-kernel exactness recovery

### 1. Reproduction

The CI host suite reported:

```text
test_modal_kernel  FAIL
Kernel differs: 0 146.83
```

The committed fixture file is `tests/m631_modal_kernel_hashes.txt`, created in
`d29da4c` when the default `POCKETPAN_DSP_CANDIDATE` was **0** (the scalar
generic loop). The test currently builds candidate **13** (the microkernel) and
compares its FNV against those scalar-baseline numbers.

### 2. Differential harness (same build, sample by sample)

`test_modal_kernel` no longer trusts only the final hash. For each of the 15
fixtures (3 presets × 5 frequencies) it runs:

- a **reference** `ModalResonatorBank` calling `processSampleReference()` — the
  exact scalar recurrence accepted in M6.3.1, and
- a **candidate** bank calling the production `processSample()` dispatcher
  (microkernel/fixed/generic),

from identical preset, pitch and damping state, with the identical excitation
schedule. Outputs are compared **bit by bit, sample by sample**. The first
divergence prints preset, frequency, candidate id, sample index, excitation,
both raw bit patterns and both float values, plus a per-mode dump (active,
excitationGain, a1, a2, z1, z2). The historical fingerprint file is read but is
**not overwritten**.

### 3. Root cause

**Classification: B + C (host code-gen drift + stale golden methodology). There
is no actual numerical divergence (A).**

Evidence collected on this machine:

| Toolchain | candidate 0 vs golden | candidate 13 vs golden | candidate 0 vs candidate 13 |
|---|---|---|---|
| GCC 14.2 (UCRT), `-O3` | 15/15 match | 15/15 match | identical |
| Clang 19, `-O3` | 15/15 match | 15/15 match | identical |
| GCC 14.2, `-march=x86-64-v3` (FMA) | 0/15 match | 0/15 match | identical |
| GCC 15.2 (MSVCRT), `-O3` | 8/15 match | 8/15 match | identical |

The decisive row is the second: **candidate 0 and candidate 13 are always
bit-identical to each other**, while the *scalar reference itself* drifts from
the recorded golden under a different code-gen / instruction set. `sinf`,
`cosf`, `expf`, `tanhf` were verified bit-identical across UCRT/MSVCRT/clang, so
the drift is compiler floating-point lowering of the recurrence, not the kernel
and not libm. A cross-host golden hash of a 24000-sample recursive state is
therefore not a valid hard gate: it encodes the host, not the kernel.

The same recurrence on Xtensa is unchanged from the accepted M6.3.1 audit.

### 4. Fix

- The authoritative gate is now in-process **kernel equivalence**.
- The historical fingerprint is still checked. It is only allowed to fail when
  the **in-process scalar reference also drifts from it**; in that case the test
  reports `Host codegen drift (not a kernel divergence)` and still passes on the
  equivalence result. When the reference matches the golden (as on the original
  Windows GCC 14 host) candidate must match it exactly. The golden file is
  untouched; no hash was regenerated or relaxed silently.

### 5. Result

```text
test_modal_kernel PASS
Modal kernel equivalence: 15/15 exact
```

On GCC 15.2 the same line reads `15/15 exact (8 host-codegen-drift fixture(s),
kernel still exact)`. PAN 12/12, PAN → BELL → PAN, and the Bell reference WAVs
remain byte-identical.

---

## Phase 1 — stable-sustain voice fast path

### 6. Design

A voice is **sustain-safe** when the historical per-sample branches are provably
no-ops:

```cpp
active_
&& !exciter_.isActive()
&& !isStealing_
&& std::abs(targetDamping_ - currentDamping_) <= 0.0001f   // the existing epsilon
```

Inside a render block `targetDamping_` cannot change (MIDI is handled between
blocks), so once the damping branch is not taken `currentDamping_` cannot move
either; the exciter cannot restart and a steal cannot begin. The decision is
therefore taken **once per voice per block**.

`ModalVoice::renderSustainBlock()` runs the *same* modal kernel
(`resonators_.processSample`) and the *same* age/lifetime bookkeeping. The
lifetime check is segmented at the exact age-aligned sample
(`128 - (age_ & 0x7F)`), so deactivation occurs at the identical sample and
`lastSample_` matches. The sample-outer paths use the lean per-sample
`processSampleSustain()`; voice order and per-sample summation order are
unchanged.

Gated by `POCKETPAN_SUSTAIN_FASTPATH` (candidates **14** and **16**), so
candidate 13 is byte-for-byte unchanged.

### 7. Exactness evidence (`test_fastpath`, candidate 16)

Fast-on vs fast-off PCM compared bit for bit over 2 s per fixture, with the fast
path confirmed exercised:

| Fixture | Result | fast voice-blocks |
|---|---|---:|
| PAN single | exact | 687 |
| PAN chord4 | exact | 2759 |
| PAN cluster8 | exact | 5661 |
| PAN steal9 | exact | 5642 |
| BELL single | exact | 748 |
| BELL chord4 | exact | 2992 |
| BELL cluster8 | exact | 5984 |
| PAN pressure / NoteOff / restrike | exact | 2019 |
| BELL poly pressure | exact | 2925 |
| PAN → BELL switch (+re-strike) | exact | — |
| BELL → PAN switch (+re-strike) | exact | — |

Candidate 16 also passes the unmodified `test_dsp` PAN `kPanGolden` 12/12 and the
Bell reference WAVs, i.e. the full engine is unchanged with the fast path on.

### 8. Hardware sustain benchmark (ESP32-S3, BLE disconnected)

Critical and full matrices, 8192 blocks/fixture, profiling OFF. Full table in
[hardware/m633_candidate_comparison.md](hardware/m633_candidate_comparison.md).

| Candidate | PAN cluster sustain avg/p99/max | BELL chord sustain avg/p99/max | BELL cluster sustain avg/p99/max |
|---|---|---|---|
| 13 (baseline) | 1921 / 2200 / 2432 us | 1122 / 1350 / 1539 us | 1923 / 2175 / 2386 us |
| 16 (sustain) | 1770 / 2050 / 2574 us | 974 / 1225 / 1420 us | 1628 / 1875 / 2444 us |
| Δavg | **-7.9%** | **-13.2%** | **-15.3%** |

All three exceed the >= 7% retain gate, so the sustain fast path is **accepted**.
Whole-callback CPU also falls (PAN cluster ~73% → ~67.5%, BELL cluster ~72% →
~61%). The cluster **gates** still fail (PAN cluster avg/p99/max; BELL cluster
p99/max), so M6.3.3 does not reach the all-green stop condition.

---

## Phase 2 — attack / exciter fast path

`CLOSED — NOT PROMOTED` in this pass. The exactness constraints were established
before writing code, and they are the reason the change is not safe to land
without a hardware measurement that can justify the risk:

1. The half-sine impulse is `(strikeAmplitude * window) * norm`. A precomputed
   `window * norm` would reassociate the multiply and change low bits. A table
   must keep `window` and `norm` separate, or reproduce the association exactly.
2. The noise envelope is `(filterState * noiseGain) * (env * env)`; the
   per-sample `env` division can be hoisted, but the final multiply association
   must be preserved.
3. Precomputing a whole exciter block at block start advances `Exciter` state
   before the age-aligned lifetime check reads `exciter_.isActive()`, which would
   change the deactivation sample. The block path must track per-sample exciter
   activity to stay exact.
4. PAN renders sample-outer through the strike bus, so a per-voice exciter block
   cache must not change the voice summation order.

`POCKETPAN_ATTACK_FASTPATH` (candidates **15** and **16**) is wired in
`dsp_profile.h`, so the segment work can be added under the same candidate matrix
and verified with the existing `test_fastpath` A/B harness once the above is
addressed and a hardware event benchmark is available. No attack code is present
in candidate 15/16 today; they currently behave as candidate 13 + sustain.

---

## Candidate matrix

| id | composition |
|---:|---|
| 0 | scalar historical baseline |
| 4 | fixed kernels |
| 5 | bounded IRAM |
| 9 | D + E (M6.3.1) |
| 11 | microkernel |
| 12 | PreparedNote |
| 13 | microkernel + PreparedNote (M6.3.2, default) |
| 14 | 13 + sustain fast path |
| 15 | 13 + (attack reserved, none yet) |
| 16 | 13 + sustain (+ attack reserved) |

## Integrity

- PAN voicing, Bell voicing, modal ratios, gains, T60, doublets, velocity curves,
  sympathetic, body, limiter, polyphony, sample rate and block size are unchanged.
- No `-ffast-math`, no new reassociation, no golden hash regeneration.
- Candidate 13 remains bit-identical and is still the default.
- Firmware default is unchanged at candidate 13 until a hardware-qualified
  candidate is selected.

## Reproduction

```sh
cmake -S tests -B build-host-13 -G Ninja -DCMAKE_BUILD_TYPE=Release -DPOCKETPAN_DSP_CANDIDATE=13
cmake -S tests -B build-host-16 -G Ninja -DCMAKE_BUILD_TYPE=Release -DPOCKETPAN_DSP_CANDIDATE=16
cmake --build build-host-16 --parallel && ctest --test-dir build-host-16 --output-on-failure
```

## Status

```text
Phase 0  exactness recovery            PASS (15/15 in-process equivalence)
Phase 1  sustain fast path             PASS (host PCM exact; hardware -7.9%/-13.2%/-15.3%)
Phase 2  attack fast path              NOT PROMOTED (designed, exactness constraints documented)
Phase 3  hardware qualification        PARTIAL (disconnected matrix done; connected needs a BLE central)
M6.3.3                                 FAIL (PAN cluster gates still red; Bell ablation NOT authorized)
```

The 8-voice clusters remain the only red fixtures. The next generic hotspot is
the modal bank / 8-voice cluster render, not the sustain or attack paths.
