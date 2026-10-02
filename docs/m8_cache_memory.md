# M8 — PreparedNote / instrument cache memory refactor

M7.7.2 completed first: [freeze gate](qualification/m772/completion.json),
both Process6 suites and all six fresh BLE-connected physical fixtures passed.
No instrument definitions or DSP arithmetic are changed by M8.

## Ownership audit before representation changes

The retained ESP32-S3 [ELF](qualification/m8/before/production.elf),
[map](qualification/m8/before/production.map), [target DWARF and section report](qualification/m8/before/memory.json)
and [connected heap log](qualification/m8/before/production_raw.log) establish
ownership. These are target sizes, not host `sizeof` estimates.

| Object | Owner / placement | Before bytes / evidence |
|---|---|---:|
| SynthEngine | `sSynth`, static internal DRAM BSS at 0x3fca81d4 | 107848 |
| Nine PreparedNote tables | members of sSynth, startup initialized | 86760 |
| VoiceAllocator | sSynth subobject (offset 620), internal BSS | 14980 |
| UduCache | VoiceAllocator member, internal BSS | 5928 |
| Eight modal voices | VoiceAllocator members, internal BSS | 8 × 1108 |
| Canonical instrument configs | flash rodata, ELF addresses 0x3c08… | 10 × 316 |
| Model config working copy | sSynth internal BSS (plus voice config copies) | 316 |
| Body caches | 10 BodyResonators in sSynth BSS | 2240 |
| Current body | sSynth internal BSS | 224 |
| Motor LUT | sSynth internal BSS, startup generated | 1028 |
| Voice mono / strike / fan buffers | sSynth internal BSS | 3 × 512 |
| MIDI Hz / three velocity LUTs | static internal BSS, symbol evidence | 4 × 512 |
| UI framebuffer | internal DMA heap, physical pointer 0x3fcd6860 | 64800 payload |
| UI scratch | internal DMA heap, physical pointer 0x3fce6584 | 9600 payload |
| UI/BLE queues and snapshots | static internal BSS, ELF symbol report | listed individually in memory.json |
| BLE controller / NimBLE / NVS | internal heap and controller BSS | startup heap-stage evidence; includes host task and allocator overhead |
| Audio output staging | internal DMA heap (`AudioI2S::init`) | 1024 payload |
| I2S driver ring | internal DMA heap, 6 × 128 stereo int32 frames | 6144 payload + descriptors/driver queues |
| Audio / UI / NimBLE task stacks | internal heap, task creation sizes | 6144 / 4096 / 4096 reserved bytes |
| Main / interrupt stacks | configured task heap / target `port_IntStack` data | main 8192; interrupt symbol 3072 |
| PSRAM | heap pool, physical startup log | 2 MiB total; 2094528 free at display initialization |

Production `.dram0.bss` = 121672 bytes, `.dram0.data` = 17664,
`.flash.rodata` = 131268, `.flash.text` = 410344, `.iram0.text` = 125947;
external BSS = 0. The display consumes 74408 heap bytes for 74400 payload.
BLE-connected production free internal SRAM = 37707; largest block = 18432.
Payload sizes do not include TLSF headers, task TCBs or driver control blocks.
Startup heap-stage captures separate DSP, I2S, display, BLE and UI ownership;
concurrent connection allocations appear in the subsequent connected heap log.
BLE/NVS totals are attributed to that subsystem group rather than inventing
per-allocation sizes. No PreparedNote/Udu/motor data resides in PSRAM or flash.

The official IDF [archive attribution](qualification/m8/before/archives.json)
also separates static BLE ownership: `libbtdm_app.a` contributes 702 BSS +
431 data + 13782 IRAM bytes, while `libbt.a` contributes 5619 BSS + 149 data
+ 784 IRAM bytes. Their flash rodata/text totals are 8476/52981 and
2734/69153 bytes respectively. NVS contributes 24 BSS bytes and 13675 flash
bytes; runtime controller/NimBLE allocations are accounted for by the startup
heap boundary below. These static and heap figures are distinct and are not
added twice.

The [fresh baseline startup audit](qualification/m8/before/audit_raw.log)
measures these actual heap boundaries (PSRAM free stays 2094528 throughout):

| Boundary | Internal free | Largest block | Internal consumption since previous |
|---|---:|---:|---:|
| app entry | 185391 | 90112 | — |
| DSP ready | 185391 | 90112 | 0 |
| I2S ready, audio task started | 170659 | 73728 | 14732 |
| display ready | 93887 | 31744 | 76772 |
| BLE/NVS/host task ready | 33575 | 24576 | 60312 |
| UI task ready | 29095 | 20480 | 4480 |
| connected after main task exits | 37715 | 20480 | -8620 |

