#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>

namespace pocketpan::midi {

enum class MidiEventType : uint8_t {
    None = 0,
    NoteOn,
    NoteOff,
    PolyPressure,
    ChannelPressure,
    ControlChange,
    PitchBend,
    ProgramChange,
    Unknown
};

struct MidiEvent {
    MidiEventType type = MidiEventType::None;
    uint8_t channel = 0;
    uint8_t data1 = 0;   // Note number or CC number
    uint8_t data2 = 0;   // Velocity, pressure, or CC value
    uint16_t timestamp13 = 0; // 13-bit BLE MIDI timestamp (preserved per specification)
    uint8_t rawBytes[3] = {0, 0, 0};
};

// Lock-Free Single-Producer Single-Consumer (SPSC) Queue for real-time audio threads
template <size_t Capacity = 64>
class SpscMidiQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
public:
    SpscMidiQueue() : head_(0), tail_(0) {}

    // Producer thread (Core 1: BLE/UI)
    bool push(const MidiEvent& event) {
        const size_t currentTail = tail_.load(std::memory_order_relaxed);
        const size_t currentHead = head_.load(std::memory_order_acquire);

        const size_t currentSize = currentTail - currentHead;
        if (currentSize >= Capacity) {
            drops_.fetch_add(1, std::memory_order_relaxed);
            return false; // Queue full, drop event to avoid blocking
        }

        buffer_[currentTail & (Capacity - 1)] = event;
        tail_.store(currentTail + 1, std::memory_order_release);
        pushCount_.fetch_add(1, std::memory_order_relaxed);

        size_t newSize = currentSize + 1;
        size_t currentHwm = highWaterMark_.load(std::memory_order_relaxed);
        while (newSize > currentHwm && !highWaterMark_.compare_exchange_weak(currentHwm, newSize, std::memory_order_relaxed)) {}

        return true;
    }

    // Consumer thread (Core 0: Audio render callback)
    bool pop(MidiEvent& event) {
        const size_t currentHead = head_.load(std::memory_order_relaxed);
        const size_t currentTail = tail_.load(std::memory_order_acquire);

        if (currentHead == currentTail) {
            return false; // Queue empty
        }

        event = buffer_[currentHead & (Capacity - 1)];
        head_.store(currentHead + 1, std::memory_order_release);
        popCount_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    size_t size() const {
        const size_t currentHead = head_.load(std::memory_order_relaxed);
        const size_t currentTail = tail_.load(std::memory_order_relaxed);
        return (currentTail >= currentHead) ? (currentTail - currentHead) : 0;
    }

    bool empty() const {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }

    uint32_t getPushCount() const { return pushCount_.load(std::memory_order_relaxed); }
    uint32_t getPopCount() const { return popCount_.load(std::memory_order_relaxed); }
    uint32_t getDrops() const { return drops_.load(std::memory_order_relaxed); }
    uint32_t getHighWaterMark() const { return static_cast<uint32_t>(highWaterMark_.load(std::memory_order_relaxed)); }

private:
    MidiEvent buffer_[Capacity];
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};
    std::atomic<uint32_t> pushCount_{0};
    std::atomic<uint32_t> popCount_{0};
    std::atomic<uint32_t> drops_{0};
    std::atomic<size_t> highWaterMark_{0};
};

} // namespace pocketpan::midi
