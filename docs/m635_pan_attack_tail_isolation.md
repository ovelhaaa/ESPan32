# M6.3.5 — PAN sympathetic fast path + attack-voice fast path + tail source isolation

Branch: `codex/m632-modal-note-cache`. Entry baseline: `58a2117`
(production candidate **20**). This pass adds the M6.3.5 candidate matrix
**22–25** on top of the validated candidate 20.

> **Update:** the connected qualification and Phase C tail isolation that were
> originally "pending hardware" have since been executed on the physical board.
> See **"Hardware qualification — completed"** below. The original
> hardware-pending section is retained verbatim for traceability and marked as
> superseded.

---

## Hardware qualification — completed

Board: ESP32-S3 (QFN56) rev v0.2, MAC `b4:3a:45:ae:6f:28`, USB-Serial/JTAG on
**COM10**, 4 MB flash / 2 MB PSRAM, 240 MHz. ESP-IDF **5.3.0**. BLE-MIDI central
connected to a real peripheral (**SMK25V2**), `state=8 (Ready)`,
connection interval **11.25 ms**, latency 0. Forensics builds, **profiling
OFF**, `POCKETPAN_FORENSICS_BLOCKS=8192`, `CRITICAL_ONLY=1`, `DISCONNECTED=0`,
`bad_voices=0`, `ble_lost=0`, `hard=0`, `sat=0` in every row. Raw captures under
`docs/hardware/m635_*`. Comparison tables in
`docs/hardware/m635_candidate_comparison.md`.

### Result summary

```text
Host regression (c20/c25) ............ PASS 8/8
ESP-IDF build ........................ PASS
candidate 25 vs 20 (steady)
  PAN cluster8   avg 1754.7 -> 1632.4 (-6.96%)
                 p99 1900   -> 1775   (-6.58%)
                 max 2389   -> 2038   (-14.7%)
  BELL cluster8  avg 1617.1 -> 1616.9 ( ~0%)
                 p99 1775   -> 1750   (-1.4%)
                 max 2293   -> 2074   (-9.5%)
  BELL chord4    avg  963.6 ->  963.5 ( ~0%)
candidate 25 vs 20 (events)
  PAN cluster8   avg 2821.1 -> 2500.3 (-11.4%)  max 3202 -> 2728
  BELL chord4    avg 1691.7 -> 1505.9 (-11.0%)  max 1890 -> 1695
  BELL cluster8  avg 2820.9 -> 2498.9 (-11.4%)  max 3398 -> 2724
fast-path counters (c25, final cycle) stable8=106 attack=16 (PAN cluster8);
  stable8=0 attack=8 (BELL chord4); stable8=0 attack=16 (BELL cluster8)
I/O ................................... timeout 0 short 0 tx 0
CPU (forensics whole-callback) ........ c20 66.7% peak -> c25 62.2% peak
```

**M6.3.5 outcome: PARTIAL.** The steady **average** and **max** gates now pass
(`avg <= 1733`, `max <= 2133`) for both 8-voice clusters; the steady **p99** gate
does not (PAN 1750–1775 µs, BELL 1750 µs vs the 1733 µs target). Phase C shows
the residual tail is a **UI-task/rendering/scheduler** artifact, not DSP and not
the LCD/GDMA transfer (U0≈U1, U2 removes it and brings PAN p99 to 1650–1675 and
BELL p99 to 1625–1650).

### Promotion

Candidate **25** is promoted to the default (`POCKETPAN_DSP_CANDIDATE 25`) on the
§22 criteria: host exactness PASS, hardware health PASS (I/O 0/0/0, deadline 0 in
the production smoke, no `bad_voices`/`ble_lost`), no meaningful regression, and
connected gains that justify the change. The stop rule (§24) is **not** met
because of the p99 item above, so generic optimization is paused rather than
declared closed.

---

## Hardware status (as originally recorded — superseded)

Every connected measurement in the M6.3.x series runs the forensics harness,
which gates all fixtures on a live BLE-MIDI link
(`polyphony_forensics.h`: `ready = sBleMidi.isMidiReady()`; while `ready==false`
`render()` emits silence and records no timing). The first M6.3.5 session had
**no BLE-MIDI central/peripheral partner and no authorization to flash**, so at
that time:

```text
connected hardware qualification ....... NOT RUN
Phase C tail isolation (U0/U1/U2) ...... NOT RUN
final performance gates ................ PENDING HARDWARE
```

