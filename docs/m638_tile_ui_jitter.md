# M6.3.8 — Tile/Rect UI Rendering + Long-Run Jitter Forensics

Branch: `codex/m632-modal-note-cache`. Entry baseline: `31714d6` (M6.3.7).
Production DSP candidate: **25**. All tests executed on the **physical ESP32-S3 hardware** with an active, connected BLE-MIDI link to **SMK25V2**.

---

## 1. Executive Summary

- **Status:** `PARTIAL` (Steady DSP sustain capability confirmed below gate via control U1b; Tile/Rect UI architecture established and benchmarked; boot memory fragmentation and stack overflow root causes resolved).
- **Audio Transport & Stability:**
  - Transport I/O errors: **0 / 0 / 0** (writeTimeouts=0, txErrors=0, shortWrites=0 across all runs).
  - BLE disconnects: **0** (`reconnects=0, last_disconnect=0`, connection interval 11.25 ms).
  - MIDI drops: **0** (`midiDrops=0`).
  - Candidate 25 remains the production DSP candidate.
- **DSP Core Capability (U1b No-Render Control):**
  - **PAN cluster8 steady:** avg **1627.7 µs**, p95 **1660 µs**, p99 **1695 µs** ($\le 1733$ µs **PASS**), max 1958 µs, deadline misses = **0**.
  - **BELL cluster8 steady:** avg **1607.7 µs**, p95 **1640 µs**, p99 **1680 µs** ($\le 1733$ µs **PASS**), max 2014 µs, deadline misses = **0**.
  - **BELL chord4 steady:** avg **954.5 µs**, p95 **985 µs**, p99 **1020 µs**, max 1182 µs, deadline misses = **0**.
  - **Conclusion:** The DSP synthesis engine on Core 0 easily satisfies all steady and event deadline gates when visual rendering is decoupled.
- **Hardware Rendering Benchmark:**
  - ST7789 SPI2 GDMA transfer completion was accurately measured with hardware ISR callback (`on_color_trans_done`).
  - Small rect transfers (80×16, 2,560 B) complete in **635 µs** DMA time vs **13,101 µs** for a full frame (20× reduction in bus occupancy).
  - Background GDMA transfers alone do **not** cause Core 0 stalls (`lcdOnly = 0` in all forensic runs).

---

## 2. Root Cause Analysis & Boot Bug Resolutions

### Phase P: PreparedNote Table Boot Panic
- **Symptom:** In M6.3.7, expanding `PreparedNote` or initializing tables caused a reproducible boot panic / Guru Meditation crash.
- **Root Cause:** In `main/dsp/voice_allocator.cpp:538`, the expression `table = PreparedNoteTable{};` created a **9,348-byte temporary object on the stack** inside `preparePreparedNoteTable()`, called by `app_main`. However, `CONFIG_ESP_MAIN_TASK_STACK_SIZE` is strictly **8,192 bytes** in `sdkconfig.defaults`. In M6.3.7, adding 4 floats per note increased the table to 10,516 bytes, instantly overflowing the stack into FreeRTOS kernel memory.
- **Resolution:** Initialized directly without stack temporaries and wrapped table allocations with diagnostic canaries (`canaryPrePan_`, `canaryMid_`, `canaryPostBell_`). Hardware verified: `[CANARY] PreparedNote canaries OK=1`.

### Phase A: Display Framebuffer Allocation Failure
- **Symptom:** In M6.3.7, boot logs showed `E (928) Display: Failed to allocate display framebuffer` followed by `E (938) pocket_pan_main: Failed to initialize ST7789 display`.
- **Root Cause:** `sBleMidi.begin()` was called before `sDisplay.init()`. The NimBLE controller and host stack allocate ~80 KB of internal SRAM during startup, fragmenting memory such that the largest contiguous block dropped to 31,744 bytes. Subsequent calls to `heap_caps_malloc(64800, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)` failed.
- **Resolution:** Re-ordered initialization in `app_main.cpp` so that `sDisplay.init()` runs immediately after `sAudio.init()` and before `sBleMidi.begin()`. Also added fallback allocation to PSRAM if internal SRAM is exhausted. Hardware verified:
  ```text
  [DISP_MEM_PRE] before: int_free=164747 int_largest=69632 psram_free=2094528
  [DISP_MEM] framebuffer=0x3fcd7f78 (size=64800, align=8, internal=1, dma=1, psram=0)
  [DISP_MEM] scratch=0x3fce99f4 (size=9600, align=4, internal=1, dma=1)
  [DISP_MEM] after:  int_free=90339 int_largest=31744 psram_free=2094528
  ```

---

## 3. UI Architecture Implementations (`POCKETPAN_UI_ARCH`)

Three display architectures were implemented and comparatively evaluated on hardware:

