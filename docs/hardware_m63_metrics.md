# M6.3 hardware qualification metrics

This report must be completed from an ESP32-S3 run; no host-rendered numbers
belong in these cells. The firmware emits the required values every five
seconds when `CONFIG_POCKETPAN_HARDWARE_QUALIFICATION_LOG` is enabled:
`model`, `avg_us`, `p99_us`, `max_us`, `cpu_load`, `deadline`, `timeout`,
`short`, and `tx_error`.

Timing is render time for a 128-frame / 48 kHz block. `p99_us` is a
nearest-rank p99 over the last completed 2048-block window, binned in 25 us
steps; its final 3175 us bin is an overflow bin. The block deadline is
2666.7 us.

## Scenario A — BLE disconnected

| Model | Fixture | Avg us | P99 us | Max us | CPU % | Deadline misses | Timeouts | Short writes | TX errors | Gate |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| PAN | single (D4 v90) | pending | pending | pending | pending | pending | pending | pending | pending | pending |
| PAN | chord (D3/A3/D4/A4 v90) | pending | pending | pending | pending | pending | pending | pending | pending | pending |
| PAN | cluster8 (existing host pitches) | pending | pending | pending | pending | pending | pending | pending | pending | pending |
| PAN | roll (D4 v90, 100 ms, >=10 s) | pending | pending | pending | pending | pending | pending | pending | pending | pending |
| BELL | single (D4 v90) | 420 | 650 | 1562 | 15.7 | 0 | 0 | 0 | 0 | PASS |
| BELL | chord (D3/A3/D4/A4 v90) | 1431 | 2000 | 2800 | 53.7 | 1 | 0 | 0 | 0 | FAIL (p99, max, deadline) |
| BELL | cluster8 (existing host pitches) | 2495 | 3175+ | 4534 | 93.6 | 1015 | 0 | 0 | 0 | FAIL (CPU, p99, max, deadline) |
| BELL | roll (D4 v90, 100 ms, >=10 s) | 597 | 1175 | 2055 | 22.4 | 0 | 0 | 0 | 0 | PASS |

## Scenario B — BLE connected, MIDI drives fixture

| Model | Fixture | Avg us | P99 us | Max us | CPU % | Deadline misses | Timeouts | Short writes | TX errors | Gate |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| PAN | single (D4 v90) | pending | pending | pending | pending | pending | pending | pending | pending | pending |
| PAN | chord (D3/A3/D4/A4 v90) | pending | pending | pending | pending | pending | pending | pending | pending | pending |
| PAN | cluster8 (existing host pitches) | 2342 | 3150 | 4109 | 87.8 | 1461 | 0 | 0 | 0 | FAIL (CPU, p99, max, deadline) |
| PAN | roll (D4 v90, 100 ms, >=10 s) | pending | pending | pending | pending | pending | pending | pending | pending | pending |
| BELL | single (D4 v90) | pending | pending | pending | pending | pending | pending | pending | pending | pending |
| BELL | chord (D3/A3/D4/A4 v90) | pending | pending | pending | pending | pending | pending | pending | pending | pending |
| BELL | cluster8 (existing host pitches) | pending | pending | pending | pending | pending | pending | pending | pending | pending |
| BELL | roll (D4 v90, 100 ms, >=10 s) | pending | pending | pending | pending | pending | pending | pending | pending | pending |

## Gates

For every fixture, require p99 <= 1733 us, max <= 2133 us, zero deadline
misses, and zero transport errors. Average CPU is a 50% target and a hard
failure above 65%. Run each fixture for at least 15 seconds (30 preferred),
after a model switch so the timing window is fresh. Record Bell minus PAN CPU
and p99 deltas per matching fixture; Bell CPU should remain within 20 points.