No hardware number in the original body is inferred, interpolated or copied
where it would be presented as a new measurement. Candidate 20 baseline figures
appear only as the previously recorded M6.3.4 reference and are labelled as such.
The remainder of this document (below the original "Commit(s)" section) preserves
the design of the M6.3.5 fast paths exactly as reviewed before the hardware pass.

---

## Commit(s)

Working tree only; **no commit was made** (the milestone's promotion decision is
hardware-gated and this environment cannot run it).

```text
main/dsp/dsp_profile.h      candidate matrix 22-25, feature macros
main/dsp/voice_allocator.h  setSympatheticConfig, steady8/attackStable8,
                            stable-8 + attack counters
main/dsp/voice_allocator.cpp sympathetic coeff cache, PAN stable-8 sample-outer
                            path, attack sample-outer path, counters
main/dsp/modal_voice.h      isAttackSafe / processSampleAttackStable /
                            renderAttackBlock, host A/B toggle
main/dsp/modal_voice.cpp    Phase B attack-voice sample path
main/dsp/synth_engine.h     coefficient-cache boundary calls, test hooks
main/dsp/synth_engine.cpp   setSympatheticConfig at init/model/toggle boundaries
tests/CMakeLists.txt        test_m635_fastpath target; candidate strings 22-25
tests/test_m635_fastpath.cpp new differential harness
docs/m635_pan_attack_tail_isolation.md
```

---

## M6.3.5: PARTIAL

- Phase A1 (sympathetic coefficient cache) **implemented, host bit-exact**;
  hardware gain **not measured**.
- Phase A2 (PAN stable-8 sample-outer sustain path) **implemented, host
  bit-exact**; hardware gain **not measured**.
- Phase B (attack-voice fast path) **implemented, host bit-exact**; hardware
  gain **not measured**.
- Phase C (tail source isolation) **not performed** (needs connected hardware).
- Phase D (Bell mode-major) not performed (optional).

---

## Candidate matrix

All M6.3.5 candidates are additive to candidate 20 (microkernel + PreparedNote +
stable sustain + accepted T4 tail/IRAM).

```text
20  baseline (unchanged default)
21  20 + exact attack/exciter fast path (M6.3.4; unchanged)
22  20 + cached sympathetic low-pass coefficient        (Phase A1)
23  22 + PAN stable-8 sample-outer sustain path         (Phase A2)
24  20 + attack-voice fast path + exciter fast path     (Phase B)
25  23 + attack-voice fast path                         (best combination / D)
```

Implementation summary:

- **A1 coefficient cache.** `VoiceAllocator::setSympatheticConfig()` computes
  `exp(-2π·clamp(lowpassHz,10,fs·0.45)/fs)` once at each configuration boundary
  (`SynthEngine::init`, `setInstrumentModel`, `setSympatheticEnabled`,
  `setModelConfigForTest`, `setSympatheticConfigForTest`). The render paths no
  longer evaluate the exponential; they read `sympatheticLowpassCoefficient_`.
  Formula, clamp bounds, operand order and the resulting float are unchanged.
- **A2 PAN stable-8.** At block start `steady8()` requires all eight voices
  active, provably sustain-safe for the whole block and no steal declick tail.
  The sample-outer loop is then specialized to eight direct
  `processSampleSustain()` calls. Sample order, voice `0→7` order, float
  summation order, the one-sample sympathetic delay, filter-state update order,
  bus clipping order and all diagnostic updates are untouched.
- **B attack-voice.** `isAttackSafe()` = active ∧ exciter running ∧ not stealing
  ∧ `|targetDamping-currentDamping| ≤ 0.0001` (the historical epsilon). For
  all-8 PAN the sample-outer loops use `processSampleAttackStable()`, which is
  `processSample()` with the provably no-op damping-smoothing and steal-crossfade
  branches removed. Bell (voice-major) uses the same per-voice
  `renderAttackBlock()`. The accepted exciter fast path (candidate 21) is
  reused verbatim — no second approximation.

Forbidden techniques were not used: no `-ffast-math`, no reassociation, no mode
ablation, no reduced polyphony, no rate/block change, no golden regeneration.

---

## Sympathetic coefficient cache (Phase A1)

```text
delta avg / p99 / max ... not measured (needs connected hardware)
exact? .................. YES — host bit-exact
```

