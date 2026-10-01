# M7.6.1 — MBIRA V1 FROZEN

MBIRA V1 = Candidate B + Buzz ON. BAR/TINE = B; BUZZ = ON.
The user's listening acceptance is final. No DSP value was changed for this
freeze; A/C and buzz-off recordings remain historical experiments only.

| Mode | Ratio | Gain | T60 seconds |
|---|---:|---:|---:|
| 1 | 1.000 | 1.000 | 3.600 |
| 2 | 3.120 | .550 | 1.200 |
| 3 | 6.267 | .380 | .550 |
| 4 | 10.800 | .160 | .220 |
| 5 | 17.550 | .110 | .110 |
| 6 | 19.550 | .045 | .065 |

Pluck exciter: gain .15, noise .06, brightness 1800–15500 Hz, velocity knee
.90 / slope .55, hardness .42–1.0, v^1.15 mapping. Soft/hard coupling is
`[1,.10,.025,.006,.002,.001]` / `[1,1,.90,.70,.55,.42]`, over velocity .20–.98.
Register D3–D5 (146.83–587.33 Hz): gain 1.02–.94, brightness .82–1.10,
T60 scale 1.25–.65. Zero detune. All values remain authoritative in source.

One shared bridge contact receives the summed intact tine output: thresholded
asymmetric half-wave contact, 80 Hz DC rejection, fixed 2350 Hz / .025 s T60
resonator, gain 1.0. Velocity strength is `max(0,(v-.25)/.75)^2`; a .035 s
envelope is refreshed by strikes. It has no feedback into the modal bank.
Production enables contact both at initialization and model selection.
The separate strike-bus bridge has 310 Hz / .065 / .25 s and 730 Hz / .035 /
.12 s modes, excitation/output .08/.055, bus gain 20, lowpass 1500 Hz.
A 20 Hz output DC guard remains intact. No sound optimization was performed.

The mandatory aggregate FNV64 is **f5ee8755af2fa270**, using the established
MIDI 24–96 × velocities 30/70/110/127 grid. All eight earlier aggregate hashes
and the Vibraphone fixture assertions pass unchanged. Seven full-precision
PCM fixtures and their mandatory hashes are retained in the
[fixture index](listening/m761/README.md). Eleven host suites pass with
Process6 ON and OFF; all required host safety counters are zero.

Fresh physical ESP32-S3 qualification on COM10, 240 MHz, 48 kHz / 128 frames,
BLE MIDI connected at 11.25 ms, UI and I2S running, 8192 blocks per fixture:

| Fixture | Avg µs | p95 µs | p99 µs | Max µs | CPU % |
|---|---:|---:|---:|---:|---:|
| single buzz OFF (historical control) | 429 | 460 | 500 | 1111 | 16.09 |
| chord4 | 867 | 920 | 1075 | 1842 | 32.51 |
| cluster8 | 1396 | 1445 | 1575 | 2236 | 52.34 |
| roll | 471 | 525 | 925 | 1094 | 17.66 |
| groove | 1279 | 1325 | 1700 | 2097 | 47.96 |
| model switching | 464 | 500 | 600 | 1356 | 17.40 |
| single Buzz ON | 455 | 490 | 550 | 980 | 17.06 |

Every fixture passes: deadline misses = I2S timeouts/errors/short writes =
hard clamps = modal saturation = nonfinite = BLE losses = 0.
Cluster8 margin is 430.7 µs. p95 combines the four full-callback histogram
classes (5 µs upper edges); p99 comes from AudioStats (75 µs bins).
[Raw captures, firmware digests and source manifest](qualification/m761/hardware_manifest.json).
The diagnostics now accumulate saturation and NaN/Inf across fixture resets.

PreparedNote remains 132 bytes, table 9640 bytes, nine tables 86760 bytes;
host SynthEngine 100928 bytes, ESP32-S3 SynthEngine 100672 bytes. Fresh
restored production capture measured 45687 internal free bytes and a 27648
byte largest block; the freeze adds no production state or cache memory.
The capture is retained in `qualification/m761/production_raw.log`.

BOOT position is ninth: VIBRAPHONE → MBIRA → PAN. The unchanged physical
firmware input-injection qualification previously completed two nine-model
cycles and three short diagnostic gestures (see the historical M7.6 BOOT log).
M7.7 must add UDU after MBIRA and preserve short-press behavior.

The focused host and seven fresh physical fixtures pass. MBIRA is immutable
for M7.7; UDU listening decisions cannot alter these parameters or assertions.

M7.7 final recheck: MBIRA cluster8 remains healthy at avg 1394 µs, max 2260 µs,
406.7 µs deadline margin, with every required fault zero. All nine aggregate
hashes and seven MBIRA fixture hashes remain exact after adding UDU. Combined
ten-model production memory is 40303 internal free bytes / 23552 largest block;
the MBIRA values above describe the completed nine-model freeze boundary.