The DSP tables allocate no heap at startup. I2S's 14732-byte boundary includes
its DMA/staging payload, driver state, semaphore, audio stack and TCB. Display's
76772 includes framebuffer/scratch plus SPI/LCD driver state. The BLE boundary
includes controller, NimBLE, NVS and host task; asynchronously scheduled startup
and connection work can move allocations between boundaries. Task stacks are
heap allocations, not extra BSS. The main-task stack/TCB are released on exit.

## PreparedNote fields and duplication

The exact target record is 132 bytes, with no padding waste: 12-byte header
plus three 10-float arrays. Table readiness contributes four bytes per table.

| Field | Bytes | Classification | Reason |
|---|---:|---|---|
| MIDI note | 1 | E | table index + 24 |
| Mode count | 1 | C | canonical preset width per model |
| Active mask | 2 | E | note/preset/Nyquist dependent; preserve exact mask |
| Fundamental frequency | 4 | D | canonical MIDI frequency repeats across models; retain exact equality guard |
| Register position | 4 | A/E | logarithm of frequency against model register bounds; expensive trigger value for Vibraphone/Mbira |
| a1[] | 40 | A | exp/cos with register T60 and split law |
| a2[] | 40 | A/E | exp-derived radius squared; no recomputation retained |
| modalAmplitude[] | 40 | A | sin and normalization; preserve generated float bits |

There are no velocity-dependent amplitudes or damping state in PreparedNote.
RegisterPosition remains precomputed; no log/trig work is added at NoteOn.
The [actual duplication probe](qualification/m8/before/duplication.log) visits
all 657 records: model widths are 8/10/6/7/5/6/6/6/6. It counts 2190 unused
tuples (**26280 bytes**) and 73 inactive tuples (**876 bytes**). The repeated
frequencies waste **2336 bytes** beyond a single shared 73-float table.
We retain inactive tuples, frequency guards and all header fields for the
lowest-risk change. Sparse active tuples could save only another 876 bytes
while adding offsets and lookup work.

## Strategies evaluated before selection

| Strategy | Actual layout estimate | Switching / latency tradeoff | Decision |
|---|---:|---|---|
| A: model-width records | 60444 payload + readiness / guards | boot preparation; fixed indexed lookup and small copy | select |
| B: aligned SoA at each model width | same 60444 payload; no unaligned floats | contiguous float arrays; reconstruct one 132-byte note at NoteOn | combine with A |
| C: one/two active full tables | 9640 / 19280 + ownership state | needs asynchronous preparation/commit protocol; cannot generate in callback | reject for M8; switching risk unnecessary |
| D: immutable full flash tables | 86760 flash, minimal table BSS | target-libm generated floats must be reproducible; XIP stalls need separate hardware experiment | defer; not selected without XIP qualification |
| E: compact flash + internal note scratch | 60444 flash + small working state | same XIP/generator risks; greater saving but more qualification scope | defer |
| PSRAM experiment | 86760 PSRAM | event latency/cache-stall qualification required | reject; internal SRAM saving already meets target |

Selected architecture: all nine model-width SoA tables stay in internal BSS,
prepared once before I2S starts by the canonical coefficient routine. No
build-time generated coefficient artifacts are needed. UDU keeps its independent
5928-byte cache. Floats remain naturally aligned and unchanged; modal application
still consumes the existing PreparedNote record. Reconstructing at most ten
tuples is bounded; same-note restrikes retain their original path. Model switches
select an immutable view and never rebuild tables or allocate heap memory.

The legacy control exposed PAN burst deadline misses. The compact path therefore
also reuses shared MIDI-Hz and exact velocity values, the PreparedNote register
snapshot and the existing nine-entry headroom table (36 bytes). One shared
canonical strike-energy cache adds 524 bytes, including its parameter/readiness
metadata, instead of duplicating 512 bytes in every voice. It is prepared from
the source PAN exciter parameters before I2S, and accepted only for exact MIDI
velocities and matching knee/slope bits. No new float formulas are introduced.
Small event/state-copy/energy routines use bounded IRAM placement; the existing
strike-bus renderer uses O3 without fast-math. Exact non-MIDI velocities,
out-of-range notes, pressure-state triggers and custom model configurations keep
their established fallbacks. These are cache-use changes; all frozen PCM hashes
remain exact with Process6 ON/OFF. The control build retains the original lookup
use for physical comparison. Legacy control timings include a 64-byte diagnostic
ownership/view overhead relative to the retained pre-refactor production ELF.

Intermediate failed captures remain under `qualification/m8/failed` and
`qualification/m8/experiments`; none count as final qualification.

<!-- M8_COMPLETE -->

## Qualified result

