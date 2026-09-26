#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include "modal_voice.h"
#include "sympathetic_config.h"
#include "instrument_model.h"

namespace pocketpan::dsp {

constexpr size_t kMaxVoices = 8;

class VoiceAllocator {
public:
    VoiceAllocator() = default;

    void init(float sampleRate);
    void reset();

    // Note On: allocate voice or steal lowest energy voice
    void noteOn(uint8_t note, float velocity, float fundamentalFrequencyHz);

    // Note Off: release note gesture (natural metallic ring down continues)
    void noteOff(uint8_t note);

    // Polyphonic Key Pressure (per-note aftertouch choke)
    void setPolyPressure(uint8_t note, float pressure);

    // Channel Pressure (global aftertouch choke across all active voices)
    void setChannelPressure(float pressure);

    // Render polyphonic sum of all active voices into outBuffer
    void renderBlock(float* outBuffer, size_t frames);
    void renderBlock(float* outBuffer, size_t frames, const SympatheticConfig& config);
    // Renders the normal dry mix and, in the same voice pass, a sum of local
    // exciter taps. The latter is a body input bus, never a sympathetic bus.
    void renderBlockWithStrikeBus(float* outBuffer, float* strikeBuffer, size_t frames,
                                  const SympatheticConfig& config);
    float getSympatheticBusPeak() const { return sympatheticBusPeak_; }
    float getSympatheticBusRms() const { return sympatheticBusSamples_ ? std::sqrt(sympatheticBusSumSquares_/sympatheticBusSamples_) : 0.0f; }
    uint32_t getSympatheticSafetyCount() const { return sympatheticSafetyCount_; }
    // Audio memory only. This deliberately does not erase telemetry.
    void resetSympatheticState();
    // Telemetry only. This deliberately does not alter the delayed bus.
    void resetSympatheticDiagnostics();

    // Real-time voice metrics
    size_t getActiveVoiceCount() const;
    size_t getActiveStealTailCount() const;
    // Diagnostic telemetry only; it never participates in allocation policy.
    uint32_t getVoiceStealCount() const { return voiceStealCount_; }
    uint32_t getInternalSaturationCount() const;
    void setInternalSafetySaturation(bool enabled);
    void setModelConfig(const InstrumentModelConfig& config);
    // M6.3.5 Phase A.  Caches the sympathetic low-pass coefficient at a
    // configuration boundary so the realtime render path never evaluates the
    // exponential.  The cached value is the bit-identical result of the
    // established expression.
    void setSympatheticConfig(const SympatheticConfig& config);
#if POCKETPAN_SYMPATHETIC_COEFF_CACHE
    // Host exactness probe only.  Never present in firmware.
    float getSympatheticLowpassCoefficientForTest() const { return sympatheticLowpassCoefficient_; }
#endif
    void setPanConfigsForTest(const ExciterConfig& exciter, const PanVoicingConfig& voicing);
#if POCKETPAN_SUSTAIN_FASTPATH
    // Host A/B only: toggles the stable-sustain fast path without rebuilding.
    void setSustainFastPathEnabledForTest(bool enabled) {
        for (auto& voice : voices_) voice.setSustainFastPathEnabledForTest(enabled);
    }
    // Counts voice-blocks that took the sustain fast path; tests assert it is
    // actually exercised rather than silently bypassed.
    uint32_t getSustainFastPathBlocksForTest() const { return sustainFastPathBlocks_; }
#endif
#if POCKETPAN_ATTACK_VOICE_FASTPATH
    // Host A/B only: toggles the attack-voice fast path without rebuilding.
    void setAttackFastPathEnabledForTest(bool enabled) {
        for (auto& voice : voices_) voice.setAttackFastPathEnabledForTest(enabled);
    }
    uint32_t getAttackFastPathBlocksForTest() const { return attackFastPathBlocks_; }
#endif
#if POCKETPAN_PAN_STABLE8_FASTPATH
    // Counts blocks that took the specialized eight-voice stable sustain path.
    uint32_t getPanStable8BlocksForTest() const { return panStable8Blocks_; }
#endif
#if POCKETPAN_PREPARED_NOTE_CACHE
    // Tables are prepared outside the callback and shared by all eight voices.
    void preparePreparedNoteTable(const InstrumentModelConfig& config,
                                  PreparedNoteTable& table) const;
    void setPreparedNoteTable(const PreparedNoteTable* table) { preparedNoteTable_ = table; }
    // Host qualification hook; it has no UI/MIDI/persisted route.
    void setPreparedNoteCacheEnabledForTest(bool enabled) { preparedNoteCacheEnabled_ = enabled; }
    bool isPreparedNoteCacheEnabledForTest() const { return preparedNoteCacheEnabled_; }
#endif
    const ModalVoice& getVoice(size_t index) const { return voices_[index]; }

private:
    int findVoiceToSteal() const;
#if POCKETPAN_PAN_STABLE8_FASTPATH
    // True when every voice is sustain-safe for the whole block and no steal
    // declick tail is in flight, i.e. the specialized stable-8 path is exact.
    bool steady8(const bool* sustainSafe) const;
#endif
#if POCKETPAN_ATTACK_VOICE_FASTPATH
    // True when every voice is attack-safe and no steal declick tail is pending.
    bool attackStable8() const;
#endif

    struct StealDeclickTail {
        bool active = false;
        float currentSample = 0.0f;
        float step = 0.0f;
        uint16_t samplesLeft = 0;
    };

    float sampleRate_ = 48000.0f;
    ModalVoice voices_[kMaxVoices];
    StealDeclickTail stealTails_[kMaxVoices]{};
    size_t nextStealTail_ = 0;
    uint32_t voiceStealCount_ = 0;
    float sympatheticPreviousBus_ = 0.0f, sympatheticFilterState_ = 0.0f, sympatheticLowpassCoefficient_ = 0.0f;
    float sympatheticBusPeak_ = 0.0f, sympatheticBusSumSquares_ = 0.0f; uint32_t sympatheticBusSamples_ = 0, sympatheticSafetyCount_ = 0;
#if POCKETPAN_PREPARED_NOTE_CACHE
    const PreparedNoteTable* preparedNoteTable_ = nullptr;
    bool preparedNoteCacheEnabled_ = true;
#endif
#if POCKETPAN_SUSTAIN_FASTPATH
    uint32_t sustainFastPathBlocks_ = 0;
#endif
#if POCKETPAN_ATTACK_VOICE_FASTPATH
    uint32_t attackFastPathBlocks_ = 0;
#endif
#if POCKETPAN_PAN_STABLE8_FASTPATH
    uint32_t panStable8Blocks_ = 0;
#endif
};

} // namespace pocketpan::dsp
