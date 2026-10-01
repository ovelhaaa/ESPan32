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

constexpr ModalVoicingConfig kKalimbaVoicing{
    0.30f, 0.95f, 1.05f, 0.92f, 1.00f, 0.80f, 0.15f, 0.90f,
    1.15f, 0.80f, 0.0f, 146.83f, 440.0f, false,
    {1.00f, 0.18f, 0.05f, 0.01f, 0.00f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {1.00f, 0.65f, 0.42f, 0.22f, 0.10f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, 1.0e-8f};

constexpr InstrumentModelConfig kKalimbaModelConfig{
    InstrumentModel::Kalimba, &kPresetKalimba,
    {0.78f, 0.22f, 1400.0f, 11000.0f, 0.85f, 0.42f, ExciterShape::Pluck},
    {1.0f, 0.95f, true}, kKalimbaVoicing,
    {{{210.0f, 0.15f, 0.35f}, {420.0f, 0.10f, 0.22f}, {820.0f, 0.05f, 0.12f}}, 3, 0.12f, 0.08f, 1200.0f, true},
    {false, 0.0f, 0.0f, 1500.0f, 0.0f},
    BodyExcitationStrategy::StrikeBus, 32.0f,
};

constexpr ModalVoicingConfig kGlassVoicing{
    0.60f, 0.98f, 1.02f, 0.92f, 1.00f, 0.85f, 0.15f, 0.90f,
    1.15f, 0.78f, 0.0f, 146.83f, 440.0f, false,
    {1.00f, 0.32f, 0.16f, 0.05f, 0.01f, 0.00f, 0.0f, 0.0f, 0.0f, 0.0f},
    {1.00f, 0.80f, 0.65f, 0.50f, 0.32f, 0.18f, 0.0f, 0.0f, 0.0f, 0.0f}, 1.0e-8f};

constexpr InstrumentModelConfig kGlassModelConfig{
    InstrumentModel::Glass, &kPresetGlass,
    {0.72f, 0.08f, 2400.0f, 16000.0f, 0.85f, 0.40f, ExciterShape::Strike},
    {1.0f, 0.95f, true}, kGlassVoicing,
    {{}, 0, 0.0f, 0.0f, 1000.0f, false},
    {false, 0.0f, 0.0f, 1500.0f, 0.0f},
    BodyExcitationStrategy::StrikeBus, 0.0f,
};
constexpr ModalVoicingConfig kMarimbaVoicing{
    .12f, .65f, 1.04f, .94f, 1.0f, .78f, .12f, .85f,
    1.25f, .80f, 0.0f, 146.83f, 587.33f, false,
    {1.0f, .42f, .18f, .06f, .02f, .005f},
    {1.0f, .92f, .72f, .48f, .28f, .12f}, 1.0e-8f};

// Candidate A is the provisional runtime configuration; listening decides freeze.
constexpr InstrumentModelConfig kMarimbaModelConfig{
    InstrumentModel::Marimba, &kPresetMarimba,
    {.20f, .12f, 650.0f, 6500.0f, .85f, .38f, ExciterShape::Strike},
    {1.0f, .95f, true}, kMarimbaVoicing,
    {{}, 0, 0.0f, 0.0f, 1000.0f, false},
    {false, 0.0f, 0.0f, 1500.0f, 0.0f},
    BodyExcitationStrategy::StrikeBus, 0.0f,
};
constexpr ModalVoicingConfig kVibraphoneVoicing{
    .10f, .85f, 1.02f, .94f, 1.0f, .85f, .20f, .90f,
    1.20f, .80f, 0.0f, 146.83f, 587.33f, false,
    {1.0f, .80f, .42f, .08f, .025f, .008f},
    {1.0f, .95f, .72f, .40f, .20f, .08f}, 1.0e-8f};
// Frozen VIBRAPHONE V1: selected C bar and M1 fundamental tube coupling.
constexpr InstrumentModelConfig kVibraphoneModelConfig{
    InstrumentModel::Vibraphone, &kPresetVibraphone,
    {.16f, .075f, 800.0f, 13500.0f, .85f, .38f, ExciterShape::Strike},
    {1.0f, .95f, true}, kVibraphoneVoicing,
    {{}, 0, 0.0f, 0.0f, 1000.0f, false},
    {false, 0.0f, 0.0f, 1500.0f, 0.0f},
    BodyExcitationStrategy::StrikeBus, 0.0f, .75f,
};
}

namespace {
constexpr ModalVoicingConfig kMbiraVoicing{
    .42f, 1.0f, 1.02f, .94f, .82f, 1.10f, .20f, .98f,
    1.25f, .65f, 0.0f, 146.83f, 587.33f, false,
    {1.0f, .10f, .025f, .006f, .002f, .001f},
    {1.0f, 1.0f, .90f, .70f, .55f, .42f}, 1.0e-8f};
// MBIRA V1 FROZEN: Candidate B + Buzz ON. Preserve this accepted voicing.
constexpr InstrumentModelConfig kMbiraModelConfig{
    InstrumentModel::Mbira, &kPresetMbira,
    {.15f, .06f, 1800.0f, 15500.0f, .90f, .55f, ExciterShape::Pluck},
    {1.0f, .95f, true}, kMbiraVoicing,
    {{{310.0f, .065f, .25f}, {730.0f, .035f, .12f}}, 2, .08f, .055f, 1500.0f, true},
    {false, 0.0f, 0.0f, 1500.0f, 0.0f},
    BodyExcitationStrategy::StrikeBus, 20.0f, 0.0f, 1.0f,
};
}

const InstrumentModelConfig& getInstrumentModelConfig(InstrumentModel model) {
    // Registry shell metadata only: UDU uses its independent hybrid air/body DSP.
    static constexpr ModalPreset shell={"UDU",4,{
        {4.38095f,.12f,.085f,0},{7.52381f,.075f,.055f,0},
        {11.71429f,.045f,.038f,0},{18.76190f,.025f,.025f,0}}};
    static constexpr InstrumentModelConfig udu={InstrumentModel::Udu,&shell,
        {.0016f,.045f,700,4200,1,1,ExciterShape::Strike},
        {1,.95f,false},{},{{},0,0,0,1500,false},
        {false,0,0,1500,0},BodyExcitationStrategy::FullMix,0};
    switch (model) {
        case InstrumentModel::Udu: return udu;
        case InstrumentModel::Bell: return kBellModelConfig;
        case InstrumentModel::Tongue: return kTongueModelConfig;
        case InstrumentModel::Bowl: return kBowlModelConfig;
        case InstrumentModel::Kalimba: return kKalimbaModelConfig;
        case InstrumentModel::Glass: return kGlassModelConfig;
        case InstrumentModel::Marimba: return kMarimbaModelConfig;
        case InstrumentModel::Vibraphone: return kVibraphoneModelConfig;
        case InstrumentModel::Mbira: return kMbiraModelConfig;
        case InstrumentModel::Pan:
        default: return kPanModelConfig;
    }
}

} // namespace pocketpan::dsp
