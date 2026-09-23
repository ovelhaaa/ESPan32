#pragma once

#include <cstdint>

namespace pocketpan::midi {

class MidiMapping {
public:
    // Fast note-to-frequency lookup (MIDI note 0 to 127 -> Hz)
    static float noteToHz(uint8_t note);

    // Get note name string (e.g. "D3", "C4", "F#2")
    static const char* noteToName(uint8_t note);

    // Normalize 7-bit MIDI value (0..127) to 0.0f..1.0f
    static float toNormalizedFloat(uint8_t value7Bit) {
        return static_cast<float>(value7Bit > 127 ? 127 : value7Bit) / 127.0f;
    }
};

} // namespace pocketpan::midi
