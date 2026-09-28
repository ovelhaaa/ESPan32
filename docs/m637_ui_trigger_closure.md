# M6.3.7 — UI render decoupling + exact trigger precompute

Branch: `codex/m632-modal-note-cache`. Entry baseline: `488d5df` (M6.3.6).
Production DSP candidate: **25**. This pass ran on the **physical board** with a
connected BLE-MIDI link. No sound parameter is touched.

Board: ESP32-S3 (QFN56) rev v0.2, MAC `b4:3a:45:ae:6f:28`, USB-Serial/JTAG on
**COM10**, 4 MB flash / 2 MB PSRAM, 240 MHz, ESP-IDF 5.3.0. BLE-MIDI central
connected to **SMK25V2**, `state=8` (Ready), interval **11.25 ms**, latency 0.
Forensics builds, profiling OFF, 8192 blocks/fixture, **5 us** histogram bins,
`POCKETPAN_FORENSICS_CRITICAL_ONLY=1` (fixtures 5 = PAN cluster8, 14 = BELL
chord4, 13 = BELL cluster8). Raw captures under `docs/hardware/m637_*`.

---

## Commit(s)

Working tree (source + docs).

```text
main/dsp/trigger_precompute.h    exact MIDI-velocity index + pow115 table
main/dsp/exciter.h/.cpp          eager strike-pow LUT at config boundary
main/dsp/modal_voice.h/.cpp      cached register logarithms; hardness LUT
main/dsp/dsp_profile.h           candidate 27/28/29 (velocity LUT)
main/ui/ui_renderer.h/.cpp       urgent/telemetry hash redraw policy + timing
main/app/app_main.cpp            shouldRender() policy, UIREDRAW/UITIME logs
main/CMakeLists.txt              UI policy on by default; UI options
tests/test_m637_trigger.cpp      velocity LUT + register-log differential test
tests/CMakeLists.txt             new target
docs/m637_ui_trigger_closure.md  this file
docs/hardware/m637_*             raw connected evidence
```

---

## M6.3.7: PARTIAL

- **Phase A / D — UI.** A formatting-free, two-signature redraw policy was
  implemented and measured. It **closes the event deadline gate** (0 misses on
  PAN cluster8, BELL chord4, BELL cluster8) with no steady regression and a fully
  responsive control loop (urgent redraws immediate). It does **not** meet the
  steady `p99 <= 1733` gate (PAN 1780, BELL 1760).
- **U1b control** (render fully removed) **passes every gate** (PAN `p99` 1700,
  BELL `p99` 1675, event misses 0), proving the audio path is capable and the
  residual steady tail is UI-induced.
- **Phase G — trigger.** The exact velocity LUT and register-log cache are
  host bit-exact but give **no measurable event gain** (PAN event avg -0.2%),
  so they are **not promoted**.
- A `PreparedNote` struct extension was additionally attempted and **rejected**:
  it reproducibly caused a boot panic (see "Negative results").

---

## Verified / rejected on hardware

| Variant | PAN cl. steady avg/p99/max | PAN cl. event avg/max/miss | BELL cl. steady avg/p99/max | BELL cl. event avg/max/miss |
|---|---|---|---|---|
| **c25** baseline | 1636 / 1775 / 2050 | 2475 / 2800 / **2** | 1617 / 1780 / 2089 | 2500 / 2752 / **2** |
| **U1b** no render | 1628 / **1700** / 1941 | 2410 / 2524 / **0** | 1608 / **1675** / 1971 | 2445 / 2562 / **0** |
| UI 1 Hz (old fmt) | 1636 / 1770 / 2041 | 2481 / 2689 / 1 | 1617 / 1760 / 2058 | 2498 / 2700 / 2 |
| UI 2 Hz (old fmt) | 1638 / 1790 / 2057 | 2473 / 2639 / 0 | 1618 / 1770 / 2182 | 2511 / 2879 / 3 |
| **UI 5 Hz (hash)** | 1632 / 1780 / 2067 | 2470 / 2610 / **0** | 1613 / 1760 / 1996 | 2502 / 2648 / **0** |
| c27 trigger (no UI) | 1634 / 1780 / 2108 | 2470 / 2798 / 2 | 1619 / 1765 / 2162 | 2499 / 2689 / 2 |

Facts established:

