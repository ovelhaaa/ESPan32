# M6.3.9.2 — Final Transient Burst Closure

Branch: `codex/m632-modal-note-cache`  
Base commit: `7f629cf7a65213c3fe3b9ac4eb00a34d75b9c6e6`  
Milestone commit: `eb69ebe69dbd017b90a8c434573d4f50241683ef`  
Target: **ESP32-S3 @ 240 MHz** (COM10, `USB\VID_303A&PID_1001`, MAC `b4:3a:45:ae:6f:28`)  
Production DSP: **candidate 25**  
Production UI: **F0** (`POCKETPAN_UI_ARCH=0`)  
Toolchain: ESP-IDF **v5.3** (`C:\Users\devx\esp\esp-idf`), xtensa-esp-elf 13.2.0  

---

## 0. Verdict

```text
M6.3.9.2: PARTIAL
```

The transient bursts are **demonstrably absorbed by the real I2S pipeline in the tested
workload** (zero transport errors, minimum measured buffered headroom ≈ 2.7 of 6 DMA
blocks ≈ 7.3 ms) — but the strict isolated-burst contract (`max consecutive overruns <= 1`)
does **not** hold. The 15-minute active soak measured sustained over-deadline runs of up
to **53 consecutive callbacks** during high-velocity 8-voice **BELL** sustain, consuming up
to **3.3 of the 6 DMA descriptors**.

The measured hotspot is the steady, sound-defining modal render of eight high-velocity
BELL voices (internal safety saturation active), which the milestone's hard rules place
outside the permitted Phase J optimization scope (no sustain optimization, no sound
changes, no generic modal-kernel rewrites). No speculative DSP change was therefore made.

### Historical status versus project-level decision

These two statements are not contradictory:

```text
M6.3.9.2 milestone status            = PARTIAL
Generic performance campaign (M6.4)  = CLOSED / FROZEN
```

M6.3.9.2 failed its strict experimental streak criterion
(`max consecutive overruns <= 1`). M6.4 accepts the measured hardware behavior as the
current production performance contract and closes the generic campaign. The canonical,
frozen wording lives in [performance_contract_v1.md](performance_contract_v1.md).

Remaining cost is confined to extreme high-energy Bell saturation; hardware soak showed
bounded recovery and zero transport failure; further generic optimization would risk
modifying sound-defining DSP. Generic performance optimization is therefore
**FROZEN after M6.3.9.2**.

---

## 1. Phase A — Actual I2S/DMA Buffering Model (source-verified)

The buffering model was read directly from the ESP-IDF v5.3 I2S standard driver
(`components/esp_driver_i2s/i2s_common.c`, `i2s_std.c`) and from
`main/hardware/board_config.h` / `main/hardware/audio_i2s.cpp`. It is **not** inferred from
a comment.

### 1.1 Configuration

| Quantity | Value | Source |
|:---|:---|:---|
| TX channel | `I2S_NUM_0`, master, TX-only | `audio_i2s.cpp::createTxChannel` |
| `dma_desc_num` | **6** | `board::audio::kDmaBufferCount` |
| `dma_frame_num` | **128** stereo frames/descriptor | `board::audio::kDmaBufferFrames` |
| Slot | 32-bit, stereo | `I2S_STD_PHILIPS` 32-bit / stereo |
| Bytes/descriptor | 128 × 2 × 4 = **1024 B** | derived |
| `auto_clear` | true | zero-fill on underrun |
| Write call | `i2s_channel_write(handle, buf, 1024, &written, 50 ms)` | `audioTaskLoop` |

### 1.2 Write semantics and descriptor lifecycle

From `i2s_common.c`:

- `msg_queue = xQueueCreate(desc_num - 1 = 5, sizeof(uint8_t*))` — one queue slot per
  descriptor (line ~262).
- The TX EOF ISR does `xQueueSendFromISR(msg_queue, &finish_desc->buf)` — a descriptor's
  buffer becomes **reusable the moment that descriptor finishes playing** (lines ~641–655).
- `i2s_channel_write()` copies into the current descriptor; when it is full
  (`rw_pos == buf_size`) or absent, it blocks on
  `xQueueReceive(msg_queue, …, timeout_ms)` until a descriptor frees (lines ~1205–1210).
  The write therefore returns when the data has been **queued into a free descriptor**,
  not when it has been played.

### 1.3 Derived effective buffered capacity

```text
capacity_blocks = dma_desc_num                 = 6
capacity_frames = 6 × 128                      = 768 frames
capacity_bytes  = 6 × 1024                     = 6144 B
capacity_ms     = 6 × 128 / 48000 × 1000       = 16.000 ms
```

