# M6.3.6 — UI scheduling isolation + attack deadline closure

Branch: `codex/m632-modal-note-cache`. Entry baseline: `2fe76c1`
(production candidate **25**). This pass ran on the **physical board** with a
connected BLE-MIDI link and adds candidate **26** plus a set of UI-isolation
diagnostics. No sound parameter is touched.

Board: ESP32-S3 (QFN56) rev v0.2, MAC `b4:3a:45:ae:6f:28`, USB-Serial/JTAG on
**COM10**, 4 MB flash / 2 MB PSRAM, 240 MHz, ESP-IDF 5.3. BLE-MIDI central
**connected** (`state=8` Ready), connection interval **11.25 ms**, latency 0.
Every fixture is 8192 blocks, forensics build, profiling **OFF** for acceptance,
`POCKETPAN_FORENSICS_CRITICAL_ONLY=1` (fixtures 5 = PAN cluster8, 14 = BELL
chord4, 13 = BELL cluster8), fine **5 us** histogram bins. Raw captures under
`docs/hardware/m636_*`.

---

## Commit(s)

Working tree only; **no commit was made** (the promotion decision is recorded
here and the default is unchanged).

```text
main/dsp/dsp_profile.h          candidate 26 (headroom table); attack segment disabled
main/dsp/synth_engine.h/.cpp    exact headroom target lookup table (§28)
main/dsp/exciter.h              samplesUntilInactive() (attack-segment experiment only)
main/dsp/modal_voice.h/.cpp     segmented renderAttackBlock (disabled, negative result)
main/dsp/voice_allocator.cpp    segmented PAN stable-8 attack path (disabled)
main/ui/ui_renderer.h/.cpp      U3 no-clear, signature dirty, partial Status redraw
main/hardware/display.h/.cpp    Display::updateRows() row-band transfer (§16)
main/app/app_main.cpp           U1b logic-only, BLE-poll split, render divider
main/app/polyphony_forensics.h  configurable bin size + p95/p99/p99.5/p99.9 (§10-11)
main/CMakeLists.txt             UI/forensics build options
tests/test_m635_fastpath.cpp    headroom-table differential check
tests/analyze_m635_tail.py      bin-size aware parsing
sdkconfig.m636.profile.defaults profiling diagnostic defaults
docs/hardware/m636_*            raw connected evidence
docs/m636_ui_attack_closure.md  this file
```

---

## M6.3.6: PARTIAL

- **Phase A — root cause identified.** The residual 8-voice steady `p99` tail and
  the event deadline misses are caused by the **UI framebuffer render
  (drawing + full-screen LCD transfer)**, not by DSP and not by the BLE poll.
  U1b (`POCKETPAN_UI_LOGIC_ONLY`, everything except `renderer.render()` retained)
  reproducibly moves PAN `p99` **1785 -> 1690/1695 us** and BELL cluster
  **1775 -> 1685/1680 us** and removes the event deadline misses.
- **Phase C — production UI fix insufficient so far.** Reducing the render
  cadence (5 Hz) recovers about half the gap (PAN `p99` 1740, BELL 1725);
  partial redraw alone gives ~15 us; signature-dirty skips 87% of frames in idle
  but not during heavy audio. None of these meets the PAN `p99 <= 1733` gate, so
  **no UI change is promoted**; candidate 25 remains the default.
- **Phase B — negative result.** The attack->sustain render segmentation is
  **rejected** (measured event regression). The connected phase profile shows the
  attack-specific cost is ~40% trigger and ~60% exciter; no safe attack
  micro-optimization closed the remaining event misses.
- Candidate **26** (exact headroom table) is host-bit-exact and firmware-links,
  but shows no reliable production gain; **not promoted**.

---

## Fine histogram (§10, §11)

`polyphony_forensics.h` now takes `POCKETPAN_FORENSICS_FINE_BINS`
(0 = legacy 25 us, 5 = 5 us, 10 = 10 us). The bin array auto-sizes to cover the
2666.7 us deadline. `[CURVE]` records `p95_us`, `p99_us`, `p995_us`, `p999_us`,
`max_us`, `deadline` and `bin_us`; `[HIST] lower_us` uses the active width.
`tests/analyze_m635_tail.py` reads `bin_us` and still parses legacy captures.