```text
U1b passes all steady/event gates                -> tail is UI render, not DSP
c25 / 1 Hz / 2 Hz / 5 Hz steady p99 all ~1770-1790, UI5-ish
  -> the steady p99 level is NOT set by redraw frequency
U1b vs every rendering build                     -> the render's mere presence adds ~75 us at p99
event deadline: c25 2 -> UI policy 0             -> fewer/urgent redraws remove the event misses
```

The steady tail mechanism is therefore not "number of redraws" but the act of
rendering itself (framebuffer formatting + bus/cache pressure on Core 1 while
Core 0 runs the audio block). Reducing cadence does not help; only removing the
render does.

---

## Phase A — Core-1 workload timing (diagnostic, `POCKETPAN_UI_TIMING`)

Measured with the snprintf-signature variant (representative), per 5 s window:

```text
state avg ~150 us (telemetry read + string formatting; includes BLE preemption)
render avg ~360 us  fb avg ~110 us  lcd avg ~5 us  render max up to ~1040 us
```

After the formatting-free hash rewrite the per-loop state cost fell
(`state avg ~80 us`) and render-avg dropped to ~80 us, but the audio steady
`p99` was unchanged — confirming the residual is broad render interference, not
the string formatting or the LCD submit (which is only ~5 us).

---

## Phase D — selected UI architecture

Adopted as production (`POCKETPAN_UI_URGENT=1`, `POCKETPAN_UI_PARTIAL=1`,
`POCKETPAN_UI_TELEMETRY_PERIOD_MS=200`):

- The control loop stays at ~30 Hz (buttons, BOOT, BLE poll, telemetry read,
  model requests) — input latency is never coupled to redraw.
- Two **formatting-free 64-bit FNV-1a fingerprints** over raw integer / float-bit
  fields: `urgentHash` (screen, preset/model, root/note, velocity, BLE state,
  voice count, MIDI-monitor fields) and `telemetryHash` (CPU, avg/p99/max,
  deadline, RSSI, heap, limiter/peak readouts).
- Redraw policy: urgent change → redraw this loop; telemetry-only change →
  redraw at most once per 200 ms; otherwise no framebuffer work. Both
  fingerprints commit only on a frame that redraws, so deferred changes are not
  lost.
- `POCKETPAN_UI_PARTIAL` keeps the pixel-identical partial Status redraw.

Visual latency: urgent changes (note/preset/root/BLE/screen) render on the next
30 Hz loop (<= 33 ms); telemetry fields update at 5 Hz.

---

## Phase G — trigger precompute (candidate 27)

Exact, host bit-exact, **not promoted** (no measurable gain):

- `velocityPow115Table()` — config-independent `pow(v, 1.15)` for the 128 MIDI
  velocities (512 B), used by `configureStrike` hardness.
- Per-`Exciter` `strikePowLut_` (512 B x 8 voices = 4 KB `.bss`) —
  `pow(energyVelocity, 1.25)` for the 128 velocities, built **eagerly** at the
  config boundary (a lazy in-event build was measured to cause a one-off
  multi-ms event spike and was removed).
- `ModalVoice` caches `log(registerLowHz/HighHz)` (configuration constants) so
  `registerPositionFor()` does one `log()` instead of three.

`tests/test_m637_trigger.cpp` asserts `bits(table[i]) == bits(formula)` for all
128 velocities x 2 models and the full exciter strike streams on/off bit-equal,
plus the cached register-position equality. Engine golden `test_dsp` PAN 12/12
confirms the PCM is unchanged.

Connected result: PAN cluster event avg 2475 -> 2470 (-0.2%), max 2800 -> 2798,
deadline 2 -> 2. **Below the >=3% keep gate and does not remove misses.**

---

## Negative results (documented, not shipped)

- **`PreparedNote` struct extension.** Adding four per-note register floats to
  `PreparedNote` reproducibly caused a boot panic — `Interrupt wdt timeout on
  CPU0` in `sAudio.init()`/`gpio_config` logging — for candidate 25, 27 and the
  unmodified M6.3.6 binary rebuilt with the change. A bisect on the physical
  board isolated it to the struct growth: an equal-sized (2.4 KB) `.bss` pad
  booted fine, but the fields inside `PreparedNote` did not. The approach was
  dropped; the register win is limited to the cached logarithms. (The board
  itself was concurrently wedged and required a physical power cycle; the panic
  reproduced before and after the power cycle, so it is code-triggered.)
