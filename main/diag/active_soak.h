#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <atomic>
#include "midi/midi_event.h"
#include "dsp/instrument_model.h"

namespace pocketpan::diag {

class ActiveSoakGenerator {
public:
    // Called from UI task loop on Core 1 (~30 Hz, every ~33 ms)
    template <size_t N>
    void tick(midi::SpscMidiQueue<N>& queue,
              std::atomic<dsp::InstrumentModel>& selectedModel,
              std::atomic<bool>& modelChangeRequested,
              char* presetName, size_t presetNameLen) {
        tickCount_++;

        // Model switching every 2250 ticks (~75 seconds): PAN -> BELL -> TONGUE -> PAN
        if (tickCount_ - lastModelSwitchTick_ >= 2250) {
            lastModelSwitchTick_ = tickCount_;
            const auto next = dsp::nextInstrumentModel(selectedModel.load(std::memory_order_acquire));
            selectedModel.store(next, std::memory_order_release);
            modelChangeRequested.store(true, std::memory_order_release);
            if (presetName && presetNameLen > 0) {
                snprintf(presetName, presetNameLen, "%s", dsp::instrumentModelName(next));
            }
        }

        // Process active notes for note-offs / poly pressure
        for (size_t i = 0; i < kMaxActiveNotes; ++i) {
            if (activeNotes_[i].active) {
                if (tickCount_ >= activeNotes_[i].offTick) {
                    // Send NoteOff
                    midi::MidiEvent offEv{};
                    offEv.type = midi::MidiEventType::NoteOff;
                    offEv.data1 = activeNotes_[i].note;
                    offEv.data2 = 0;
                    offEv.rawBytes[0] = 0x80;
                    offEv.rawBytes[1] = offEv.data1;
                    offEv.rawBytes[2] = 0;
                    queue.push(offEv);
                    activeNotes_[i].active = false;
                } else if (activeNotes_[i].polyPressure && (tickCount_ == activeNotes_[i].pressureTick)) {
                    // Send PolyPressure
                    midi::MidiEvent pressEv{};
                    pressEv.type = midi::MidiEventType::PolyPressure;
                    pressEv.data1 = activeNotes_[i].note;
                    pressEv.data2 = activeNotes_[i].pressureVal;
                    pressEv.rawBytes[0] = 0xA0;
                    pressEv.rawBytes[1] = pressEv.data1;
                    pressEv.rawBytes[2] = pressEv.data2;
                    queue.push(pressEv);
                }
            }
        }

        // Step musical sequence
        if (tickCount_ < nextStepTick_) return;

        stepIndex_ = (stepIndex_ + 1) % kTotalSteps;
        const auto& step = kSteps[stepIndex_];
        nextStepTick_ = tickCount_ + step.delayTicks;

        // ChannelPressure modulation if step specifies it
        if (step.channelPressure > 0) {
            midi::MidiEvent cpEv{};
            cpEv.type = midi::MidiEventType::ChannelPressure;
            cpEv.data1 = step.channelPressure;
            cpEv.rawBytes[0] = 0xD0;
            cpEv.rawBytes[1] = cpEv.data1;
            queue.push(cpEv);
        }

        // Inject notes for this step
        for (uint8_t n = 0; n < step.noteCount; ++n) {
            const uint8_t pitch = step.notes[n];
            const uint8_t vel = step.velocities[n];

            midi::MidiEvent onEv{};
            onEv.type = midi::MidiEventType::NoteOn;
            onEv.data1 = pitch;
            onEv.data2 = vel;
            onEv.rawBytes[0] = 0x90;
            onEv.rawBytes[1] = pitch;
            onEv.rawBytes[2] = vel;
            queue.push(onEv);

            // Register active note for note-off
            for (size_t i = 0; i < kMaxActiveNotes; ++i) {
                if (!activeNotes_[i].active || activeNotes_[i].note == pitch) {
                    activeNotes_[i].active = true;
                    activeNotes_[i].note = pitch;
                    activeNotes_[i].offTick = tickCount_ + step.durationTicks;
                    activeNotes_[i].polyPressure = (step.polyPressureVal > 0);
                    activeNotes_[i].pressureVal = step.polyPressureVal;
                    activeNotes_[i].pressureTick = tickCount_ + (step.durationTicks / 2);
                    break;
                }
            }
        }
    }

private:
    struct ActiveNote {
        bool active = false;
        uint8_t note = 0;
        uint32_t offTick = 0;
        bool polyPressure = false;
        uint8_t pressureVal = 0;
        uint32_t pressureTick = 0;
    };
    static constexpr size_t kMaxActiveNotes = 8;
    ActiveNote activeNotes_[kMaxActiveNotes]{};

