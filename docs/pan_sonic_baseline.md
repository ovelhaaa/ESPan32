# PAN sonic baseline (M4.2)

This is a host-side regression baseline, not a statement of physical acoustic
validation. The PAN voice uses a fundamental modal pair (the second member is
split by 0.0032), an octave (2f), and a compound fifth (3f), followed by
quiet upper metallic modes. `PanCalibration` is the single source for PAN
exciter and resonator configuration; `ModalVoice` applies the resulting
generic configurations explicitly at initialization.

## Performance behavior

- Velocity increases strike energy and brightness through the PAN exciter.
- A same-note restrike preserves resonator state and injects a new excitation;
  it does not reset the vibrating modes.
- The qualification metric `attackRms` is the first **0.100 s**: 4,800 frames
  / 9,600 interleaved samples at 48 kHz.
- Host restrike fixtures (100 ms, 250 ms, and roll) verify finite output,
  output-safety peak at or below 1.0, bounded roll energy, and DC mean below
  0.005.

## Safety behavior

The output safety limiter is linear through 0.85 and then softly constrains
headroom. It can engage for aggressive restrikes, rolls, and dense clusters.

The M4.2 A/B audit measures `internalSafetySaturation` for single D3 velocity
127, 100 ms double strike, rapid roll, and cluster8. The current host result
is **A: it does not engage in the audited musical/stress material** (zero
modal saturation counts and zero RMS difference between A/B). It remains a
defensive guard, not part of the intended timbre. The generated detailed
table is `saturation_ab_metrics.md` in the host-test artifact.

## Diagnostic source policy

When a diagnostic source other than PAN owns output, MIDI is consumed and
published to the MIDI diagnostic telemetry but is intentionally not dispatched
to the synth. Entering or returning from that state requests a Core 0 synth
reset at the next audio-block boundary. The UI only changes the atomic source
selector; oscillator state is exclusively owned by Core 0. The recursive
oscillator is renormalized every 4096 samples and is regression-tested at 440
Hz for 30 seconds; 440 Hz, 1 kHz, -12 dBFS peak, and left/right routing are
also checked host-side.

## Known limitations and required physical validation

Host tests provide spectral sanity probes around f, 2f, and 3f; they do not
attempt to resolve the sub-1 Hz D3 doublet. They also do not validate actual
DAC level, speakers/headphones, mechanical resonances, BLE end-to-end latency,
or subjective tuning.

**Physical validation is still required** on TENSTAR + PCM5102 + SMC-PAD.
Record those results in `docs/hardware_qualification.md`.
