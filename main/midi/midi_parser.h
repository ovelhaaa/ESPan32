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

    // Parse incoming raw MIDI 1.0 byte stream
    void parseByte(uint8_t byte);

    // Reset parser state
    void reset();

private:
    enum class BleParseState : uint8_t {
        ExpectTimestamp,
        ExpectStatusOrData,
        ExpectData
    };

    void dispatchEvent(const MidiEvent& ev);
    void dispatchCurrentMessage(uint8_t status, const uint8_t* data, uint8_t count, uint16_t timestamp13);

    EventCallback callback_ = nullptr;
    void* userData_ = nullptr;

    // Running status and parser state for BLE
    BleParseState bleState_ = BleParseState::ExpectTimestamp;
    uint8_t runningStatus_ = 0;
    uint8_t currentStatus_ = 0;
    uint8_t pendingBytes_[2] = {0, 0};
    uint8_t pendingCount_ = 0;
    uint8_t expectedCount_ = 0;
    uint16_t timestampHigh6_ = 0;
    uint16_t currentTimestamp13_ = 0;
    bool inSysEx_ = false;

    // Parser state for serial MIDI 1.0 byte stream
    uint8_t serialRunningStatus_ = 0;
    uint8_t serialPendingBytes_[2] = {0, 0};
    uint8_t serialPendingCount_ = 0;
    uint8_t serialExpectedCount_ = 0;
};

} // namespace pocketpan::midi
