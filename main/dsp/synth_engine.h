#pragma once

#include <cstdint>
#include <cstddef>
#include "voice_allocator.h"
#include "../midi/midi_event.h"

namespace pocketpan::dsp {

constexpr size_t kMaxBlockFrames = 128;

class SynthEngine {
public:
    SynthEngine() = default;

    void init(float sampleRate);
    void reset();

    // Process MIDI event on audio thread (NoteOn, NoteOff, Pressure, CC)
    void handleMidiEvent(const midi::MidiEvent& event);

    // Render an interleaved stereo audio block directly to 32-bit I2S format
    void renderBlock(int32_t* outInterleaved, size_t frames);

    VoiceAllocator& getVoiceAllocator() { return allocator_; }
    const VoiceAllocator& getVoiceAllocator() const { return allocator_; }

    void setMasterVolume(float vol);

private:
    float sampleRate_ = 48000.0f;
    float masterGain_ = 0.85f; // Headroom protection

    VoiceAllocator allocator_;

    // Internal mono voice mix buffer
    float monoBuffer_[kMaxBlockFrames];
};

} // namespace pocketpan::dsp
