# PAN host baseline metrics

Generated deterministically by `test_dsp` at 48 kHz / 128 frames. WAV output is
a CI artifact, not source-controlled audio. `Modal saturation` is zero for the
normal single-note velocity range in the host assertions.

| Fixture | Peak | RMS | Crest | Limiter hits | Modal saturation |
|---|---:|---:|---:|---:|---:|
| D3 v40 | 0.314 | 0.033 | 9.5 | 0 | 0 |
| D3 v90 | 0.654 | 0.068 | 9.6 | 0 | 0 |
| D3 v127 | 0.944 | 0.102 | 9.3 | 69 | 0 |
| D4 double strike | 0.971 | 0.114 | 8.5 | 121 | recorded by test run |
| chord | 0.788 | 0.108 | 7.3 | 0 | recorded by test run |

The modal nonlinearity remains a safety guard pending an offline A/B audit; it
is not intentionally used as normal PAN voicing.
