# PAN M5C.1 host metrics

Host timing is not ESP32-S3 realtime timing; hardware telemetry is authoritative for CPU, deadline misses, write timeouts, and TX errors.

## A — D3 component isolation

| Candidate | Dry baseline RMS | Candidate RMS | Difference RMS | Relative dB | Body peak/RMS | Bus peak/RMS | Max/avg GR dB | GR >0.1 / >1 dB | Clamp / safety |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| dry | 0.104613 | 0.104613 | 0.000000 | -220.391708 | 0.000000 / 0.000000 | 0.000000 / 0.000000 | 0.000000 / 0.000000 | 0 / 0 | 0 / 0 |
| body only contribution | 0.104613 | 0.106360 | 0.031905 | -10.314540 | 0.294334 / 0.037531 | 0.000000 / 0.000000 | 0.000000 / 0.000000 | 0 / 0 | 0 / 0 |
| sympathetic only contribution | 0.104613 | 0.104679 | 0.004746 | -26.864470 | 0.000000 / 0.000000 | 0.001638 / 0.000244 | 0.000000 / 0.000000 | 0 / 0 | 0 / 0 |
| full | 0.104613 | 0.106392 | 0.032348 | -10.194831 | 0.294730 / 0.037593 | 0.001638 / 0.000244 | 0.000000 / 0.000000 | 0 / 0 | 0 / 0 |

## B — musical fixtures

| Fixture | Dry baseline RMS | Full RMS | Difference RMS | Relative dB | Dry/full max GR | Dry/full avg GR | Clamp / safety |
|---|---:|---:|---:|---:|---:|---:|---:|
| interval | 0.111298 | 0.100009 | 0.058777 | -5.545633 | 0.000000 / 0.000000 | 0.000000 / 0.000000 | 0 / 0 |
| chord | 0.113074 | 0.102882 | 0.045742 | -7.860970 | -4.830681 / -5.182087 | -0.173559 / -0.183235 | 0 / 0 |
| roll | 0.392470 | 0.374865 | 0.069294 | -15.062214 | -6.966732 / -7.727427 | -1.180758 / -1.362126 | 0 / 0 |
| cluster8 | 0.125774 | 0.116709 | 0.054838 | -7.210273 | -12.718273 / -13.013968 | -0.367649 / -0.442989 | 0 / 0 |

### A.1 — D3 attack and tail contribution

| Velocity | Path | 0-5 ms RMS | 0-20 ms RMS | 0-100 ms RMS | 500-1500 ms RMS | Attack delta RMS / % | Tail delta RMS / % |
|---:|---|---:|---:|---:|---:|---:|---:|
| 70 | dry | 0.203880 | 0.240200 | 0.221065 | 0.019907 | 0.000000 / 0.000000 | 0.000000 / 0.000000 |
| 70 | body | 0.261590 | 0.236277 | 0.228440 | 0.019353 | -0.003923 / -1.633062 | -0.000555 / -2.785872 |
| 70 | full | 0.261743 | 0.236279 | 0.228273 | 0.019814 | -0.003921 / -1.632361 | -0.000094 / -0.470559 |
| 110 | dry | 0.320987 | 0.365713 | 0.335434 | 0.029172 | 0.000000 / 0.000000 | 0.000000 / 0.000000 |
| 110 | body | 0.413214 | 0.361209 | 0.345911 | 0.028372 | -0.004504 / -1.231655 | -0.000800 / -2.741463 |
| 110 | full | 0.413465 | 0.361224 | 0.345769 | 0.028960 | -0.004489 / -1.227507 | -0.000212 / -0.726419 |

### A.2 — body mode spectral validation (D3 body ON vs OFF)

| Mode Hz | Body-off magnitude | Body-on magnitude | bodyModeDeltaDb |
|---:|---:|---:|---:|
| 110.000000 | 0.000684 | 0.008450 | 21.836527 |
| 205.000000 | 0.000127 | 0.001004 | 17.958033 |
| 390.000000 | 0.000003 | 0.000015 | 13.163987 |
| 730.000000 | 0.000017 | 0.000041 | 7.641663 |
| 1280.000000 | 0.000019 | 0.000020 | 0.559368 |
| 1980.000000 | 0.000004 | 0.000004 | -0.984267 |