    struct Step {
        uint8_t noteCount;
        uint8_t notes[8];
        uint8_t velocities[8];
        uint16_t delayTicks;     // Ticks until next step (~33 ms per tick)
        uint16_t durationTicks;  // Ticks until NoteOff
        uint8_t polyPressureVal; // >0 for PolyPressure
        uint8_t channelPressure; // >0 for ChannelPressure
    };

    // Realistic musical sequence covering:
    // - single notes, 2-note intervals, 4-note chords, 6-note chords, 8-note cluster
    // - restrikes, short rolls, NoteOff, PolyPressure, ChannelPressure
    // - Velocities: 30, 60, 90, 100, 110, 127 (predominance 60-110)
    // - Density: mostly 1-4 notes, occasional 6, occasional 8, ring-down / idle
    static constexpr Step kSteps[] = {
        // 1. Single note D3, v70, duration 400ms, wait 500ms
        { 1, {50}, {70}, 15, 12, 0, 0 },
        // 2. Single note A3, v90, duration 400ms, wait 400ms
        { 1, {57}, {90}, 12, 12, 0, 0 },
        // 3. 2-note interval D3+F4, v60+v90, duration 600ms, PolyPressure 50, wait 600ms
        { 2, {50, 65}, {60, 90}, 18, 18, 50, 0 },
        // 4. Single note C4, v100, duration 300ms, wait 300ms
        { 1, {60}, {100}, 9, 9, 0, 0 },
        // 5. Restrike: strike D4 at v60, then re-strike at v110 before note off
        { 1, {62}, {60}, 5, 15, 0, 0 },
        { 1, {62}, {110}, 15, 15, 0, 0 },
        // 6. 4-note chord D Kurd: 2 notes left hand + 2 notes right hand (66ms later), sustaining together:
        { 2, {50, 57}, {90, 90}, 2, 35, 0, 64 },
        { 2, {60, 65}, {90, 90}, 30, 35, 0, 64 },
        // 7. Soft single note E4, v30, ring-down 800ms
        { 1, {64}, {30}, 24, 24, 0, 0 },
        // 8. 2-note interval Bb3+D4, v100+v100, wait 500ms
        { 2, {58, 62}, {100, 100}, 15, 15, 0, 0 },
        // 9. Short roll on D3: 4 rapid strikes at 66ms cadence (2 ticks) with ascending velocities
        { 1, {50}, {60}, 2, 8, 0, 0 },
        { 1, {50}, {80}, 2, 8, 0, 0 },
        { 1, {50}, {100}, 2, 8, 0, 0 },
        { 1, {50}, {127}, 15, 20, 0, 0 },
        // 10. 4-note chord with PolyPressure: 2 notes left + 2 notes right:
        { 2, {50, 65}, {70, 90}, 2, 35, 75, 0 },
        { 2, {69, 72}, {100, 110}, 36, 35, 75, 0 },
        // 11. Idle / ring-down segment: wait 1500ms (~45 ticks)
        { 0, {}, {}, 45, 0, 0, 0 },
        // 12. 6-note chord: 3 pairs of 2 notes (66ms stride), sustaining together (6 active voices):
        { 2, {50, 57}, {90, 90}, 2, 50, 0, 0 },
        { 2, {58, 60}, {90, 90}, 2, 50, 0, 0 },
        { 2, {62, 65}, {90, 90}, 45, 50, 0, 0 },
        // 13. Idle / ring-down: wait 1200ms (~36 ticks)
        { 0, {}, {}, 36, 0, 0, 0 },
        // 14. Full 8-voice polyphonic passage: 4 pairs of 2 notes, all sustaining together (8 active voices):
        { 2, {50, 54}, {90, 100}, 2, 65, 0, 0 },
        { 2, {57, 60}, {100, 110}, 2, 65, 0, 0 },
        { 2, {62, 65}, {100, 110}, 2, 65, 0, 0 },
        { 2, {69, 72}, {110, 127}, 60, 65, 0, 80 },
        // 15. Extended ring-down into silence: 2000ms (~60 ticks)
        { 0, {}, {}, 60, 0, 0, 0 },
    };
    static constexpr size_t kTotalSteps = sizeof(kSteps) / sizeof(kSteps[0]);

    uint32_t tickCount_ = 0;
    uint32_t lastModelSwitchTick_ = 0;
    uint32_t nextStepTick_ = 0;
    size_t stepIndex_ = 0;
};

inline ActiveSoakGenerator gActiveSoakGenerator;

} // namespace pocketpan::diag
