#include <atomic>
#include <cassert>
#include <thread>
#include "hardware/block_timing_histogram.h"
#include "ui/audio_telemetry.h"
using namespace pocketpan::ui;
int main() {
    // Nearest-rank p99: the 99th ordered observation of 100 is 500 us.
    pocketpan::hardware::BlockTimingHistogram timing;
    for (int i=0; i<99; ++i) timing.add(500);
    timing.add(2000);
    assert(timing.p99Us() == 500);
    timing.reset();
    // The final fixed bin is an overflow bin: samples are never discarded.
    for (uint32_t i=0; i<pocketpan::hardware::BlockTimingHistogram::kWindowBlocks; ++i) timing.add(999999);
    assert(timing.p99Us() == 3175);

    AudioTelemetryPublisher publisher;
    std::atomic<bool> done{false};
    std::thread writer([&] { for (uint32_t i=1;i<=500000;++i) { AudioTelemetrySnapshot s; s.midiPushCount=i; s.midiPopCount=i*2; s.midiDrops=i*3; s.p99BlockTimeUs=i*7; s.preLimiterPeak=static_cast<float>(i); s.averageGainReductionDb=-static_cast<float>(i); s.gainReductionOver0p1DbSamples=i*5; s.gainReductionOver1DbSamples=i*6; s.hardClampCount=i*4; publisher.publish(s); } done=true; });
    uint32_t reads=0; AudioTelemetrySnapshot s;
    while (!done.load() || reads<1000) if (publisher.read(s)) { assert(s.midiPopCount==s.midiPushCount*2); assert(s.midiDrops==s.midiPushCount*3); assert(s.p99BlockTimeUs==s.midiPushCount*7); assert(s.preLimiterPeak==static_cast<float>(s.midiPushCount)); assert(s.averageGainReductionDb==-static_cast<float>(s.midiPushCount)); assert(s.gainReductionOver0p1DbSamples==s.midiPushCount*5); assert(s.gainReductionOver1DbSamples==s.midiPushCount*6); assert(s.hardClampCount==s.midiPushCount*4); ++reads; }
    writer.join(); assert(reads>=1000);
}
