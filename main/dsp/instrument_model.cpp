#include "instrument_model.h"
#include "pan_calibration.h"

namespace pocketpan::dsp {

namespace {
constexpr ModalVoicingConfig kPanVoicing{};
constexpr ModalVoicingConfig kBellVoicing{
    .30f, 1.00f, 1.0f, 1.0f, 1.0f, .95f, .18f, .94f,
    1.10f, .85f, 0.0f, 146.83f, 440.0f, false,
    {.55f, 1.0f, .12f, .28f, .06f, .50f, .06f, .12f, .05f, .02f},
    {.45f, 1.0f, .25f, .70f, .28f, .90f, .20f, .68f, .45f, .26f}, 1.0e-9f};

constexpr InstrumentModelConfig kPanModelConfig{
    InstrumentModel::Pan, &kPresetPan, kPanExciterConfig, kPanResonatorConfig,
    kPanVoicing, kPanBodyConfig.body, kPanBodyConfig.sympathetic,
    BodyExcitationStrategy::StrikeBus, kPanBodyConfig.strikeBusGain,
};

constexpr InstrumentModelConfig kBellModelConfig{
    InstrumentModel::Bell, &kPresetBell,
    {0.80f, 0.70f, 900.0f, 14000.0f, 0.85f, 0.35f},
    {1.0f, 0.95f, true}, kBellVoicing,
    {{}, 0, 0.0f, 0.0f, 1000.0f, false},
    {false, 0.0f, 0.0f, 1500.0f, 0.0f},
    BodyExcitationStrategy::StrikeBus, 0.0f,
};
}

const InstrumentModelConfig& getInstrumentModelConfig(InstrumentModel model) {
    return model == InstrumentModel::Bell ? kBellModelConfig : kPanModelConfig;
}

} // namespace pocketpan::dsp
