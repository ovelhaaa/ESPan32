# PAN M5B host metrics

D3 baseline: MIDI 50, 146.832 Hz. Goertzel probes are exact PAN modal frequencies.

| Velocity | RMS | Attack RMS | f | 2f | 3f | Upper energy | modalBrightnessRatio | highModalRatio | Modal saturation |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 30 | 0.0378818 | 0.120162 | 0.000195117 | 2.01842e-05 | 1.00479e-06 | 1.1613e-08 | 0.00381037 | 8.53822e-06 | 0 |
| 70 | 0.0701479 | 0.223743 | 0.00062878 | 9.0173e-05 | 1.12518e-05 | 3.90121e-07 | 0.0129577 | 9.60583e-05 | 0 |
| 110 | 0.105752 | 0.339436 | 0.001305 | 0.000274755 | 4.61136e-05 | 2.93762e-06 | 0.024634 | 0.000290947 | 0 |

## Register metrics (velocity 70, current default fixed 1 Hz split)

| Note | MIDI | Hz | RMS | Attack RMS | Tail RMS (500-1500 ms) | modalBrightnessRatio | Pre-limiter peak | Max GR dB | Avg GR dB |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| D3 | 50 | 146.832 | 0.0701479 | 0.223743 | 0.0203879 | 0.0129577 | 0.518517 | 0 | 0 |
| A3 | 57 | 220 | 0.0656028 | 0.214516 | 0.017075 | 0.00712087 | 0.46981 | 0 | 0 |
| D4 | 62 | 293.665 | 0.0596563 | 0.199111 | 0.0142 | 0.0136779 | 0.489462 | 0 | 0 |
| A4 | 69 | 440 | 0.0657274 | 0.222185 | 0.0134937 | 0.00652222 | 0.519541 | 0 | 0 |
