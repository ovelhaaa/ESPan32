#include "body_resonator.h"
#include <algorithm>
#include <cmath>

namespace pocketpan::dsp {
namespace { constexpr float kPi=3.14159265358979323846f, kLn001=-6.907755278982137f; }

void BodyResonator::init(float sampleRate) { sampleRate_ = sampleRate > 1000.0f ? sampleRate : 48000.0f; updateCoefficients(); reset(); }
void BodyResonator::reset() { lowpassState_=0.0f; for (auto& m:modes_) m.z1=m.z2=0.0f; }
void BodyResonator::setConfig(const BodyConfig& config) { config_=config; config_.modeCount=std::min<uint8_t>(config_.modeCount,kMaxBodyModes); updateCoefficients(); reset(); }
void BodyResonator::updateCoefficients() {
    const float cutoff=std::clamp(config_.lowpassHz, 10.0f, sampleRate_*0.45f);
    lowpassCoefficient_=std::exp(-2.0f*kPi*cutoff/sampleRate_);
    for(size_t i=0;i<kMaxBodyModes;++i) {
        auto& s=modes_[i]; const auto& d=config_.modes[i];
        const float hz=std::clamp(d.frequencyHz,10.0f,sampleRate_*0.45f);
        const float r=std::clamp(std::exp(kLn001/(std::max(0.005f,d.t60Seconds)*sampleRate_)),0.00001f,0.99999f);
        s.a1=2.0f*r*std::cos(2.0f*kPi*hz/sampleRate_); s.a2=-r*r;
        s.gain=std::sin(2.0f*kPi*hz/sampleRate_)*std::max(0.0f,d.gain);
    }
}
float BodyResonator::processSample(float excitation) {
    if(!config_.enabled) return 0.0f;
    const float x=excitation*config_.excitationGain;
    lowpassState_=(1.0f-lowpassCoefficient_)*x+lowpassCoefficient_*lowpassState_;
    float out=0.0f;
    for(size_t i=0;i<config_.modeCount;++i) { auto& m=modes_[i]; const float y=m.gain*lowpassState_+m.a1*m.z1+m.a2*m.z2; m.z2=m.z1; m.z1=std::abs(y)<1e-15f?0.0f:y; out+=m.z1; }
    return out*config_.outputGain;
}
float BodyResonator::getEnergy() const { float e=0; for(size_t i=0;i<config_.modeCount;++i) e+=modes_[i].z1*modes_[i].z1+modes_[i].z2*modes_[i].z2; return e; }
} // namespace pocketpan::dsp
