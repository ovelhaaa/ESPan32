# Bell V1 final report (updated by M6.4)

## Status

**BELL V1 FROZEN.** Human listening found no significant difference between
candidates A, B and C, so the performance tie-break retained baseline A
(fewest modal safety-saturation events; no timing difference). No parameter
changed. The earlier hardware-gate blocker is resolved: all transport counters
stayed zero and the production 6x128-frame I2S pipeline absorbed the measured
high-energy Bell load with bounded timing debt. The canonical interpretation is
in [performance_contract_v1.md](performance_contract_v1.md). Details:
[bell_v1_freeze.md](bell_v1_freeze.md).

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

## Freeze closure

1. Human listening: DONE — A / B / C perceived as equivalent; no click or
   dropout on the board.
2. Performance tie-break: DONE — baseline A has the fewest modal
   safety-saturation events and no timing difference; A retained.
3. Hardware objective smoke: PASS (`docs/hardware/m64_smoke_summary.md`).

Bell V1 is `FROZEN` in [bell_v1_freeze.md](bell_v1_freeze.md). No parameter
delta and no regenerated Bell hashes are required because nothing changed.
