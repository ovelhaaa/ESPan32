#pragma once

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <type_traits>
#ifndef POCKETPAN_PACKED_NOTE_CACHE
#define POCKETPAN_PACKED_NOTE_CACHE 1
#endif

#include "modal_mode.h"

namespace pocketpan::dsp {

// This is intentionally the useful playing range rather than a 128-note
// maximum table.  Notes outside it (and non-canonical external frequencies)
// retain the established coefficient path.
constexpr uint8_t kPreparedNoteFirst = 24;
constexpr uint8_t kPreparedNoteLast = 96;
constexpr size_t kPreparedNoteCount =
    static_cast<size_t>(kPreparedNoteLast - kPreparedNoteFirst + 1);

// Values here are invariant for one model, one MIDI note, 48 kHz, and the
// initial undamped NoteOn state.  Register and all velocity-dependent strike
// work deliberately stay on the normal trigger path; the table owns only the
// expensive modal-coefficient result.
struct PreparedNote {
    uint8_t midiNote = 0;
    uint8_t modeCount = 0;
    uint16_t activeMask = 0;

    float fundamentalFrequencyHz = 0.0f;
    float registerPosition = 0.0f;

    float a1[kMaxModesPerVoice]{};
    float a2[kMaxModesPerVoice]{};
    float modalAmplitude[kMaxModesPerVoice]{};

    bool matches(uint8_t note, float frequencyHz) const {
        return midiNote == note && fundamentalFrequencyHz == frequencyHz;
    }
};

struct PreparedNoteTable {
    PreparedNote entries[kPreparedNoteCount]{};
    bool ready = false;

    // Reset table in-place to avoid stack temporary allocations.
    void reset() {
        ready = false;
        for (auto& entry : entries) {
            entry = PreparedNote{};
        }
    }

    const PreparedNote* find(uint8_t note, float frequencyHz) const {
        if (!ready || note < kPreparedNoteFirst || note > kPreparedNoteLast) {
            return nullptr;
        }
        const PreparedNote& candidate =
            entries[static_cast<size_t>(note - kPreparedNoteFirst)];
        return candidate.matches(note, frequencyHz)
            ? &candidate : nullptr;
    }

    static constexpr size_t bytesPerModel() { return sizeof(PreparedNoteTable); }
};

// SAFETY GUARD (M6.3.8 / M6.3.9): PreparedNoteTable is ~10.5 KB, exceeding
// the ESP-IDF main task stack (typically 8,192 bytes). Never allocate
// PreparedNoteTable as a local stack variable or automatic temporary!
// Always allocate as static, member of SynthEngine in BSS/heap, or reset in-place.
static_assert(sizeof(PreparedNoteTable) > 4096,
              "PreparedNoteTable is large; must never be allocated on the stack");

// M8: preserve every coefficient bit; omit only slots beyond the model width.
// All owners are SynthEngine members in internal BSS and prepared before I2S.
struct PackedNoteHeader {
    uint8_t midiNote=0, modeCount=0;
    uint16_t activeMask=0;
    float fundamentalFrequencyHz=0, registerPosition=0;
};
static_assert(sizeof(PackedNoteHeader)==12 && alignof(PackedNoteHeader)==alignof(float));
constexpr uint32_t kPreparedNoteCanary=0x50414e32;
struct PackedNoteView {
    const PackedNoteHeader* headers=nullptr;
    const float* a1=nullptr;
    const float* a2=nullptr;
    const float* amplitude=nullptr;
    const bool* ready=nullptr;
    const uint32_t* before=nullptr;
    const uint32_t* after=nullptr;
    uint8_t width=0;
    bool guarded() const {
        return before && after && *before==kPreparedNoteCanary && *after==kPreparedNoteCanary;
    }
    bool load(uint8_t note,float hz,PreparedNote& dst) const {
        if(!ready || !*ready || !guarded() || width==0 || width>kMaxModesPerVoice ||
            note<kPreparedNoteFirst || note>kPreparedNoteLast) return false;
        const size_t index=note-kPreparedNoteFirst;
        const auto& h=headers[index];
        if(h.midiNote!=note || h.fundamentalFrequencyHz!=hz || h.modeCount!=width) return false;
        dst=PreparedNote{}; // bounded 132-byte scratch, never a table temporary
        dst.midiNote=h.midiNote;dst.modeCount=h.modeCount;dst.activeMask=h.activeMask;
        dst.fundamentalFrequencyHz=h.fundamentalFrequencyHz;dst.registerPosition=h.registerPosition;
        for(unsigned m=0;m<width;++m) {
            const size_t i=index*width+m;
            dst.a1[m]=a1[i];dst.a2[m]=a2[i];dst.modalAmplitude[m]=amplitude[i];
        }
        return true;
    }
};
template<size_t N> struct PackedPreparedNoteTable {
    static_assert(N>0 && N<=kMaxModesPerVoice);
    uint32_t guardBefore=kPreparedNoteCanary;
    PackedNoteHeader headers[kPreparedNoteCount]{};
    float a1[kPreparedNoteCount*N]{},a2[kPreparedNoteCount*N]{},amplitude[kPreparedNoteCount*N]{};
    bool ready=false;
    uint32_t guardAfter=kPreparedNoteCanary;
    void reset() {
        ready=false;
        for(auto& h:headers) h=PackedNoteHeader{};
        std::fill(a1,a1+kPreparedNoteCount*N,0);
        std::fill(a2,a2+kPreparedNoteCount*N,0);
        std::fill(amplitude,amplitude+kPreparedNoteCount*N,0);
    }
    PackedNoteView view() const {
        return {headers,a1,a2,amplitude,&ready,&guardBefore,&guardAfter,uint8_t(N)};
    }
    bool guarded() const { return view().guarded(); }
};
constexpr size_t kPackedPreparedNoteTotalBytes=
    sizeof(PackedPreparedNoteTable<8>)+sizeof(PackedPreparedNoteTable<10>)+
    sizeof(PackedPreparedNoteTable<7>)+sizeof(PackedPreparedNoteTable<5>)+
    5*sizeof(PackedPreparedNoteTable<6>);
static_assert(kPackedPreparedNoteTotalBytes==60552,"M8 packed storage budget drift");
static_assert(sizeof(PackedPreparedNoteTable<5>)>4096,"Packed owners must never be stack temporaries");
// Diagnostic rollback for physical before/after event measurements only.
struct GuardedFullPreparedNoteTable {
    uint32_t guardBefore=kPreparedNoteCanary;
    PreparedNoteTable table{};
    uint32_t guardAfter=kPreparedNoteCanary;
    bool guarded() const { return guardBefore==kPreparedNoteCanary && guardAfter==kPreparedNoteCanary; }
};
template<size_t N> using InstrumentPreparedNotes=std::conditional_t<
    POCKETPAN_PACKED_NOTE_CACHE,PackedPreparedNoteTable<N>,GuardedFullPreparedNoteTable>;
constexpr size_t kInstrumentPreparedNoteTotalBytes=POCKETPAN_PACKED_NOTE_CACHE?
    kPackedPreparedNoteTotalBytes:9*sizeof(GuardedFullPreparedNoteTable);

} // namespace pocketpan::dsp