---

## Phase A — connected isolation results

All variants keep BLE connected (interval unchanged) and the same audio
workload. Fixtures critical-only, 8192 blocks, 5 us bins. `avg/max` in us.

| Variant | PAN cl steady avg / p99 / max | PAN cl event avg / max / miss | BELL cl steady avg / p99 / max | BELL cl event avg / max / miss |
|---|---|---|---|---|
| **U0** normal (c25) | 1633 / 1785 / 2069 | 2490 / 2674 / **1** | 1619 / 1770 / 2144 | 2499 / 2681 / **1** |
| **U1b** logic-only | 1624 / 1690 / 1953 | 2412 / 2523 / **0** | 1609 / 1685 / 1939 | 2440 / 2626 / **0** |
| **U3** no full clear | 1633 / 1775 / 2054 | 2493 / 2681 / 1 | 1618 / 1785 / 1986 | 2543 / 2895 / 5 |
| **dirty** (signature) | 1633 / 1785 / 2118 | 2526 / 2901 / 4 | 1619 / 1775 / 2185 | 2529 / 2957 / 1 |
| **div6** (5 Hz render) | 1625 / 1740 / 2174 | 2496 / 2736 / 1 | 1610 / 1725 / 1984 | 2513 / 2732 / 1 |
| **div10** (3 Hz render) | 1624 / 1750 / 1969 | 2501 / 2582 / 0 | 1609 / 1730 / 2089 | 2536 / 2719 / 1 |
| **partial** Status redraw | 1632 / 1770 / 2133 | 2510 / 2681 / 1 | 1617 / 1755 / 2163 | 2528 / 2715 / 2 |

Reproducibility: U0 PAN steady `p99` = 1785 / 1790 / 1785 across three runs;
U1b = 1690 / 1695. The ~95 us gap is real. U0 `event max` varies 2549-2682 us
between runs and the miss count varies 0-1 (BELL similar), i.e. the event tail
is near the deadline and sensitive to rare system jitter.

Instrumentation: the signature-dirty build logged `[UI] dirty_render=21
dirty_skip=137` in the first 5.8 s, i.e. **87% of frames skipped**, yet the tail
was unchanged — the skips are all in the idle/low-activity gaps, and during the
heavy cluster the displayed CPU value changes every frame.

### Interpretation (§4)

```text
U1b p99 (1690) << U0 p99 (1785)   -> framebuffer rendering implicated
U3 p99 (1775) ~= U0 p99           -> the full clear is NOT the cost
div6/div10 p99 1740-1750          -> render/transfer COUNT matters
dirty 87% skip, no gain           -> skips only the idle frames
partial redraw, ~15 us gain       -> transfer SIZE matters little; COUNT matters
```

Root cause: the UI **render + LCD transfer** performed on Core 1 interferes with
the Core 0 audio block, adding ~90-100 us to affected blocks. This is consistent
with the GDMA/SPI transfer and its ISR competing with the I2S DMA. The BLE poll
was not isolated separately in this pass (variant built, run `NOT RUN`), but the
U1b result — which keeps the poll — already excludes it as the dominant source.

### Event deadline misses

U1b leaves the DSP path alone and still shows occasional BELL event misses
(one repeat run: max 2711 us > 2666.7). So the event misses have two
contributors: (a) UI interference (~150 us on the worst event block) and (b) an
attack path that is itself close to the deadline. Candidate 25's event
`max` under normal UI is 2658-2901 us across runs.

---

## Phase B — attack profiling and attempt (§18-§28)

Profiling build (`CONFIG_POCKETPAN_DSP_PROFILE=1`, diagnostic only, perturbed)
on candidate 25, critical fixtures:

**PAN cluster8 event block (total 2746 us, profiled):**

```text
trigger probes 336.1 us (12.2%)
  exciter setup     122.2 us   <- largest single trigger phase
  register/pitch     45.4 us
  modal coefficients 39.7 us
  coupling           21.8 us
  MIDI dispatch      17.1 us
  prepared lookup    14.2 us
  voice allocation   12.3 us
  other trigger      63.4 us
render 2269.6 us
  modal kernel     1007.7 us   (nested)
  exciter           490.0 us   (nested)
  limiter           276.7 us
  mix               136.1 us
  body               97.9 us
  PCM                17.5 us
unaccounted/mix     140.3 us
```