## C — register audit (body ON vs OFF, velocity 70)

| Note | RMS off/on | Attack RMS off/on | Tail RMS off/on | Body RMS | Pre-limiter peak off/on | Max/avg GR off/on | Nearest body mode | Distance % |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| D3 | 0.069375 / 0.070627 | 0.221065 / 0.228440 | 0.019907 / 0.019353 | 0.024964 | 0.433974 / 0.541827 | 0.000000 / 0.000000; 0.000000 / 0.000000 | 390.000000 | 12.947983 |
| A3 | 0.064879 / 0.043561 | 0.211038 / 0.158961 | 0.016831 / 0.007344 | 0.057275 | 0.410043 / 0.465364 | 0.000000 / 0.000000; 0.000000 / 0.000000 | 205.000000 | 7.317073 |
| D4 | 0.058920 / 0.058893 | 0.195204 / 0.195840 | 0.014050 / 0.013899 | 0.009899 | 0.383474 / 0.439135 | 0.000000 / 0.000000; 0.000000 / 0.000000 | 730.000000 | 19.543900 |
| A4 | 0.065254 / 0.056577 | 0.219189 / 0.190896 | 0.013453 / 0.011462 | 0.014764 | 0.429059 / 0.429639 | 0.000000 / 0.000000; 0.000000 / 0.000000 | 1280.000000 | 3.125000 |

## D — stability and sympathetic gain sweep

| Case | Bus RMS | Bus peak | Final body energy | Safety | Hard clamp |
|---|---:|---:|---:|---:|---:|
| 10s fixture | 0.000109 | 0.001638 | 0.000000 | 0 | 0 |
| 10s fixture | 0.000295 | 0.009583 | 0.000000 | 0 | 0 |
| 10s fixture | 0.000575 | 0.004821 | 0.000000 | 0 | 0 |
| 15s decay | 0.000157 | 0.003874 | 0.000000 | 0 | 0 |
| sympathetic 0.500000x | 0.000147 | 0.004790 | 0.000000 | 0 | 0 |
| sympathetic 1.000000x | 0.000295 | 0.009583 | 0.000000 | 0 | 0 |
| sympathetic 2.000000x | 0.000586 | 0.019200 | 0.000000 | 0 | 0 |
| sympathetic 4.000000x | 0.001136 | 0.030000 | 0.000000 | 25 | 0 |

## E — gain staging

| Fixture | Dry max GR | Full max GR | Dry avg GR | Full avg GR | Dry >0.1/>1 dB | Full >0.1/>1 dB | Hard clamp | Safety |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| D3 | 0.000000 | 0.000000 | 0.000000 | 0.000000 | 0 / 0 | 0 / 0 | 0 | 0 |
| interval | 0.000000 | 0.000000 | 0.000000 | 0.000000 | 0 / 0 | 0 / 0 | 0 | 0 |
| chord | -4.830681 | -5.182087 | -0.173559 | -0.183235 | 14012 / 5366 | 14177 / 5530 | 0 | 0 |
| roll | -6.966732 | -7.727427 | -1.180758 | -1.362126 | 45380 / 34732 | 48662 / 36433 | 0 | 0 |
| cluster8 | -12.718273 | -13.013968 | -0.367649 | -0.442989 | 16392 / 7737 | 17995 / 9349 | 0 | 0 |

## Hardware CPU capture (required before freeze)

| Scenario | M5B-like avg/max block us | M5C avg/max block us | CPU delta | Deadline misses | Write timeouts | TX errors |
|---|---:|---:|---:|---:|---:|---:|
| single | hardware required | hardware required | hardware required | hardware required | hardware required | hardware required |
| 4-note chord | hardware required | hardware required | hardware required | hardware required | hardware required | hardware required |
| 8-note cluster | hardware required | hardware required | hardware required | hardware required | hardware required | hardware required |
| roll | hardware required | hardware required | hardware required | hardware required | hardware required | hardware required |

### B.1 — M5B velocity expression with M5C enabled

| Velocity | modalBrightnessRatio | highModalRatio |
|---:|---:|---:|
| 30 | 0.003554 | 0.000008 |
| 70 | 0.012141 | 0.000098 |
| 110 | 0.023176 | 0.000296 |
