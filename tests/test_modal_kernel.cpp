#include "dsp/modal_resonator.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
using namespace pocketpan::dsp;

int main(int argc, char** argv) {
    std::ifstream golden;
    if (argc == 2) { golden.open(argv[1]); assert(golden); }
    ModalPreset sparse = kPresetBell;
    // Deliberately unsorted and noncontiguous: prefix-only pruning is invalid.
    sparse.modes[0].ratio = 100.0f;
    sparse.modes[3].ratio = 0.0f;
    sparse.modes[7].ratio = 0.75f;
    const ModalPreset* presets[] = {&kPresetPan, &kPresetBell, &sparse};
    for (unsigned preset = 0; preset < 3; ++preset) {
        for (float frequency : {146.83f, 440.0f, 4000.0f, 12000.0f, 15000.0f}) {
            ModalResonatorBank bank;
            bank.init(48000);
            bank.setPreset(*presets[preset]);
            bank.updatePitchAndDamping(frequency, 0);
            const auto active = bank.getActiveModeCount();
            assert(active <= bank.getModeCount());
            uint64_t hash = 14695981039346656037ULL;
            for (unsigned sample = 0; sample < 24000; ++sample) {
                if (sample == 8000) bank.updatePitchAndDamping(frequency, 0.7f);
                if (sample == 16000) bank.updatePitchAndDamping(frequency / 2, 0.0f);
                const float excitation = sample % 997 == 0 ? 30.0f : sample % 113 == 0 ? -0.01f : 0.0f;
                const float value = bank.processSample(excitation);
                assert(std::isfinite(value));
                uint32_t bits;
                std::memcpy(&bits, &value, sizeof(bits));
                for (unsigned byte = 0; byte < 4; ++byte) { hash ^= (bits >> (byte * 8)) & 255; hash *= 1099511628211ULL; }
            }
            if (argc == 2) {
                unsigned expectedPreset; float expectedFrequency; uint64_t expected;
                golden >> expectedPreset >> expectedFrequency >> std::hex >> expected >> std::dec;
                assert(golden && expectedPreset == preset && expectedFrequency == frequency);
                if (hash != expected) { std::cerr << "Kernel differs: " << preset << " " << frequency << '\n'; return 1; }
            } else {
                std::cout << preset << " " << std::setprecision(9) << frequency << " " << std::hex << hash << std::dec << '\n';
            }
        }
    }
}
