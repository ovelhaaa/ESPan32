# PAN M5C.1 host metrics

Host timing is not ESP32-S3 realtime timing; hardware telemetry is authoritative for CPU, deadline misses, write timeouts, and TX errors.

## A — D3 component isolation

| Candidate | Dry baseline RMS | Candidate RMS | Difference RMS | Relative dB | Body peak/RMS | Bus peak/RMS | Max/avg GR dB | GR >0.1 / >1 dB | Clamp / safety |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| dry | 0.105033 | 0.105033 | 0.000000 | -220.426559 | 0.000000 / 0.000000 | 0.000000 / 0.000000 | 0.000000 / 0.000000 | 0 / 0 | 0 / 0 |
| body only contribution | 0.105033 | 0.106758 | 0.032045 | -10.311420 | 0.296514 / 0.037696 | 0.000000 / 0.000000 | 0.000000 / 0.000000 | 0 / 0 | 0 / 0 |
| sympathetic only contribution | 0.105033 | 0.105095 | 0.004768 | -26.860130 | 0.000000 / 0.000000 | 0.001656 / 0.000245 | 0.000000 / 0.000000 | 0 / 0 | 0 / 0 |
| full | 0.105033 | 0.106785 | 0.032484 | -10.193263 | 0.296897 / 0.037751 | 0.001656 / 0.000245 | 0.000000 / 0.000000 | 0 / 0 | 0 / 0 |

## B — musical fixtures

| Fixture | Dry baseline RMS | Full RMS | Difference RMS | Relative dB | Dry/full max GR | Dry/full avg GR | Clamp / safety |
|---|---:|---:|---:|---:|---:|---:|---:|
| interval | 0.111622 | 0.100340 | 0.058884 | -5.555126 | 0.000000 / 0.000000 | 0.000000 / 0.000000 | 0 / 0 |
| chord | 0.113318 | 0.103120 | 0.045770 | -7.874228 | -4.892741 / -5.241468 | -0.175544 / -0.185054 | 0 / 0 |
| roll | 0.392034 | 0.374581 | 0.069216 | -15.062390 | -6.982193 / -7.742230 | -1.193354 / -1.372393 | 0 / 0 |
| cluster8 | 0.126024 | 0.116849 | 0.054881 | -7.220559 | -12.792095 / -13.090065 | -0.370543 / -0.446925 | 0 / 0 |

### A.1 — D3 attack and tail contribution

| Velocity | Path | 0-5 ms RMS | 0-20 ms RMS | 0-100 ms RMS | 500-1500 ms RMS | Attack delta RMS / % | Tail delta RMS / % |
|---:|---|---:|---:|---:|---:|---:|---:|
| 70 | dry | 0.204380 | 0.240694 | 0.221553 | 0.019916 | 0.000000 / 0.000000 | 0.000000 / 0.000000 |
| 70 | body | 0.262208 | 0.236742 | 0.228903 | 0.019361 | -0.003952 / -1.641739 | -0.000555 / -2.786619 |
| 70 | full | 0.262358 | 0.236743 | 0.228731 | 0.019814 | -0.003951 / -1.641417 | -0.000102 / -0.513970 |
| 110 | dry | 0.322595 | 0.367369 | 0.337011 | 0.029207 | 0.000000 / 0.000000 | 0.000000 / 0.000000 |
| 110 | body | 0.415120 | 0.362744 | 0.347387 | 0.028405 | -0.004625 / -1.258949 | -0.000801 / -2.744138 |
| 110 | full | 0.415380 | 0.362774 | 0.347233 | 0.028994 | -0.004596 / -1.250934 | -0.000212 / -0.727442 |

### A.2 — body mode spectral validation (D3 body ON vs OFF)

