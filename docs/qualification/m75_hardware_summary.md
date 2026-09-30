## Completed hardware results

| Fixture | Avg callback us | p95 us | p99 us | Max us | CPU % | Misses | I2S timeout/error/short |
|---|---:|---:|---:|---:|---:|---:|---|
| single motor OFF | 266 | 305 | 400 | 1210 | 9.97 | 0 | 0/0/0 |
| chord4 motor OFF | 632 | 695 | 900 | 1357 | 23.70 | 0 | 0/0/0 |
| cluster8 motor OFF | 1098 | 1160 | 1625 | 2193 | 41.17 | 0 | 0/0/0 |
| roll motor ON | 295 | 385 | 800 | 992 | 11.06 | 0 | 0/0/0 |
| sustained phrase motor ON | 628 | 685 | 875 | 1843 | 23.55 | 0 | 0/0/0 |
| repeated model switches | 282 | 320 | 400 | 1345 | 10.57 | 0 | 0/0/0 |
| single motor ON | 283 | 320 | 400 | 949 | 10.61 | 0 | 0/0/0 |

p95 combines the 5 us callback class histograms (8191 callback records/fixture); FIXTURE_IO p99 uses the existing transport histogram with 25 us bins. These are upper bin bounds. All 28 inner/callback class rows and seven I/O summaries pass deadlines; DSP clamp/saturation and corrected voice checks are zero. Phrase reached four voices, allowing natural expiration thereafter.

Motor steady inner avg OFF 247.90 us, ON 259.77 us: delta 11.87 us/128 frames (~0.45% of the 2666.7 us deadline). These are separate real-board runs, so the difference includes run variability; callback averages add about 17 us. The paired host medians were 3.81161 versus 3.86091 us/block (+.04930 us).

M7.4 selection boundary: callback 2776 us, inner 2556.26 us, one deadline miss. M7.5 first selection: callback 1210 us, inner 968.62 us, no miss. Repeated paired switches (44 event blocks) had max callback 1345 us including both selections, strike and render. This comparison crosses instruments (historic MARIMBA and new VIBRAPHONE); it demonstrates the boundary meets the deadline, not a precisely isolated speedup ratio.

The first batch1 capture, retained as `m75_esp32s3_batch1_initial_raw.log`, incorrectly flagged 1343 voice-count observations because it expected all four tails to remain permanently active. The final diagnostic verifies 1..notes-introduced active voices, tracks the observed maximum, and keeps exact-count checks on other fixtures. No synth audio or timing gate changed. The repeated capture passes; both logs are retained.

Startup after BLE init: batch0 internal free 60055 bytes, largest block 31744; batch1 final internal free 48019, largest 31744. These are diagnostic builds with retained histograms, before UI task creation; production memory is measured separately below.

BLE-connected PASS is not claimed. Both logs show state=1 (scanning), interval 0. BLE scanning, LCD, UI and I2S were active, the diagnostic disconnected flag intentionally declined discovered peripherals. Connected runs are recorded separately.
