# M7.6 completion evidence

All eleven host suites pass with Process6 ON and OFF; all eight frozen aggregates and twelve frozen Vibraphone dry/M1 fixtures remain exact. MBIRA is provisional. The listening pack contains 21 verified RMS-matched A/B/C and buzz off/on files.

Physical qualification passes all seven BLE-connected fixtures with zero deadline misses, I2S timeouts/errors/short writes, hard clamps and modal saturation. Cluster8 maximum is 2286 us, with 380.7 us of deadline margin. Full timings are in [hardware report](hardware_report.md).

The isolated, continuously refreshed contact DSP probe averages 28.328–28.330 us per 128-frame block. Maximum observed isolated blocks are 34.913–35.987 us, including interrupt variability. The average achieves the preferred <30 us target. The full callback separately includes the body and output DC guard.

BOOT qualification runs on the physical ESP32-S3 using a firmware schedule injected at the BOOT input read. It is not evidence of manually pressing the mechanical switch. Three short gestures retain the diagnostic flow, 18 long gestures complete two full nine-model cycles, and one extended hold produces exactly one additional PAN→BELL transition. Every transition exposes the correct UI label and status mode. VIBRAPHONE→MBIRA→PAN is observed twice. The short-press production handler itself was not changed. [Raw log](boot_raw.log).

Normal firmware was rebuilt with BOOT qualification and forensic flags disabled and restored to COM10. It boots PAN, connects BLE MIDI at 11.25 ms, and the retained normal capture has zero audio deadline/I2S faults. [Raw log](production_raw.log).

Restored production internal free SRAM: 45687–45687 bytes. Largest free internal block: 27648–27648 bytes. PreparedNote stays 132 bytes, table/model 9,640 bytes, total tables 86,760 bytes, host engine 100,928 bytes, device engine BSS symbol 100,672 bytes. One shared velocity table adds 512 bytes and a readiness flag outside the engine.

The unsuccessful event capture (one cluster8 deadline miss at 2,932 us) is retained as `development_event_batch0_raw.log` with its flashed binary. Exact Mbira trigger precomputation reduced event work; qualification was repeated for the final source. Earlier contact probes are retained too. No failed run is substituted for a passing final capture.

Remaining decision: listen to A/B/C and the two-velocity/groove buzz comparisons, then explicitly choose tine/contact/body balance before a freeze. No automatic winner is selected.
