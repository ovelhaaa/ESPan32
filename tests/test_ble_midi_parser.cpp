#include <cassert>
#include <cstdio>
#include <vector>
#include "midi/midi_parser.h"

using namespace pocketpan::midi;

namespace {

struct ReceivedEvent {
    MidiEventType type;
    uint8_t channel;
    uint8_t data1;
    uint8_t data2;
    uint16_t timestamp13;
    uint8_t rawBytes[3];
};

std::vector<ReceivedEvent> gEvents;

void testCallback(void* userData, const MidiEvent& event) {
    ReceivedEvent r;
    r.type = event.type;
    r.channel = event.channel;
    r.data1 = event.data1;
    r.data2 = event.data2;
    r.timestamp13 = event.timestamp13;
    r.rawBytes[0] = event.rawBytes[0];
    r.rawBytes[1] = event.rawBytes[1];
    r.rawBytes[2] = event.rawBytes[2];
    gEvents.push_back(r);
}

void testSingleNoteOn() {
    printf("[TEST] BLE MIDI Parser: Single Note On...\n");
    gEvents.clear();
    BleMidiParser parser;
    parser.setCallback(testCallback, nullptr);

    // Header: bit 7=1, bits 5..0 = 0b001010 (10) -> timestampHigh6 = 10
    // Timestamp Low: bit 7=1, bits 6..0 = 0b0100100 (36) -> timestampLow7 = 36
    // Expected timestamp13 = (10 << 7) | 36 = 1280 + 36 = 1316
    // Status: 0x90 (NoteOn, Ch 1)
    // Note: 62 (D3)
    // Velocity: 100
    const uint8_t packet[] = { 0x80 | 10, 0x80 | 36, 0x90, 62, 100 };
    parser.parseBlePacket(packet, sizeof(packet));

    assert(gEvents.size() == 1);
    assert(gEvents[0].type == MidiEventType::NoteOn);
    assert(gEvents[0].channel == 1);
    assert(gEvents[0].data1 == 62);
    assert(gEvents[0].data2 == 100);
    assert(gEvents[0].timestamp13 == 1316);
    printf("  PASSED: NoteOn D3 vel 100 with 13-bit timestamp 1316\n");
}

void testNoteOnZeroVelocityAsNoteOff() {
    printf("[TEST] BLE MIDI Parser: NoteOn with velocity 0 as NoteOff...\n");
    gEvents.clear();
    BleMidiParser parser;
    parser.setCallback(testCallback, nullptr);

    const uint8_t packet[] = { 0x80 | 5, 0x80 | 20, 0x90, 62, 0 };
    parser.parseBlePacket(packet, sizeof(packet));

    assert(gEvents.size() == 1);
    assert(gEvents[0].type == MidiEventType::NoteOff);
    assert(gEvents[0].data1 == 62);
    assert(gEvents[0].data2 == 0);
    printf("  PASSED: Velocity 0 converted to NoteOff\n");
}

void testNoteOnAndNoteOffInSamePacket() {
    printf("[TEST] BLE MIDI Parser: NoteOn + NoteOff in same packet...\n");
    gEvents.clear();
    BleMidiParser parser;
    parser.setCallback(testCallback, nullptr);

    // Header (high=2)
    // TsLow (10) -> ts=266, NoteOn 62, 90
    // TsLow (15) -> ts=271, NoteOff 62, 0
    const uint8_t packet[] = {
        0x80 | 2,
        0x80 | 10, 0x90, 62, 90,
        0x80 | 15, 0x80, 62, 0
    };
    parser.parseBlePacket(packet, sizeof(packet));

    assert(gEvents.size() == 2);
    assert(gEvents[0].type == MidiEventType::NoteOn);
    assert(gEvents[0].timestamp13 == ((2 << 7) | 10));
    assert(gEvents[1].type == MidiEventType::NoteOff);
    assert(gEvents[1].timestamp13 == ((2 << 7) | 15));
    printf("  PASSED: Both messages dispatched with respective timestamps\n");
}

void testPolyPressureAndChannelPressure() {
    printf("[TEST] BLE MIDI Parser: Poly Pressure & Channel Pressure...\n");
    gEvents.clear();
    BleMidiParser parser;
    parser.setCallback(testCallback, nullptr);

    // Header
    // Poly Pressure: 0xA0 (ch 1), note 62, pressure 85
    // Channel Pressure: 0xD0 (ch 1), pressure 110
    const uint8_t packet[] = {
        0x80 | 1,
        0x80 | 5, 0xA0, 62, 85,
        0x80 | 8, 0xD0, 110
    };
    parser.parseBlePacket(packet, sizeof(packet));

    assert(gEvents.size() == 2);
    assert(gEvents[0].type == MidiEventType::PolyPressure);
    assert(gEvents[0].channel == 1);
    assert(gEvents[0].data1 == 62);
    assert(gEvents[0].data2 == 85);

    assert(gEvents[1].type == MidiEventType::ChannelPressure);
    assert(gEvents[1].channel == 1);
    assert(gEvents[1].data1 == 110);
    printf("  PASSED: PolyPressure and ChannelPressure distinguished and parsed\n");
}

void testRunningStatus() {
    printf("[TEST] BLE MIDI Parser: Running Status...\n");
    gEvents.clear();
    BleMidiParser parser;
    parser.setCallback(testCallback, nullptr);

    // In BLE MIDI, successive messages of same status can omit the status byte,
    // but each message is still preceded by a timestamp byte!
    // NoteOn 62 100, then (ts) NoteOn 65 95 (omitting status byte 0x90)
    const uint8_t packet[] = {
        0x80 | 4,
        0x80 | 1, 0x90, 62, 100,
        0x80 | 5, 65, 95
    };
    parser.parseBlePacket(packet, sizeof(packet));

    assert(gEvents.size() == 2);
    assert(gEvents[0].type == MidiEventType::NoteOn);
    assert(gEvents[0].data1 == 62);
    assert(gEvents[0].data2 == 100);

    assert(gEvents[1].type == MidiEventType::NoteOn);
    assert(gEvents[1].data1 == 65);
    assert(gEvents[1].data2 == 95);
    assert(gEvents[1].timestamp13 == ((4 << 7) | 5));
    printf("  PASSED: Running status decoded under BLE timestamp prefixes\n");
}

void testInterleavedSystemRealTime() {
    printf("[TEST] BLE MIDI Parser: Interleaved System Real-Time (0xF8)...\n");
    gEvents.clear();
    BleMidiParser parser;
    parser.setCallback(testCallback, nullptr);

    // NoteOn with 0xF8 (clock) interleaved between data bytes
    const uint8_t packet[] = {
        0x80 | 1,
        0x80 | 2, 0x90, 62, 0xF8, 100
    };
    parser.parseBlePacket(packet, sizeof(packet));

    assert(gEvents.size() == 2);
    // First dispatched: Real-time clock 0xF8
    assert(gEvents[0].rawBytes[0] == 0xF8 || gEvents[0].data1 == 0xF8);
    // Second dispatched: NoteOn 62, 100
    assert(gEvents[1].type == MidiEventType::NoteOn);
    assert(gEvents[1].data1 == 62);
    assert(gEvents[1].data2 == 100);
    printf("  PASSED: 0xF8 clock dispatched without interrupting NoteOn\n");
}

void testTruncatedAndInvalidPackets() {
    printf("[TEST] BLE MIDI Parser: Truncated and Invalid Packet Recovery...\n");
    gEvents.clear();
    BleMidiParser parser;
    parser.setCallback(testCallback, nullptr);

    // 1. Invalid header (MSB not 1)
    const uint8_t invalidHeader[] = { 0x40, 0x80, 0x90, 60, 100 };
    parser.parseBlePacket(invalidHeader, sizeof(invalidHeader));
    assert(gEvents.empty());

    // 2. Truncated packet (missing velocity)
    const uint8_t truncated[] = { 0x80, 0x80, 0x90, 60 };
    parser.parseBlePacket(truncated, sizeof(truncated));
    assert(gEvents.empty());

    // 3. Immediately follow with a valid packet to verify state was not corrupted
    const uint8_t validPacket[] = { 0x80 | 3, 0x80 | 12, 0x90, 72, 88 };
    parser.parseBlePacket(validPacket, sizeof(validPacket));
    assert(gEvents.size() == 1);
    assert(gEvents[0].type == MidiEventType::NoteOn);
    assert(gEvents[0].data1 == 72);
    assert(gEvents[0].data2 == 88);
    printf("  PASSED: Clean recovery from invalid/truncated packets\n");
}

} // namespace

int main() {
    printf("=========================================\n");
    printf("  Pocket Pan: BLE MIDI Parser Test Suite \n");
    printf("=========================================\n");

    testSingleNoteOn();
    testNoteOnZeroVelocityAsNoteOff();
    testNoteOnAndNoteOffInSamePacket();
    testPolyPressureAndChannelPressure();
    testRunningStatus();
    testInterleavedSystemRealTime();
    testTruncatedAndInvalidPackets();

    printf("\nAll BLE MIDI Parser Tests Passed Successfully!\n");
    return 0;
}
