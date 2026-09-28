// M6.3.7 Phase G trigger-precompute differential harness.
//
// Proves the exact MIDI-velocity lookup tables and the cached register-domain
// logarithms are bit-identical to the historical libm/lerp expressions.  The
// engine-level PCM golden (test_dsp kPanGolden) independently proves that
// applying these precomputed values does not change the rendered audio.
#include "dsp/dsp_profile.h"
#include "dsp/exciter.h"
#include "dsp/instrument_model.h"
#include "dsp/modal_voice.h"
#include "dsp/prepared_note.h"
#include "dsp/trigger_precompute.h"
#include "midi/midi_mapping.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace pocketpan::dsp;

namespace {

void checkVelocityTables() {
#if POCKETPAN_VELOCITY_LUT
    const float* table = velocityPow115Table();
    for (int i = 0; i < kMidiVelocityCount; ++i) {
        const float v = static_cast<float>(i) / 127.0f;
        const float expected = std::pow(v, 1.15f);
        if (!sameFloatBits(table[i], expected)) {
            std::fprintf(stderr, "pow115 table mismatch at %d\n", i);
            assert(false);
        }
        assert(exactMidiVelocityIndex(v) == i);
        if (i > 0) {
            assert(exactMidiVelocityIndex(v + 1.0f / 254.0f) == -1);
        }
    }

    const InstrumentModel models[] = {InstrumentModel::Pan, InstrumentModel::Bell};
    for (InstrumentModel model : models) {
        const ExciterConfig cfg = getInstrumentModelConfig(model).exciter;
        for (int i = 0; i < kMidiVelocityCount; ++i) {
            const float vel = static_cast<float>(i) / 127.0f;
            Exciter lut, ref;
            lut.init(48000.0f);
            ref.init(48000.0f);
            lut.setConfig(cfg);
            ref.setConfig(cfg);
            lut.setVelocityLutEnabledForTest(true);
            ref.setVelocityLutEnabledForTest(false);
            lut.trigger(vel, 0.5f, 1.0f);
            ref.trigger(vel, 0.5f, 1.0f);
            for (uint32_t s = 0; s < 600; ++s) {
                const float x = lut.processSample();
                const float y = ref.processSample();
                if (!sameFloatBits(x, y)) {
                    std::fprintf(stderr, "exciter LUT mismatch model=%d v=%d sample=%u\n",
                                 static_cast<int>(model), i, s);
                    assert(false);
                }
                if (lut.isActive() != ref.isActive()) {
                    std::fprintf(stderr, "exciter active mismatch model=%d v=%d\n",
                                 static_cast<int>(model), i);
                    assert(false);
                }
            }
        }
    }
    std::fprintf(stderr, "velocity LUT: pow115 + exciter strike exact for 128 velocities x 2 models\n");
#endif
}

void checkRegisterLogCache() {
#ifdef POCKETPAN_TRIGGER_DIFFERENTIAL_TEST
    const InstrumentModel models[] = {InstrumentModel::Pan, InstrumentModel::Bell};
    for (InstrumentModel model : models) {
        const InstrumentModelConfig cfg = getInstrumentModelConfig(model);
        const auto& v = cfg.voicing;
        const float lo = std::log(v.registerLowHz);
        const float hi = std::log(v.registerHighHz);
        ModalVoice voice;
        voice.init(48000.0f);
        voice.setModelConfig(cfg);
        for (uint8_t note = kPreparedNoteFirst; note <= kPreparedNoteLast; ++note) {
            const float freq = pocketpan::midi::MidiMapping::noteToHz(note);
            const float expected = std::clamp(
                (std::log(std::max(freq, 1.0f)) - lo) / (hi - lo), 0.0f, 1.0f);
            assert(sameFloatBits(voice.registerPositionForTest(freq), expected));
        }
    }
    std::fprintf(stderr, "register log cache: exact for PAN/BELL notes %u..%u\n",
                 (unsigned)kPreparedNoteFirst, (unsigned)kPreparedNoteLast);
#endif
}

} // namespace

int main() {
    checkVelocityTables();
    checkRegisterLogCache();
    std::fprintf(stderr, "m637 trigger precompute: all exact\n");
    return 0;
}