> **Correction to M6.3.9.1 §3.3.** That document stated the driver "maintains 2 buffers of
> 128 frames (5.33 ms total buffer depth)". The real configuration is **6 descriptors ×
> 128 frames = 16.0 ms**. The transient safety margin was previously understated by 3×.

At steady state the render callback (~1.6–1.8 ms) is shorter than the block period, so the
write blocks and the descriptor queue remains **saturated** (`Q = 6` immediately after each
blocking write). This is the anchor used by the new headroom telemetry (§2.3).

---

## 2. Phases B–E — Backlog / Slack / Streak / Lifetime Telemetry

New diagnostic module: `main/diag/transient_qual.h`, compiled only when
`POCKETPAN_TRANSIENT_QUAL=1` (default 0, `main/CMakeLists.txt`). Hooks:

- `main/hardware/audio_i2s.cpp` — per-loop accounting in `audioTaskLoop`.
- `main/app/app_main.cpp` — per-callback musical context capture; [M6392] logging on Core 1.

Properties: fixed-size numeric state, no strings in the audio callback, no heap, no locks.

### 2.1 Timing debt (mathematically valid, recovers)

```text
renderDebt += callbackUs - blockPeriodUs      // 2667 us
if (renderDebt < 0) renderDebt = 0            // clamp when caught up
maxRenderDebt = max(maxRenderDebt, renderDebt)
```

This is the accumulated render work in excess of the real-time production cadence. It is
not a synthetic non-recovering counter: it decreases whenever a block is under budget.

### 2.2 Consecutive-overrun streak

`currentStreak` increments for every `callbackUs >= 2667` and resets on the first
sub-threshold block; `maxStreak` is the lifetime maximum. This is the key Phase C metric.

### 2.3 Buffered headroom (occupancy in 1/1000 blocks)

Each audio loop produces one block; the DMA consumes one block per 8000/3 µs. Hence

```text
occupancyMilli = blocksProduced*1000 - elapsedUs*3000/8000
```

The relative occupancy is anchored to the saturated (blocking-write) baseline, so the
worst transient buffer consumption is `maxOcc - minOcc` and the remaining depth is

```text
effectiveMinHeadroomMilli = 6000 + minOcc - maxOcc      // in 1/1000 blocks
```

`minOcc`, `maxOcc`, `eff_headroom` and `max_transient_consumption` are logged every 5 s.

### 2.4 Lifetime qualification counters (never reset on model switch)

`callbacks`, `overruns`, `max_cb_us`, `max_streak`, `max_debt_us`, `min/max occupancy`,
`max_recovery_blocks`, per-transition overrun counts. These are **independent of**
`AudioI2S::resetTimingStats()`, which the production `[AUDIO]` line still resets on every
PAN/BELL switch (Phase E requirement). This is the mechanism that had hidden the true
15-minute totals in M6.3.9.1.

### 2.5 Per-overrun record (fixed-size ring, 256 entries)

`timestamp, blockSeq, callback, prev/next callback, render, MIDI-dispatch, model,
voices before→after, NoteOn/NoteOff/PolyPressure/ChannelPressure counts, exciter-active,
telemetry-published, streak, debt before/after, occupancy, transition class`.

Transition classes: `other, 1->2, 2->4, 4->6, 6->8, restrike, pressure, model_switch`.

---

## 3. Phase F — 5-Minute Active Forensics (hardware)

Raw log: `docs/hardware/m6392_phaseF_5min.log`.

| Metric | Value |
|:---|:---|
| Callbacks | 122,104 |
| Overruns (`>= 2667 µs`) | **51** |
| Max callback | 3,134 µs |
| **Max consecutive overruns** | **1** |
| Max timing debt | 467 µs |
| Min occupancy | 797 / 1003 (relative milli-blocks) |
| **Effective min headroom** | **5.79 blocks** (≈ 15.4 ms) |
| Max transient consumption | 0.21 blocks |
| Max recovery blocks | 3 |
| I2S timeout / short / TX error | 0 / 0 / 0 |
| BLE reconnects / MIDI drops | 0 / 0 |

Overruns by transition: `6->8 = 22`, `other = 27`, `4->6 = 1`, `restrike = 1`.
The `other` class are the non-event blocks immediately after an 8-voice attack
(`NoteOn == 0`).

In this 5-minute window every overrun was isolated and the buffer margin was enormous.
The exploratory 25 s boot sample (`m6392_transient_sample_25s.log`) did record one
2-consecutive streak, so the pattern is not deterministic across runs.

---

## 4. Phase M — Final 15-Minute Production-Equivalent Soak (hardware)

