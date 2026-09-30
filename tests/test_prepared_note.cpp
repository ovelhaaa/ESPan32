// M6.3.2 PreparedNote cache qualification.
//
// This intentionally compares the production SynthEngine cache path against
// the same engine with only the host-only cache switch disabled.  It therefore
// covers the whole PCM path (voices, body, limiter, and conversion), instead
// of checking cache coefficients in isolation.

#include "dsp/dsp_profile.h"

#if !POCKETPAN_PREPARED_NOTE_CACHE

#include <iostream>

int main() {
    std::cout << "[SKIP] PreparedNote cache is only enabled for candidates 12 and 13.\n";
    return 0;
}

#else

#include "dsp/instrument_model.h"
#include "dsp/prepared_note.h"
#include "dsp/synth_engine.h"
#include "dsp/voice_allocator.h"
#include "midi/midi_event.h"
#include "midi/midi_mapping.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr size_t kSampleRate = 48000;
constexpr size_t kBlockFrames = 128;
constexpr size_t kGridFrames = 2048;
constexpr size_t kPressureFrames = 4096;

using pocketpan::dsp::InstrumentModel;
using pocketpan::dsp::SynthEngine;
using pocketpan::midi::MidiEvent;
using pocketpan::midi::MidiEventType;

struct TimedEvent {
    size_t frame = 0;
    MidiEvent event{};
};

struct RenderedPcm {
    std::vector<int32_t> samples;
    uint64_t fnv = 0;
};

MidiEvent noteOn(uint8_t note, uint8_t velocity) {
    MidiEvent event{};
    event.type = MidiEventType::NoteOn;
    event.data1 = note;
    event.data2 = velocity;
    return event;
}

MidiEvent polyPressure(uint8_t note, uint8_t pressure) {
    MidiEvent event{};
    event.type = MidiEventType::PolyPressure;
    event.data1 = note;
    event.data2 = pressure;
    return event;
}

MidiEvent channelPressure(uint8_t pressure) {
    MidiEvent event{};
    event.type = MidiEventType::ChannelPressure;
    event.data1 = pressure;
    return event;
}

