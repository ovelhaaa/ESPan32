#pragma once

#include <cstdint>
#include <cstddef>
#include "modal_voice.h"

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

    // Real-time voice metrics
    size_t getActiveVoiceCount() const;
    const ModalVoice& getVoice(size_t index) const { return voices_[index]; }

private:
    int findVoiceToSteal() const;

    struct StealDeclickTail {
        bool active = false;
        float currentSample = 0.0f;
        float step = 0.0f;
        uint16_t samplesLeft = 0;
    };

    float sampleRate_ = 48000.0f;
    ModalVoice voices_[kMaxVoices];
    StealDeclickTail stealTail_{};
};

} // namespace pocketpan::dsp
