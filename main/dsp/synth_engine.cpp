#include "synth_engine.h"
#include "dsp_profile.h"
#include "pan_calibration.h"
#include "../midi/midi_mapping.h"
#include <algorithm>
#include <cmath>
#include <cassert>

namespace pocketpan::dsp {

void SynthEngine::init(float sampleRate) {
    sampleRate_ = sampleRate;
    allocator_.init(sampleRate_);
#if POCKETPAN_PREPARED_NOTE_CACHE
    // Model changes are applied by the audio callback.  Build all three fixed
    // tables now, before the callback/I2S transport exists, so no coefficient
    // table generation or heap work can occur at a realtime boundary.
    assert(verifyPreparedNoteCanaries());
    allocator_.preparePreparedNoteTable(getInstrumentModelConfig(InstrumentModel::Pan), panPreparedNotes_);
    assert(verifyPreparedNoteCanaries());
    allocator_.preparePreparedNoteTable(getInstrumentModelConfig(InstrumentModel::Bell), bellPreparedNotes_);
    assert(verifyPreparedNoteCanaries());
    allocator_.preparePreparedNoteTable(getInstrumentModelConfig(InstrumentModel::Tongue), tonguePreparedNotes_);
    assert(verifyPreparedNoteCanaries());
#endif
    model_ = InstrumentModel::Pan;
    modelConfig_ = getInstrumentModelConfig(model_);
    allocator_.setModelConfig(modelConfig_);
#if POCKETPAN_SYMPATHETIC_COEFF_CACHE
    allocator_.setSympatheticConfig(modelConfig_.sympathetic);
#endif
#if POCKETPAN_PREPARED_NOTE_CACHE
    allocator_.setPreparedNoteTable(&panPreparedNotes_);
#endif
    body_.init(sampleRate_); body_.setConfig(modelConfig_.body);
    // Cheap one-pole reference for the host-only transient candidate (~1 ms).
    bodyTransientCoefficient_ = std::exp(-1.0f / (sampleRate_ * 0.001f));
    limiter_.init(sampleRate_);
    // Fast attenuation catches simultaneous-note attacks; slower recovery
    // avoids loudness steps while modal voices naturally become inactive.
    polyHeadroomAttackCoefficient_ = std::exp(-1.0f / (sampleRate_ * 0.003f));
    polyHeadroomReleaseCoefficient_ = std::exp(-1.0f / (sampleRate_ * 0.050f));
#if POCKETPAN_HEADROOM_TABLE
    // Same expression as the historical per-block computation, evaluated once.
    for (size_t v = 0; v <= kMaxVoices; ++v) {
        const float voices = static_cast<float>(v);
        const float targetDb = voices <= 1.0f ? 0.0f : -1.5f * std::log2(voices);
        headroomTarget_[v] = std::pow(10.0f, std::max(targetDb, -5.0f) / 20.0f);
    }
#endif
    reset();
}

void SynthEngine::reset() {
    allocator_.reset();
    limiter_.reset();
    polyHeadroomGain_ = 1.0f;
    preLimiterPeak_ = postLimiterPeak_ = 0.0f;
    hardClampCount_ = 0;
    body_.reset(); bodyPeak_=bodySumSquares_=0.0f; bodySamples_=0; allocator_.resetSympatheticDiagnostics();
    bodyTransientState_ = 0.0f;
    std::fill(monoBuffer_, monoBuffer_ + kMaxBlockFrames, 0.0f);
}

void SynthEngine::setInstrumentModel(InstrumentModel model) {
    // Reinitialize the small, fixed DSP state exactly as boot does. This is
    // intentionally stronger than killing voices: no exciter, modal, body,
    // sympathetic, or limiter state crosses a model boundary.
    model_ = model;
    modelConfig_ = getInstrumentModelConfig(model_);
    allocator_.init(sampleRate_);
    allocator_.setModelConfig(modelConfig_);
#if POCKETPAN_SYMPATHETIC_COEFF_CACHE
    allocator_.setSympatheticConfig(modelConfig_.sympathetic);
#endif
#if POCKETPAN_PREPARED_NOTE_CACHE
    assert(verifyPreparedNoteCanaries());
    const PreparedNoteTable* table = &panPreparedNotes_;
    if (model_ == InstrumentModel::Bell) {
        table = &bellPreparedNotes_;
    } else if (model_ == InstrumentModel::Tongue) {
        table = &tonguePreparedNotes_;
    }
    allocator_.setPreparedNoteTable(table);
#endif
    body_.init(sampleRate_);
    body_.setConfig(modelConfig_.body);
    limiter_.init(sampleRate_);
    bodyStrategy_ = modelConfig_.bodyStrategy;
    reset();
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
    if (modelConfig_.sympathetic.enabled == enabled) return;
    modelConfig_.sympathetic.enabled = enabled;
#if POCKETPAN_SYMPATHETIC_COEFF_CACHE
    allocator_.setSympatheticConfig(modelConfig_.sympathetic);
#endif
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
                float freqHz;
                float vel;
                {
                    // Keep MIDI mapping/normalization separate from allocator
                    // and trigger work in the profile breakdown.
                    DSP_PROFILE_SCOPE(MidiDispatch);
                    freqHz = midi::MidiMapping::noteToHz(event.data1);
                    vel = midi::MidiMapping::toNormalizedFloat(event.data2);
                }
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

DSP_IRAM_RENDERBLOCK void SynthEngine::renderBlock(int32_t* outInterleaved, size_t frames) {
    if (!outInterleaved || frames == 0) return;

    // 1. Synthesize 8-voice polyphony into mono buffer
    const bool useStrikeBus = modelConfig_.body.enabled && bodyStrategy_ == BodyExcitationStrategy::StrikeBus;
    { DSP_PROFILE_SCOPE(Allocator);
    if (useStrikeBus) allocator_.renderBlockWithStrikeBus(monoBuffer_, strikeBuffer_, frames, modelConfig_.sympathetic);
    else if (modelConfig_.sympathetic.enabled) allocator_.renderBlock(monoBuffer_, frames, modelConfig_.sympathetic);
    else allocator_.renderBlock(monoBuffer_, frames);

    }
    // Smooth count-based polyphonic headroom: 1=0 dB, 2=-1.5 dB,
    // 4=-3 dB, 8=-5 dB. This preserves per-voice modal gains.
#if POCKETPAN_HEADROOM_TABLE
    const float targetHeadroom = headroomTarget_[allocator_.getActiveVoiceCount()];
#else
    const float voices = static_cast<float>(allocator_.getActiveVoiceCount());
    const float targetDb = voices <= 1.0f ? 0.0f : -1.5f * std::log2(voices);
    const float targetHeadroom = std::pow(10.0f, std::max(targetDb, -5.0f) / 20.0f);
#endif

    // 2. Mixdown, headroom, lookahead gain limiting, and safe conversion.
    for (size_t i = 0; i < frames; ++i) {
        float sample;
        { DSP_PROFILE_SCOPE(Mix);
        const float coefficient = targetHeadroom < polyHeadroomGain_
            ? polyHeadroomAttackCoefficient_ : polyHeadroomReleaseCoefficient_;
        polyHeadroomGain_ = targetHeadroom + coefficient * (polyHeadroomGain_ - targetHeadroom);
        float bodyExcitation = monoBuffer_[i];
        if (bodyStrategy_ == BodyExcitationStrategy::Transient) {
            bodyTransientState_ = (1.0f-bodyTransientCoefficient_)*monoBuffer_[i] + bodyTransientCoefficient_*bodyTransientState_;
            bodyExcitation = monoBuffer_[i] - bodyTransientState_;
        } else if (useStrikeBus) {
            bodyExcitation = strikeBuffer_[i] * modelConfig_.strikeBusGain;
        }
        float bodySample;
        { DSP_PROFILE_SCOPE(Body); bodySample=body_.processSample(bodyExcitation); }
        bodyPeak_=std::max(bodyPeak_,std::abs(bodySample)); bodySumSquares_+=bodySample*bodySample; ++bodySamples_;
        sample = (monoBuffer_[i] + bodySample) * masterGain_ * polyHeadroomGain_;

        }
        { DSP_PROFILE_SCOPE(Limiter);
        // NaN / Inf safety guard
        if (std::isnan(sample) || std::isinf(sample)) {
            sample = 0.0f;
        }

        preLimiterPeak_ = std::max(preLimiterPeak_, std::abs(sample));
        sample = limiter_.processSample(sample);
        postLimiterPeak_ = std::max(postLimiterPeak_, std::abs(sample));

        }
        { DSP_PROFILE_SCOPE(Pcm);
        // Clamp to [-1.0, 1.0] and convert to 32-bit full-scale integer
        const float clamped = std::clamp(sample, -1.0f, 1.0f);
        if (clamped != sample) ++hardClampCount_;
        const int32_t sample32 = static_cast<int32_t>(clamped * 2147483647.0f);

        // Interleaved stereo duplicate
        outInterleaved[i * 2]     = sample32;
        outInterleaved[i * 2 + 1] = sample32;
        }
    }
}

} // namespace pocketpan::dsp
