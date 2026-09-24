#pragma once
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <atomic>

namespace pocketpan::dsp {
enum class DiagnosticTone : uint8_t { Silence, Sine440, Sine1k, Sine1kMinus12, LeftOnly, RightOnly, Pan };

class DiagnosticToneSource {
public:
    void setTone(DiagnosticTone tone) { tone_.store(static_cast<uint8_t>(tone), std::memory_order_release); }
    DiagnosticTone tone() const { return static_cast<DiagnosticTone>(tone_.load(std::memory_order_acquire)); }
    const char* name() const {
        static constexpr const char* kNames[] = {"SILENCE", "440 HZ", "1K HZ", "1K -12", "LEFT", "RIGHT", "PAN"};
        return kNames[static_cast<uint8_t>(tone())];
    }
    bool isPan() const { return tone() == DiagnosticTone::Pan; }
    void render(int32_t* out, size_t frames, float sampleRate) {
        const auto tone = this->tone();
        const float hz = tone == DiagnosticTone::Sine440 ? 440.0f : 1000.0f;
        const float gain = tone == DiagnosticTone::Sine1kMinus12 ? 0.2511886f : 0.50f;
        for (size_t i = 0; i < frames; ++i) {
            float v = tone == DiagnosticTone::Silence ? 0.0f : std::sin(phase_) * gain;
            phase_ += 6.28318530718f * hz / sampleRate;
            if (phase_ >= 6.28318530718f) phase_ -= 6.28318530718f;
            int32_t s = static_cast<int32_t>(v * 2147483647.0f);
            out[i * 2] = tone == DiagnosticTone::RightOnly ? 0 : s;
            out[i * 2 + 1] = tone == DiagnosticTone::LeftOnly ? 0 : s;
        }
    }
private:
    std::atomic<uint8_t> tone_{static_cast<uint8_t>(DiagnosticTone::Pan)};
    float phase_ = 0.0f;
};
} // namespace pocketpan::dsp
