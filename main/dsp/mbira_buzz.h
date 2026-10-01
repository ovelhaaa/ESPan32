#pragma once

#include <algorithm>
#include <cmath>

namespace pocketpan::dsp {

// One shared bridge contact, driven by the rendered tines, never fed back into
// them. Half-wave contact creates a short asymmetric rattle, not a waveshaper
// on the dry instrument. All coefficients are fixed and prepared at startup.
class MbiraBuzz {
public:
    void init(float sampleRate) {
        const float sr = std::max(1000.0f, sampleRate);
        highpass_ = 1.0f-std::exp(-6.283185307f * 700.0f / sr);
        dcPole_ = 1.0f-std::exp(-6.283185307f * 80.0f / sr);
        envelopePole_ = std::exp(-1.0f / (.035f * sr));
        const float r = std::exp(-6.907755279f / (.025f * sr));
        a1_ = 2.0f * r * std::cos(6.283185307f * 2350.0f / sr);
        a2_ = -r * r;
        inputGain_ = (1.0f-r) * 2.0f;
        reset();
    }
    void reset() { envelope_=low_=contactDc_=z1_=z2_=0.0f; }
    void strike(float velocity) {
        const float drive = std::clamp((velocity-.25f)/.75f, 0.0f, 1.0f);
        envelope_ = std::max(envelope_, drive * drive);
    }
    __attribute__((always_inline)) inline float process(float tine, float gain) {
        if (envelope_ < 1.0e-6f) { reset(); return 0.0f; }
        low_ += highpass_*(tine-low_);
        const float high = tine-low_;
        const float contact = .5f*(high + std::abs(high));
        contactDc_ += dcPole_*(contact-contactDc_);
        const float y = inputGain_*(contact-contactDc_) + a1_*z1_ + a2_*z2_;
        z2_=z1_; z1_=y;
        // Input DC rejection plus the engine's 20 Hz output guard avoid a
        // redundant second contact filter in this tiny hot path.
        const float out = y * envelope_ * gain;
        envelope_ *= envelopePole_;
        return out;
    }
private:
    float highpass_=0, dcPole_=0, envelopePole_=0, a1_=0, a2_=0, inputGain_=0;
    float envelope_=0, low_=0, contactDc_=0, z1_=0, z2_=0;
};

} // namespace pocketpan::dsp
