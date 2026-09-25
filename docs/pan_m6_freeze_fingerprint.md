# PAN M6 freeze fingerprint

Host fixture fingerprint for the approved M5D.1 PAN configuration (48 kHz,
interleaved stereo `int32_t` PCM, FNV-1a 64-bit). M6 keeps these as test
constants and verifies an explicit `PAN -> BELL -> PAN` switch against the
same direct-PAN fixture.

| Fixture | FNV-1a 64 |
|---|---:|
| D3 v30 | `dfff8cb4bc9451ad` |
| D3 v70 | `625f551231401141` |
| D3 v110 | `b28e308f6f3b7ff9` |
| D3 v127 | `857e3b19560fb8fd` |
| A3 v70 | `8fd51136812f9c09` |
| D4 v70 | `fb6d1d3cb77ae911` |
| A4 v70 | `1c664493f822e449` |
| soft -> hard restrike | `1230248779ad08dd` |
| hard -> soft restrike | `153a4d12cc9da585` |
| D3 roll | `c84c3e607e2b5129` |
| D3+A3+D4+A4 chord | `3c6d8246768089e9` |
| cluster8 | `98494cd426a587a1` |

Hashes are environment-specific by design: the gate is exact PCM on the same
host/compiler configuration, not a cross-platform floating-point claim.