Host evidence (`test_m635_fastpath`, `coefficientExactness`): 3 sample rates
(44.1/48/96 kHz) × 7 low-pass values (0, 10, 500, 1500, 5000, 20000, 100000 Hz)
= 21 configurations. The cached coefficient is compared to the historical
expression **bit-for-bit** (`memcmp` of the float). Result: **exact**.

Hardware keep gate (spec §4: `avg >= 1%`) remains open.

---

## PAN stable-8 fast path (Phase A2)

```text
implementation ... specialized sample-outer sustain loop (see above)
avg delta / p99 delta / max delta ... not measured (needs connected hardware)
exact? ........... YES — host PCM + voice-state bit-exact
```

Host evidence:

- `test_fastpath` (sustain fast-path on vs off) covers PAN single / chord4 /
  cluster8 / steal9 / pressure+off+restrike / PAN↔BELL switches. The stable-8
  path is selected whenever eight sustain-safe voices are present; on/off PCM is
  identical for every fixture. Candidate 23, all 11 fixtures **exact**.
- `test_m635_fastpath` records `panStable8Blocks` and asserts the path was
  actually selected (not silently bypassed): PAN cluster8 steady = 361 stable-8
  blocks (attack on, candidate 25).

Hardware keep gate (spec §12: `avg >= 2%` or `p99 >= 5%`, max regression ≤ 5%)
remains open.

---

## Attack voice fast path (Phase B)

```text
implementation ... processSampleAttackStable / renderAttackBlock + block select
PAN event delta / Bell event delta ... not measured (needs connected hardware)
exact? .......... YES — PCM, strike bus and voice-state bit-exact
```

Host evidence (`test_m635_fastpath`, attack path on vs off, independent engine
instances), velocities 30/70/110/127:

| Fixture | PAN single | PAN chord4 | PAN cluster8 | BELL single | BELL chord4 | BELL cluster8 |
|---|---|---|---|---|---|---|
| PCM exact | yes | yes | yes | yes | yes | yes |
| voice state exact | yes | yes | yes | yes | yes | yes |
| attack path selected | (general) | (general) | 16–24 blocks | 2–3 blocks | 8–12 blocks | 16–24 blocks |

Additional exactness: PAN steal9, PAN channel-pressure on cluster8, PAN
noteoff/restrike. Voice-state comparison covers `estimatedEnergy`, `lastSample`,
`age` and `active` for all eight voices. Exciter PRNG/active exactness is
covered separately by `test_exciter_fastpath` (candidates 21/24/25), which still
passes for all 12 impulse lengths and the dense hardness sweep.

Hardware keep gate (spec §24: event avg PAN or Bell cluster `>= 10%`,
preferred `>= 15%`; §25 `event max <= 2133 us`) remains open. Candidate 21's
already-recorded exciter-only event gain (-4.9%…-7.6%) did not clear it, and this
pass cannot measure whether the voice-level specialization closes the gap.

---

## Tail isolation (Phase C)

```text
U0 normal UI/display ............... NOT RUN
U1 LCD transfer disabled ........... NOT RUN
U2 UI suspended .................... NOT RUN
BLE-off diagnostic ................. NOT RUN
tail source conclusion ............. unresolved (not measured)
```

Rationale: all three variants require the connected forensics harness (spec
§26 "BLE must remain connected in all three") and flashing. Neither was
available. Per spec §29/§31 no attribution to DSP, LCD DMA, UI scheduling or BLE
is made without evidence. The residual 2150–2350 µs cluster reported in M6.3.4
remains **unexplained** at this point.

---

## Baseline candidate 20 (M6.3.4 recorded reference, not re-measured)

```text
PAN  cluster8 steady:  avg 1755.75 us  p99 1900 us  max 2372 us  CPU ~61%
BELL cluster8 steady:  avg 1617.53 us  p99 1775 us  max 2426 us
```

These are carried forward from `docs/m634_tail_attack_closure.md` as context
only.

---

## Final qualification

Not performed — requires connected hardware.

| Metric | Required | Result |
|---|---|---|
| PAN cluster avg/p99/max | ≤1733/≤1733/≤2133 | PENDING HW |
| BELL chord sustain | PASS | PENDING HW |
| BELL cluster avg/p99/max | ≤1733/≤1733/≤2133 | PENDING HW |
| event max | ≤2133 | PENDING HW |
| CPU | ≤65% | PENDING HW |
| deadlineMisses | 0 | PENDING HW |
| I/O timeout/short/tx | 0/0/0 | PENDING HW |

