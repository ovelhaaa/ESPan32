// M6.3.4 exact attack/exciter fast-path differential harness.
//
// Compares the candidate-21 segmented exciter (table window + inlined PRNG)
// against the historical scalar reference sample by sample, for every
// supported impulse length, several velocities and hardnesses.  Requires
// POCKETPAN_DSP_CANDIDATE=21 and POCKETPAN_EXCITER_DIFFERENTIAL_TEST=1 so the
// reference body is compiled alongside the fast path.
#include "dsp/exciter.h"
#include "dsp/dsp_profile.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace pocketpan::dsp;

namespace {

uint32_t bits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

void run_one(float velocity, float hardness, bool (&seen)[15], ExciterShape shape = ExciterShape::Strike) {
    ExciterConfig cfg{};
    cfg.shape = shape;
    Exciter fast, ref;
    fast.init(48000.0f);
    ref.init(48000.0f);
    fast.setConfig(cfg);
    ref.setConfig(cfg);
    fast.trigger(velocity, hardness, 1.0f);
    ref.trigger(velocity, hardness, 1.0f);

    const uint32_t impulse = ref.impulseSamplesForTest();
    assert(impulse >= 3 && impulse <= 14);
    seen[impulse] = true;

    // Render the complete strike plus margin.  The two paths must agree on the
    // exact sample where active_ falls false.
    const uint32_t limit = ref.noiseSamplesForTest() + 4;
    for (uint32_t i = 0; i <= limit; ++i) {
        const float a = ref.processSampleReference();
        const float b = fast.processSample();
        if (bits(a) != bits(b)) {
            std::fprintf(stderr,
                "exciter differs shape=%d v=%.4f h=%.4f impulse=%u sample=%u (ref=%08x %g fast=%08x %g)\n",
                static_cast<int>(shape), velocity, hardness, impulse, i, bits(a), a, bits(b), b);
            assert(false);
        }
        if (ref.activeForTest() != fast.activeForTest()) {
            std::fprintf(stderr,
                "exciter active_ differs shape=%d v=%.4f h=%.4f impulse=%u sample=%u (ref=%d fast=%d)\n",
                static_cast<int>(shape), velocity, hardness, impulse, i, ref.activeForTest(), fast.activeForTest());
            assert(false);
        }
    }

    assert(ref.rngStateForTest() == fast.rngStateForTest() && "PRNG state must match");
    assert(bits(ref.filterStateForTest()) == bits(fast.filterStateForTest()) && "filter state must match");
    assert(ref.sampleIndexForTest() == fast.sampleIndexForTest() && "sample index must match");
}

} // namespace

int main() {
#if POCKETPAN_ATTACK_FASTPATH
    bool seen[15] = {};
    const float velocities[] = {30.0f / 127.0f, 70.0f / 127.0f, 110.0f / 127.0f, 1.0f};
    const float hardnesses[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};

    for (float v : velocities)
        for (float h : hardnesses)
            run_one(v, h, seen, ExciterShape::Strike);

    // Sweep hardness densely so every integer impulse length is exercised.
    for (float v : velocities)
        for (int s = 0; s <= 40; ++s)
            run_one(v, static_cast<float>(s) / 40.0f, seen, ExciterShape::Strike);

    int covered = 0;
    for (int n = 3; n <= 14; ++n) { if (seen[n]) ++covered; }
    assert(covered == 12 && "all 12 supported impulse lengths must be exercised");
    std::fprintf(stderr, "exciter fast path: exact for all impulse lengths (3..14), PRNG state exact\n");

    bool seenPluck[15] = {};
    for (float v : velocities)
        for (float h : hardnesses)
            run_one(v, h, seenPluck, ExciterShape::Pluck);
    for (float v : velocities)
        for (int s = 0; s <= 40; ++s)
            run_one(v, static_cast<float>(s) / 40.0f, seenPluck, ExciterShape::Pluck);
    std::fprintf(stderr, "exciter fast path: Pluck shape exact across velocities and hardnesses\n");
#else
    std::fprintf(stderr, "exciter fast path: candidate %d has no attack fast path; skipped\n",
                 POCKETPAN_DSP_CANDIDATE);
#endif
    return 0;
}
