#pragma once

#include <cstdint>
#include "modal_preset.h"
#include "exciter.h"
#include "modal_resonator.h"
#include "body_resonator.h"
#include "sympathetic_config.h"

namespace pocketpan::dsp {

enum class InstrumentModel : uint8_t { Pan = 0, Bell = 1 };
enum class BodyExcitationStrategy : uint8_t { FullMix, Transient, StrikeBus };

// Shared musical controls for a pitch-dependent modal instrument.  A model
// supplies values once at selection time; the audio hot path reads no model
// branches or dynamic dispatch.
struct ModalVoicingConfig {
    float strikeHardnessMin = 0.18f;
    float strikeHardnessMax = 1.00f;
    float lowRegisterGain = 1.04f;
    float highRegisterGain = 0.94f;
    float lowRegisterBrightness = 1.00f;
    float highRegisterBrightness = 0.82f;
    float upperModeSoftVelocity = 0.18f;
    float upperModeHardVelocity = 0.94f;
    float t60LowRegisterScale = 1.10f;
    float t60HighRegisterScale = 0.90f;
    float splitBeatTargetHz = 1.00f;
    float registerLowHz = 146.83f;
    float registerHighHz = 440.00f;
    bool fixedHzSplit = true;
    float softModeCoupling[kMaxModesPerVoice] = {1.0f, .82f, .72f, .22f, .08f, .05f, .03f, .02f, 0.0f, 0.0f};
    float hardModeCoupling[kMaxModesPerVoice] = {1.0f, .94f, .88f, .78f, .70f, .62f, .54f, .46f, 0.0f, 0.0f};
    float silenceThreshold = 1.0e-7f;
};

struct InstrumentModelConfig {
    InstrumentModel id;
    const ModalPreset* modalPreset;
    ExciterConfig exciter;
    ResonatorConfig resonator;
    ModalVoicingConfig voicing;
    BodyConfig body;
    SympatheticConfig sympathetic;
    BodyExcitationStrategy bodyStrategy;
    float strikeBusGain;
};

const InstrumentModelConfig& getInstrumentModelConfig(InstrumentModel model);

} // namespace pocketpan::dsp
