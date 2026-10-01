#include "voice_allocator.h"
#include "dsp_profile.h"
#include "../midi/midi_mapping.h"
#include <algorithm>
#include <limits>
#include <cmath>
#include <type_traits>

namespace pocketpan::dsp {

void VoiceAllocator::init(float sampleRate) {
    sampleRate_ = sampleRate;
    uduCache_.prepare(sampleRate_);
    for (size_t i = 0; i < kMaxVoices; ++i) {
        voices_[i].init(sampleRate_);
        voices_[i].setUduCache(&uduCache_);
    }
#if POCKETPAN_PREPARED_NOTE_CACHE
    preparedNoteTable_ = nullptr;
#endif
}

void VoiceAllocator::reset() {
    for (size_t i = 0; i < kMaxVoices; ++i) {
        voices_[i].reset();
    }
    for (auto& tail : stealTails_) tail = StealDeclickTail{};
    nextStealTail_ = 0;
    voiceStealCount_ = 0;
#if POCKETPAN_SUSTAIN_FASTPATH
    sustainFastPathBlocks_ = 0;
#endif
#if POCKETPAN_ATTACK_VOICE_FASTPATH
    attackFastPathBlocks_ = 0;
#endif
#if POCKETPAN_PAN_STABLE8_FASTPATH
    panStable8Blocks_ = 0;
#endif
    resetSympatheticState();
    resetSympatheticDiagnostics();
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
    int sameNoteIndex = -1;
    {
        DSP_PROFILE_SCOPE(VoiceAllocation);
        for (size_t i = 0; i < kMaxVoices; ++i) {
            if (voices_[i].isActive() && voices_[i].getMidiNote() == note) {
                sameNoteIndex = static_cast<int>(i);
                break;
            }
        }
    }
    if (sameNoteIndex >= 0) {
        voices_[sameNoteIndex].restrike(velocity);
        return;
    }

    const PreparedNote* prepared = nullptr;
#if POCKETPAN_PREPARED_NOTE_CACHE
    {
        DSP_PROFILE_SCOPE(PreparedLookup);
        if (preparedNoteCacheEnabled_ && preparedNoteTable_) {
            prepared = preparedNoteTable_->find(note, fundamentalFrequencyHz);
        }
    }
#endif

    // 2. Look for a completely free/inactive voice
    int freeVoiceIndex = -1;
    {
        DSP_PROFILE_SCOPE(VoiceAllocation);
        for (size_t i = 0; i < kMaxVoices; ++i) {
            if (!voices_[i].isActive()) {
                freeVoiceIndex = static_cast<int>(i);
                break;
            }
        }
    }
    if (freeVoiceIndex >= 0) {
        voices_[freeVoiceIndex].trigger(note, fundamentalFrequencyHz, velocity, prepared);
        return;
    }

    // 3. All 8 voices active: Steal voice with lowest energy using declicked crossfade tail
    int stealIdx;
    {
        DSP_PROFILE_SCOPE(VoiceAllocation);
        stealIdx = findVoiceToSteal();
    }
    if (stealIdx >= 0 && stealIdx < static_cast<int>(kMaxVoices)) {
        ++voiceStealCount_;
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
        voices_[stealIdx].trigger(note, fundamentalFrequencyHz, velocity, prepared);
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

DSP_HOT void VoiceAllocator::renderBlock(float* outBuffer, size_t frames) {
    std::fill(outBuffer, outBuffer + frames, 0.0f);

    for (size_t v = 0; v < kMaxVoices; ++v) {
        if (!voices_[v].isActive()) continue;
#if POCKETPAN_ATTACK_VOICE_FASTPATH
        if (voices_[v].isAttackFastPathEnabledForTest() && voices_[v].isAttackSafe()) {
            ++attackFastPathBlocks_;
            voices_[v].renderAttackBlock(outBuffer, frames, 0.0f);
            continue;
        }
#endif
#if POCKETPAN_SUSTAIN_FASTPATH
        if (voices_[v].isSustainFastPathEnabledForTest() && voices_[v].isSustainSafe()) {
            ++sustainFastPathBlocks_;
            voices_[v].renderSustainBlock(outBuffer, frames, 0.0f);
            continue;
        }
#endif

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

// Vibraphone has no body or sympathetic feedback. Keep the normal modal
// kernels and voice order; tap mode 0 immediately after each recurrence.
// Frozen M1: retain historical voice and multiplication order. Only the
// traversal is sample-first; no tube/phase buffers are accumulated per voice.
__attribute__((optimize("O3"))) DSP_HOT void VoiceAllocator::renderVibraphoneBlock(
    float* out, size_t frames, const float* fanResponse, float tubeCoupling, float depthScale) {
#if POCKETPAN_SUSTAIN_FASTPATH && POCKETPAN_ATTACK_VOICE_FASTPATH
    // Stable voices (unrolled 0..7), including the strike block. Sum while the modal tap
    // is hot, then apply the common shaft gains once, rather than eight times.
    bool stable = getActiveStealTailCount() == 0;
    bool sustain[kMaxVoices]{};
    bool sixSafety = true;
    for (size_t v=0; v<kMaxVoices && stable; ++v) {
        auto& voice = voices_[v];
        if (!voice.active_) continue;
        sixSafety = sixSafety && voice.resonators_.canProcessSixSafety();
        sustain[v] = voice.sustainFastPathEnabled_ && voice.isSustainSafe();
        stable = sustain[v] || (voice.attackFastPathEnabled_ && voice.isAttackSafe());
    }
    if (stable) {
        for (size_t v=0; v<kMaxVoices; ++v) if (voices_[v].active_) {
            if (sustain[v]) ++sustainFastPathBlocks_; else ++attackFastPathBlocks_;
        }
#if POCKETPAN_PAN_STABLE8_FASTPATH
        if (getActiveVoiceCount()==8) ++panStable8Blocks_;
#endif
        size_t done=0;
        while (done<frames) {
            // No age/active-state transition can occur inside this segment.
            // End exactly at the earliest lifetime check or exciter boundary.
            size_t segment=frames-done;
            bool active[kMaxVoices]{};
            float last[kMaxVoices]{};
            bool allActive=true, allSustain=true;
            for (size_t v=0;v<kMaxVoices;++v) {
                auto& voice=voices_[v];
                active[v]=voice.active_;
                allActive=allActive && active[v];
                if (!active[v]) continue;
                sustain[v]=!voice.exciter_.isActive();
                allSustain=allSustain && sustain[v];
                segment=std::min(segment,size_t(128u-(voice.age_&0x7fu)));
                if (!sustain[v]) segment=std::min(segment,size_t(voice.exciter_.samplesUntilInactive()));
            }
            // Specialize the common stable-eight segment once, outside the
            // sample loop. Mixed/partial voices retain the same general path.
            auto renderSegment=[&](auto full, auto quiet) {
              for (size_t i=done;i<done+segment;++i) {
                float drySum=0, fundamentalSum=0;
                auto advance=[&](size_t v) {
                    if constexpr (!decltype(full)::value) { if (!active[v]) return; }
                    auto& voice=voices_[v];
                    float excitation=0;
                    if constexpr (!decltype(quiet)::value) { if (!sustain[v]) {
                        DSP_PROFILE_SCOPE(Exciter);
                        excitation=voice.exciter_.processSample()+0.0f;
                    } }
                    last[v]=sixSafety ? voice.resonators_.processSampleSixSafety(excitation)
                                      : voice.resonators_.processSample(excitation);
                    drySum+=last[v];
                    fundamentalSum+=voice.resonators_.fundamentalSample();
                };
                advance(0); advance(1); advance(2); advance(3);
                advance(4); advance(5); advance(6); advance(7);
                out[i]=drySum + fundamentalSum*tubeCoupling*depthScale*fanResponse[i];
              }
            };
            if (allActive && allSustain) renderSegment(std::true_type{},std::true_type{});
            else if (allActive) renderSegment(std::true_type{},std::false_type{});
            else renderSegment(std::false_type{},std::false_type{});
            for (size_t v=0;v<kMaxVoices;++v) if (active[v]) {
                auto& voice=voices_[v];
                voice.age_+=uint32_t(segment);
                voice.lastSample_=last[v];
                if ((voice.age_&0x7fu)==0u) {
                    voice.estimatedEnergy_=voice.resonators_.getEnergy();
                    if (!voice.exciter_.isActive() && voice.estimatedEnergy_<voice.voicingConfig_.silenceThreshold) {
                        voice.active_=false; voice.estimatedEnergy_=0;
                    }
                }
            }
            done+=segment;
        }
        return;
    }
#endif
    // Pressure/steal/reference path. Captured tails are added to dry before
    // the tube mix, exactly as in the retained M1 reference.
    for (size_t i=0; i<frames; ++i) {
        float drySum=0, fundamentalSum=0;
        for (auto& voice : voices_) {
            if (!voice.active_) continue;
            const float fade=voice.tubeFadeGain();
            drySum += voice.processSample();
            fundamentalSum += voice.resonators_.fundamentalSample() * fade;
        }
        for (auto& tail : stealTails_) {
            if (!tail.active || !tail.samplesLeft) continue;
            drySum += tail.currentSample;
            tail.currentSample -= tail.step;
            if (--tail.samplesLeft == 0) tail.active=false;
        }
        out[i]=drySum + fundamentalSum * tubeCoupling * depthScale * fanResponse[i];
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

void VoiceAllocator::resetSympatheticState() {
    sympatheticPreviousBus_ = 0.0f;
    sympatheticFilterState_ = 0.0f;
}

void VoiceAllocator::resetSympatheticDiagnostics() {
    sympatheticBusPeak_=sympatheticBusSumSquares_=0.0f;
    sympatheticBusSamples_=sympatheticSafetyCount_=0;
}

DSP_HOT void VoiceAllocator::renderBlock(float* outBuffer, size_t frames, const SympatheticConfig& config) {
    if (!config.enabled) { renderBlock(outBuffer, frames); return; }
#if !POCKETPAN_SYMPATHETIC_COEFF_CACHE
    const float cutoff=std::clamp(config.lowpassHz,10.0f,sampleRate_*0.45f);
    sympatheticLowpassCoefficient_=std::exp(-2.0f*3.14159265358979323846f*cutoff/sampleRate_);
#endif
#if POCKETPAN_SUSTAIN_FASTPATH
    // The decision is taken once per voice per block; the sample-outer loop and
    // voice summation order are unchanged.
    bool sustainSafe[kMaxVoices];
    for (size_t v=0; v<kMaxVoices; ++v)
        sustainSafe[v] = voices_[v].isSustainFastPathEnabledForTest() && voices_[v].isSustainSafe();
    for (size_t v=0; v<kMaxVoices; ++v) if (sustainSafe[v]) ++sustainFastPathBlocks_;
#endif
#if POCKETPAN_PAN_STABLE8_FASTPATH
    // Block-stable eight-voice sustain: every voice is active, provably
    // sustain-safe for the whole block, and no steal tail is in flight.  The
    // per-sample active/mode branches are hoisted, but the sample-outer loop,
    // voice 0->7 order and float summation order are byte-for-byte preserved.
    if (steady8(sustainSafe)) {
        ++panStable8Blocks_;
        for (size_t i=0;i<frames;++i) {
            const float external=sympatheticPreviousBus_*config.inputGain;
            float sum=0.0f;
            sum+=voices_[0].processSampleSustain(external);
            sum+=voices_[1].processSampleSustain(external);
            sum+=voices_[2].processSampleSustain(external);
            sum+=voices_[3].processSampleSustain(external);
            sum+=voices_[4].processSampleSustain(external);
            sum+=voices_[5].processSampleSustain(external);
            sum+=voices_[6].processSampleSustain(external);
            sum+=voices_[7].processSampleSustain(external);
            sympatheticFilterState_=(1.0f-sympatheticLowpassCoefficient_)*sum+sympatheticLowpassCoefficient_*sympatheticFilterState_;
            float next=sympatheticFilterState_*config.feedbackGain;
            const float limit=std::max(0.0f,config.maxBusLevel);
            if(limit>0.0f && std::abs(next)>limit) { next=std::copysign(limit,next); ++sympatheticSafetyCount_; }
            sympatheticPreviousBus_=next; outBuffer[i]=sum;
            sympatheticBusPeak_=std::max(sympatheticBusPeak_,std::abs(next)); sympatheticBusSumSquares_+=next*next; ++sympatheticBusSamples_;
        }
        return;
    }
#endif
#if POCKETPAN_ATTACK_VOICE_FASTPATH
    // Eight simultaneous attacks on a sympathetic instrument: same sample-outer
    // dependency, but the per-voice damping/steal/active branches are hoisted.
    // Voice 0->7 order, float summation order and every state update match the
    // general path; a voice that ends its attack mid-block is handled by the
    // shared exciter/lifetime logic inside processSampleAttackStable.
    if (attackStable8()) {
        attackFastPathBlocks_ += kMaxVoices;
#if POCKETPAN_ATTACK_SEGMENT
        // M6.3.6 (§22): when every voice shares the same exact exciter end
        // sample, segment the block there and finish with the sustain kernel.
        // The two kernels agree bit-for-bit once localStrike is zero and the
        // exciter-active guard is false, so the per-sample stream is unchanged.
        const uint32_t remaining = voices_[0].samplesUntilExciterInactive();
        bool equal = remaining > 0;
        for (size_t v = 1; v < kMaxVoices; ++v) {
            if (voices_[v].samplesUntilExciterInactive() != remaining) { equal = false; break; }
        }
        const size_t split = (equal && remaining < frames) ? remaining : frames;
#else
        const size_t split = frames;
#endif
        for (size_t i=0;i<frames;++i) {
            const float external=sympatheticPreviousBus_*config.inputGain;
            float sum=0.0f;
#if POCKETPAN_ATTACK_SEGMENT
            if (i < split) {
                sum+=voices_[0].processSampleAttackStable(external);
                sum+=voices_[1].processSampleAttackStable(external);
                sum+=voices_[2].processSampleAttackStable(external);
                sum+=voices_[3].processSampleAttackStable(external);
                sum+=voices_[4].processSampleAttackStable(external);
                sum+=voices_[5].processSampleAttackStable(external);
                sum+=voices_[6].processSampleAttackStable(external);
                sum+=voices_[7].processSampleAttackStable(external);
            } else {
                sum+=voices_[0].processSampleSustain(external);
                sum+=voices_[1].processSampleSustain(external);
                sum+=voices_[2].processSampleSustain(external);
                sum+=voices_[3].processSampleSustain(external);
                sum+=voices_[4].processSampleSustain(external);
                sum+=voices_[5].processSampleSustain(external);
                sum+=voices_[6].processSampleSustain(external);
                sum+=voices_[7].processSampleSustain(external);
            }
#else
            sum+=voices_[0].processSampleAttackStable(external);
            sum+=voices_[1].processSampleAttackStable(external);
            sum+=voices_[2].processSampleAttackStable(external);
            sum+=voices_[3].processSampleAttackStable(external);
            sum+=voices_[4].processSampleAttackStable(external);
            sum+=voices_[5].processSampleAttackStable(external);
            sum+=voices_[6].processSampleAttackStable(external);
            sum+=voices_[7].processSampleAttackStable(external);
#endif
            sympatheticFilterState_=(1.0f-sympatheticLowpassCoefficient_)*sum+sympatheticLowpassCoefficient_*sympatheticFilterState_;
            float next=sympatheticFilterState_*config.feedbackGain;
            const float limit=std::max(0.0f,config.maxBusLevel);
            if(limit>0.0f && std::abs(next)>limit) { next=std::copysign(limit,next); ++sympatheticSafetyCount_; }
            sympatheticPreviousBus_=next; outBuffer[i]=sum;
            sympatheticBusPeak_=std::max(sympatheticBusPeak_,std::abs(next)); sympatheticBusSumSquares_+=next*next; ++sympatheticBusSamples_;
        }
        return;
    }
#endif
    for(size_t i=0;i<frames;++i) {
        float sum=0.0f;
        // One-sample delayed global bus: active/ringing voices only. At this
        // deliberately tiny gain, residual self-feedback is negligible.
        const float external=sympatheticPreviousBus_*config.inputGain;
        for(size_t v=0;v<kMaxVoices;++v) if(voices_[v].isActive()) {
#if POCKETPAN_SUSTAIN_FASTPATH
            if (sustainSafe[v]) sum+=voices_[v].processSampleSustain(external);
            else
#endif
            sum+=voices_[v].processSample(external);
        }
        for(auto& tail:stealTails_) if(tail.active && tail.samplesLeft) { sum+=tail.currentSample; tail.currentSample-=tail.step; if(--tail.samplesLeft==0) tail.active=false; }
        sympatheticFilterState_=(1.0f-sympatheticLowpassCoefficient_)*sum+sympatheticLowpassCoefficient_*sympatheticFilterState_;
        float next=sympatheticFilterState_*config.feedbackGain;
        const float limit=std::max(0.0f,config.maxBusLevel);
        if(limit>0.0f && std::abs(next)>limit) { next=std::copysign(limit,next); ++sympatheticSafetyCount_; }
        sympatheticPreviousBus_=next; outBuffer[i]=sum;
        sympatheticBusPeak_=std::max(sympatheticBusPeak_,std::abs(next)); sympatheticBusSumSquares_+=next*next; ++sympatheticBusSamples_;
    }
}

DSP_HOT void VoiceAllocator::renderBlockWithStrikeBus(float* outBuffer, float* strikeBuffer, size_t frames,
                                              const SympatheticConfig& config) {
    std::fill(outBuffer, outBuffer + frames, 0.0f);
    std::fill(strikeBuffer, strikeBuffer + frames, 0.0f);
    const bool sympathetic = config.enabled;
#if !POCKETPAN_SYMPATHETIC_COEFF_CACHE
    if (sympathetic) {
        const float cutoff=std::clamp(config.lowpassHz,10.0f,sampleRate_*0.45f);
        sympatheticLowpassCoefficient_=std::exp(-2.0f*3.14159265358979323846f*cutoff/sampleRate_);
    }
#endif
#if POCKETPAN_SUSTAIN_FASTPATH
    bool sustainSafe[kMaxVoices];
    for (size_t v=0; v<kMaxVoices; ++v)
        sustainSafe[v] = voices_[v].isSustainFastPathEnabledForTest() && voices_[v].isSustainSafe();
    for (size_t v=0; v<kMaxVoices; ++v) if (sustainSafe[v]) ++sustainFastPathBlocks_;
#endif
#if POCKETPAN_PAN_STABLE8_FASTPATH
    // Strike-bus sibling of the stable-8 sustain path.  Every exciter has
    // finished, so the local strike tap is exactly 0 for every sample: the
    // pre-zeroed strike buffer is already the exact output and no strike math
    // is performed.  Sample/voice/summation order is unchanged.
    if (sympathetic && steady8(sustainSafe)) {
        ++panStable8Blocks_;
        for (size_t i=0; i<frames; ++i) {
            const float external = sympatheticPreviousBus_*config.inputGain;
            float sum=0.0f;
            sum += voices_[0].processSampleSustain(external);
            sum += voices_[1].processSampleSustain(external);
            sum += voices_[2].processSampleSustain(external);
            sum += voices_[3].processSampleSustain(external);
            sum += voices_[4].processSampleSustain(external);
            sum += voices_[5].processSampleSustain(external);
            sum += voices_[6].processSampleSustain(external);
            sum += voices_[7].processSampleSustain(external);
            sympatheticFilterState_=(1.0f-sympatheticLowpassCoefficient_)*sum+sympatheticLowpassCoefficient_*sympatheticFilterState_;
            float next=sympatheticFilterState_*config.feedbackGain;
            const float limit=std::max(0.0f,config.maxBusLevel);
            if(limit>0.0f && std::abs(next)>limit) { next=std::copysign(limit,next); ++sympatheticSafetyCount_; }
            sympatheticPreviousBus_=next;
            sympatheticBusPeak_=std::max(sympatheticBusPeak_,std::abs(next)); sympatheticBusSumSquares_+=next*next; ++sympatheticBusSamples_;
            outBuffer[i]=sum;
        }
        return;
    }
#endif
#if POCKETPAN_ATTACK_VOICE_FASTPATH
    // Eight simultaneous attacks using the strike bus: the local strike taps
    // are summed in voice order and written to the strike buffer exactly as the
    // general path does.  Sample-outer sympathetic dependency is preserved.
    if (attackStable8()) {
        attackFastPathBlocks_ += kMaxVoices;
#if POCKETPAN_ATTACK_SEGMENT
        const uint32_t remaining = voices_[0].samplesUntilExciterInactive();
        bool equal = remaining > 0;
        for (size_t v = 1; v < kMaxVoices; ++v) {
            if (voices_[v].samplesUntilExciterInactive() != remaining) { equal = false; break; }
        }
        const size_t split = (equal && remaining < frames) ? remaining : frames;
#else
        const size_t split = frames;
#endif
        for (size_t i=0; i<frames; ++i) {
            const float external = sympathetic ? sympatheticPreviousBus_*config.inputGain : 0.0f;
            float sum=0.0f, strikes=0.0f, strike=0.0f;
#if POCKETPAN_ATTACK_SEGMENT
            if (i < split) {
                sum += voices_[0].processSampleAttackStable(external, &strike); strikes += strike;
                sum += voices_[1].processSampleAttackStable(external, &strike); strikes += strike;
                sum += voices_[2].processSampleAttackStable(external, &strike); strikes += strike;
                sum += voices_[3].processSampleAttackStable(external, &strike); strikes += strike;
                sum += voices_[4].processSampleAttackStable(external, &strike); strikes += strike;
                sum += voices_[5].processSampleAttackStable(external, &strike); strikes += strike;
                sum += voices_[6].processSampleAttackStable(external, &strike); strikes += strike;
                sum += voices_[7].processSampleAttackStable(external, &strike); strikes += strike;
            } else {
                sum += voices_[0].processSampleSustain(external, &strike); strikes += strike;
                sum += voices_[1].processSampleSustain(external, &strike); strikes += strike;
                sum += voices_[2].processSampleSustain(external, &strike); strikes += strike;
                sum += voices_[3].processSampleSustain(external, &strike); strikes += strike;
                sum += voices_[4].processSampleSustain(external, &strike); strikes += strike;
                sum += voices_[5].processSampleSustain(external, &strike); strikes += strike;
                sum += voices_[6].processSampleSustain(external, &strike); strikes += strike;
                sum += voices_[7].processSampleSustain(external, &strike); strikes += strike;
            }
#else
            sum += voices_[0].processSampleAttackStable(external, &strike); strikes += strike;
            sum += voices_[1].processSampleAttackStable(external, &strike); strikes += strike;
            sum += voices_[2].processSampleAttackStable(external, &strike); strikes += strike;
            sum += voices_[3].processSampleAttackStable(external, &strike); strikes += strike;
            sum += voices_[4].processSampleAttackStable(external, &strike); strikes += strike;
            sum += voices_[5].processSampleAttackStable(external, &strike); strikes += strike;
            sum += voices_[6].processSampleAttackStable(external, &strike); strikes += strike;
            sum += voices_[7].processSampleAttackStable(external, &strike); strikes += strike;
#endif
            if (sympathetic) {
                sympatheticFilterState_=(1.0f-sympatheticLowpassCoefficient_)*sum+sympatheticLowpassCoefficient_*sympatheticFilterState_;
                float next=sympatheticFilterState_*config.feedbackGain;
                const float limit=std::max(0.0f,config.maxBusLevel);
                if(limit>0.0f && std::abs(next)>limit) { next=std::copysign(limit,next); ++sympatheticSafetyCount_; }
                sympatheticPreviousBus_=next;
                sympatheticBusPeak_=std::max(sympatheticBusPeak_,std::abs(next)); sympatheticBusSumSquares_+=next*next; ++sympatheticBusSamples_;
            }
            outBuffer[i]=sum;
            strikeBuffer[i]=strikes;
        }
        return;
    }
#endif
    for (size_t i=0; i<frames; ++i) {
        float sum=0.0f, strikes=0.0f;
        const float external = sympathetic ? sympatheticPreviousBus_*config.inputGain : 0.0f;
        for (size_t v=0; v<kMaxVoices; ++v) if (voices_[v].isActive()) {
            float strike=0.0f;
#if POCKETPAN_SUSTAIN_FASTPATH
            if (sustainSafe[v]) sum += voices_[v].processSampleSustain(external, &strike);
            else
#endif
            sum += voices_[v].processSample(external, &strike);
            strikes += strike;
        }
        for(auto& tail:stealTails_) if(tail.active && tail.samplesLeft) {
            sum+=tail.currentSample; tail.currentSample-=tail.step;
            if(--tail.samplesLeft==0) tail.active=false;
        }
        if (sympathetic) {
            sympatheticFilterState_=(1.0f-sympatheticLowpassCoefficient_)*sum+sympatheticLowpassCoefficient_*sympatheticFilterState_;
            float next=sympatheticFilterState_*config.feedbackGain;
            const float limit=std::max(0.0f,config.maxBusLevel);
            if(limit>0.0f && std::abs(next)>limit) { next=std::copysign(limit,next); ++sympatheticSafetyCount_; }
            sympatheticPreviousBus_=next;
            sympatheticBusPeak_=std::max(sympatheticBusPeak_,std::abs(next)); sympatheticBusSumSquares_+=next*next; ++sympatheticBusSamples_;
        }
        outBuffer[i]=sum;
        strikeBuffer[i]=strikes;
    }
}

void VoiceAllocator::setInternalSafetySaturation(bool enabled) {
    for (auto& voice : voices_) voice.setInternalSafetySaturation(enabled);
}

void VoiceAllocator::setModelConfig(const InstrumentModelConfig& config) {
    for (auto& voice : voices_) voice.setModelConfig(config);
}

#if POCKETPAN_PAN_STABLE8_FASTPATH
bool VoiceAllocator::steady8(const bool* sustainSafe) const {
    for (size_t v = 0; v < kMaxVoices; ++v) {
        if (!sustainSafe[v]) return false;
    }
    for (const auto& tail : stealTails_) {
        if (tail.active) return false;
    }
    return true;
}
#endif

#if POCKETPAN_ATTACK_VOICE_FASTPATH
bool VoiceAllocator::attackStable8() const {
    for (size_t v = 0; v < kMaxVoices; ++v) {
        if (!voices_[v].isAttackFastPathEnabledForTest() || !voices_[v].isAttackSafe()) return false;
    }
    for (const auto& tail : stealTails_) {
        if (tail.active) return false;
    }
    return true;
}
#endif

void VoiceAllocator::setSympatheticConfig(const SympatheticConfig& config) {
    // Configuration boundary only.  Same expression, same float result as the
    // historical in-block computation, so the cached coefficient is exact.
    const float cutoff = std::clamp(config.lowpassHz, 10.0f, sampleRate_ * 0.45f);
    sympatheticLowpassCoefficient_ = std::exp(-2.0f * 3.14159265358979323846f * cutoff / sampleRate_);
}

void VoiceAllocator::setPanConfigsForTest(const ExciterConfig& exciter, const PanVoicingConfig& voicing) {
    for (auto& voice : voices_) voice.setPanConfigsForTest(exciter, voicing);
#if POCKETPAN_PREPARED_NOTE_CACHE
    // Test-injected voicing is deliberately not one of the two canonical
    // tables prepared at boot.
    preparedNoteTable_ = nullptr;
#endif
}

#if POCKETPAN_PREPARED_NOTE_CACHE
void VoiceAllocator::preparePreparedNoteTable(const InstrumentModelConfig& config,
                                              PreparedNoteTable& table) const {
    table.reset();
    ModalVoice preparer;
    preparer.init(sampleRate_);
    preparer.setModelConfig(config);
    for (uint16_t note = kPreparedNoteFirst; note <= kPreparedNoteLast; ++note) {
        PreparedNote& entry = table.entries[note - kPreparedNoteFirst];
        if (!preparer.prepareNote(static_cast<uint8_t>(note),
                                  midi::MidiMapping::noteToHz(static_cast<uint8_t>(note)), entry)) {
            return;
        }
    }
    table.ready = true;
}
#endif

size_t VoiceAllocator::getActiveVoiceCount() const {
    size_t count = 0;
    for (size_t i = 0; i < kMaxVoices; ++i) {
        if (voices_[i].isActive()) count++;
    }
    return count;
}

} // namespace pocketpan::dsp
