#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include "../midi/midi_transport.h"
#include "../midi/midi_parser.h"
#include "../midi/midi_event.h"

namespace pocketpan::hardware {

enum class BleMidiState : uint8_t {
    Idle, Scanning, Connecting, Connected, DiscoveringService,
    DiscoveringCharacteristic, DiscoveringCccd, Subscribing, Ready, Error
};

class BleMidi : public midi::MidiTransport {
public:
    BleMidi();
    ~BleMidi() override;

    bool begin() override;
    void poll() override;
    bool isConnected() const override { return connected_.load(std::memory_order_relaxed); }
    bool isMidiReady() const { return state() == BleMidiState::Ready; }
    BleMidiState state() const { return state_.load(std::memory_order_acquire); }
    void setState(BleMidiState state) { state_.store(state, std::memory_order_release); }
    void failAndRecover(const char* reason);

    void setQueue(midi::SpscMidiQueue<64>* queue) { queue_ = queue; }
    const std::string& getConnectedDeviceName() const { return deviceName_; }

    // Internal callbacks from NimBLE events
    void onMidiDataReceived(const uint8_t* data, size_t len);
    void onConnected(uint16_t connHandle);
    void onDisconnected();

private:
    std::atomic<bool> connected_{false};
    std::atomic<bool> scanning_{false};
    std::atomic<BleMidiState> state_{BleMidiState::Idle};
    std::string deviceName_;

    midi::SpscMidiQueue<64>* queue_ = nullptr;
    midi::BleMidiParser parser_;

#ifdef ESP_PLATFORM
    static void bleHostTaskEntry(void* param);
    void bleHostLoop();
#endif
};

} // namespace pocketpan::hardware