| Mode Hz | Body-off magnitude | Body-on magnitude | bodyModeDeltaDb |
|---:|---:|---:|---:|
| 110.000000 | 0.000687 | 0.008487 | 21.840752 |
| 205.000000 | 0.000125 | 0.000987 | 17.954514 |
| 390.000000 | 0.000003 | 0.000016 | 13.384914 |
| 730.000000 | 0.000017 | 0.000040 | 7.643394 |
| 1280.000000 | 0.000019 | 0.000021 | 0.559859 |
| 1980.000000 | 0.000004 | 0.000004 | -0.983575 |

## C — register audit (body ON vs OFF, velocity 70)

| Note | RMS off/on | Attack RMS off/on | Tail RMS off/on | Body RMS | Pre-limiter peak off/on | Max/avg GR off/on | Nearest body mode | Distance % |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| D3 | 0.069503 / 0.070749 | 0.221553 / 0.228903 | 0.019916 / 0.019361 | 0.025019 | 0.436787 / 0.543693 | 0.000000 / 0.000000; 0.000000 / 0.000000 | 390.000000 | 12.947983 |
| A3 | 0.064996 / 0.043705 | 0.211474 / 0.159462 | 0.016841 / 0.007364 | 0.057330 | 0.412000 / 0.467787 | 0.000000 / 0.000000; 0.000000 / 0.000000 | 205.000000 | 7.317073 |
| D4 | 0.059026 / 0.058998 | 0.195630 / 0.196265 | 0.014053 / 0.013902 | 0.009924 | 0.385890 / 0.441856 | 0.000000 / 0.000000; 0.000000 / 0.000000 | 730.000000 | 19.543900 |
| A4 | 0.065357 / 0.056678 | 0.219589 / 0.191287 | 0.013456 / 0.011466 | 0.014783 | 0.431585 / 0.431550 | 0.000000 / 0.000000; 0.000000 / 0.000000 | 1280.000000 | 3.125000 |

## D — stability and sympathetic gain sweep

| Case | Bus RMS | Bus peak | Final body energy | Safety | Hard clamp |
|---|---:|---:|---:|---:|---:|
| 10s fixture | 0.000110 | 0.001656 | 0.000000 | 0 | 0 |
| 10s fixture | 0.000296 | 0.009650 | 0.000000 | 0 | 0 |
| 10s fixture | 0.000575 | 0.004829 | 0.000000 | 0 | 0 |
| 15s decay | 0.000158 | 0.003898 | 0.000000 | 0 | 0 |
| sympathetic 0.500000x | 0.000148 | 0.004823 | 0.000000 | 0 | 0 |
| sympathetic 1.000000x | 0.000296 | 0.009650 | 0.000000 | 0 | 0 |
| sympathetic 2.000000x | 0.000588 | 0.019334 | 0.000000 | 0 | 0 |
| sympathetic 4.000000x | 0.001141 | 0.030000 | 0.000000 | 25 | 0 |

## E — gain staging

| Fixture | Dry max GR | Full max GR | Dry avg GR | Full avg GR | Dry >0.1/>1 dB | Full >0.1/>1 dB | Hard clamp | Safety |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| D3 | 0.000000 | 0.000000 | 0.000000 | 0.000000 | 0 / 0 | 0 / 0 | 0 | 0 |
| interval | 0.000000 | 0.000000 | 0.000000 | 0.000000 | 0 / 0 | 0 / 0 | 0 | 0 |
| chord | -4.892741 | -5.241468 | -0.175544 | -0.185054 | 14052 / 5405 | 14210 / 5563 | 0 | 0 |
| roll | -6.982193 | -7.742230 | -1.193354 | -1.372393 | 47059 / 34865 | 48708 / 36477 | 0 | 0 |
| cluster8 | -12.792095 | -13.090065 | -0.370543 | -0.446925 | 16447 / 7792 | 18041 / 9396 | 0 | 0 |

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
| 30 | 0.003611 | 0.000008 |
| 70 | 0.012828 | 0.000107 |
| 110 | 0.024776 | 0.000324 |