BELL chord4 event trigger is 290.6 us (exciter setup 106.2 us). The
attack-specific delta over steady (~850-900 us) is therefore roughly **60%
exciter render + 40% trigger setup**, not the modal bank. This is why candidate
21's exciter fast path only recovered a few percent: the exciter kernel is
already specialized, but it still runs for 8 voices x up to 384 samples
(>= one 128-frame block).

**Attack->sustain segmentation (rejected).** Segmenting the attack sample loop
at the exact exciter end (`Exciter::samplesUntilInactive()`, §22) was implemented
and bit-exact on host. On hardware it **regressed** the event:

```text
              c25 event avg   c26 event avg   deadline
PAN cluster8  2490 us         2568 us         1 -> 2
BELL cluster8 2500 us         2607 us         1 -> 7
```

Two independent reasons: (1) the 128-frame event block never reaches the exciter
end (noise burst is >= 144 samples), so the segment only helps the *following*
blocks, which are counted as steady here; (2) the extra per-sample branch and the
doubled hot-loop body cost more than the exciter samples they save.
`POCKETPAN_ATTACK_SEGMENT` is therefore compiled out (documented negative
result). Candidate 26 was reduced to the headroom table only.

**Candidate 26 = 25 + exact headroom target table (§28).** Voice count is an
integer 0..8, so the historical per-block
`pow(10, max(voices<=1?0:-1.5*log2(voices), -5)/20)` is precomputed once at
`init` and read per block. Host differential test: **bit-exact** (9 entries).
Connected measurement shows no reliable gain (event differences between the c25
and c26 builds are dominated by code-layout/run variance). **Not promoted.**

---

## Phase C — production UI candidates (implemented, measured, not promoted)

| Option | Mechanism | Measured PAN / BELL steady p99 | Visual/control |
|---|---|---|---|
| `POCKETPAN_UI_PARTIAL` | static background drawn once; per-frame only changed fields erased/redrawn; only their row band transferred via `Display::updateRows` | 1770 / 1755 | identical; control + input still 30 Hz |
| `POCKETPAN_UI_RENDER_DIVIDER=N` | control/telemetry/BLE at 30 Hz, visual redraw every Nth loop | div6 1740 / 1725; div10 1750 / 1730 | control + input 30 Hz, visual 5/3 Hz |
| `POCKETPAN_UI_DIRTY` | skip the whole render when displayed text is unchanged | 1785 / 1775 (no gain during heavy audio) | identical |
| `POCKETPAN_UI_NO_FULL_CLEAR` | diagnostic only | 1775 / 1785 | diagnostic |

None reaches PAN `p99 <= 1733`; the best measured is `div6`/`partial`. Because
the target is not met and enabling a UI change is production-visible, all UI
options stay **default OFF** and candidate 25 remains the default. No visual
redesign was performed; the partial path is pixel-identical by construction
(same strings, same positions, same colors, only the transfer region differs).

Input latency is never coupled to redraw: buttons and the BOOT poll remain in the
30 Hz loop in every option (§34).

---

## Connected gate matrix (§38-§43, candidate 25 U0 normal)

| Gate | Required | Result |
|---|---|---|
| PAN cluster steady avg | <= 1733 | 1633 PASS |
| PAN cluster steady p99 | <= 1733 | 1785 **FAIL** |
| PAN cluster steady max (preferred) | <= 2133 | 2069 PASS (hard: < 2666.7 PASS) |
| BELL cluster steady avg | <= 1733 | 1619 PASS |
| BELL cluster steady p99 | <= 1733 | 1770 **FAIL** |
| BELL cluster steady max | <= 2133 | 2144 **FAIL** (hard PASS) |
| PAN cluster event deadlineMisses | 0 | **1** FAIL |
| PAN cluster event max (hard) | < 2666.7 | 2674 FAIL (preferred 2133 FAIL) |
| BELL cluster event deadlineMisses | 0 | **1** FAIL |
| BELL cluster event max (hard) | < 2666.7 | 2681 FAIL |
| BELL chord4 steady/event | <= 1733 / <= 2133 | 965/1120/1251, 1522/1822 PASS |
| CPU | <= 65% | ~62% PASS |
| I/O timeout/short/tx | 0/0/0 | 0/0/0 PASS |
| BLE lost / MIDI drops | 0 / 0 | 0 / 0 PASS |

