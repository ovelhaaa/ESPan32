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
    void onConnectionUpdate(uint16_t intervalUnits, uint16_t latency, uint16_t supervisionTimeout);
    void onRssi(int8_t rssi);
    void onDisconnectReason(uint8_t reason);

    uint16_t connectionIntervalUnits() const { return intervalUnits_.load(std::memory_order_acquire); }
    uint16_t connectionLatency() const { return latency_.load(std::memory_order_acquire); }
    uint16_t supervisionTimeout() const { return supervisionTimeout_.load(std::memory_order_acquire); }
    int8_t rssi() const { return rssi_.load(std::memory_order_acquire); }
    uint32_t reconnectCount() const { return reconnectCount_.load(std::memory_order_acquire); }
    uint8_t lastDisconnectReason() const { return lastDisconnectReason_.load(std::memory_order_acquire); }

private:
    std::atomic<bool> connected_{false};
    std::atomic<bool> scanning_{false};
    std::atomic<BleMidiState> state_{BleMidiState::Idle};
    std::string deviceName_;

    midi::SpscMidiQueue<64>* queue_ = nullptr;
    midi::BleMidiParser parser_;
    std::atomic<uint16_t> intervalUnits_{0}, latency_{0}, supervisionTimeout_{0};
    std::atomic<int8_t> rssi_{0};
    std::atomic<uint32_t> reconnectCount_{0};
    std::atomic<uint8_t> lastDisconnectReason_{0};
    std::atomic<bool> hasConnectedBefore_{false};
    std::atomic<bool> disconnectedSinceConnect_{false};

#ifdef ESP_PLATFORM
    static void bleHostTaskEntry(void* param);
    void bleHostLoop();
#endif
};

} // namespace pocketpan::hardware
