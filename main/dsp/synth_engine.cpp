#include "synth_engine.h"
#include "dsp_profile.h"
#include "pan_calibration.h"
#include "../midi/midi_mapping.h"
#include <algorithm>
#include <cmath>
#include <cassert>

namespace pocketpan::dsp {
namespace {
float vibraphoneMidiHz[128];
}

void SynthEngine::init(float sampleRate) {
    sampleRate_ = sampleRate;
    mbiraBuzz_.init(sampleRate_);
    mbiraDcPole_ = std::exp(-6.283185307f * 20.0f / sampleRate_);
    (void)velocityPow115Table();
    (void)vibraphoneStrikePowTable();
    (void)mbiraStrikePowTable();
#if POCKETPAN_PACKED_NOTE_CACHE
    prepareCanonicalStrikePow(kPanExciterConfig.velocityKnee,kPanExciterConfig.velocityKneeSlope);
#endif
    for (unsigned note=0; note<128; ++note)
        vibraphoneMidiHz[note]=midi::MidiMapping::noteToHz(uint8_t(note));
    allocator_.init(sampleRate_);
#if POCKETPAN_PREPARED_NOTE_CACHE
    // Model changes are applied by the audio callback.  Build all nine fixed
    // tables now, before the callback/I2S transport exists, so no coefficient
    // table generation or heap work can occur at a realtime boundary.
    assert(verifyPreparedNoteCanaries());
    allocator_.preparePreparedNoteTable(getInstrumentModelConfig(InstrumentModel::Pan), panPreparedNotes_);
    assert(verifyPreparedNoteCanaries());
    allocator_.preparePreparedNoteTable(getInstrumentModelConfig(InstrumentModel::Bell), bellPreparedNotes_);
    assert(verifyPreparedNoteCanaries());
    allocator_.preparePreparedNoteTable(getInstrumentModelConfig(InstrumentModel::Tongue), tonguePreparedNotes_);
    assert(verifyPreparedNoteCanaries());
    allocator_.preparePreparedNoteTable(getInstrumentModelConfig(InstrumentModel::Bowl), bowlPreparedNotes_);
    assert(verifyPreparedNoteCanaries());
    allocator_.preparePreparedNoteTable(getInstrumentModelConfig(InstrumentModel::Kalimba), kalimbaPreparedNotes_);
    assert(verifyPreparedNoteCanaries());
    allocator_.preparePreparedNoteTable(getInstrumentModelConfig(InstrumentModel::Glass), glassPreparedNotes_);
    assert(verifyPreparedNoteCanaries());
    allocator_.preparePreparedNoteTable(getInstrumentModelConfig(InstrumentModel::Marimba), marimbaPreparedNotes_);
    allocator_.preparePreparedNoteTable(getInstrumentModelConfig(InstrumentModel::Vibraphone), vibraphonePreparedNotes_);
    allocator_.preparePreparedNoteTable(getInstrumentModelConfig(InstrumentModel::Mbira), mbiraPreparedNotes_);
    assert(verifyPreparedNoteCanaries());
#endif
    for (unsigned m = 0; m < unsigned(InstrumentModel::Count); ++m) {
        preparedBodies_[m].init(sampleRate_);
        preparedBodies_[m].setConfig(getInstrumentModelConfig(static_cast<InstrumentModel>(m)).body);
    }
    for (unsigned i = 0; i <= 256; ++i)
        motorTable_[i] = .5f + .5f * std::cos(6.283185307179586f * float(i) / 256.0f);
    setVibraphoneMotor(true);
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

DSP_EVENT_HOT void SynthEngine::reset() {
    mbiraBuzz_.reset();
    mbiraDcState_ = 0.0f;
    motorPhase_ = 0;
    nonfiniteCount_ = 0;
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
    // Fixed sample-rate infrastructure is retained; only model state changes.
    model_ = unsigned(model) < unsigned(InstrumentModel::Count) ? model : InstrumentModel::Pan;
    modelConfig_ = getInstrumentModelConfig(model_);
    allocator_.setModelConfig(modelConfig_);
#if POCKETPAN_SYMPATHETIC_COEFF_CACHE
    allocator_.setSympatheticConfig(modelConfig_.sympathetic);
#endif
#if POCKETPAN_PREPARED_NOTE_CACHE
    assert(verifyPreparedNoteCanaries());
    allocator_.setPreparedNoteTable(&panPreparedNotes_);
    if (model_ == InstrumentModel::Bell) {
        allocator_.setPreparedNoteTable(&bellPreparedNotes_);
    } else if (model_ == InstrumentModel::Tongue) {
        allocator_.setPreparedNoteTable(&tonguePreparedNotes_);
    } else if (model_ == InstrumentModel::Bowl) {
        allocator_.setPreparedNoteTable(&bowlPreparedNotes_);
    } else if (model_ == InstrumentModel::Kalimba) {
        allocator_.setPreparedNoteTable(&kalimbaPreparedNotes_);
    } else if (model_ == InstrumentModel::Glass) {
        allocator_.setPreparedNoteTable(&glassPreparedNotes_);
    } else if (model_ == InstrumentModel::Marimba) {
        allocator_.setPreparedNoteTable(&marimbaPreparedNotes_);
    } else if (model_ == InstrumentModel::Vibraphone) {
        allocator_.setPreparedNoteTable(&vibraphonePreparedNotes_);
    } else if (model_ == InstrumentModel::Mbira) {
        allocator_.setPreparedNoteTable(&mbiraPreparedNotes_);
    } else if (model_ == InstrumentModel::Udu) {
        allocator_.setPreparedNoteTable(nullptr); // UDU keeps its independent cache.
    }

#endif
    body_ = preparedBodies_[static_cast<size_t>(model_)];
    setVibraphoneMotor(true);
    bodyStrategy_ = modelConfig_.bodyStrategy;
    mbiraBuzzEnabled_ = true;
    reset();
}

void SynthEngine::setVibraphoneMotor(bool enabled, float rateHz, float depth) {
    motorEnabled_ = enabled;
    motorDepth_ = std::isfinite(depth) ? std::clamp(depth, 0.0f, .8f) : .32f;
    const float rate = std::isfinite(rateHz) ? std::clamp(rateHz, 2.0f, 8.0f) : 4.5f;
    motorIncrement_ = static_cast<uint32_t>(double(rate) * 4294967296.0 / sampleRate_);
}

void SynthEngine::resetDiagnostics() {
    nonfiniteCount_ = 0;
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

DSP_EVENT_HOT void SynthEngine::handleMidiEvent(const midi::MidiEvent& event) {
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
#if POCKETPAN_PACKED_NOTE_CACHE
                    // M8: the existing startup MIDI-Hz table is model independent.
                    // Reuse its exact pow() results for every canonical MIDI event.
                    freqHz = event.data1 < 128 ? vibraphoneMidiHz[event.data1]
                        : midi::MidiMapping::noteToHz(event.data1);
#else
                    freqHz = (model_ == InstrumentModel::Vibraphone || model_ == InstrumentModel::Mbira || model_ == InstrumentModel::Udu) && event.data1 < 128
                        ? vibraphoneMidiHz[event.data1] : midi::MidiMapping::noteToHz(event.data1);
#endif
                    vel = midi::MidiMapping::toNormalizedFloat(event.data2);
                }
                allocator_.noteOn(event.data1, vel, freqHz);
                if (model_ == InstrumentModel::Mbira && mbiraBuzzEnabled_) mbiraBuzz_.strike(vel);
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
    if (model_ == InstrumentModel::Vibraphone && motorEnabled_ && motorDepth_ > 0.0f) {
        uint32_t phase = motorPhase_;
        for (size_t i = 0; i < frames; ++i) {
            const unsigned index = phase >> 24;
            const float fraction = float(phase & 0x00ffffffU) * (1.0f / 16777216.0f);
            const float shutter = motorTable_[index] + fraction * (motorTable_[index+1] - motorTable_[index]);
            fanResponseBuffer_[i] = .10f + .90f * (1.0f - shutter);
            phase += motorIncrement_;
        }
        allocator_.renderVibraphoneBlock(monoBuffer_, frames, fanResponseBuffer_,
                                         modelConfig_.tubeCoupling, motorDepth_ / .32f);
    }
    else if (useStrikeBus) allocator_.renderBlockWithStrikeBus(monoBuffer_, strikeBuffer_, frames, modelConfig_.sympathetic);
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
        float mixed = monoBuffer_[i] + bodySample;
        if (model_ == InstrumentModel::Mbira) {
            if (mbiraBuzzEnabled_)
                mixed += mbiraBuzz_.process(monoBuffer_[i], modelConfig_.mbiraBuzzGain);
            // Remove the unipolar pluck/bridge impulse area as well as contact
            // DC; retain this guard with buzz off. Other instruments are exact.
            mbiraDcState_ = (1.0f-mbiraDcPole_)*mixed + mbiraDcPole_*mbiraDcState_;
            mixed -= mbiraDcState_;
        } else if (model_ == InstrumentModel::Udu) {
            // A shared output guard also covers the removed DC state of stolen pots.
            mbiraDcState_ = (1.0f-mbiraDcPole_)*mixed + mbiraDcPole_*mbiraDcState_;
            mixed -= mbiraDcState_;
        }
        if (model_ == InstrumentModel::Vibraphone) {
            motorPhase_ += motorIncrement_;
        }
        sample = mixed * masterGain_ * polyHeadroomGain_;

        }
        { DSP_PROFILE_SCOPE(Limiter);
        // NaN / Inf safety guard
        if (std::isnan(sample) || std::isinf(sample)) {
            ++nonfiniteCount_;
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
