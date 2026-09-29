# ESPan32 performance contract v1

Status: **FROZEN** (M6.4)
Canonical performance contract for the production firmware. This document
supersedes any previous per-callback wording, including the M6.3.9.1 §6.2
"1976 us" and the M6.3.9.1 §3 / M6.3.9.2 §1 I2S buffer-depth statements.

Production configuration (all clauses below assume exactly this build):

```text
MCU                 ESP32-S3 @ 240 MHz
Sample rate         48 kHz
Block size          128 frames
Channels            stereo I2S (PCM5102, I2S_STD_PHILIPS, 32-bit slots)
DMA descriptors     6
Frames/descriptor   128 stereo frames
Nominal period      128 / 48000 = 2.666666... ms = 2666.7 us
Polyphony           8 voices
DSP candidate       25
UI architecture     F0 (POCKETPAN_UI_ARCH=0)
```

---

## 1. Platform contract

The contract is only valid for the frozen production platform above. Any change
to the sample rate, block size, DMA descriptor count, polyphony, DSP candidate,
or UI architecture requires re-qualification against this contract.

## 2. Nominal audio period

```text
block period = 128 frames / 48000 Hz
             = 2.666666... ms
             = 2666.7 us   (used as the integer deadline everywhere)
```

The I2S DMA consumes one 128-frame descriptor every 2666.7 us.

## 3. Steady contract

For ordinary steady qualification fixtures (homogeneous, sustained polyphony
inside the fast sustain path):

```text
average callback <= 1733 us
p99 callback     <= 2133 us
max callback     <  2666.7 us
nominal deadline overruns = 0
```

Qualified hardware results (M6.3.9.1, production 6-descriptor transport):

| Fixture | avg | p99 | max | overruns |
|---|---:|---:|---:|---:|
| PAN 8 voices | ~1651 us | ~1845 us | ~2193 us | 0 |
| BELL 8 voices | ~1634 us | ~1855 us | ~2141 us | 0 |

Do not rerun unless needed for regression verification.

## 4. Attack-tail contract

For attack-tail blocks (exciter still vibrating) of ordinary fixtures:

```text
max callback < 2666.7 us
overruns = 0
```

Qualified hardware results (M6.3.9.1):

```text
PAN  max ~2051 us
BELL max ~2114 us
```

## 5. Buffered high-energy Bell exception

The production system must **not** be described as "every callback meets
2.667 ms". That statement is false. The correct interpretation is:

> ESPan32 is qualified as a **buffered real-time audio engine**. Normal sustain
> and attack-tail processing remain within the nominal 2.667 ms block period.
> Extreme high-energy polyphonic Bell states may temporarily exceed one block
> period, but the configured six-descriptor I2S pipeline absorbed all measured
> timing debt during the qualified active hardware soak without transport
> starvation, I2S error, MIDI loss, BLE loss, crash, or memory drift.

During unusually high-energy Bell polyphony:

```text
8 active voices
10 modal resonators/voice
high internal amplitude
internal safety saturation active
```

the per-mode `std::tanh()` safety processing
(`ModalResonatorBank::processSample*`, `main/dsp/modal_resonator.cpp`) can place
callback runs above the nominal period. This saturation is sound-defining and
is intentionally **not** optimized by approximation.

Current hardware qualification (M6.3.9.2, 15.47 min active soak):

```text
worst callback                      3.609 ms
worst accumulated render debt       7.536 ms
estimated minimum effective
  buffered headroom                 ~2.73 blocks ~ 7.27 ms
transport starvation                0
I2S timeout / short write / TX err  0 / 0 / 0
BLE reconnects / MIDI drops         0 / 0
heap drift / crash                  0 / 0
nominal overruns (lifetime)         317
max consecutive nominal overruns    53
```

The strict experimental clause `max consecutive overruns <= 1` is **not**
satisfied. M6.3.9.2 is therefore historically **PARTIAL**. M6.4 accepts the
measured hardware behavior as the production performance contract, because the
remaining cost is confined to extreme high-energy Bell saturation and hardware
soak showed bounded recovery with zero transport failure.

## 6. Wording rule

Do **not** call the derived number a direct hardware descriptor occupancy
measurement. It is derived telemetry.

```text
Correct:   estimated minimum effective buffered headroom
Incorrect: measured I2S descriptor occupancy
```

The occupancy figures are reconstructed from produced-block count and elapsed
time (`main/diag/transient_qual.h`), not read from the DMA controller.

## 7. Known limitation (frozen)

> Under extreme high-energy 8-voice Bell conditions, internal nonlinear modal
> safety saturation can raise render cost above the nominal 2.667 ms block
> period for multiple consecutive callbacks. Hardware qualification with the
> production 6x128-frame I2S DMA configuration showed bounded accumulated
> timing debt and no I2S starvation, transport errors, MIDI loss, BLE loss,
> crash, or memory drift. Further optimization of this path is deferred unless
> real musical use demonstrates an audible failure.

## 8. Scope and non-goals

- The generic performance optimization campaign is **CLOSED** after M6.3.9.2.
- This contract does not imply that all future instruments share the Bell
  worst case. A model with 4-6 modes/voice can be substantially cheaper. Every
  new instrument receives its own lightweight CPU qualification.

## 9. Reopen conditions

Performance may be reopened only for one of:

```text
audible click
audible dropout
transport failure
instability
NaN / Inf
runaway
reproducible musical defect
```

Small host or hardware timing variation is **not** a reopen condition.
