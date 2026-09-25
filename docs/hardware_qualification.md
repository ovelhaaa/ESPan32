# M4 hardware qualification

The normal firmware includes the built-in Audio Diagnostic page; no separate
`HARDWARE_DIAGNOSTIC` build mode exists. Record physical observations below.
Firmware metrics are diagnostic evidence,
not a claim that playback or BLE was physically validated.

## Audio — TENSTAR + PCM5102

| Check | Result / notes |
|---|---|
| Boot, TFT visible | |
| Silence source has no audible output | |
| 440 Hz and 1 kHz frequency verified | |
| -12 dBFS level verified | |
| Left-only / right-only routing verified | |
| No glitches while TFT updates | |
| PAN single note and 8-note cluster | |
| Single notes clean (v40/v90/v127) | |
| 2-note interval clean | |
| 4-note chord clean | |
| Cluster8 clean | |
| Rapid roll clean | |
| No obvious pumping / harsh waveshaping | |
| Transient remains natural | |
| Max observed limiter GR | |

The BOOT button opens Status, MIDI Diagnostic, then Audio Diagnostic. In Audio
Diagnostic, tap to cycle `SILENCE`, `440 HZ`, `1K HZ`, `1K -12`, `LEFT`,
`RIGHT`, and `PAN`; hold it to return to Status.

## BLE — SMC-PAD

| Check | Result / notes |
|---|---|
| Scan / connect / service discovery / CCCD / READY | |
| Disconnect and reconnect while audio runs | |
| Note and velocity shown in MIDI Diagnostic | |
| Raw bytes captured | |
| Aftertouch classification: Poly Pressure / Channel Pressure / CC / none | |
| Rapid playing, 8 voices, TFT active | |

Poly Pressure maps to damping for its matching note. Channel Pressure maps to
global damping; it is never assigned to an arbitrary last note.

## Timing and analyzer readiness

`deadlineMisses` means render time was at least 2667 us for a 128-frame,
48 kHz block. `writeTimeouts`, `txErrors`, and `shortWrites` are independent
I2S transport outcomes. Do not call a write timeout an underrun.

No spare GPIO has been validated for `DEBUG_AUDIO_BLOCK` or `DEBUG_MIDI_EVENT`.
Reserve and electrically validate pins before adding logic-analyzer pulses;
the desired measurement is BLE callback to next audio block.

## Physical-validation status

**REQUIRES PHYSICAL VALIDATION:** PCM5102 playback, TENSTAR display, BLE
connection parameters/RSSI, SMC-PAD aftertouch type, real CPU load, deadline
misses, and end-to-end latency.
