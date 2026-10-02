# M7.7 — UDU technically qualified listening model

This is the retained fixed-PARTIAL M7.7 baseline. Velocity opening is now
implemented and measured in [M7.7.1](m771_udu_dynamic_hole.md), then frozen in
[M7.7.2](m772_udu_v1_freeze.md) as B + CENTERED + R1. The evidence below
continues to describe the original M7.7 firmware and reference pack.

UDU follows frozen MBIRA V1 in the ten-model registry. The user selected
character B after listening. B / PARTIAL / compressed size mapping remains the
runtime baseline, without a UDU freeze hash. Velocity-driven opening transitions
are deferred to the next implementation round. MBIRA V1 = B + Buzz ON was frozen and
physically qualified first; its nine-instrument regression baseline is immutable.

## Air/body signal flow

`hand displacement → two air resonators ┐`

`short finger/palm contact → four ceramic shell resonators ┴→ sum → DC guard → existing output`

This independent hybrid path uses six second-order damped recurrences, not the
pitched-bar ModalResonatorBank. The two air modes receive a rounded low-frequency
hand pulse. Four shell modes receive a separate short ceramic contact pulse with
a small deterministic broadband component. Stored resonator energy survives
restrikes; allocation, declicked steals, release gestures and pressure remain in
the established allocator. No oscillator, pitch sweep, sample transposition,
metallic tail, delay network, convolution, airflow solver or added effect is used.

The air resonators obey `y = sin(ω) x + 2 r cos(ω) z1 - r² z2`, with
`r = exp(-ln(1000)/(sampleRate*T60))`. Coefficients are prepared before I2S
starts. The final fixed six-mode updates preserve summation order and match all
21 pre-unroll listening WAVs byte for byte. They do not change previous DSP paths.

## Acoustic parameters

At MIDI 60 and PARTIAL, provisional B has main cavity **105 Hz / .42 s T60**,
secondary **242.55 Hz / .1218 s**, with output weights 1 / .17. Its four dry
ceramic shell modes are:

| Shell Hz at MIDI 60 | Output weight | T60 seconds |
|---:|---:|---:|
| 460 | .120 | .085 |
| 790 | .075 | .055 |
| 1230 | .045 | .038 |
| 1970 | .025 | .025 |

The main cavity represents opening air inertia against enclosed air compliance;
the second represents a weak cavity/opening interaction. The shell modes are
inharmonic, short, and independently driven. These frequencies are reduced
acoustic design assumptions, not measured ceramic-pot data.

The hand pulse is a difference of two decaying envelopes (.35 ms fast, candidate
slow time constant); high velocity slightly shortens the slow pulse. Air drive
is `v*(.72+.28*v)*cavityGain`. The small air broadband component is .045 times
colored noise. Ceramic contact uses `4*fast*(1-fast)` with a 4200 Hz one-pole
colored component; its short-pulse normalization is 45. Shell drive is
`v*(.12+.88*v²)*shellGain`. Thus soft hits emphasize warm air, while hard hits
excite more ceramic attack and stronger cavity punch. Output gain is .0016.
No limiter reduction is used for tone. A local 8 Hz DC guard and the shared
UDU-only 20 Hz output guard cover finite strikes and removed steal state.

| Candidate | Cavity Hz | Main T60 | Cavity drive | Shell drive | Slow hand ms |
|---|---:|---:|---:|---:|---:|
| A — Deep / Hollow | 80 | .65 | 1.20 | .38 | 4.2 |
| B — Balanced / Acoustic | 105 | .42 | 1.00 | 1.00 | 2.8 |
| C — Dry / Percussive | 140 | .22 | .85 | 1.50 | 1.3 |

At MIDI 60 v110, first-50-ms windowed energy above 400 Hz versus below 350 Hz
is −29.99 / −16.02 / −2.88 dB for A/B/C. This checks distinct shell presence
while the air band remains stronger; it is not a perceptual listening verdict.

## Virtual size and opening experiments

Runtime clamps MIDI to 36–84 and uses `size = 2^((note-60)*.25/12)`.
Air frequencies scale by size; shell frequencies by size^.82. Four keyboard
octaves therefore span one air octave. B main frequencies are approximately
74.25 / 105 / 148.49 Hz at low/mid/high. Notes outside the range retain the
endpoint pot size and deterministic excitation. This avoids implausibly tiny
or huge pots and leaves high taps recognizably ceramic.

A host-only fully pitched alternative uses exponent 1.0, yielding B air modes
26.25 / 105 / 420 Hz across MIDI 36/60/84; those three probes are retained.
The bounded family is a provisional engineering choice, subject to listening.

OPEN shifts only air frequency ×1.18 and T60 ×.72; PARTIAL is ×1 / ×1;
CLOSED uses ×.72 and ×1.30. These are cheap covered-opening hypotheses,
not fluid simulation. They do not change shell modes and add no UI control.
The [listening index](listening/m77/README.md) includes three RMS-matched B
opening grooves, the 15 A/B/C comparisons and the three mapping probes.

## Host qualification and immutable regressions

Both Process6 configurations pass all twelve host suites. UDU has 120 raw
fixtures covering the focused notes/groove, opening states, extreme MIDI,
velocity, chord4/cluster8 at v127, rolls, pressure and deliberate steals.
All have zero NaN/Inf, hard clamps, modal saturation, max/average limiter GR
and samples above .1 / 1 dB GR. Maximum peak/pre-limiter peak is .434929;
maximum absolute phrase DC is 4.753e-5, maximum voices 8, maximum steals 8.
Peak, RMS, crest and all requested counters are retained per fixture in the
[metrics CSV](listening/m77/host_metrics.csv) and [summary](qualification/m77/host_summary.json).

