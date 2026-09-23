#include "synth_engine.h"
#include "../midi/midi_mapping.h"
#include <algorithm>
#include <cmath>

namespace pocketpan::dsp {

namespace {
// Safety soft-clipper: 100% linear (0.000% THD) below threshold (0.85),
// smoothly transitioning into a soft saturation knee only if exceeding headroom.
inline float safetySoftClip(float x, bool& clipped) {
    const float kThreshold = 0.85f;
    if (std::abs(x) <= kThreshold) {
        clipped = false;
        return x;
    }
    clipped = true;
    if (x > 0.0f) {
        const float excess = x - kThreshold;
        return kThreshold + (1.0f - kThreshold) * std::tanh(excess / (1.0f - kThreshold));
    } else {
        const float excess = -x - kThreshold;
        return -(kThreshold + (1.0f - kThreshold) * std::tanh(excess / (1.0f - kThreshold)));
    }
}
}

void SynthEngine::init(float sampleRate) {
    sampleRate_ = sampleRate;
    allocator_.init(sampleRate_);
    reset();
}

void SynthEngine::reset() {
    allocator_.reset();
    softClipCount_ = 0;
    std::fill(monoBuffer_, monoBuffer_ + kMaxBlockFrames, 0.0f);
}

void SynthEngine::setMasterVolume(float vol) {
    masterGain_ = std::clamp(vol, 0.0f, 1.0f);
}

void SynthEngine::handleMidiEvent(const midi::MidiEvent& event) {
    switch (event.type) {
        case midi::MidiEventType::NoteOn: {
            if (event.data2 == 0) {
                // Velocity 0 is standard MIDI Note Off
                allocator_.noteOff(event.data1);
            } else {
                const float freqHz = midi::MidiMapping::noteToHz(event.data1);
                const float vel = midi::MidiMapping::toNormalizedFloat(event.data2);
                allocator_.noteOn(event.data1, vel, freqHz);
            }
            break;
        }

        case midi::MidiEventType::NoteOff: {
            allocator_.noteOff(event.data1);
            break;
        }

        case midi::MidiEventType::PolyPressure: {
            const float press = midi::MidiMapping::toNormalizedFloat(event.data2);
            allocator_.setPolyPressure(event.data1, press);
            break;
        }

        case midi::MidiEventType::ChannelPressure: {
            const float press = midi::MidiMapping::toNormalizedFloat(event.data1);
            allocator_.setChannelPressure(press);
            break;
        }

        default:
            break;
    }
}

void SynthEngine::renderBlock(int32_t* outInterleaved, size_t frames) {
    if (!outInterleaved || frames == 0) return;

    // 1. Synthesize 8-voice polyphony into mono buffer
    allocator_.renderBlock(monoBuffer_, frames);

    // 2. Mixdown, headroom, soft-limiting, and safe 32-bit conversion
    for (size_t i = 0; i < frames; ++i) {
        float sample = monoBuffer_[i] * masterGain_;

        // NaN / Inf safety guard
        if (std::isnan(sample) || std::isinf(sample)) {
            sample = 0.0f;
        }

        // Output safety soft limiting
        bool clipped = false;
        sample = safetySoftClip(sample, clipped);
        if (clipped) {
            softClipCount_++;
        }

        // Clamp to [-1.0, 1.0] and convert to 32-bit full-scale integer
        const float clamped = std::clamp(sample, -1.0f, 1.0f);
        const int32_t sample32 = static_cast<int32_t>(clamped * 2147483647.0f);

        // Interleaved stereo duplicate
        outInterleaved[i * 2]     = sample32;
        outInterleaved[i * 2 + 1] = sample32;
    }
}

} // namespace pocketpan::dsp
