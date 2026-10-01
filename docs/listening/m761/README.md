# MBIRA V1 exact PCM fixtures

MBIRA V1 FROZEN = Candidate B + Buzz ON.

Seven canonical, unnormalized six-second fixtures at 48 kHz are retained as
little-endian signed int32 interleaved stereo PCM (duplicated mono). Each file
contains 288,000 stereo frames / 2,304,000 bytes. These retain full engine output,
without the historical listening pack's 16-bit conversion or RMS attenuation.

| Fixture | Exact PCM FNV64 |
|---|---|
| D4 v70 | d2d22bb4f0527a91 |
| D4 v110 | fd0dee33bee22f45 |
| chord | 87754361a1eb13a9 |
| groove | 085e26a7f14868c9 |
| cluster8 | be6481b0ed7d647d |
| roll | fb46dbb9d58783e1 |
| buzz-on reference (D4 v127) | d63800af5cdb96dd |

`tests/test_mbira.cpp` asserts these hashes with Process6 ON and OFF, and
`tests/test_prepared_note.cpp` asserts the full MIDI 24–96 × v30/70/110/127
aggregate `f5ee8755af2fa270`. Historical A/C fixtures remain under `m76`.
The accompanying metrics CSV contains the safety measurements; candidate A/C
rows are historical comparisons, never production alternatives.
