#pragma once

#include <cstdint>
#include <cstddef>
#include "voice_allocator.h"
#include "peak_limiter.h"
#include "../midi/midi_event.h"

namespace pocketpan::dsp {

constexpr size_t kMaxBlockFrames = 128;

class SynthEngine {
public:
    SynthEngine() = default;

    void init(float sampleRate);
    void reset();

    // Used when entering/leaving diagnostic playback so MIDI cannot leave a
    // resonator ringing behind a tone source.
    void killAllVoices() { reset(); }

    // Process MIDI event on audio thread (NoteOn, NoteOff, Pressure, CC)
    void handleMidiEvent(const midi::MidiEvent& event);

    // Render an interleaved stereo audio block directly to 32-bit I2S format
    void renderBlock(int32_t* outInterleaved, size_t frames);

    VoiceAllocator& getVoiceAllocator() { return allocator_; }
    const VoiceAllocator& getVoiceAllocator() const { return allocator_; }

    void setMasterVolume(float vol);
    // Compatibility names retained for host reports; this is limiter activity,
    // not nonlinear clipping.
    uint32_t getSoftClipCount() const { return limiter_.getActiveSampleCount(); }
    void resetSoftClipCount() { limiter_.reset(); }
    float getPreLimiterPeak() const { return preLimiterPeak_; }
    float getPostLimiterPeak() const { return postLimiterPeak_; }
    float getCurrentGainReductionDb() const { return limiter_.getCurrentGainReductionDb(); }
    float getMaxGainReductionDb() const { return limiter_.getMaxGainReductionDb(); }
    uint32_t getLimiterActiveSamples() const { return limiter_.getActiveSampleCount(); }
    uint32_t getHardClampCount() const { return hardClampCount_; }
    uint32_t getModalInternalSaturationCount() const;
    // Host qualification only; normal firmware uses the PAN model default.
    void setInternalSafetySaturation(bool enabled) { allocator_.setInternalSafetySaturation(enabled); }

private:
    float sampleRate_ = 48000.0f;
    float masterGain_ = 0.85f; // Headroom protection
    float polyHeadroomGain_ = 1.0f;
    float polyHeadroomRelease_ = 0.0f;
    float preLimiterPeak_ = 0.0f;
    float postLimiterPeak_ = 0.0f;
    uint32_t hardClampCount_ = 0;
    PeakLimiter limiter_;

    VoiceAllocator allocator_;

    // Internal mono voice mix buffer
    float monoBuffer_[kMaxBlockFrames];
};

} // namespace pocketpan::dsp