With the UI render removed (U1b), the steady gates pass (PAN p99 1690,
BELL p99 1685, both max <= 2014) and the event misses go to zero in run 1, one
in a repeat — evidence that the DSP path itself is inside the gate once UI
interference is gone.

---

## Production smoke (§44, forensics OFF)

```text
avg 346-348 us   p99 400 us   max 515 us   cpu_load 13.0%   deadline 0
timeout 0        short 0      tx_error 0
internal_free 153443 bytes    largest_internal 51200 bytes
BLE state=8 (Ready)  interval 11.25 ms  latency 0  rssi -65..-79  reconnects 0
```

---

## Exactness

```text
host ctest, candidates 20-26 .................. 8/8 PASS each
headroom table vs formula ..................... bit-exact (9 entries)
PAN 12/12, modal kernel 15/15, PreparedNote ... exact
sustain fast path on/off ...................... exact
attack fast path on/off (c26, segment off) .... exact
sympathetic coefficient cache ................. bit-exact (21 configs)
Bell reference WAVs ........................... no drift
```

No golden was regenerated; no `-ffast-math`, no reassociation, no Bell/mode
ablation, no polyphony/rate/block change.

---

## Integrity

- No preset, modal ratio, gain, T60, doublet, velocity curve, sympathetic gain,
  body voicing, limiter setting, polyphony (8), sample rate (48 kHz) or block
  size (128) changed.
- All new behavior is behind compile-time gates; the production default is
  unchanged at candidate 25.

## Production candidate

```text
25 — remains default (POCKETPAN_DSP_CANDIDATE=25), connected-qualified baseline.
26 — 25 + exact headroom table; host bit-exact, firmware links, NOT promoted.
```

## Bell ablation authorized: NO

PAN cluster steady `p99` (1785 us) still fails under the normal production UI,
so the `PAN PASS / Bell FAIL` authorization rule is not met. No ablation was
prepared or performed.

## Next

```text
1. Close the PAN steady p99 tail: the evidence points at the LCD transfer/ISR
   competing with I2S DMA. Investigate moving the SPI/LCD ISR off the audio
   core, a non-blocking transfer path, or a synchronized render window, and
   measure with the fine-bin forensics build.
2. Re-test the event matrix after the UI fix; the attack path is also close to
   the deadline in isolation (BELL event max ~2711 with UI off).
3. Only then re-run the full §38 matrix and promote a UI change.
4. Bell V1 freeze and Bell ablation remain gated on PAN passing the formal
   steady gate under normal production UI.
```

## Reproduction

```sh
# Host exactness
cmake -S tests -B build-host-m636-c26 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DPOCKETPAN_DSP_CANDIDATE=26 && cmake --build build-host-m636-c26 --parallel
ctest --test-dir build-host-m636-c26 --output-on-failure

# Connected forensics (candidate 25, 5 us bins)
idf.py -B build-m636-fx-c25 -DSDKCONFIG=build-m636-fx-c25/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.m635.defaults" \
  -DPOCKETPAN_DSP_CANDIDATE=25 -DPOCKETPAN_FORENSICS_CRITICAL_ONLY=1 \
  -DPOCKETPAN_FORENSICS_FINE_BINS=5 build
idf.py -B build-m636-fx-c25 -p COM10 flash

# UI isolation variants: add one of
#   -DPOCKETPAN_UI_LOGIC_ONLY=1 | -DPOCKETPAN_UI_NO_FULL_CLEAR=1
#   -DPOCKETPAN_UI_DIRTY=1 | -DPOCKETPAN_UI_RENDER_DIVIDER=6 | -DPOCKETPAN_UI_PARTIAL=1

# Profiling (diagnostic only, perturbed)
idf.py -B build-m636-prof -DSDKCONFIG=build-m636-prof/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.m636.profile.defaults" \
  -DPOCKETPAN_DSP_CANDIDATE=25 -DPOCKETPAN_FORENSICS_CRITICAL_ONLY=1 build
python tests/summarize_forensics.py docs/hardware/m636_c25_profile_connected.log
```
