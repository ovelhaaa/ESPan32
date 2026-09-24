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
        if (tone != renderedTone_) {
            // A source change starts at a deterministic zero crossing.  The expensive
            // trigonometry below is consequently outside the per-sample loop.
            oscCos_ = 1.0f;
            oscSin_ = 0.0f;
            renderedTone_ = tone;
        }
        const float hz = tone == DiagnosticTone::Sine440 ? 440.0f : 1000.0f;
        const float gain = tone == DiagnosticTone::Sine1kMinus12 ? 0.2511886f : 0.50f;
        const float step = 6.28318530718f * hz / sampleRate;
        const float stepCos = std::cos(step);
        const float stepSin = std::sin(step);
        for (size_t i = 0; i < frames; ++i) {
            const float v = tone == DiagnosticTone::Silence ? 0.0f : oscSin_ * gain;
            int32_t s = static_cast<int32_t>(v * 2147483647.0f);
            out[i * 2] = tone == DiagnosticTone::RightOnly ? 0 : s;
            out[i * 2 + 1] = tone == DiagnosticTone::LeftOnly ? 0 : s;
            // Recursive oscillator: no std::sin() or std::cos() per sample.
            const float nextCos = oscCos_ * stepCos - oscSin_ * stepSin;
            oscSin_ = oscSin_ * stepCos + oscCos_ * stepSin;
            oscCos_ = nextCos;
            // Bound recursive-oscillator numerical drift without per-sample sqrt.
            if ((++sampleCounter_ & 0xFFFu) == 0) {
                const float normSq = oscCos_ * oscCos_ + oscSin_ * oscSin_;
                if (normSq > 0.0f) {
                    const float norm = 1.0f / std::sqrt(normSq);
                    oscCos_ *= norm;
                    oscSin_ *= norm;
                }
            }
        }
    }
private:
    std::atomic<uint8_t> tone_{static_cast<uint8_t>(DiagnosticTone::Pan)};
    DiagnosticTone renderedTone_ = DiagnosticTone::Pan;
    float oscCos_ = 1.0f;
    float oscSin_ = 0.0f;
    uint32_t sampleCounter_ = 0;
};
} // namespace pocketpan::dsp