| Measurement | Before | After | Change bytes |
|---|---:|---:|---:|
| PreparedNote owners (after includes guards) | 86760 | 60552 | -26208 |
| UduCache | 5928 | 5928 | +0 |
| SynthEngine BSS | 107848 | 81668 | -26180 |
| Total DRAM BSS | 121672 | 96016 | -25656 |
| IRAM code | 125947 | 130971 | +5024 |
| Static internal BSS + data + IRAM | 265283 | 244651 | -20632 |
| Connected internal free | 37715 | 58531 | +20816 |
| Largest internal block | 20480 | 31744 | +11264 |
| PSRAM pool used at UI startup (including pool overhead) | 2624 | 2624 | +0 |
| Flash rodata | 131268 | 132532 | +1264 |
| Firmware binary | 687136 | 693440 | +6304 |

No cache data moved to PSRAM; external BSS remains zero. The startup PSRAM heap-stage logs and display pointer capability logs retain actual UI allocation evidence. The source generates compact coefficients once at boot using the same canonical builder; there is no generated coefficient file or CI regeneration step.

## Paired physical event and callback qualification

ESP32-S3 / COM10, BLE MIDI ready at 11.25 ms, 240 MHz, live UI and I2S. Legacy rollback and compact builds use identical fixtures, 4096 blocks each. Switch events step through all ten models across successive callbacks and return to the measured model (two selections per timed event). Rapid restrikes run every eight blocks, alternating v30/v127. Event timing excludes render and reset; model-cycle timing includes all selection work. Callback max includes all work; avg is weighted across all classes. p95 is the 5 µs histogram upper edge; p99 is the AudioStats histogram upper edge. These sequential physical runs include scheduling variance.

| Model / fixture | Legacy event avg / max µs | Packed event avg / max µs | Packed avg µs | p95 | p99 | Max | CPU % | Legacy callback max / misses |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| PAN / single | 388.73 / 419 | 72.45 / 81 | 520.42 | 665 | 700 | 862 | 19.52 | 1205 / 0 |
| PAN / chord4 | 347.12 / 552 | 98.39 / 106 | 1158.12 | 1215 | 1375 | 1786 | 43.43 | 2367 / 0 |
| PAN / cluster8 | 396.13 / 437 | 143.69 / 152 | 1672.03 | 1725 | 2000 | 2603 | 62.7 | 3058 / 22 |
| PAN / rapid | 289.49 / 630 | 84.54 / 235 | 721.3 | 870 | 950 | 1157 | 27.05 | 1695 / 0 |
| PAN / cycle switching | 687.59 / 895 | 395.49 / 553 | 515.57 | 545 | 600 | 1262 | 19.33 | 1601 / 0 |
| VIBRAPHONE / single | 249.56 / 266 | 66.24 / 75 | 315.02 | 345 | 450 | 755 | 11.81 | 960 / 0 |
| VIBRAPHONE / chord4 | 271.95 / 290 | 94.84 / 115 | 675.13 | 705 | 900 | 1128 | 25.32 | 1455 / 0 |
| VIBRAPHONE / cluster8 | 324.92 / 504 | 145.16 / 216 | 1109.75 | 1140 | 1525 | 2173 | 41.62 | 2494 / 0 |
| VIBRAPHONE / rapid | 190.31 / 657 | 92.14 / 283 | 346.72 | 490 | 575 | 869 | 13.0 | 1289 / 0 |
| VIBRAPHONE / cycle switching | 579.35 / 662 | 399.4 / 464 | 316.24 | 345 | 400 | 876 | 11.86 | 1110 / 0 |
| MBIRA / single | 268.34 / 283 | 69.55 / 77 | 459.43 | 485 | 550 | 1056 | 17.23 | 955 / 0 |
| MBIRA / chord4 | 296.34 / 321 | 100.16 / 116 | 855.49 | 880 | 950 | 1342 | 32.08 | 1606 / 0 |
| MBIRA / cluster8 | 343.92 / 498 | 142.91 / 177 | 1382.02 | 1410 | 1450 | 1887 | 51.83 | 2312 / 0 |
| MBIRA / rapid | 220.68 / 598 | 85.64 / 495 | 483.53 | 625 | 700 | 1218 | 18.13 | 1259 / 0 |
| MBIRA / cycle switching | 592.45 / 662 | 395.49 / 449 | 461.07 | 490 | 550 | 1022 | 17.29 | 1315 / 0 |
| UDU / single | 114.8 / 128 | 72.64 / 89 | 271.82 | 315 | 400 | 705 | 10.19 | 686 / 0 |
| UDU / chord4 | 127.36 / 134 | 82.23 / 87 | 612.31 | 715 | 875 | 1122 | 22.96 | 1275 / 0 |
| UDU / cluster8 | 140.45 / 152 | 108.06 / 190 | 1076.93 | 1270 | 1575 | 1982 | 40.38 | 1899 / 0 |
| UDU / rapid | 27.42 / 125 | 12.27 / 88 | 302.21 | 355 | 425 | 953 | 11.33 | 920 / 0 |
| UDU / cycle switching | 507.91 / 648 | 449.74 / 582 | 276.2 | 325 | 425 | 989 | 10.36 | 1069 / 0 |

