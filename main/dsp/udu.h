#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pocketpan::dsp {

// UDU is a reduced air/body model, independent of the pitched-bar bank.
enum class UduCandidate : uint8_t { Deep, Balanced, Dry };
enum class UduOpening : uint8_t { Open, Partial, Closed };
struct UduConfig {
    float cavityHz=105.0f, cavityT60=.42f, cavityGain=1.0f;
    float shellGain=1.0f, handMs=2.8f, outputGain=.0016f;
    UduOpening opening=UduOpening::Partial;
    bool compressedRange=true;
};
inline UduConfig uduCandidateConfig(UduCandidate candidate) {
    UduConfig c;
    if(candidate==UduCandidate::Deep) {
        c.cavityHz=80; c.cavityT60=.65f; c.cavityGain=1.2f;
        c.shellGain=.38f; c.handMs=4.2f;
    } else if(candidate==UduCandidate::Dry) {
        c.cavityHz=140; c.cavityT60=.22f; c.cavityGain=.85f;
        c.shellGain=1.5f; c.handMs=1.3f;
    }
    return c;
}
struct UduCoefficients { float a1[6]{}, a2[6]{}, injection[6]{}; };
struct UduCache {
    // 49 virtual sizes, not a tenth 9640-byte PreparedNote table.
    static constexpr unsigned first=36, last=84, count=last-first+1;
    UduCoefficients notes[count]{};
    UduConfig config{};
    float handSlow=0, handFast=0, clickPole=0, dcPole=0;
    float sampleRate=48000;
    void prepare(float rate, const UduConfig& c={}) {
        sampleRate=rate; config=c;
        handSlow=std::exp(-1.0f/(rate*c.handMs*.001f));
        handFast=std::exp(-1.0f/(rate*.00035f));
        clickPole=std::exp(-6.283185307f*4200.0f/rate);
        dcPole=std::exp(-6.283185307f*8.0f/rate);
        const float openingHz=c.opening==UduOpening::Open?1.18f:
            c.opening==UduOpening::Closed?.72f:1.0f;
        const float openingDecay=c.opening==UduOpening::Open?.72f:
            c.opening==UduOpening::Closed?1.3f:1.0f;
        const float shellHz[]={460,790,1230,1970};
        const float decay[]={.085f,.055f,.038f,.025f};
        for(unsigned i=0;i<count;++i) {
            const float semitones=float(int(i)+int(first)-60);
            // Limited family spans one octave across four MIDI octaves.
            const float size=std::pow(2.0f,semitones*(c.compressedRange?.25f:1.0f)/12.0f);
            for(unsigned m=0;m<6;++m) {
                const float hz=m<2?c.cavityHz*openingHz*size*(m==0?1.0f:2.31f):
                    shellHz[m-2]*std::pow(size,.82f);
                const float t60=m<2?c.cavityT60*openingDecay*(m==0?1.0f:.29f):decay[m-2];
                const float r=std::exp(-6.907755279f/(rate*t60));
                const float angle=6.283185307f*std::min(hz,rate*.44f)/rate;
                notes[i].a1[m]=2*r*std::cos(angle);
                notes[i].a2[m]=-r*r;
                // Normalized impulse response; excitation is a rounded hand pulse.
                notes[i].injection[m]=std::sin(angle);
            }
        }
    }
    const UduCoefficients& find(uint8_t note) const {
        return notes[std::clamp(unsigned(note),first,last)-first];
    }
};

class UduVoice {
public:
    void setCache(const UduCache* cache) { cache_=cache; }
    void reset() {
        std::fill(z1_,z1_+6,0); std::fill(z2_,z2_+6,0);
        slow_=fast_=click_=dc_=0; remaining_=0; fault_=0;
    }
    void strike(uint8_t note,float velocity,bool clear) {
        if(clear) reset();
        if(!cache_) return;
        coeff_=&cache_->find(note);
        velocity_=std::clamp(velocity,0.0f,1.0f);
        // More shell and a shorter hand pulse at high velocity; air always present.
        cavityDrive_=velocity_*(.72f+.28f*velocity_)*cache_->config.cavityGain;
        shellDrive_=velocity_*(.12f+.88f*velocity_*velocity_)*cache_->config.shellGain;
        slow_=fast_=1; remaining_=uint32_t(cache_->sampleRate*.04f);
        noise_=0x9e3779b9u ^ (std::clamp(unsigned(note),UduCache::first,UduCache::last)*7919u);
    }
    bool hasExciter() const { return remaining_>0; }
    uint32_t faults() const { return fault_; }
    float energy() const {
        float sum=0; for(unsigned m=0;m<6;++m) sum+=z1_[m]*z1_[m]+z2_[m]*z2_[m];
        return sum;
    }
    float process(float damping) {
        if(!cache_ || !coeff_) return 0;
        float air=0,shell=0;
        if(remaining_) {
            const float hand=slow_-fast_;
            slow_*=cache_->handSlow*(1.0f-.0025f*velocity_);
            fast_*=cache_->handFast;
            noise_^=noise_<<13; noise_^=noise_>>17; noise_^=noise_<<5;
            const float n=float(int32_t(noise_))/2147483648.0f;
            click_+=(1-cache_->clickPole)*(n-click_);
            // Low-frequency palm displacement and a small broadband finger component.
            air=hand*(1.0f+.045f*click_)*cavityDrive_;
            // A separate short finger/palm contact pulse excites the ceramic
            // modes; sending the slow air displacement here suppresses them.
            const float contact=4.0f*fast_*(1.0f-fast_);
            shell=45.0f*contact*(.18f+.82f*click_)*shellDrive_;
            --remaining_;
        }
        constexpr float gains[]={1.0f,.17f,.12f,.075f,.045f,.025f};
        float out=0;
        const float choke=1.0f-.006f*damping;
        // Fixed six recurrences, in the original summation order. Literal
        // indices eliminate loop/index loads on ESP32 without altering PCM.
        out+=gains[0]*advance<0>(air,choke);
        out+=gains[1]*advance<1>(air,choke);
        out+=gains[2]*advance<2>(shell,choke);
        out+=gains[3]*advance<3>(shell,choke);
        out+=gains[4]*advance<4>(shell,choke);
        out+=gains[5]*advance<5>(shell,choke);
        out*=cache_->config.outputGain;
        dc_=(1-cache_->dcPole)*out+cache_->dcPole*dc_;
        out-=dc_;
        // Preserve nonfinite evidence for SynthEngine's existing safety guard.
        if(!std::isfinite(out)) ++fault_;
        return out;
    }
private:
    template<unsigned M> float advance(float input,float choke) {
        const float y=coeff_->injection[M]*input+coeff_->a1[M]*z1_[M]+coeff_->a2[M]*z2_[M];
        z2_[M]=z1_[M]*choke; z1_[M]=y*choke;
        return y;
    }
    const UduCache* cache_=nullptr;
    const UduCoefficients* coeff_=nullptr;
    float z1_[6]{},z2_[6]{};
    float slow_=0,fast_=0,click_=0,dc_=0,velocity_=0,cavityDrive_=0,shellDrive_=0;
    uint32_t remaining_=0,noise_=1,fault_=0;
};
} // namespace pocketpan::dsp
