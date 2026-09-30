## BLE MIDI connected qualification

Actual MIDI-ready connection, interval 11.25 ms, latency 0. UI/LCD/I2S active, 8192 blocks per fixture. Both captures contain CCCD subscription success and periodic state=8. All inner/callback class rows have zero deadline, voice-count, BLE-loss, clamp and saturation observations. There was no externally generated MIDI flood; the musical workload is the deterministic diagnostic fixture.

| Fixture | Avg callback us | p95 us | p99 us | Max us | CPU % | Misses | I2S timeout/error/short |
|---|---:|---:|---:|---:|---:|---:|---|
| single motor OFF | 267 | 295 | 350 | 1151 | 10.01 | 0 | 0/0/0 |
| chord4 motor OFF | 634 | 690 | 900 | 1548 | 23.77 | 0 | 0/0/0 |
| cluster8 motor OFF | 1099 | 1155 | 1625 | 2297 | 41.21 | 0 | 0/0/0 |
| roll motor ON | 291 | 365 | 800 | 1104 | 10.91 | 0 | 0/0/0 |
| sustained phrase motor ON | 628 | 690 | 850 | 1737 | 23.55 | 0 | 0/0/0 |
| repeated model switches | 279 | 305 | 375 | 1019 | 10.46 | 0 | 0/0/0 |
| single motor ON | 280 | 305 | 375 | 905 | 10.50 | 0 | 0/0/0 |

Raw logs: `qualification/m75_ble_batch0_raw.log` and `m75_ble_batch1_raw.log`; extracted fields/checksums: `m75_ble_hardware_manifest.json`. Initial connected selection callback 632 us; 44 repeated double-selection events max 1019 us including strike/render. Steady motor delta 11.69 us/128 frames between the two connected runs; separate-run variability applies.

Periodic AUDIO model labels track the UI selection (PAN) while forensics owns synthesis. CURVE and FIXTURE_IO identify the actual VIBRAPHONE model. This matches the M7.4 reporting convention.

Repeat connected captures by setting POCKETPAN_FORENSICS_DISCONNECTED=0; the diagnostic waits for MIDI-ready subscription before starting. The disconnected setting intentionally declines discovered MIDI peripherals; it is a controlled radio scenario, not evidence that a controller was unavailable.