- **Attack->sustain segmentation** stays disabled (M6.3.6 negative result).

---

## Exactness

```text
host ctest candidates 20-27 ......... 9/9 PASS (incl. test_m637_trigger)
velocity LUT vs formula ............. bit-exact (128 velocities x 2 models)
register log cache .................. bit-exact
PAN 12/12 / modal 15/15 / PreparedNote exact
sustain / attack fast path on/off ... exact
sympathetic coefficient cache ....... bit-exact
Bell reference WAVs ................. no drift
```

No golden regenerated; no `-ffast-math`, reassociation, Bell/mode ablation,
polyphony/rate/block change.

---

## Production smoke (candidate 25 + UI policy, forensics OFF)

```text
BLE state=8 (Ready)  interval 11.25 ms
I/O timeout 0  short 0  tx_error 0  reconnects 0
idle avg ~4 us  p99 25 us  cpu 0.1%  long-run deadline ~7 over ~31 min
internal_free 121819  largest_internal 31744
[UIREDRAW] urgent=9 telemetry~2.5/s skip dominant
```

---

## Gates

| Gate | Required | c25 | M6.3.7 (UI policy) | Result |
|---|---|---|---|---|
| PAN cluster steady avg | <=1733 | 1636 | 1632 | PASS |
| PAN cluster steady p99 | <=1733 | 1775 | 1780 | **FAIL** |
| PAN cluster steady max | <=2133 | 2050 | 2067 | PASS |
| BELL cluster steady avg | <=1733 | 1617 | 1613 | PASS |
| BELL cluster steady p99 | <=1733 | 1780 | 1760 | **FAIL** |
| BELL cluster steady max | <=2133 | 2089 | 1996 | PASS |
| BELL chord steady | <=1733/1733/2133 | PASS | PASS | PASS |
| event deadline misses (all) | 0 | 2 | **0** | **PASS** |
| CPU | <=65% | ~62% | ~62% | PASS |
| I/O / BLE / MIDI drops | 0 | 0/0 | 0/0 | PASS |

Only the two 8-voice cluster **steady p99** gates remain red; U1b proves they are
UI-induced and the audio path passes in isolation.

---

## Production candidate

```text
DSP 25 unchanged (default).  UI urgent/telemetry policy promoted (default on).
27/28/29 remain selectable, host-exact, not promoted.
```

## Bell ablation authorized: NO

PAN cluster steady `p99` still fails under the production UI, so the
`PAN PASS / Bell FAIL` rule is not met. No ablation performed.

## Next

```text
1. Close the steady p99: the render's mere presence costs ~75 us at p99 but
   cadence does not matter.  Investigate moving the framebuffer render off the
   audio core's cache/bus window (e.g. a dedicated low-priority pinning, or an
   off-core draw path), measured with the 5 us forensics build.
2. Re-run the full matrix; if PAN + BELL cluster p99 <= 1733 with I/O 0/0/0 and
   event deadline 0, M6.3.7 -> PASS and stop generic optimization.
3. Trigger precompute re-evaluated only alongside a passing UI.
```

## Reproduction

```sh
cmake -S tests -B build-host-m637 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DPOCKETPAN_DSP_CANDIDATE=27 && cmake --build build-host-m637 --parallel
ctest --test-dir build-host-m637 --output-on-failure

idf.py -B build-m637-ui5 -DSDKCONFIG=build-m637-ui5/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.m635.defaults" \
  -DPOCKETPAN_DSP_CANDIDATE=25 -DPOCKETPAN_FORENSICS_CRITICAL_ONLY=1 \
  -DPOCKETPAN_FORENSICS_FINE_BINS=5 -DPOCKETPAN_UI_URGENT=1 \
  -DPOCKETPAN_UI_PARTIAL=1 -DPOCKETPAN_UI_TELEMETRY_PERIOD_MS=200 build
idf.py -B build-m637-ui5 -p COM10 flash
python tests/capture_forensics.py docs/hardware/m637_ui5_connected.log \
  --port COM10 --rows 6 --seconds 200
```

## Integrity

- No preset, modal ratio, gain, T60, doublet, velocity curve, sympathetic gain,
  body voicing, limiter setting, polyphony (8), sample rate (48 kHz) or block
  size (128) changed.
- All trigger behavior is behind candidate macros; the UI policy changes only
  when a redraw is issued, never what is drawn.
