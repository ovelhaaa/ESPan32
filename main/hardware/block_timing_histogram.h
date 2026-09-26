#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace pocketpan::hardware {

// Audio-task-owned fixed histogram.  It has no allocation and each submitted
// block performs one division, one bounded array increment, and one counter
// increment.  Percentiles use the nearest-rank convention: ceil(p * N).
class BlockTimingHistogram {
public:
    static constexpr uint32_t kBinUs = 25;
    static constexpr size_t kBins = 128;
    static constexpr uint32_t kWindowBlocks = 2048;

    void reset() {
        bins_.fill(0);
        count_ = 0;
    }

    bool add(uint32_t elapsedUs) {
        const uint32_t candidate = elapsedUs / kBinUs;
        const size_t bin = candidate < kBins ? candidate : kBins - 1;
        ++bins_[bin]; // A bin cannot exceed the fixed 2048-sample window.
        ++count_;
        return count_ == kWindowBlocks;
    }

    uint32_t p99Us() const {
        if (count_ == 0) return 0;
        const uint32_t rank = (count_ * 99U + 99U) / 100U;
        uint32_t cumulative = 0;
        for (size_t i = 0; i < kBins; ++i) {
            cumulative += bins_[i];
            if (cumulative >= rank) return static_cast<uint32_t>(i) * kBinUs;
        }
        return static_cast<uint32_t>(kBins - 1) * kBinUs;
    }

private:
    std::array<uint16_t, kBins> bins_{};
    uint32_t count_ = 0;
};

} // namespace pocketpan::hardware
