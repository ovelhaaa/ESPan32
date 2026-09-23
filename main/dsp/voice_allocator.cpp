#include "voice_allocator.h"
#include <algorithm>
#include <limits>

namespace pocketpan::dsp {

void VoiceAllocator::init(float sampleRate) {
    sampleRate_ = sampleRate;
    for (size_t i = 0; i < kMaxVoices; ++i) {
        voices_[i].init(sampleRate_);
    }
}

void VoiceAllocator::reset() {
    for (size_t i = 0; i < kMaxVoices; ++i) {
        voices_[i].reset();
    }
}

int VoiceAllocator::findVoiceToSteal() const {
    int bestCandidate = -1;
    float lowestEnergy = std::numeric_limits<float>::max();

    // Preference 1: steal released voice with lowest energy
    for (size_t i = 0; i < kMaxVoices; ++i) {
        if (voices_[i].isReleased()) {
            float e = voices_[i].getEstimatedEnergy();
            if (e < lowestEnergy) {
                lowestEnergy = e;
                bestCandidate = static_cast<int>(i);
            }
        }
    }

    if (bestCandidate >= 0) return bestCandidate;

    // Preference 2: steal oldest / lowest energy among unreleased voices
    for (size_t i = 0; i < kMaxVoices; ++i) {
        float e = voices_[i].getEstimatedEnergy();
        if (e < lowestEnergy) {
            lowestEnergy = e;
            bestCandidate = static_cast<int>(i);
        }
    }

    return (bestCandidate >= 0) ? bestCandidate : 0;
}

void VoiceAllocator::noteOn(uint8_t note, float velocity, float fundamentalFrequencyHz) {
    // 1. If this note is already active, retrigger that voice
    for (size_t i = 0; i < kMaxVoices; ++i) {
        if (voices_[i].isActive() && voices_[i].getMidiNote() == note) {
            voices_[i].trigger(note, fundamentalFrequencyHz, velocity);
            return;
        }
    }

    // 2. Look for a completely free/inactive voice
    for (size_t i = 0; i < kMaxVoices; ++i) {
        if (!voices_[i].isActive()) {
            voices_[i].trigger(note, fundamentalFrequencyHz, velocity);
            return;
        }
    }

    // 3. All 8 voices active: Steal voice with lowest energy
    int stealIdx = findVoiceToSteal();
    if (stealIdx >= 0 && stealIdx < static_cast<int>(kMaxVoices)) {
        voices_[stealIdx].kill();
        voices_[stealIdx].trigger(note, fundamentalFrequencyHz, velocity);
    }
}

void VoiceAllocator::noteOff(uint8_t note) {
    for (size_t i = 0; i < kMaxVoices; ++i) {
        if (voices_[i].isActive() && voices_[i].getMidiNote() == note) {
            voices_[i].release();
        }
    }
}

void VoiceAllocator::setPolyPressure(uint8_t note, float pressure) {
    for (size_t i = 0; i < kMaxVoices; ++i) {
        if (voices_[i].isActive() && voices_[i].getMidiNote() == note) {
            voices_[i].setDamping(pressure);
        }
    }
}

void VoiceAllocator::setChannelPressure(float pressure) {
    for (size_t i = 0; i < kMaxVoices; ++i) {
        if (voices_[i].isActive()) {
            voices_[i].setDamping(pressure);
        }
    }
}

void VoiceAllocator::renderBlock(float* outBuffer, size_t frames) {
    std::fill(outBuffer, outBuffer + frames, 0.0f);

    for (size_t v = 0; v < kMaxVoices; ++v) {
        if (!voices_[v].isActive()) continue;

        for (size_t i = 0; i < frames; ++i) {
            outBuffer[i] += voices_[v].processSample();
        }
    }
}

size_t VoiceAllocator::getActiveVoiceCount() const {
    size_t count = 0;
    for (size_t i = 0; i < kMaxVoices; ++i) {
        if (voices_[i].isActive()) count++;
    }
    return count;
}

} // namespace pocketpan::dsp
