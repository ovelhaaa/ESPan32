#include "midi_parser.h"

namespace pocketpan::midi {

void BleMidiParser::dispatchEvent(const MidiEvent& ev) {
    if (callback_) {
        callback_(userData_, ev);
    }
}

void BleMidiParser::parseBlePacket(const uint8_t* data, size_t length) {
    if (!data || length < 2) return;

    // Byte 0: Header Byte
    // Bit 7: 1
    // Bit 6: 0
    // Bits 5..0: Timestamp High (6 bits)
    const uint8_t header = data[0];
    if (!(header & 0x80)) return; // Valid BLE MIDI header must have MSB=1

    const uint16_t timestampHigh6 = (header & 0x3F);

    size_t idx = 1;
    while (idx < length) {
        uint8_t byte = data[idx++];

        // Check for timestamp low byte (MSB is 1)
        // In BLE MIDI, each message or group of messages can be prefixed by a timestamp low byte
        if ((byte & 0x80) && idx < length && !(data[idx] & 0x80)) {
            // It could be a timestamp byte if followed by data or running status,
            // OR it is a timestamp byte if it's not a RealTime MIDI status.
            // In standard BLE MIDI:
            // Timestamp Low: bits 6..0 contain low 7 bits of timestamp
            uint16_t timestampLow7 = (byte & 0x7F);
            currentTimestamp13_ = (timestampHigh6 << 7) | timestampLow7;
            continue;
        }

        if (byte & 0x80) {
            // MIDI Status byte or Timestamp followed by Status
            if (byte >= 0xF8) {
                // System Real-Time message (1 byte)
                continue;
            }

            // If byte is between 0x80 and 0xEF, it's Channel Voice status
            runningStatus_ = byte;
            pendingCount_ = 0;

            const uint8_t typeNibble = byte & 0xF0;
            if (typeNibble == 0xC0 || typeNibble == 0xD0) {
                // Program Change or Channel Pressure: 1 data byte
                expectedCount_ = 1;
            } else {
                // NoteOn, NoteOff, PolyPressure, CC, PitchBend: 2 data bytes
                expectedCount_ = 2;
            }
        } else {
            // Data byte
            if (runningStatus_ == 0) continue;

            pendingBytes_[pendingCount_++] = byte;

            if (pendingCount_ == expectedCount_) {
                MidiEvent ev;
                ev.channel = (runningStatus_ & 0x0F) + 1;
                ev.timestamp13 = currentTimestamp13_;
                ev.data1 = pendingBytes_[0];
                ev.data2 = (expectedCount_ == 2) ? pendingBytes_[1] : 0;
                ev.rawBytes[0] = runningStatus_;
                ev.rawBytes[1] = pendingBytes_[0];
                ev.rawBytes[2] = (expectedCount_ == 2) ? pendingBytes_[1] : 0;

                const uint8_t statusType = runningStatus_ & 0xF0;
                switch (statusType) {
                    case 0x80:
                        ev.type = MidiEventType::NoteOff;
                        break;
                    case 0x90:
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
                pendingCount_ = 0;
            }
        }
    }
}

void BleMidiParser::parseByte(uint8_t byte) {
    if (byte & 0x80) {
        if (byte >= 0xF8) return; // Real-time

        runningStatus_ = byte;
        pendingCount_ = 0;
        const uint8_t typeNibble = byte & 0xF0;
        if (typeNibble == 0xC0 || typeNibble == 0xD0) {
            expectedCount_ = 1;
        } else {
            expectedCount_ = 2;
        }
    } else {
        if (runningStatus_ == 0) return;

        pendingBytes_[pendingCount_++] = byte;
        if (pendingCount_ == expectedCount_) {
            MidiEvent ev;
            ev.channel = (runningStatus_ & 0x0F) + 1;
            ev.timestamp13 = currentTimestamp13_;
            ev.data1 = pendingBytes_[0];
            ev.data2 = (expectedCount_ == 2) ? pendingBytes_[1] : 0;
            ev.rawBytes[0] = runningStatus_;
            ev.rawBytes[1] = pendingBytes_[0];
            ev.rawBytes[2] = (expectedCount_ == 2) ? pendingBytes_[1] : 0;

            const uint8_t statusType = runningStatus_ & 0xF0;
            switch (statusType) {
                case 0x80: ev.type = MidiEventType::NoteOff; break;
                case 0x90: ev.type = (ev.data2 == 0) ? MidiEventType::NoteOff : MidiEventType::NoteOn; break;
                case 0xA0: ev.type = MidiEventType::PolyPressure; break;
                case 0xB0: ev.type = MidiEventType::ControlChange; break;
                case 0xC0: ev.type = MidiEventType::ProgramChange; break;
                case 0xD0: ev.type = MidiEventType::ChannelPressure; break;
                case 0xE0: ev.type = MidiEventType::PitchBend; break;
                default:   ev.type = MidiEventType::Unknown; break;
            }

            dispatchEvent(ev);
            pendingCount_ = 0;
        }
    }
}

} // namespace pocketpan::midi
