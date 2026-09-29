# M6.4 hardware smoke summary

Board: ESP32-S3 (COM10, `USB\VID_303A&PID_1001`)
Toolchain: ESP-IDF v5.3 (`C:\Users\devx\esp\esp-idf`)
Production config: DSP candidate 25, UI F0, 48 kHz / 128, 8 voices, no soak,
no transient qualification.

## Build (Phase Q)

| Build | Result |
|---|---|
| Production (`build/`, defaults) | PASS, `pocket_pan.bin` 0x9dc00 B, 38% partition free |
| Active-soak (`build-m64-soak/`, `POCKETPAN_ACTIVE_SOAK=1`, `POCKETPAN_TRANSIENT_QUAL=1`) | PASS, `pocket_pan.bin` 0xa2d70 B, 36% free |

## Production boot smoke

Log: `docs/hardware/m64_production_boot.log` (22 s, idle PAN).

```text
[AUDIO] model=PAN blocks=13140 avg_us=331 p99_us=425 max_us=806 cpu_load=12.4
        deadline=0 timeout=0 tx_error=0 short=0
[MIDI]  push=0 pop=0 drop=0 hwm=0
[BLE]   state=8 interval_ms=11.25 rssi=-64 reconnects=0
[MEM]   internal_free=149127 largest_internal=47104  (delta over run: 0)
```

## Active musical soak (Phase P / R objective half)

Log: `docs/hardware/m64_active_smoke.log` (210 s, 3.51 min). The automated
workload covers single notes, 2-note intervals, 4/6/8-voice chords, restrikes,
a fast roll, NoteOff, PolyPressure, ChannelPressure, and PAN/BELL toggling
every ~75 s.

Lifetime qualification counters (never reset on model switch), final:

```text
callbacks          82,686
nominal overruns   31
max callback       3,079 us
max streak         1
max render debt    412 us
max recovery       3 blocks
```

Transport and system counters, final:

```text
I2S timeout / short / TX error   0 / 0 / 0
MIDI push / pop / drop           1,197 / 1,197 / 0   (queue hwm 4)
BLE reconnects                   0                    (state 8, rssi -66)
crash                            0
heap internal_free               130,007 B -> 130,007 B (delta 0)
```

Representative overrun records (all isolated, `streak=1`):

```text
seq=77601 cb_us=3007 render_us=2736 model=BELL v=6->8 on=2 transition=6->8  debt_after=340
seq=77613 cb_us=2896 render_us=2856 model=BELL v=8->8 on=0 transition=other debt_after=229
seq=72332 cb_us=2887 render_us=2573 model=PAN  v=6->8 on=2 transition=6->8  debt_after=220
```

## Interpretation

- The soak monitor reports `FAIL` only because its gate is the superseded
  strict rule `deadline == 0`; the production `[AUDIO] deadline` counter is a
  reset-on-model-switch window counter (peak 9 here). This is the accepted
  buffered high-energy behavior of
  [performance_contract_v1.md](../performance_contract_v1.md), not a transport
  failure.
- Every overrun was isolated (`max_streak = 1`) with small, fully recovered
  debt (<= 412 us), smaller than the 15-minute M6.3.9.2 soak (streak 53,
  debt 7,536 us). No catastrophic regression.
- No I2S timeout, short write, TX error, MIDI drop, BLE reconnect, crash or
  heap drift.
- The `[M6392]` occupancy-derived headroom was **not usable** in this short
  run: its lifetime anchors are invalidated across model switches, producing a
  spurious large negative `min_occ`. The headroom number is therefore deferred
  to the M6.3.9.2 15-minute soak (~2.73 blocks estimated minimum effective
  headroom). Per the wording rule it is derived telemetry either way.

## Audible assessment

**Not performed by the automated harness.** Audible glitch / click / dropout
must be judged by a human on the same DAC/headphone chain, together with the
Bell V1 listening pack in [../bell_v1_listening_pack.md](../bell_v1_listening_pack.md).
No transport-level realtime artifact was observed.