---

## CI / build evidence (this session)

```text
Host CI (ctest, Release, 8 tests) .... PASS 8/8 for candidates 20,21,22,23,24,25
ESP-IDF 5.3 firmware build ........... PASS  candidate 20 and candidate 25
```

Firmware section footprint (ESP-IDF 5.3, default production config):

| Build | `.iram0.text` | `.dram0.bss` |
|---|---:|---:|
| candidate 20 | 98,351 | 39,552 |
| candidate 25 | 101,139 | 40,400 |
| delta | **+2,788** | **+848** |

Candidate 20's sections are **bit-identical to the validated M6.3.4 production
map** (`0x1802f` `.iram0.text`, `0x9a80` `.dram0.bss`), confirming the guarded
changes do not perturb it. Candidate 25's BSS delta is the 840-byte exciter
half-sine window table (non-const static) inherited from candidate 21 plus
alignment; the IRAM delta is the specialized sustain/attack code.

---

## Exactness summary

```text
PAN                      12/12 exact      PASS (candidates 20-25)
modal kernel             15/15 in-process PASS
PreparedNote             exact            PASS
sustain fast path        on/off exact     PASS (test_fastpath, c20-25)
attack fast path         on/off exact     PASS (test_m635_fastpath, c24/25)
exciter fast path        reference exact  PASS (test_exciter_fastpath, c21/24/25)
sympathetic coefficient  bit exact        PASS (test_m635_fastpath, c22-25)
PAN -> BELL -> PAN       exact            PASS (test_dsp)
Bell reference WAVs      no drift         PASS (git status clean of WAVs)
```

---

## Production candidate

```text
25 — PROMOTED (POCKETPAN_DSP_CANDIDATE default = 25; firmware and host tests).
Connected-qualified: host exact, BLE Ready, I/O 0/0/0, production deadline 0,
no bad_voices/ble_lost.  PAN cluster8 steady avg 1754.7 -> 1632.4 (-7.0%) and
max 2389 -> 2038; events -11.4%; CPU 66.7% -> 62.2%.  Candidate 20 remains
selectable as the previous baseline.
Open: the two 8-voice cluster steady p99 (PAN 1775 us, BELL 1750-1775 us) remain
just above the 1733 us target; Phase C attributes this to the UI task/rendering,
not DSP.
```

---

## Bell ablation authorized: NO

PAN cluster steady p99 still fails (1775 µs > 1733) under the normal UI, so the
`PAN PASS / Bell FAIL` authorization rule is not met. No Bell mode ablation was
performed or prepared.

---

## Next

```text
1. Attack the UI-induced residual p99 tail (U2 proves the audio path itself is
   already under the gate): reduce UI render cost / move framebuffer work out of
   the audio-adjacent scheduling window.  Measure; do not change on hypothesis.
2. Re-run the connected p99 gates.  If PAN + BELL cluster p99 <= 1733 with I/O
   0/0/0 and deadline 0, M6.3.5 PASS and STOP generic optimization; return to
   Bell V1 freeze.
3. Bell ablation remains NOT authorized until PAN cluster p99 passes.
```

## Reproduction

```sh
# Host exactness for any candidate 20..25
cmake -S tests -B build-host-m635 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DPOCKETPAN_DSP_CANDIDATE=25
cmake --build build-host-m635 --parallel
ctest --test-dir build-host-m635 --output-on-failure

# Firmware build (ESP-IDF 5.3)
idf.py -B build-m635-c25 -DPOCKETPAN_DSP_CANDIDATE=25 build

# Connected qualification (requires a BLE-MIDI central, BLE Ready on COM10)
idf.py -B build-m635-c25 -p COM10 -DPOCKETPAN_DSP_CANDIDATE=25 \
  -DPOCKETPAN_FORENSICS_CRITICAL_ONLY=1 flash monitor
python tests/capture_forensics.py docs/hardware/m635_c25_connected.log \
  --port COM10 --rows 6 --seconds 180
```

---

## Integrity

- No preset, modal ratio, gain, T60, doublet, velocity curve, sympathetic gain,
  body voicing, limiter setting, polyphony (8), sample rate (48 kHz) or block
  size (128) changed.
- Candidate 20 firmware is section-for-section identical to the validated
  M6.3.4 production build; its host output is bit-exact.
- The M6.3.5 fast paths are additive candidates behind compile-time gates and
  are not reachable from the default production candidate.
