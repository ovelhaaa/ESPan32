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

constexpr ModalVoicingConfig kTongueVoicing{
    0.25f, 0.95f, 1.02f, 0.95f, 1.00f, 0.85f, 0.15f, 0.90f,
    1.15f, 0.85f, 0.0f, 146.83f, 440.0f, false,
    {1.00f, 0.35f, 0.15f, 0.05f, 0.02f, 0.00f, 0.0f, 0.0f, 0.0f, 0.0f},
    {1.00f, 0.80f, 0.65f, 0.45f, 0.30f, 0.18f, 0.0f, 0.0f, 0.0f, 0.0f}, 1.0e-8f};

constexpr InstrumentModelConfig kTongueModelConfig{
    InstrumentModel::Tongue, &kPresetTongue,
    {0.78f, 0.42f, 750.0f, 8500.0f, 0.85f, 0.38f},
    {1.0f, 0.95f, true}, kTongueVoicing,
    {{}, 0, 0.0f, 0.0f, 1000.0f, false},
    {false, 0.0f, 0.0f, 1500.0f, 0.0f},
    BodyExcitationStrategy::StrikeBus, 0.0f,
};

constexpr ModalVoicingConfig kBowlVoicing{
    0.20f, 0.90f, 1.05f, 0.92f, 1.00f, 0.80f, 0.15f, 0.90f,
    1.15f, 0.85f, 0.70f, 146.83f, 440.0f, true,
    {1.00f, 0.30f, 0.22f, 0.08f, 0.03f, 0.01f, 0.00f, 0.0f, 0.0f, 0.0f},
    {1.00f, 0.70f, 0.65f, 0.50f, 0.35f, 0.20f, 0.10f, 0.0f, 0.0f, 0.0f}, 1.0e-8f};

constexpr InstrumentModelConfig kBowlModelConfig{
    InstrumentModel::Bowl, &kPresetBowl,
    {0.76f, 0.30f, 600.0f, 9000.0f, 0.85f, 0.36f},
    {1.0f, 0.95f, true}, kBowlVoicing,
    {{}, 0, 0.0f, 0.0f, 1000.0f, false},
    {false, 0.0f, 0.0f, 1500.0f, 0.0f},
    BodyExcitationStrategy::StrikeBus, 0.0f,
};
}

const InstrumentModelConfig& getInstrumentModelConfig(InstrumentModel model) {
    switch (model) {
        case InstrumentModel::Bell: return kBellModelConfig;
        case InstrumentModel::Tongue: return kTongueModelConfig;
        case InstrumentModel::Bowl: return kBowlModelConfig;
        case InstrumentModel::Pan:
        default: return kPanModelConfig;
    }
}

} // namespace pocketpan::dsp
