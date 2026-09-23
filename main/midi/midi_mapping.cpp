#include "midi_mapping.h"
#include <cmath>

namespace pocketpan::midi {

namespace {
// Note names for standard 12-TET chromatic scale
const char* const kNoteNames[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

// Formatted note name buffer (e.g. "D3", "G#2")
char sNoteNameBuffer[8] = "";

// Generate precalculated 128-element Note-to-Hz table
struct NoteTable {
    float hz[128];
    constexpr NoteTable() : hz{} {
        // Run-time or static initialization
    }
};

float calculateNoteHz(int note) {
    return 440.0f * std::pow(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
}

} // namespace

float MidiMapping::noteToHz(uint8_t note) {
    static bool initialized = false;
    static float sNoteTable[128];

    if (!initialized) {
        for (int i = 0; i < 128; ++i) {
            sNoteTable[i] = calculateNoteHz(i);
        }
        initialized = true;
    }

    if (note > 127) note = 127;
    return sNoteTable[note];
}

const char* MidiMapping::noteToName(uint8_t note) {
    if (note > 127) return "---";

    const int semitone = note % 12;
    const int octave = (note / 12) - 1; // MIDI Note 60 is C4 (or C3 depending on standard; 60/12 - 1 = 4)

    const char* base = kNoteNames[semitone];
    // Format into thread-local / static buffer
    sNoteNameBuffer[0] = base[0];
    if (base[1] == '#') {
        sNoteNameBuffer[1] = '#';
        sNoteNameBuffer[2] = static_cast<char>('0' + (octave >= 0 ? octave : 0));
        sNoteNameBuffer[3] = '\0';
    } else {
        sNoteNameBuffer[1] = static_cast<char>('0' + (octave >= 0 ? octave : 0));
        sNoteNameBuffer[2] = '\0';
    }

    return sNoteNameBuffer;
}

} // namespace pocketpan::midi