RT allocation guards exercise all ten models; single/restrike PCM is exact
across 128/37-frame partitions and the fast/reference toggles. Existing
block-count headroom makes arbitrary polyphonic partition comparisons depend
on active-count boundaries; that shared behavior is preserved. The PreparedNote
suite covers all 100 model boundaries and restores PAN exactly after UDU.

All nine frozen aggregate hashes remain exact, as do the Vibraphone dry/M1
fixtures and seven MBIRA B + Buzz ON fixtures:

| Model | Frozen FNV64 |
|---|---|
| PAN | 9e9801244165101d |
| BELL | 7c2dc6f37fb99f4b |
| TONGUE | 83f75567fb47ebaf |
| BOWL | 14772092e35bc4a7 |
| KALIMBA | e9bcabdd0b6e3bd2 |
| GLASS | 14e0dd6ba566e1cf |
| MARIMBA | 236d684f05f5f1d2 |
| VIBRAPHONE V1 | 3cea893644aa0283 |
| MBIRA V1 | f5ee8755af2fa270 |

[Full exact regression log](qualification/m77/regression_hashes.log).

## Memory and expansion boundary

PreparedNote remains 132 bytes. The nine 9640-byte tables still total 86760
bytes. A separate 49-size UduCache is **3576 bytes**, including six sets of
three coefficients per size and shared hand/DC constants. It saves 6064 bytes
against a tenth full table. UduVoice is 104 host bytes; host SynthEngine is
105624 bytes (+4696), device SynthEngine 105272 (+4600).

Final restored connected production memory is **40303 internal free bytes**, largest
block **23552 bytes**, versus frozen MBIRA production 45687 / 27648. No RT heap,
trig or coefficient generation is added. Pressure uses a cheap smoothed damping
multiplier. Model switching selects ready coefficients; host-only configuration
changes rebuild the independent cache outside rendering.

The current memory remains above conservative 32 KiB free / 16 KiB largest-block
guardrails. **Further instrument expansion stops here until cache storage is
reviewed**: another full 9.6 KB table would cross the free-memory guardrail.
No general cache redesign was necessary for UDU.

## Physical evidence and remaining decisions

Final physical timing, BOOT and restored production evidence is retained under
`qualification/m77`. Qualification uses actual ESP32-S3 / COM10, BLE MIDI ready,
240 MHz, 48 kHz / 128 frames, live UI/I2S, and deterministic fixture events in
the audio callback. BOOT gestures are injected at the GPIO input sample and
exercise the real production handler; they are not manual button presses.

| Fixture | Avg µs | p95 µs | p99 µs | Max µs | CPU % |
|---|---:|---:|---:|---:|---:|
| single v127 | 269 | 310 | 400 | 727 | 10.09 |
| chord4 v127 | 624 | 740 | 850 | 1203 | 23.40 |
| cluster8 v127 | 1083 | 1285 | 1475 | 1950 | 40.61 |
| hand groove | 426 | 560 | 750 | 1198 | 15.97 |
| model switching | 278 | 325 | 425 | 1141 | 10.42 |

All five 8192-block fixtures have zero deadline misses, I2S timeouts/errors/
short writes, hard clamps, modal saturation, nonfinite and BLE losses. Cluster8
retains 716.7 µs of deadline margin. p95 combines the 5 µs full-callback histogram
classes; p99 is the existing 75 µs AudioStats histogram. Exact callbacks include
event and transition work. [Verified manifest](qualification/m77/hardware_manifest.json).

The first shell-pulse development capture and the pre-unroll capture are retained
with their binaries, clearly named `development_*` and `pre_unroll_*`. Neither
is substituted for final evidence. The pre-unroll cluster had only 163.7 µs
margin; fixed-index updates reduced average 1598→1083 µs and maximum 2503→1950 µs
without changing the listening WAVs. UDU is now cheaper than MBIRA on these
single/chord/cluster fixtures. No global DSP optimization or cache redesign was done.

BOOT qualification passes 20 long gestures (two full ten-model cycles), with
VIBRAPHONE → MBIRA → UDU → PAN observed twice and all 21 transition labels
correct, including one additional PAN→BELL from the extended hold. Short gestures
retain Status→MidiDiagnostic→AudioDiagnostic; the third invokes the unchanged
diagnostic-tone handler. The long gesture returns to Status. The
[BOOT manifest](qualification/m77/boot_manifest.json) verifies the captured
sequence and zero deadline/I2S faults.

A final physical MBIRA recheck after UDU retains exact sound hashes and healthy
cluster margin: avg 1394 µs, p95 1455 µs, p99 1600 µs, max 2260 µs, 52.27% CPU,
406.7 µs margin, all required faults zero. Chord4 averages 867 µs / 32.51%.
This confirms that the new path did not compromise the frozen production model.
[Recheck manifest](qualification/m77/mbira_recheck/hardware_manifest.json).

Normal production firmware was rebuilt with BOOT injection and all instrument
forensics disabled, flashed back to COM10, and verified booting PAN with BLE
MIDI ready at 11.25 ms. Three retained periodic samples show 40303 internal
free bytes / 23552 largest block and zero deadline/I2S faults. Startup before
BLE settles has 36127 internal free bytes / 27648 largest block. Diagnostic
MBIRA builds temporarily fall to 29463 / 9216 because their measurement records
also consume internal SRAM; those are not the production cache budget.
[Final restore/provenance checks](qualification/m77/completion.json).

The end state is MBIRA V1 frozen B + Buzz ON and technically qualified UDU
with character B selected by the user. Velocity-driven opening transitions
remain for the next implementation round, followed by listening and qualification.
Final opening behavior and size mapping remain open; UDU is not frozen.
