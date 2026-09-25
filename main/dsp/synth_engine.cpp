#include "synth_engine.h"
#include "pan_calibration.h"
#include "../midi/midi_mapping.h"
#include <algorithm>
#include <cmath>

namespace pocketpan::dsp {

void SynthEngine::init(float sampleRate) {
    sampleRate_ = sampleRate;
    allocator_.init(sampleRate_);
    bodyConfig_=kPanBodyConfig; body_.init(sampleRate_); body_.setConfig(bodyConfig_.body);
    limiter_.init(sampleRate_);
    // Fast attenuation catches simultaneous-note attacks; slower recovery
    // avoids loudness steps while modal voices naturally become inactive.
    polyHeadroomAttackCoefficient_ = std::exp(-1.0f / (sampleRate_ * 0.003f));
    polyHeadroomReleaseCoefficient_ = std::exp(-1.0f / (sampleRate_ * 0.050f));
    reset();
}

void SynthEngine::reset() {
    allocator_.reset();
    limiter_.reset();
    polyHeadroomGain_ = 1.0f;
    preLimiterPeak_ = postLimiterPeak_ = 0.0f;
    hardClampCount_ = 0;
    body_.reset(); bodyPeak_=bodySumSquares_=0.0f; bodySamples_=0; allocator_.resetSympatheticDiagnostics();
    std::fill(monoBuffer_, monoBuffer_ + kMaxBlockFrames, 0.0f);
}

void SynthEngine::resetDiagnostics() {
    limiter_.resetDiagnostics();
    preLimiterPeak_ = postLimiterPeak_ = 0.0f;
    hardClampCount_ = 0;
    bodyPeak_=bodySumSquares_=0.0f; bodySamples_=0; allocator_.resetSympatheticDiagnostics();
}

void SynthEngine::setMasterVolume(float vol) {
    masterGain_ = std::clamp(vol, 0.0f, 1.0f);
}

void SynthEngine::setSympatheticEnabled(bool enabled) {
    if (bodyConfig_.sympathetic.enabled == enabled) return;
    bodyConfig_.sympathetic.enabled = enabled;
    allocator_.resetSympatheticState();
}

uint32_t SynthEngine::getModalInternalSaturationCount() const {
    uint32_t total = 0;
    for (size_t i = 0; i < kMaxVoices; ++i) total += allocator_.getVoice(i).getInternalSaturationCount();
    return total;
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
    if (bodyConfig_.sympathetic.enabled) allocator_.renderBlock(monoBuffer_, frames, bodyConfig_.sympathetic);
    else allocator_.renderBlock(monoBuffer_, frames);

    // Smooth count-based polyphonic headroom: 1=0 dB, 2=-1.5 dB,
    // 4=-3 dB, 8=-5 dB. This preserves per-voice modal gains.
    const float voices = static_cast<float>(allocator_.getActiveVoiceCount());
    const float targetDb = voices <= 1.0f ? 0.0f : -1.5f * std::log2(voices);
    const float targetHeadroom = std::pow(10.0f, std::max(targetDb, -5.0f) / 20.0f);

    // 2. Mixdown, headroom, lookahead gain limiting, and safe conversion.
    for (size_t i = 0; i < frames; ++i) {
        const float coefficient = targetHeadroom < polyHeadroomGain_
            ? polyHeadroomAttackCoefficient_ : polyHeadroomReleaseCoefficient_;
        polyHeadroomGain_ = targetHeadroom + coefficient * (polyHeadroomGain_ - targetHeadroom);
        const float bodySample=body_.processSample(monoBuffer_[i]);
        bodyPeak_=std::max(bodyPeak_,std::abs(bodySample)); bodySumSquares_+=bodySample*bodySample; ++bodySamples_;
        float sample = (monoBuffer_[i] + bodySample) * masterGain_ * polyHeadroomGain_;

        // NaN / Inf safety guard
        if (std::isnan(sample) || std::isinf(sample)) {
            sample = 0.0f;
        }

        preLimiterPeak_ = std::max(preLimiterPeak_, std::abs(sample));
        sample = limiter_.processSample(sample);
        postLimiterPeak_ = std::max(postLimiterPeak_, std::abs(sample));

        // Clamp to [-1.0, 1.0] and convert to 32-bit full-scale integer
        const float clamped = std::clamp(sample, -1.0f, 1.0f);
        if (clamped != sample) ++hardClampCount_;
        const int32_t sample32 = static_cast<int32_t>(clamped * 2147483647.0f);

        // Interleaved stereo duplicate
        outInterleaved[i * 2]     = sample32;
        outInterleaved[i * 2 + 1] = sample32;
    }
}

} // namespace pocketpan::dsp