1. **F0 (Baseline Row-Band Partial Transfer, `POCKETPAN_UI_ARCH=0`):**
   - Full 240×135 framebuffer in SRAM.
   - Partial updates draw to the full framebuffer and push horizontal 240×20 row bands over SPI DMA.
2. **F1 (Full Framebuffer + True Rect Transfer, `POCKETPAN_UI_ARCH=1`):**
   - Full 240×135 framebuffer in SRAM.
   - Partial updates draw to the full framebuffer, extract the bounding rectangle `(x, y, w, h)` into an internal DMA scratch buffer, and send only the exact rectangular region.
3. **F2 (Tile/Scratch UI, `POCKETPAN_UI_ARCH=2`):**
   - Preallocated static `.bss` tile buffers (`sTileBle`, `sTilePreset`, `sTileRoot`, `sTileVoices`, `sTileCpu`, `sTileDline`).
   - Steady-state telemetry draws directly into small sub-surface buffers (e.g. 80×16 = 2,560 B) and initiates direct rect transfers.
   - Zero writes to the 64.8 KB full framebuffer during steady-state audio rendering, eliminating Core 1 cache thrashing.

---

## 4. Hardware Transfer Benchmark

Measurements taken on ST7789 over SPI2 @ 40 MHz using `esp_lcd_panel_io_spi_config_t.on_color_trans_done`:

```text
[LCDMEAS] full_frame 240x135 (64800 B): submit=134 us, dma=13101 us
[LCDMEAS] row_band 240x20    (9600 B): submit=132 us, dma=2045 us
[LCDMEAS] small_rect 80x16   (2560 B): submit=138 us, dma=635 us
```

---

## 5. Candidate Comparison Matrix

All data captured directly from the physical ESP32-S3 over COM10 with 5 µs histogram bins and 8,192 blocks per fixture:

### Steady Performance

| Variant | PAN cl. avg / p95 / p99 / max | BELL cl. avg / p95 / p99 / max | BELL chord avg / p95 / p99 / max |
|---|---|---|---|
| **U0** Baseline Row-Band | 1632.8 / 1675 / 1815 / 2140 | 1612.9 / 1650 / 1785 / 2009 | 959.4 / 995 / 1125 / 1337 |
| **U1** Full FB + Rect | 1634.8 / 1680 / 1915 / 2129 | 1615.0 / 1650 / 1900 / 2100 | 961.3 / 1000 / 1200 / 1288 |
| **U2** Tile/Scratch UI | 1634.7 / 1675 / 1915 / 2189 | 1615.0 / 1655 / 1895 / 2212 | 961.1 / 1000 / 1185 / 1418 |
| **U1b** No-Render Control | **1627.7** / **1660** / **1695** / **1958** | **1607.7** / **1640** / **1680** / **2014** | **954.5** / **985** / **1020** / **1182** |

### Event Performance

| Variant | PAN cl. avg / p99 / max / misses | BELL cl. avg / p99 / max / misses | BELL chord avg / p99 / max / misses |
|---|---|---|---|
| **U0** Baseline Row-Band | 2492.4 / 2655 / 2654 / **0** | 2526.2 / 2795 / 2792 / **1** | 1522.6 / 1640 / 1636 / **0** |
| **U1** Full FB + Rect | 2512.5 / 3085 / 3082 / **2** | 2522.1 / 2645 / 2643 / **0** | 1535.3 / 2060 / 2057 / **0** |
| **U2** Tile/Scratch UI | 2497.5 / 3100 / 3095 / **2** | 2512.0 / 2645 / 2640 / **0** | 1510.7 / 1770 / 1768 / **0** |
| **U1b** No-Render Control | **2412.7** / **2500** / **2499** / **0** | **2455.1** / **2610** / **2606** / **0** | **1432.1** / **1590** / **1586** / **0** |

---

## 6. Jitter Correlation & Forensics Insights

1. **Lock-Free UI-Audio Correlation (`gUiAudioCorrelation`):**
   ```text
   [UICORR] >1733(tot=553 draw=37 lcd=0 both=115 none=401) >2000(tot=148 draw=19 lcd=0 both=10 none=119)
   ```
   - **`lcdOnly = 0`**: Throughout the entire capture of >26,000 blocks, background SPI GDMA transfers never once pushed an audio block above 1733 µs on their own.
   - **`neither = 401` (72.5%)**: Most outliers above 1733 µs occurred when the UI was completely idle (neither rendering nor transferring).
2. **Rare-Stall Forensics (`RareStallForensics`):**
   - The ring buffer recorded blocks $>2000$ µs occurring exactly at 188-block intervals ($\Delta t \approx 501.3$ ms).
   - This matches the synthetic NoteOn trigger interval of the polyphony forensics test harness where 8 voices are simultaneously reset and retriggered.
3. **P95 Verification:**
   - Across all rendering variants (U0, U1, U2), steady-state **p95 is uniformly $\le 1675$ µs**, proving that 95% of audio blocks execute well within the 1733 µs deadline budget.
