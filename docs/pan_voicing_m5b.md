# PAN voicing M5B

M5B refines the existing PAN voice before the frozen M5A output stage. The
polyphonic headroom, 32-sample lookahead limiter, -0.5 dBFS ceiling, 80 ms
release, and final hard-clamp guard are unchanged.

## Exciter and modal coupling

`PanVoicingConfig` is the single home for musical controls. A strike derives a
continuous hardness from velocity, then adjusts contact duration, noise burst
duration, and cutoff. Harder strikes are shorter and brighter, but noise gain
only grows modestly; the primary expressive change is modal coupling.

Each modal state now has a static `modalAmplitude` and an `excitationGain`.
The former is its preset identity; the latter is rebuilt for each new strike.
The resonator recursion and its delay state are not changed by a velocity
update. Therefore a same-note restrike is existing vibration plus newly
distributed excitation, not a reset or a rescaling of the tail.

Soft strikes favor fundamental, split fundamental, and octave. As velocity
rises, the 3f, 3.98f, 5.25f, 6.62f, and 8.18f modes receive progressively more
of the new excitation. This costs only a small per-mode multiplier and a
short interpolation; it adds no resonators, allocation, FFT, or hot-path I/O.

## Register and doublet behavior

Gain, brightness, and T60 use continuous interpolation from D3 to A4. Low
notes get a small gain/T60 lift; high notes receive a small gain, brightness,
and T60 reduction. The T60 span is 1.10x to 0.90x.

The old relative split (`0.0032`) produces beat rates from about 0.47 Hz at D3
to 1.41 Hz at A4. M5B uses a fixed 1.0 Hz target expressed as relative detune
at coefficient-update time. It keeps perceived movement consistent through
the tested register without changing the main fundamental pitch.

## Host evidence

Run `ctest --test-dir build-host-ninja --output-on-failure` after building.
The DSP fixture writes `pan_m5b_metrics.md` and untracked WAVs for D3 velocity
30/70/110 and D3/A3/D4/A4. Its compact Goertzel bank verifies that both
upper-mode/fundamental energy and the >3 kHz brightness proxy rise monotonically
from velocity 30 to 70 to 110. It also retains the 100 ms, 250 ms, and 75 ms
roll restrike fixtures and output limiter diagnostics.

For a strict M5A/M5B audible A/B, render the `*_old.wav` fixtures from commit
`aacf829` in a clean checkout, then render the identically named M5B fixtures;
WAVs are intentionally ignored by Git.

## Hardware listening checklist

- [ ] Soft strikes sound rounded.
- [ ] Medium strikes have clear pitch.
- [ ] Hard strikes gain brightness without harsh clipping.
- [ ] Upper notes are not excessively clicky.
- [ ] Low notes do not disappear.
- [ ] Beating is musical, not out of tune.
- [ ] Restrikes preserve the ringing tail.
- [ ] Rolls grow naturally.
- [ ] Four-note chords remain clean.
