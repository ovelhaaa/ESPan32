#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include "voice_allocator.h"
#include "peak_limiter.h"
#include "body_resonator.h"
#include "pan_calibration.h"
#include "instrument_model.h"
#include "../midi/midi_event.h"

namespace pocketpan::dsp {

constexpr size_t kMaxBlockFrames = 128;

class SynthEngine {
public:
    SynthEngine() = default;

    void init(float sampleRate);
    void reset();
    // Switching is deliberately not a performance gesture: all ringing state
    // is reset before the static configuration is installed.
    void setInstrumentModel(InstrumentModel model);
    InstrumentModel getInstrumentModel() const { return model_; }

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
    // not nonlinear clipping. Resetting diagnostics must not flush lookahead.
    uint32_t getSoftClipCount() const { return limiter_.getActiveSampleCount(); }
    void resetSoftClipCount() { resetDiagnostics(); }
    void resetDiagnostics();
    float getPreLimiterPeak() const { return preLimiterPeak_; }
    float getPostLimiterPeak() const { return postLimiterPeak_; }
    float getCurrentGainReductionDb() const { return limiter_.getCurrentGainReductionDb(); }
    float getMaxGainReductionDb() const { return limiter_.getMaxGainReductionDb(); }
    uint32_t getLimiterActiveSamples() const { return limiter_.getActiveSampleCount(); }
    uint32_t getGainReductionOver0p1DbSamples() const { return limiter_.getGainReductionOver0p1DbSamples(); }
    uint32_t getGainReductionOver1DbSamples() const { return limiter_.getGainReductionOver1DbSamples(); }
    float getAverageGainReductionDb() const { return limiter_.getAverageGainReductionDb(); }
    uint32_t getHardClampCount() const { return hardClampCount_; }
    uint32_t getModalInternalSaturationCount() const;
    // Host qualification only; normal firmware uses the PAN model default.
    void setInternalSafetySaturation(bool enabled) { allocator_.setInternalSafetySaturation(enabled); }
    void setPanConfigsForTest(const ExciterConfig& exciter, const PanVoicingConfig& voicing) { allocator_.setPanConfigsForTest(exciter, voicing); }
    void setBodyEnabled(bool enabled) { modelConfig_.body.enabled=enabled; body_.setConfig(modelConfig_.body); }
    // A runtime toggle must not retain delayed feedback from its prior mode.
    void setSympatheticEnabled(bool enabled);
    // Host qualification hooks. They are intentionally not connected to UI or persisted settings.
    void setBodyConfigForTest(const BodyConfig& config) { modelConfig_.body=config; body_.setConfig(modelConfig_.body); }
    void setStrikeBusGainForTest(float gain) { modelConfig_.strikeBusGain = gain; }
    void setBodyExcitationStrategyForTest(BodyExcitationStrategy strategy) { bodyStrategy_=strategy; }
    void setSympatheticConfigForTest(const SympatheticConfig& config) { modelConfig_.sympathetic=config; allocator_.resetSympatheticState(); }
    float getBodyEnergy() const { return body_.getEnergy(); }
    float getBodyPeak() const { return bodyPeak_; }
    float getBodyRms() const { return bodySamples_ ? std::sqrt(bodySumSquares_/bodySamples_) : 0.0f; }
    float getSympatheticBusPeak() const { return allocator_.getSympatheticBusPeak(); }
    float getSympatheticBusRms() const { return allocator_.getSympatheticBusRms(); }
    uint32_t getSympatheticSafetyCount() const { return allocator_.getSympatheticSafetyCount(); }

private:
    float sampleRate_ = 48000.0f;
    float masterGain_ = 0.85f; // Headroom protection
    float polyHeadroomGain_ = 1.0f;
    float polyHeadroomAttackCoefficient_ = 0.0f;
    float polyHeadroomReleaseCoefficient_ = 0.0f;
    float preLimiterPeak_ = 0.0f;
    float postLimiterPeak_ = 0.0f;
    uint32_t hardClampCount_ = 0;
    PeakLimiter limiter_;

    VoiceAllocator allocator_;
    BodyResonator body_; InstrumentModelConfig modelConfig_{};
    BodyExcitationStrategy bodyStrategy_ = BodyExcitationStrategy::StrikeBus;
    float bodyTransientState_ = 0.0f;
    float bodyTransientCoefficient_ = 0.0f;
    float bodyPeak_=0.0f, bodySumSquares_=0.0f; uint32_t bodySamples_=0;
    InstrumentModel model_ = InstrumentModel::Pan;

    // Internal mono voice mix buffer
    float monoBuffer_[kMaxBlockFrames];
    float strikeBuffer_[kMaxBlockFrames];
};

} // namespace pocketpan::dsp