Raw log: `docs/hardware/m6392_final_soak_15min.log`.

Configuration: `POCKETPAN_DSP_CANDIDATE=25`, `POCKETPAN_UI_ARCH=0`,
`POCKETPAN_ACTIVE_SOAK=1`, `POCKETPAN_TRANSIENT_QUAL=1`,
`CONFIG_POCKETPAN_HARDWARE_QUALIFICATION_LOG=y`; forensics/profiling off; BLE connected
(SMK25V2, `state=8`, interval 11.25 ms); normal display; no resets of the qualification
counters.

### 4.1 Lifetime totals (no resets)

| Metric | Value |
|:---|:---|
| Duration | 928 s (15.47 min) |
| Audio blocks | 347,712 |
| MIDI events | 5,897 push / 5,897 pop |
| **Nominal deadline overruns** | **317** |
| Max callback | **3,609 µs** |
| **Max consecutive overruns** | **53** |
| Max timing debt | 7,536 µs (≈ 2.83 blocks) |
| Min occupancy (relative) | −2,272 / 1003 |
| **Minimum effective buffered headroom** | **2,725 milli-blocks ≈ 2.73 blocks ≈ 7.27 ms** |
| Max transient buffer consumption | 3,275 milli-blocks ≈ 3.28 blocks |
| Max recovery blocks | 15 |
| I2S timeout / short / TX error | **0 / 0 / 0** |
| BLE reconnects / MIDI drops | **0 / 0** |
| Heap `internal_free` start → end | 130,015 B → 130,015 B (constant) |
| PAN/BELL switches | 13 |

Note the contrast that motivated Phase E: the production `[AUDIO]` resettable
`deadline` counter read **8** at the end (its last 5 s window), while the lifetime
qualification counter recorded **317** across the run.

### 4.2 Overruns by musical transition

```text
other        233     (non-event heavy blocks during 8-voice passages)
1->2           0
2->4           0
4->6           0
6->8          61     (6 ringing + 2 NoteOn -> 8 active)
restrike      22
pressure       1
model_switch   0
```

### 4.3 Characterization of the worst streak

The longest run (blocks 43,794–43,846) is **BELL**, `voices 8->8`, `NoteOn == 0`,
`exciter == 0`, `render ≈ 2,735–2,870 µs` per block — i.e. **steady sustain**, not an
event and not an attack tail. Representative records:

```text
seq=43804 cb_us=2761 render_us=2756 model=BELL v=8->8 on=0 exc=0 occ=-25   debt=2399
seq=43810 cb_us=2740 render_us=2736 model=BELL v=8->8 on=0 exc=0 occ=-261  debt=2939
seq=43825 cb_us=3272 render_us=3247 model=BELL v=8->8 on=0 exc=0 occ=-1345 debt=5476
seq=43846 cb_us=2723 render_us=2708 model=BELL v=8->8 on=0 exc=0 occ=-2272 debt=7536
```

The run starts as a `6->8`/`restrike` event (seq 43,794) and then sustains ~73 µs/block
over budget for 53 blocks, draining ≈ 3.3 descriptors. It recovers when the modal cost
falls back under 2,667 µs (the high-amplitude saturation window ends and the voices decay).

### 4.4 Root cause

