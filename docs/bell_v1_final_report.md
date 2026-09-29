# Bell V1 final report (updated by M6.4)

## Status

**NOT FROZEN — human listening decision pending.** The previous blocker
(Bell chord/cluster8 "failing" the ESP32-S3 real-time gate) is resolved: the
battery of transport counters never moved, and the production 6x128-frame I2S
pipeline absorbed the measured high-energy Bell load with bounded timing debt.
The canonical interpretation is in
[performance_contract_v1.md](performance_contract_v1.md).

## Current production candidate

Candidate A remains the compiled `kPresetBell` baseline; no M6.4 listening
candidate was promoted and no parameter was changed. Prime doublet remains
1.0020, nominal doublet remains 2.0015. PAN remains the M6 fingerprinted model.

See:
- [bell_v1_baseline.md](bell_v1_baseline.md) — verified baseline tables.
- [bell_v1_listening_pack.md](bell_v1_listening_pack.md) — A/B/C pack and procedure.
- [bell_v1_freeze.md](bell_v1_freeze.md) — freeze record and criteria.
- [m64_final_report.md](m64_final_report.md) — M6.4 milestone report.

## Gates

- PAN FNV regression: 12/12 exact, including PAN -> BELL -> PAN bit identity.
- Bell baseline safety: hard clamp 0, modal saturation 0, NaN/Inf 0 across the
  full M6.4 pack.
- Host suite: 9/9 PASS.
- Real-time: qualified by the frozen performance contract; no transport
  starvation, I2S error, MIDI drop, BLE loss, crash or heap drift.

## Remaining to freeze Bell V1

1. Record the human listening decision on A/B/C (A wins an unresolved tie;
   keep baseline if no candidate clearly improves it).
2. Confirm audibly, while listening, that the hardware musical smoke
   (soft/hard singles, four-note chord, eight-voice passage, restrikes, roll,
   PAN/BELL switching) has no click or dropout. The objective counters already
   passed: `docs/hardware/m64_smoke_summary.md` (I2S 0/0/0, MIDI drops 0,
   BLE reconnects 0, max streak 1, heap delta 0).

Then replace this status with `BELL V1 FROZEN` in
[bell_v1_freeze.md](bell_v1_freeze.md). If a candidate wins, record the exact
parameter delta and the regenerated Bell reference hashes — never as a silent
test update.
