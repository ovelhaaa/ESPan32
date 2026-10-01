#pragma once

#include <cstddef>
#include <cstdint>

#include "modal_mode.h"

namespace pocketpan::dsp {

// This is intentionally the useful playing range rather than a 128-note
// maximum table.  Notes outside it (and non-canonical external frequencies)
// retain the established coefficient path.
constexpr uint8_t kPreparedNoteFirst = 24;
constexpr uint8_t kPreparedNoteLast = 96;
constexpr size_t kPreparedNoteCount =
    static_cast<size_t>(kPreparedNoteLast - kPreparedNoteFirst + 1);

// Values here are invariant for one model, one MIDI note, 48 kHz, and the
// initial undamped NoteOn state.  Register and all velocity-dependent strike
// work deliberately stay on the normal trigger path; the table owns only the
// expensive modal-coefficient result.
struct PreparedNote {
    uint8_t midiNote = 0;
    uint8_t modeCount = 0;
    uint16_t activeMask = 0;

    float fundamentalFrequencyHz = 0.0f;
    float tubePhaseCoefficient = 0.0f;
    float tubeRegisterPosition = 0.0f;

    float a1[kMaxModesPerVoice]{};
    float a2[kMaxModesPerVoice]{};
    float modalAmplitude[kMaxModesPerVoice]{};

    bool matches(uint8_t note, float frequencyHz) const {
        return midiNote == note && fundamentalFrequencyHz == frequencyHz;
    }
};

struct PreparedNoteTable {
    PreparedNote entries[kPreparedNoteCount]{};
    bool ready = false;

    // Reset table in-place to avoid stack temporary allocations.
    void reset() {
        ready = false;
        for (auto& entry : entries) {
            entry = PreparedNote{};
        }
    }

    const PreparedNote* find(uint8_t note, float frequencyHz) const {
        if (!ready || note < kPreparedNoteFirst || note > kPreparedNoteLast) {
            return nullptr;
        }
        const PreparedNote& candidate =
            entries[static_cast<size_t>(note - kPreparedNoteFirst)];
        return candidate.matches(note, frequencyHz)
            ? &candidate : nullptr;
    }

    static constexpr size_t bytesPerModel() { return sizeof(PreparedNoteTable); }
};

// SAFETY GUARD (M6.3.8 / M6.3.9): PreparedNoteTable is ~10.5 KB, exceeding
// the ESP-IDF main task stack (typically 8,192 bytes). Never allocate
// PreparedNoteTable as a local stack variable or automatic temporary!
// Always allocate as static, member of SynthEngine in BSS/heap, or reset in-place.
static_assert(sizeof(PreparedNoteTable) > 4096,
              "PreparedNoteTable is large; must never be allocated on the stack");

} // namespace pocketpan::dsp