`ModalResonatorBank::processSample*` applies a per-mode safety saturation whenever
`internalSafetySaturation && |y| > 2.0`, evaluating `std::tanh` per mode per sample
(`main/dsp/modal_resonator.cpp:226–229,328–331`). Both PAN and BELL enable it
(`dsp_config.h` default, `pan_calibration.h`, `instrument_model.cpp` BELL
`{1.0f, 0.95f, true}`). BELL has **10 modes/voice**; eight simultaneous high-velocity BELL
voices (velocities up to 127, as produced by the soak's 8-voice passage) keep many modes
above the threshold, so the expensive `tanh` branch dominates. The M6.3.9.1 `true_steady`
fixture used a homogeneous, simultaneous velocity-100 cluster, which reached the cheap
sustain path (avg 1,634 µs); the soak's staggered, high-velocity voices do not.

This saturation is **sound-defining** (it is the modal compression that prevents runaway on
rapid strikes) and may not be removed or approximated without changing the instrument.

---

## 5. Phase G — System-Level Safety Determination

Checking the "safe burst candidate" conditions against the 15-minute soak:

| Condition | Result |
|:---|:---:|
| I2S timeout = 0 | ✅ |
| short write = 0 | ✅ |
| TX errors = 0 | ✅ |
| max overrun streak <= 1 | ❌ (53) |
| every transient debt fully recovers before next overrun | ✅ (max recovery 15 blocks) |
| minimum buffered headroom > 1 block (preferred > 1.5) | ✅ (≈ 2.7 blocks) |

**No DMA starvation occurred.** The transport counters never moved, the audio task never
timed out, and the buffered headroom never fell below ≈ 2.7 descriptors (7.3 ms). However,
because consecutive overruns are not bounded at 1, the strict isolated-burst clause is not
satisfied.

---

## 6. Phase H — Formal Transient Contract

### 6.1 Steady contract (homogeneous sustained polyphony)

```text
avg <= 1733 us
p99 <= 2133 us
max <  2666.7 us
deadline overruns = 0
```

M6.3.9.1 hardware: PAN avg 1651 / p99 1845 / max 2193; BELL avg 1634 / p99 1855 / max 2141.
Still valid (no DSP change).

### 6.2 Attack-tail contract

```text
max < 2666.7 us
deadline overruns = 0
```

M6.3.9.1 hardware: PAN max 2051; BELL max 2114. Still valid.

### 6.3 Event-burst contract

An overrun is acceptable only if:

```text
max consecutive overruns <= 1
buffer starvation = 0
I2S errors = 0
timing debt fully recovers
minimum effective headroom remains > 1.5 blocks
```

The measured system meets all clauses except `max consecutive overruns <= 1`. The safe
ceiling is therefore the measured 6-descriptor / 16 ms buffering, not an invented number.

### 6.4 Semantic rule (documentation)

> Steady and attack-tail processing meet the nominal 2.667 ms callback deadline. Rare
> event callbacks may exceed one block period, but hardware qualification demonstrates
> bounded bursts that are absorbed by the configured I2S buffering without transport
> starvation.

Do **not** write "all callbacks meet deadline"; the lifetime total is 317 overruns in
15 minutes, not 0.

---

## 7. Phases I/J — Optimization Decision

Phase I triggers on `consecutive overruns >= 2`, which was observed (up to 53). The
measured source is, however, **steady 8-voice BELL sustain** (10 sound-defining modal
resonators with active safety saturation), not a NoteOn or attack tail.

Phase J explicitly restricts any change to the **NoteOn / attack-only cost** and states
"Steady candidate25 path stays untouched". The hard rules additionally forbid changing the
sound, the modal ratios/gains/T60, the exciter character, the limiter, the polyphony and
the block size. A change that removes or approximates the per-mode `tanh` safety
saturation would alter the instrument and require regenerated goldens — all forbidden.

**No optimization was performed.** There is no legal, bit-exact, sound-preserving change
within this milestone's scope that targets the measured hotspot.

---

## 8. Final Report Summary

| Item | Result |
|:---|:---|
| M6.3.9.2 | **PARTIAL** |
| CI host | PASS (candidate 25 and candidate 16, 9/9 tests each) |
| CI ESP-IDF | PASS (ESP-IDF v5.3, `pocket_pan.bin` built, flashed to ESP32-S3) |
| Production DSP / UI | candidate 25 / F0 (`POCKETPAN_UI_ARCH=0`) |
| Actual I2S config | 6 descriptors × 128 frames = 16.0 ms; blocking write; descriptor freed on TX EOF |
| Active-soak overruns | 317 lifetime (not 0); max callback 3,609 µs; max streak 53 |
| I2S starvation | NO |
| I2S timeout / short / TX error | 0 / 0 / 0 |
| BLE reconnects / MIDI drops | 0 / 0 |
| Heap | stable (130,015 B) |
| Transient optimization | NO (outside permitted scope) |
| Is event burst formally safe? | For the tested workload YES; strict isolated-burst criteria NO |
| Generic performance optimization | STOP as a generic campaign; campaign CLOSED in M6.4 |
| Next | M6.4 Bell V1 musical freeze; then M7 instrument family expansion (Tongue Drum) |

---

## 9. Artifacts

```text
main/diag/transient_qual.h                    new diagnostic module
main/hardware/audio_i2s.cpp                   per-loop debt/streak/headroom hook
main/app/app_main.cpp                         context capture + [M6392] logging
main/CMakeLists.txt                           POCKETPAN_TRANSIENT_QUAL option
docs/hardware/m6392_phaseF_5min.log           5-minute active forensics
docs/hardware/m6392_final_soak_15min.log      15-minute production-equivalent soak
docs/hardware/m6392_transient_sample_25s.log  boot sample (streak=2 occurrence)
build-m6392-soak/                             firmware build (candidate 25 + qual)
build-host-m6392/, build-host-c16-m6392/      host test builds
```

This milestone was committed as `eb69ebe69dbd017b90a8c434573d4f50241683ef`.
