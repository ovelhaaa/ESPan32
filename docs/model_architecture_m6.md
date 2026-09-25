# M6 model architecture

`InstrumentModel` selects a static `InstrumentModelConfig` from the registry:
PAN is the boot default and BELL is the first additional model. A config owns
its preset, exciter, resonator, generic `ModalVoicingConfig`, body setup,
sympathetic setup, body-input strategy, and strike-bus gain.

`ModalVoice` receives this configuration when the model changes; it has no
PAN conditional in its sample path. The allocator copies it to its fixed eight
voices. There is no virtual dispatch, heap allocation, RTTI, or callback
polymorphism in audio processing.

Model switching is intentionally a reset gesture, not a performance crossfade:
the allocator/exciters, modal states, body, sympathetic bus, and limiter are
reinitialized before the new static config is applied. Re-seeding the exciter
noise during this initialization makes `PAN -> BELL -> PAN` deterministic.
The M6 host fixture compares the final PAN PCM hash with direct PAN and with
the M5D.1 freeze fingerprints.

Bell uses MIDI frequency as its prime reference (`ratio 1.0`). Its ten modes
are in the static Bell preset; its explicit ratio doublets are independent of
the PAN fixed-1-Hz doublet. Bell V1 disables the shared PAN body and
sympathetic path. Modes over the safe Nyquist region are disabled by the
generic resonator bank rather than clamped.
