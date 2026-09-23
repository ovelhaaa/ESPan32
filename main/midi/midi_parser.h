#pragma once

#include <cstdint>
#include <cstddef>
#include "midi_event.h"

namespace pocketpan::midi {

class BleMidiParser {
public:
    using EventCallback = void (*)(void* userData, const MidiEvent& event);

    BleMidiParser() = default;

    void setCallback(EventCallback callback, void* userData) {
        callback_ = callback;
        userData_ = userData;
    }

    // Parse incoming packet from BLE GATT notification
    void parseBlePacket(const uint8_t* data, size_t length);

    // Parse incoming raw MIDI byte stream
    void parseByte(uint8_t byte);

private:
    void dispatchEvent(const MidiEvent& ev);

    EventCallback callback_ = nullptr;
    void* userData_ = nullptr;

    // Running status and parser state
    uint8_t runningStatus_ = 0;
    uint8_t pendingBytes_[2] = {0, 0};
    uint8_t pendingCount_ = 0;
    uint8_t expectedCount_ = 0;
    uint16_t currentTimestamp13_ = 0;
};

} // namespace pocketpan::midi
