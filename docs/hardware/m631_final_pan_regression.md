# PAN M6 regression

| Fixture | Expected FNV | Actual FNV | Result |
|---|---|---|---|
| D3 v30 | `0xdfff8cb4bc9451ad` | `0xdfff8cb4bc9451ad` | PASS |
| D3 v70 | `0x625f551231401141` | `0x625f551231401141` | PASS |
| D3 v110 | `0xb28e308f6f3b7ff9` | `0xb28e308f6f3b7ff9` | PASS |
| D3 v127 | `0x857e3b19560fb8fd` | `0x857e3b19560fb8fd` | PASS |
| A3 v70 | `0x8fd51136812f9c09` | `0x8fd51136812f9c09` | PASS |
| D4 v70 | `0xfb6d1d3cb77ae911` | `0xfb6d1d3cb77ae911` | PASS |
| A4 v70 | `0x1c664493f822e449` | `0x1c664493f822e449` | PASS |
| restrike soft-hard | `0x1230248779ad08dd` | `0x1230248779ad08dd` | PASS |
| restrike hard-soft | `0x153a4d12cc9da585` | `0x153a4d12cc9da585` | PASS |
| roll | `0xc84c3e607e2b5129` | `0xc84c3e607e2b5129` | PASS |
| chord | `0x3c6d8246768089e9` | `0x3c6d8246768089e9` | PASS |
| cluster8 | `0x98494cd426a587a1` | `0x98494cd426a587a1` | PASS |

## PAN → BELL → PAN

Direct PAN and PAN after a Bell switch: **PASS** (bit-identical `0x625f551231401141`).
