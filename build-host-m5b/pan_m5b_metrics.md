# PAN M5B host metrics

D3 baseline: MIDI 50, 146.832 Hz. Goertzel probes are exact PAN modal frequencies.

| Velocity | RMS | Attack RMS | f | 2f | 3f | Upper energy | modalBrightnessRatio | highModalRatio | Modal saturation |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 30 | 0.0381942 | 0.122843 | 0.000183936 | 1.98076e-05 | 7.46438e-07 | 1.21121e-08 | 0.00299456 | 7.36965e-06 | 0 |
| 70 | 0.070967 | 0.229541 | 0.00059307 | 9.04445e-05 | 9.27465e-06 | 4.595e-07 | 0.0113615 | 9.93366e-05 | 0 |
| 110 | 0.108837 | 0.354221 | 0.00126602 | 0.000288042 | 4.04467e-05 | 3.64383e-06 | 0.022464 | 0.000302603 | 0 |

## Register metrics (velocity 70, current default fixed 1 Hz split)

| Note | MIDI | Hz | RMS | Attack RMS | Tail RMS (500-1500 ms) | modalBrightnessRatio | Pre-limiter peak | Max GR dB | Avg GR dB |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| D3 | 50 | 146.832 | 0.070967 | 0.229541 | 0.0198227 | 0.0113615 | 0.547401 | 0 | 0 |
| A3 | 57 | 220 | 0.043904 | 0.160397 | 0.00739585 | 0.0432911 | 0.472284 | 0 | 0 |
| D4 | 62 | 293.665 | 0.0590295 | 0.196706 | 0.0140477 | 0.0151424 | 0.446013 | 0 | 0 |
| A4 | 69 | 440 | 0.0566109 | 0.191569 | 0.0114971 | 0.00885521 | 0.433984 | 0 | 0 |