All 20 final compact-cache physical trials: **zero deadline misses, I2S faults, nonfinite, resonator faults, hard clamps, BLE losses and invalid voice counts**. Normal production is restored with compact caches, PAN boot and BLE connected.

The narrowest measured full-callback margin is 63.7 µs (PAN cluster8). PAN render headroom remains the main timing limitation; UDU cluster8 retains 684.7 µs of measured margin.

The legacy diagnostic control recorded 22 deadline misses; these are retained explicitly rather than treated as a passing baseline. The final event path additionally reuses the existing model-independent MIDI-Hz and exact MIDI-velocity LUTs, and the register value already held in each PreparedNote. The 36-byte existing headroom table and one shared 524-byte canonical strike-energy cache avoid repeated libm calls. Small event, copy and energy helpers use bounded IRAM, and the existing strike-bus renderer uses O3 without fast-math. These placement/cache changes preserve float results. Noncanonical velocities, custom configs, damped triggers and out-of-table notes retain the established fallbacks.

## Exactness, CI and limitations

Both Process6 configurations and the retained candidate-16 CI configuration pass all **15 suites**. The candidate-16 fast-path counter assertion now reflects its unavailable Vibraphone attack specialization; PCM checks remain. All ten aggregate hashes and every retained Vibraphone, Mbira and UDU special golden fixture remain exact. See the [ten aggregate results](qualification/m8/aggregate.log) and [layout/corruption/switch tests](qualification/m8/layout.log). The compact table test compares all 657 records byte-for-byte with the canonical full builder, so model definitions cannot drift from stored values. Readiness, out-of-range MIDI, frequency mismatch, both guards and cold/warm selections are tested. A thousand cyclic switches allocate no heap and reproduce cold-selected PCM. Views point only into persistent owners; no pointers to note scratch escape.

CI runs the exact host suites with Process6 ON/OFF and verifies target ELF PreparedNote totals (60552), UduCache (5928) and SynthEngine (<=82000). Firmware DSP compilation rejects frames above 2048 bytes and emits stack-usage files. Only one 132-byte unpacked note and the existing small coefficient-preparation objects are automatic; every table owner remains static/member storage.

Target compiler frame evidence is retained as `after/*.su` and in the completion JSON. These are individual frames, not measured total task high-water marks. Boot preparation runs on the 8192-byte main stack; NoteOn and model selection run on the existing 6144-byte audio stack.

Intermediate failing PAN captures are preserved under `qualification/m8/failed` and `qualification/m8/experiments`; they do not count toward the final gate. The incomplete boot-reset capture is also retained, with its successful retry in the paired control evidence.

The architecture still grows with model count, retains 2336 duplicated frequency bytes and 876 inactive tuple bytes, and prepares caches at startup. Flash/XIP and asynchronous active-model caching remain unqualified alternatives. The diagnostic legacy rollback is disabled in production. No eleventh instrument was added, and no gain, decay, excitation, modal recurrence, allocation rule, body, motor, buzz or UDU sound changed.

[Completion and timings](qualification/m8/completion.json), [legacy hardware](qualification/m8/legacy/hardware_manifest.json), [packed hardware](qualification/m8/packed/hardware_manifest.json), [target memory after](qualification/m8/after/memory.json).

## Frozen aggregate hashes

292 cases per model; notes 24–96, velocities 30/70/110/127.

| Model | FNV64 |
|---|---|
| PAN | `9e9801244165101d` |
| BELL | `7c2dc6f37fb99f4b` |
| TONGUE | `83f75567fb47ebaf` |
| BOWL | `14772092e35bc4a7` |
| KALIMBA | `e9bcabdd0b6e3bd2` |
| GLASS | `14e0dd6ba566e1cf` |
| MARIMBA | `236d684f05f5f1d2` |
| VIBRAPHONE | `3cea893644aa0283` |
| MBIRA | `f5ee8755af2fa270` |
| UDU | `552d8d59691b008e` |

## Target stack frames

| Function | Own frame bytes |
|---|---:|
| ModalVoice::prepareNote | 832 |
| ModalVoice::configureStrike | 128 |
| SynthEngine::init | 128 |
| SynthEngine::setInstrumentModel | 112 |
| VoiceAllocator::noteOn | 192 |
| VoiceAllocator::renderVibraphoneBlock | 304 |
| VoiceAllocator::preparePackedNotes | 1328 |