uint64_t fnv1a64(const std::vector<int32_t>& pcm) {
    uint64_t hash = 14695981039346656037ULL;
    for (const int32_t sample : pcm) {
        const uint32_t value = static_cast<uint32_t>(sample);
        for (unsigned shift = 0; shift < 32; shift += 8) {
            hash ^= (value >> shift) & 0xffU;
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}

void appendRender(SynthEngine& engine, size_t frames, std::vector<int32_t>& output) {
    std::array<int32_t, kBlockFrames * 2> block{};
    for (size_t rendered = 0; rendered < frames; rendered += kBlockFrames) {
        const size_t count = std::min(kBlockFrames, frames - rendered);
        engine.renderBlock(block.data(), count);
        output.insert(output.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(count * 2));
    }
}

RenderedPcm renderEvents(InstrumentModel model, bool cacheEnabled,
                         const std::vector<TimedEvent>& events, size_t totalFrames) {
    assert(std::is_sorted(events.begin(), events.end(),
                          [](const TimedEvent& left, const TimedEvent& right) {
                              return left.frame < right.frame;
                          }));

    SynthEngine engine;
    engine.init(static_cast<float>(kSampleRate));
    engine.setInstrumentModel(model);
    engine.setPreparedNoteCacheEnabledForTest(cacheEnabled);
    assert(engine.getVoiceAllocator().isPreparedNoteCacheEnabledForTest() == cacheEnabled);

    RenderedPcm result;
    result.samples.reserve(totalFrames * 2);
    size_t eventIndex = 0;
    std::array<int32_t, kBlockFrames * 2> block{};
    for (size_t frame = 0; frame < totalFrames; frame += kBlockFrames) {
        while (eventIndex < events.size() && events[eventIndex].frame <= frame) {
            engine.handleMidiEvent(events[eventIndex].event);
            ++eventIndex;
        }
        const size_t count = std::min(kBlockFrames, totalFrames - frame);
        engine.renderBlock(block.data(), count);
        result.samples.insert(result.samples.end(), block.begin(),
                              block.begin() + static_cast<std::ptrdiff_t>(count * 2));
    }
    assert(eventIndex == events.size());
    assert(result.samples.size() == totalFrames * 2);
    result.fnv = fnv1a64(result.samples);
    return result;
}

[[noreturn]] void failPcmComparison(const std::string& name, const RenderedPcm& cached,
                                    const RenderedPcm& uncached) {
    std::cerr << "PreparedNote PCM mismatch: " << name
              << "\n  cached FNV:   0x" << std::hex << cached.fnv
              << "\n  uncached FNV: 0x" << uncached.fnv << std::dec << '\n';
    const size_t common = std::min(cached.samples.size(), uncached.samples.size());
    for (size_t i = 0; i < common; ++i) {
        if (cached.samples[i] != uncached.samples[i]) {
            std::cerr << "  first PCM difference at sample " << i << ": "
                      << cached.samples[i] << " vs " << uncached.samples[i] << '\n';
            break;
        }
    }
    std::abort();
}

uint64_t mixHash(uint64_t aggregate, uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        aggregate ^= (value >> shift) & 0xffU;
        aggregate *= 1099511628211ULL;
    }
    return aggregate;
}

uint64_t expectExactPcm(const std::string& name, InstrumentModel model,
                        const std::vector<TimedEvent>& events, size_t totalFrames) {
    const RenderedPcm cached = renderEvents(model, true, events, totalFrames);
    const RenderedPcm uncached = renderEvents(model, false, events, totalFrames);
    if (cached.fnv != uncached.fnv || cached.samples != uncached.samples) {
        failPcmComparison(name, cached, uncached);
    }
    return cached.fnv;
}

const char* modelName(InstrumentModel model) {
    switch (model) {
        case InstrumentModel::Bell: return "BELL";
        case InstrumentModel::Tongue: return "TONGUE";
        case InstrumentModel::Bowl: return "BOWL";
        case InstrumentModel::Kalimba: return "KALIMBA";
        case InstrumentModel::Pan:
        default: return "PAN";
    }
}

void testPreparedTableCoverage() {
    std::cout << "[PreparedNote] table coverage...\n";
    for (const InstrumentModel model : {InstrumentModel::Pan, InstrumentModel::Bell, InstrumentModel::Tongue, InstrumentModel::Bowl, InstrumentModel::Kalimba}) {
        pocketpan::dsp::VoiceAllocator allocator;
        allocator.init(static_cast<float>(kSampleRate));
        pocketpan::dsp::PreparedNoteTable table{};
        allocator.preparePreparedNoteTable(pocketpan::dsp::getInstrumentModelConfig(model), table);
        assert(table.ready);

        const uint8_t expectedModes = (model == InstrumentModel::Pan ? 8U : (model == InstrumentModel::Bell ? 10U : (model == InstrumentModel::Bowl ? 7U : (model == InstrumentModel::Kalimba ? 5U : 6U))));
        for (uint16_t note = pocketpan::dsp::kPreparedNoteFirst;
             note <= pocketpan::dsp::kPreparedNoteLast; ++note) {
            const uint8_t midiNote = static_cast<uint8_t>(note);
            const auto* entry = table.find(
                midiNote, pocketpan::midi::MidiMapping::noteToHz(midiNote));
            assert(entry != nullptr);
            assert(entry->midiNote == midiNote);
            assert(entry->modeCount == expectedModes);
        }

        // These are deliberately one note outside the bounded 24--96 table,
        // so the production lookup must retain the normal coefficient path.
        for (const uint8_t outside : {uint8_t(23), uint8_t(97)}) {
            assert(table.find(outside, pocketpan::midi::MidiMapping::noteToHz(outside)) == nullptr);
        }
    }
}

void testGrid() {
    std::cout << "[PreparedNote] PCM/FNV grid: 24--96 x 30/70/110/127...\n";
    constexpr std::array<uint8_t, 4> velocities = {30, 70, 110, 127};
    for (const InstrumentModel model : {InstrumentModel::Pan, InstrumentModel::Bell, InstrumentModel::Tongue, InstrumentModel::Bowl, InstrumentModel::Kalimba}) {
        uint64_t aggregate = 14695981039346656037ULL;
        size_t caseCount = 0;
        for (uint16_t note = pocketpan::dsp::kPreparedNoteFirst;
             note <= pocketpan::dsp::kPreparedNoteLast; ++note) {
            for (const uint8_t velocity : velocities) {
                const uint8_t midiNote = static_cast<uint8_t>(note);
                const std::string name = std::string(modelName(model)) + " grid note=" +
                    std::to_string(midiNote) + " velocity=" + std::to_string(velocity);
                const uint64_t hash = expectExactPcm(name, model,
                    {{0, noteOn(midiNote, velocity)}}, kGridFrames);
                aggregate = mixHash(aggregate, hash);
                ++caseCount;
            }
        }
        std::cout << "  " << modelName(model) << " " << caseCount << " exact cases, aggregate FNV 0x"
                  << std::hex << aggregate << std::dec << '\n';
    }
}

void testFallbacks() {
    std::cout << "[PreparedNote] below/above-table fallback...\n";
    for (const InstrumentModel model : {InstrumentModel::Pan, InstrumentModel::Bell, InstrumentModel::Tongue, InstrumentModel::Bowl, InstrumentModel::Kalimba}) {
        for (const uint8_t note : {uint8_t(23), uint8_t(97)}) {
            const std::string name = std::string(modelName(model)) +
                (note < pocketpan::dsp::kPreparedNoteFirst ? " low fallback" : " high fallback") +
                " note=" + std::to_string(note);
            expectExactPcm(name, model, {{0, noteOn(note, 110)}}, kGridFrames);
        }
    }
}

void testSameNoteRestrike() {
    std::cout << "[PreparedNote] same-note restrike...\n";
    for (const InstrumentModel model : {InstrumentModel::Pan, InstrumentModel::Bell, InstrumentModel::Tongue, InstrumentModel::Bowl, InstrumentModel::Kalimba}) {
        expectExactPcm(std::string(modelName(model)) + " same-note restrike", model,
                       {{0, noteOn(60, 70)}, {512, noteOn(60, 127)}}, kGridFrames);
    }
}

void testPolyPressure() {
    std::cout << "[PreparedNote] PolyPressure after cached trigger...\n";
    for (const InstrumentModel model : {InstrumentModel::Pan, InstrumentModel::Bell, InstrumentModel::Tongue, InstrumentModel::Bowl, InstrumentModel::Kalimba}) {
        expectExactPcm(std::string(modelName(model)) + " PolyPressure", model,
                       {{0, noteOn(60, 110)}, {512, polyPressure(60, 96)}}, kPressureFrames);
    }
}

void testChannelPressure() {
    std::cout << "[PreparedNote] ChannelPressure after cached triggers...\n";
    for (const InstrumentModel model : {InstrumentModel::Pan, InstrumentModel::Bell, InstrumentModel::Tongue, InstrumentModel::Bowl, InstrumentModel::Kalimba}) {
        expectExactPcm(std::string(modelName(model)) + " ChannelPressure", model,
                       {{0, noteOn(55, 70)}, {0, noteOn(64, 110)},
                        {512, channelPressure(88)}}, kPressureFrames);
    }
}

struct SwitchRender {
    RenderedPcm whole;
    RenderedPcm finalPan;
};

SwitchRender renderPanBellPan(bool cacheEnabled) {
    SynthEngine engine;
    engine.init(static_cast<float>(kSampleRate));
    engine.setPreparedNoteCacheEnabledForTest(cacheEnabled);
    assert(engine.getVoiceAllocator().isPreparedNoteCacheEnabledForTest() == cacheEnabled);

    SwitchRender result;
    result.whole.samples.reserve((512 + 512 + kGridFrames) * 2);

    engine.setInstrumentModel(InstrumentModel::Pan);
    engine.handleMidiEvent(noteOn(50, 70));
    appendRender(engine, 512, result.whole.samples);

    engine.setInstrumentModel(InstrumentModel::Bell);
    engine.handleMidiEvent(noteOn(62, 110));
    appendRender(engine, 512, result.whole.samples);

    engine.setInstrumentModel(InstrumentModel::Pan);
    engine.handleMidiEvent(noteOn(50, 70));
    appendRender(engine, kGridFrames, result.whole.samples);
    result.finalPan.samples.assign(result.whole.samples.end() - static_cast<std::ptrdiff_t>(kGridFrames * 2),
                                   result.whole.samples.end());
    result.whole.fnv = fnv1a64(result.whole.samples);
    result.finalPan.fnv = fnv1a64(result.finalPan.samples);
    return result;
}

void testPanBellPan() {
    std::cout << "[PreparedNote] PAN -> BELL -> PAN...\n";
    const SwitchRender cached = renderPanBellPan(true);
    const SwitchRender uncached = renderPanBellPan(false);
    if (cached.whole.fnv != uncached.whole.fnv || cached.whole.samples != uncached.whole.samples) {
        failPcmComparison("PAN -> BELL -> PAN", cached.whole, uncached.whole);
    }

    const RenderedPcm directPan = renderEvents(InstrumentModel::Pan, true,
                                                {{0, noteOn(50, 70)}}, kGridFrames);
    if (cached.finalPan.fnv != directPan.fnv || cached.finalPan.samples != directPan.samples) {
        failPcmComparison("PAN after BELL switch versus direct PAN", cached.finalPan, directPan);
    }
}

SwitchRender renderPanBellTonguePan(bool cacheEnabled) {
    SynthEngine engine;
    engine.init(static_cast<float>(kSampleRate));
    engine.setPreparedNoteCacheEnabledForTest(cacheEnabled);
    assert(engine.getVoiceAllocator().isPreparedNoteCacheEnabledForTest() == cacheEnabled);

    SwitchRender result;
    result.whole.samples.reserve((512 + 512 + 512 + kGridFrames) * 2);

    engine.setInstrumentModel(InstrumentModel::Pan);
    engine.handleMidiEvent(noteOn(50, 70));
    appendRender(engine, 512, result.whole.samples);

    engine.setInstrumentModel(InstrumentModel::Bell);
    engine.handleMidiEvent(noteOn(62, 110));
    appendRender(engine, 512, result.whole.samples);

    engine.setInstrumentModel(InstrumentModel::Tongue);
    engine.handleMidiEvent(noteOn(58, 90));
    appendRender(engine, 512, result.whole.samples);

    engine.setInstrumentModel(InstrumentModel::Pan);
    engine.handleMidiEvent(noteOn(50, 70));
    appendRender(engine, kGridFrames, result.whole.samples);
    result.finalPan.samples.assign(result.whole.samples.end() - static_cast<std::ptrdiff_t>(kGridFrames * 2),
                                   result.whole.samples.end());
    result.whole.fnv = fnv1a64(result.whole.samples);
    result.finalPan.fnv = fnv1a64(result.finalPan.samples);
    return result;
}

void testPanBellTonguePan() {
    std::cout << "[PreparedNote] PAN -> BELL -> TONGUE -> PAN...\n";
    const SwitchRender cached = renderPanBellTonguePan(true);
    const SwitchRender uncached = renderPanBellTonguePan(false);
    if (cached.whole.fnv != uncached.whole.fnv || cached.whole.samples != uncached.whole.samples) {
        failPcmComparison("PAN -> BELL -> TONGUE -> PAN", cached.whole, uncached.whole);
    }

    const RenderedPcm directPan = renderEvents(InstrumentModel::Pan, true,
                                                {{0, noteOn(50, 70)}}, kGridFrames);
    if (cached.finalPan.fnv != directPan.fnv || cached.finalPan.samples != directPan.samples) {
        failPcmComparison("PAN after TONGUE switch versus direct PAN", cached.finalPan, directPan);
    }
}

SwitchRender renderPanBellTongueBowlPan(bool cacheEnabled) {
    SynthEngine engine;
    engine.init(static_cast<float>(kSampleRate));
    engine.setPreparedNoteCacheEnabledForTest(cacheEnabled);
    assert(engine.getVoiceAllocator().isPreparedNoteCacheEnabledForTest() == cacheEnabled);

    SwitchRender result;
    result.whole.samples.reserve((512 + 512 + 512 + 512 + kGridFrames) * 2);

    engine.setInstrumentModel(InstrumentModel::Pan);
    engine.handleMidiEvent(noteOn(50, 70));
    appendRender(engine, 512, result.whole.samples);

    engine.setInstrumentModel(InstrumentModel::Bell);
    engine.handleMidiEvent(noteOn(62, 110));
    appendRender(engine, 512, result.whole.samples);

    engine.setInstrumentModel(InstrumentModel::Tongue);
    engine.handleMidiEvent(noteOn(58, 90));
    appendRender(engine, 512, result.whole.samples);

    engine.setInstrumentModel(InstrumentModel::Bowl);
    engine.handleMidiEvent(noteOn(60, 85));
    appendRender(engine, 512, result.whole.samples);

    engine.setInstrumentModel(InstrumentModel::Pan);
    engine.handleMidiEvent(noteOn(50, 70));
    appendRender(engine, kGridFrames, result.whole.samples);
    result.finalPan.samples.assign(result.whole.samples.end() - static_cast<std::ptrdiff_t>(kGridFrames * 2),
                                   result.whole.samples.end());
    result.whole.fnv = fnv1a64(result.whole.samples);
    result.finalPan.fnv = fnv1a64(result.finalPan.samples);
    return result;
}

void testPanBellTongueBowlPan() {
    std::cout << "[PreparedNote] PAN -> BELL -> TONGUE -> BOWL -> PAN...\n";
    const SwitchRender cached = renderPanBellTongueBowlPan(true);
    const SwitchRender uncached = renderPanBellTongueBowlPan(false);
    if (cached.whole.fnv != uncached.whole.fnv || cached.whole.samples != uncached.whole.samples) {
        failPcmComparison("PAN -> BELL -> TONGUE -> BOWL -> PAN", cached.whole, uncached.whole);
    }

    const RenderedPcm directPan = renderEvents(InstrumentModel::Pan, true,
                                                {{0, noteOn(50, 70)}}, kGridFrames);
    if (cached.finalPan.fnv != directPan.fnv || cached.finalPan.samples != directPan.samples) {
        failPcmComparison("PAN after BOWL switch versus direct PAN", cached.finalPan, directPan);
    }
}

SwitchRender renderPanBellTongueBowlKalimbaPan(bool cacheEnabled) {
    SynthEngine engine;
    engine.init(static_cast<float>(kSampleRate));
    engine.setPreparedNoteCacheEnabledForTest(cacheEnabled);
    assert(engine.getVoiceAllocator().isPreparedNoteCacheEnabledForTest() == cacheEnabled);

    SwitchRender result;
    result.whole.samples.reserve((512 + 512 + 512 + 512 + 512 + kGridFrames) * 2);

    engine.setInstrumentModel(InstrumentModel::Pan);
    engine.handleMidiEvent(noteOn(50, 70));
    appendRender(engine, 512, result.whole.samples);

    engine.setInstrumentModel(InstrumentModel::Bell);
    engine.handleMidiEvent(noteOn(62, 110));
    appendRender(engine, 512, result.whole.samples);

    engine.setInstrumentModel(InstrumentModel::Tongue);
    engine.handleMidiEvent(noteOn(58, 90));
    appendRender(engine, 512, result.whole.samples);

    engine.setInstrumentModel(InstrumentModel::Bowl);
    engine.handleMidiEvent(noteOn(60, 85));
    appendRender(engine, 512, result.whole.samples);

    engine.setInstrumentModel(InstrumentModel::Kalimba);
    engine.handleMidiEvent(noteOn(62, 95));
    appendRender(engine, 512, result.whole.samples);

    engine.setInstrumentModel(InstrumentModel::Pan);
    engine.handleMidiEvent(noteOn(50, 70));
    appendRender(engine, kGridFrames, result.whole.samples);
    result.finalPan.samples.assign(result.whole.samples.end() - static_cast<std::ptrdiff_t>(kGridFrames * 2),
                                   result.whole.samples.end());
    result.whole.fnv = fnv1a64(result.whole.samples);
    result.finalPan.fnv = fnv1a64(result.finalPan.samples);
    return result;
}

void testPanBellTongueBowlKalimbaPan() {
    std::cout << "[PreparedNote] PAN -> BELL -> TONGUE -> BOWL -> KALIMBA -> PAN...\n";
    const SwitchRender cached = renderPanBellTongueBowlKalimbaPan(true);
    const SwitchRender uncached = renderPanBellTongueBowlKalimbaPan(false);
    if (cached.whole.fnv != uncached.whole.fnv || cached.whole.samples != uncached.whole.samples) {
        failPcmComparison("PAN -> BELL -> TONGUE -> BOWL -> KALIMBA -> PAN", cached.whole, uncached.whole);
    }

    const RenderedPcm directPan = renderEvents(InstrumentModel::Pan, true,
                                                {{0, noteOn(50, 70)}}, kGridFrames);
    if (cached.finalPan.fnv != directPan.fnv || cached.finalPan.samples != directPan.samples) {
        failPcmComparison("PAN after KALIMBA switch versus direct PAN", cached.finalPan, directPan);
    }
}

void testPreparedTableResetAndSwitch() {
    std::cout << "[PreparedNote] in-place reset and model switch regression guard...\n";
    static pocketpan::dsp::PreparedNoteTable table;
    pocketpan::dsp::VoiceAllocator allocator;
    allocator.init(static_cast<float>(kSampleRate));

    // 1. Prepare PAN
    allocator.preparePreparedNoteTable(pocketpan::dsp::getInstrumentModelConfig(InstrumentModel::Pan), table);
    assert(table.ready);
    assert(table.find(60, pocketpan::midi::MidiMapping::noteToHz(60))->modeCount == 8);

    // 2. In-place reset
    table.reset();
    assert(!table.ready);
    assert(table.find(60, pocketpan::midi::MidiMapping::noteToHz(60)) == nullptr);

    // 3. Prepare BELL
    allocator.preparePreparedNoteTable(pocketpan::dsp::getInstrumentModelConfig(InstrumentModel::Bell), table);
    assert(table.ready);
    assert(table.find(60, pocketpan::midi::MidiMapping::noteToHz(60))->modeCount == 10);

    // 4. In-place reset and prepare TONGUE
    table.reset();
    assert(!table.ready);
    allocator.preparePreparedNoteTable(pocketpan::dsp::getInstrumentModelConfig(InstrumentModel::Tongue), table);
    assert(table.ready);
    assert(table.find(60, pocketpan::midi::MidiMapping::noteToHz(60))->modeCount == 6);

    // 5. In-place reset and prepare BOWL
    table.reset();
    assert(!table.ready);
    allocator.preparePreparedNoteTable(pocketpan::dsp::getInstrumentModelConfig(InstrumentModel::Bowl), table);
    assert(table.ready);
    assert(table.find(60, pocketpan::midi::MidiMapping::noteToHz(60))->modeCount == 7);

    // 6. In-place reset and prepare KALIMBA
    table.reset();
    assert(!table.ready);
    allocator.preparePreparedNoteTable(pocketpan::dsp::getInstrumentModelConfig(InstrumentModel::Kalimba), table);
    assert(table.ready);
    assert(table.find(60, pocketpan::midi::MidiMapping::noteToHz(60))->modeCount == 5);

    // 7. Verify stack safety guard
    static_assert(sizeof(pocketpan::dsp::PreparedNoteTable) > 4096, "PreparedNoteTable must be large");
}

void testPreparedNoteMemoryFootprint() {
    std::cout << "[PreparedNote] Memory footprint:\n";
    std::cout << "  sizeof(PreparedNote):      " << sizeof(pocketpan::dsp::PreparedNote) << " bytes\n";
    std::cout << "  sizeof(PreparedNoteTable): " << sizeof(pocketpan::dsp::PreparedNoteTable) << " bytes\n";
    std::cout << "  sizeof(SynthEngine):       " << sizeof(pocketpan::dsp::SynthEngine) << " bytes\n";
    std::cout << "  kMaxModesPerVoice:         " << pocketpan::dsp::kMaxModesPerVoice << "\n";
    std::cout << "  kPreparedNoteCount:        " << pocketpan::dsp::kPreparedNoteCount << " (MIDI "
              << static_cast<int>(pocketpan::dsp::kPreparedNoteFirst) << ".."
              << static_cast<int>(pocketpan::dsp::kPreparedNoteLast) << ")\n";

    static_assert(sizeof(pocketpan::dsp::PreparedNote) == 128, "PreparedNote size unexpected");
    static_assert(sizeof(pocketpan::dsp::PreparedNoteTable) == (sizeof(pocketpan::dsp::PreparedNote) * pocketpan::dsp::kPreparedNoteCount + 4),
                  "PreparedNoteTable size mismatch");
}

} // namespace

int main() {
    std::cout << "M6.3.2 PreparedNote cache host qualification (candidate "
              << POCKETPAN_DSP_CANDIDATE << ")\n";
    testPreparedNoteMemoryFootprint();
    testPreparedTableCoverage();
    testPreparedTableResetAndSwitch();
    testGrid();
    testFallbacks();
    testSameNoteRestrike();
    testPolyPressure();
    testChannelPressure();
    testPanBellPan();
    testPanBellTonguePan();
    testPanBellTongueBowlPan();
    testPanBellTongueBowlKalimbaPan();
    std::cout << "PreparedNote cache PCM/FNV qualification passed.\n";
    return 0;
}

#endif // POCKETPAN_PREPARED_NOTE_CACHE
