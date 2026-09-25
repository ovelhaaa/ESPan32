#include "voice_allocator.h"
#include <algorithm>
#include <limits>
#include <cmath>

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
    for (auto& tail : stealTails_) tail = StealDeclickTail{};
    nextStealTail_ = 0;
    sympatheticPreviousBus_=sympatheticFilterState_=0.0f; resetSympatheticDiagnostics();
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
    // 1. If this note is already active, restrike that voice (accumulate energy physically)
    for (size_t i = 0; i < kMaxVoices; ++i) {
        if (voices_[i].isActive() && voices_[i].getMidiNote() == note) {
            voices_[i].restrike(velocity);
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

    // 3. All 8 voices active: Steal voice with lowest energy using declicked crossfade tail
    int stealIdx = findVoiceToSteal();
    if (stealIdx >= 0 && stealIdx < static_cast<int>(kMaxVoices)) {
        float residual = voices_[stealIdx].getLastSample();
        // Reserve an independent fixed tail even near a zero crossing. Besides
        // making burst ownership deterministic, this prevents a later steal
        // from replacing an earlier non-zero residual.
        auto& tail = stealTails_[nextStealTail_];
        nextStealTail_ = (nextStealTail_ + 1) % kMaxVoices;
        tail.active = true;
        tail.currentSample = residual;
        tail.samplesLeft = 32;
        tail.step = residual / 32.0f;
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

    // Independently mix every steal captured since the previous render.
    for (auto& tail : stealTails_) {
        if (!tail.active) continue;
        for (size_t i = 0; i < frames && tail.samplesLeft > 0; ++i) {
            outBuffer[i] += tail.currentSample;
            tail.currentSample -= tail.step;
            if (--tail.samplesLeft == 0) {
                tail.active = false;
                break;
            }
        }
    }
}

size_t VoiceAllocator::getActiveStealTailCount() const {
    size_t count = 0;
    for (const auto& tail : stealTails_) if (tail.active) ++count;
    return count;
}

uint32_t VoiceAllocator::getInternalSaturationCount() const {
    uint32_t count = 0;
    for (const auto& voice : voices_) count += voice.getInternalSaturationCount();
    return count;
}

void VoiceAllocator::resetSympatheticDiagnostics() { sympatheticBusPeak_=sympatheticBusSumSquares_=0.0f; sympatheticBusSamples_=sympatheticSafetyCount_=0; }

void VoiceAllocator::renderBlock(float* outBuffer, size_t frames, const SympatheticConfig& config) {
    if (!config.enabled) { renderBlock(outBuffer, frames); return; }
    const float cutoff=std::clamp(config.lowpassHz,10.0f,sampleRate_*0.45f);
    sympatheticLowpassCoefficient_=std::exp(-2.0f*3.14159265358979323846f*cutoff/sampleRate_);
    for(size_t i=0;i<frames;++i) {
        float sum=0.0f;
        // One-sample delayed global bus: active/ringing voices only. At this
        // deliberately tiny gain, residual self-feedback is negligible.
        const float external=sympatheticPreviousBus_*config.inputGain;
        for(auto& voice:voices_) if(voice.isActive()) sum+=voice.processSample(external);
        for(auto& tail:stealTails_) if(tail.active && tail.samplesLeft) { sum+=tail.currentSample; tail.currentSample-=tail.step; if(--tail.samplesLeft==0) tail.active=false; }
        sympatheticFilterState_=(1.0f-sympatheticLowpassCoefficient_)*sum+sympatheticLowpassCoefficient_*sympatheticFilterState_;
        float next=sympatheticFilterState_*config.feedbackGain;
        const float limit=std::max(0.0f,config.maxBusLevel);
        if(limit>0.0f && std::abs(next)>limit) { next=std::copysign(limit,next); ++sympatheticSafetyCount_; }
        sympatheticPreviousBus_=next; outBuffer[i]=sum;
        sympatheticBusPeak_=std::max(sympatheticBusPeak_,std::abs(next)); sympatheticBusSumSquares_+=next*next; ++sympatheticBusSamples_;
    }
}

void VoiceAllocator::setInternalSafetySaturation(bool enabled) {
    for (auto& voice : voices_) voice.setInternalSafetySaturation(enabled);
}

size_t VoiceAllocator::getActiveVoiceCount() const {
    size_t count = 0;
    for (size_t i = 0; i < kMaxVoices; ++i) {
        if (voices_[i].isActive()) count++;
    }
    return count;
}

} // namespace pocketpan::dsp
