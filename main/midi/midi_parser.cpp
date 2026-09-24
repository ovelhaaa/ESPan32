#include "midi_parser.h"

namespace pocketpan::midi {

void BleMidiParser::dispatchEvent(const MidiEvent& ev) {
    if (callback_) {
        callback_(userData_, ev);
    }
}

void BleMidiParser::reset() {
    bleState_ = BleParseState::ExpectTimestamp;
    runningStatus_ = 0;
    currentStatus_ = 0;
    pendingCount_ = 0;
    expectedCount_ = 0;
    timestampHigh6_ = 0;
    currentTimestamp13_ = 0;
    inSysEx_ = false;

    serialRunningStatus_ = 0;
    serialPendingCount_ = 0;
    serialExpectedCount_ = 0;
}

void BleMidiParser::dispatchCurrentMessage(uint8_t status, const uint8_t* data, uint8_t count, uint16_t timestamp13) {
    MidiEvent ev;
    ev.channel = (status & 0x0F) + 1;
    ev.timestamp13 = timestamp13;
    ev.data1 = (count >= 1) ? data[0] : 0;
    ev.data2 = (count >= 2) ? data[1] : 0;
    ev.rawBytes[0] = status;
    ev.rawBytes[1] = ev.data1;
    ev.rawBytes[2] = ev.data2;

    const uint8_t statusType = status & 0xF0;
    switch (statusType) {
        case 0x80:
            ev.type = MidiEventType::NoteOff;
            break;
        case 0x90:
            // MIDI specification: NoteOn with velocity 0 is treated as NoteOff
            ev.type = (ev.data2 == 0) ? MidiEventType::NoteOff : MidiEventType::NoteOn;
            break;
        case 0xA0:
            ev.type = MidiEventType::PolyPressure;
            break;
        case 0xB0:
            ev.type = MidiEventType::ControlChange;
            break;
        case 0xC0:
            ev.type = MidiEventType::ProgramChange;
            break;
        case 0xD0:
            ev.type = MidiEventType::ChannelPressure;
            break;
        case 0xE0:
            ev.type = MidiEventType::PitchBend;
            break;
        default:
            ev.type = MidiEventType::Unknown;
            break;
    }

    dispatchEvent(ev);
}

void BleMidiParser::parseBlePacket(const uint8_t* data, size_t length) {
    if (!data || length < 2) return;

    // Byte 0: Header Byte
    // Bit 7: 1 (required)
    // Bit 6: 0
    // Bits 5..0: Timestamp High (6 bits)
    const uint8_t header = data[0];
    if (!(header & 0x80)) return; // Invalid header

    timestampHigh6_ = (header & 0x3F);
    currentTimestamp13_ = (timestampHigh6_ << 7);

    // In BLE MIDI, running status does not persist across packet boundaries
    runningStatus_ = 0;
    currentStatus_ = 0;
    pendingCount_ = 0;
    expectedCount_ = 0;
    inSysEx_ = false;
    bleState_ = BleParseState::ExpectTimestamp;

    size_t idx = 1;
    while (idx < length) {
        const uint8_t byte = data[idx++];

        // In message states, realtime may be interleaved without disturbing
        // parsing. In ExpectTimestamp the exact same byte range is timestamp low.
        if (bleState_ != BleParseState::ExpectTimestamp && byte >= 0xF8) {
            MidiEvent ev;
            ev.channel = 0;
            ev.type = MidiEventType::Unknown;
            ev.data1 = byte;
            ev.data2 = 0;
            ev.timestamp13 = currentTimestamp13_;
            ev.rawBytes[0] = byte;
            ev.rawBytes[1] = 0;
            ev.rawBytes[2] = 0;
            dispatchEvent(ev);
            continue; // Real-Time does not modify running status or message parsing state
        }

        // 2. SysEx handling
        if (inSysEx_) {
            if (byte == 0xF7) {
                inSysEx_ = false;
                bleState_ = BleParseState::ExpectTimestamp;
            } else if (byte & 0x80) {
                // Unexpected status / timestamp ends SysEx
                inSysEx_ = false;
                // Re-evaluate byte below
            } else {
                // Ignore SysEx data bytes in MVP
                continue;
            }
        }

        // 3. Parser State Machine
        switch (bleState_) {
            case BleParseState::ExpectTimestamp: {
                if (byte & 0x80) {
                    // Valid Timestamp Low byte (bit 7 is 1, bits 6..0 is timestamp low)
                    const uint16_t timestampLow7 = (byte & 0x7F);
                    currentTimestamp13_ = (timestampHigh6_ << 7) | timestampLow7;
                    bleState_ = BleParseState::ExpectStatusOrData;
                } else {
                    // Non-compliant packet: missing timestamp byte before message
                    // If we have running status, treat as running status data
                    if (runningStatus_ != 0) {
                        currentStatus_ = runningStatus_;
                        const uint8_t type = currentStatus_ & 0xF0;
                        expectedCount_ = (type == 0xC0 || type == 0xD0) ? 1 : 2;
                        pendingBytes_[0] = byte;
                        pendingCount_ = 1;
                        if (pendingCount_ == expectedCount_) {
                            dispatchCurrentMessage(currentStatus_, pendingBytes_, pendingCount_, currentTimestamp13_);
                            bleState_ = BleParseState::ExpectTimestamp;
                        } else {
                            bleState_ = BleParseState::ExpectData;
                        }
                    }
                }
                break;
            }

            case BleParseState::ExpectStatusOrData: {
                if (byte & 0x80) {
                    // Status byte
                    if (byte == 0xF0) {
                        inSysEx_ = true;
                        runningStatus_ = 0;
                    } else if (byte >= 0x80 && byte <= 0xEF) {
                        currentStatus_ = byte;
                        runningStatus_ = byte;
                        const uint8_t type = currentStatus_ & 0xF0;
                        expectedCount_ = (type == 0xC0 || type == 0xD0) ? 1 : 2;
                        pendingCount_ = 0;
                        bleState_ = BleParseState::ExpectData;
                    } else {
                        // System common (0xF1 - 0xF7) - cancel running status
                        runningStatus_ = 0;
                        bleState_ = BleParseState::ExpectTimestamp;
                    }
                } else {
                    // Data byte following timestamp -> RUNNING STATUS!
                    if (runningStatus_ != 0) {
                        currentStatus_ = runningStatus_;
                        const uint8_t type = currentStatus_ & 0xF0;
                        expectedCount_ = (type == 0xC0 || type == 0xD0) ? 1 : 2;
                        pendingBytes_[0] = byte;
                        pendingCount_ = 1;
                        if (pendingCount_ == expectedCount_) {
                            dispatchCurrentMessage(currentStatus_, pendingBytes_, pendingCount_, currentTimestamp13_);
                            bleState_ = BleParseState::ExpectTimestamp;
                        } else {
                            bleState_ = BleParseState::ExpectData;
                        }
                    } else {
                        // Data byte received without active running status: invalid / out-of-sync
                        bleState_ = BleParseState::ExpectTimestamp;
                    }
                }
                break;
            }

            case BleParseState::ExpectData: {
                if (!(byte & 0x80)) {
                    // Valid data byte
                    if (pendingCount_ < 2) {
                        pendingBytes_[pendingCount_++] = byte;
                    }
                    if (pendingCount_ == expectedCount_) {
                        dispatchCurrentMessage(currentStatus_, pendingBytes_, pendingCount_, currentTimestamp13_);
                        // Once a message completes, the BLE-MIDI spec specifies the next message begins with a timestamp byte
                        bleState_ = BleParseState::ExpectTimestamp;
                    }
                } else {
                    // Unexpected byte with MSB=1 while waiting for data!
                    // Packet was truncated or corrupted. Recover gracefully.
                    // Could be a new timestamp byte or status byte:
                    // If followed by data, treat this byte as a new timestamp
                    const uint16_t timestampLow7 = (byte & 0x7F);
                    currentTimestamp13_ = (timestampHigh6_ << 7) | timestampLow7;
                    bleState_ = BleParseState::ExpectStatusOrData;
                }
                break;
            }
        }
    }
}

void BleMidiParser::parseByte(uint8_t byte) {
    // 1. System Real-Time messages (0xF8 - 0xFF)
    if (byte >= 0xF8) {
        MidiEvent ev;
        ev.channel = 0;
        ev.type = MidiEventType::Unknown;
        ev.data1 = byte;
        ev.data2 = 0;
        ev.timestamp13 = 0;
        ev.rawBytes[0] = byte;
        ev.rawBytes[1] = 0;
        ev.rawBytes[2] = 0;
        dispatchEvent(ev);
        return;
    }

    // 2. Status byte
    if (byte & 0x80) {
        if (byte >= 0x80 && byte <= 0xEF) {
            serialRunningStatus_ = byte;
            serialPendingCount_ = 0;
            const uint8_t type = byte & 0xF0;
            serialExpectedCount_ = (type == 0xC0 || type == 0xD0) ? 1 : 2;
        } else {
            serialRunningStatus_ = 0;
            serialPendingCount_ = 0;
            serialExpectedCount_ = 0;
        }
        return;
    }

    // 3. Data byte
    if (serialRunningStatus_ == 0) return;

    if (serialPendingCount_ < 2) {
        serialPendingBytes_[serialPendingCount_++] = byte;
    }

    if (serialPendingCount_ == serialExpectedCount_) {
        dispatchCurrentMessage(serialRunningStatus_, serialPendingBytes_, serialPendingCount_, 0);
        serialPendingCount_ = 0;
    }
}

} // namespace pocketpan::midi
