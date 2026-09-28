#include "ui_renderer.h"
#include <cstdio>
#include <cstring>

#if defined(POCKETPAN_UI_TIMING) && POCKETPAN_UI_TIMING && defined(ESP_PLATFORM)
#include "esp_timer.h"
#define POCKETPAN_UI_TIMING_ACTIVE 1
#else
#define POCKETPAN_UI_TIMING_ACTIVE 0
#endif

namespace pocketpan::ui {

// M6.3.6 Phase A U3 (diagnostic only): the unconditional full framebuffer
// clear can be skipped so the test can separate "clear + full rewrite" from
// "logical formatting/control work".  Production never defines this.
#if defined(POCKETPAN_UI_NO_FULL_CLEAR) && POCKETPAN_UI_NO_FULL_CLEAR
#define POCKETPAN_UI_SKIP_FULL_CLEAR 1
#else
#define POCKETPAN_UI_SKIP_FULL_CLEAR 0
#endif

void UiRenderer::render(const UiState& state) {
#if defined(POCKETPAN_UI_DIRTY) && POCKETPAN_UI_DIRTY
    char signature[sizeof(lastSignature_)];
    buildSignature(state, signature, sizeof(signature));
    if (hasRendered_ && std::strcmp(signature, lastSignature_) == 0) {
        ++skipCount_;
        return; // Displayed text identical: no draw and no LCD transfer.
    }
    ++renderCount_;
#endif
#if POCKETPAN_UI_TIMING_ACTIVE
    const int64_t tFb0 = esp_timer_get_time();
#endif
    if (state.mode == UiScreenMode::MidiDiagnostic) {
        renderDiagnostic(state);
    } else if (state.mode == UiScreenMode::AudioDiagnostic) {
#if !POCKETPAN_UI_SKIP_FULL_CLEAR
        display_.clear(hardware::colors::Background);
#endif
        display_.fillRect(0, 0, hardware::Display::kWidth, 18, hardware::colors::DarkGray);
        display_.drawText(8, 5, "AUDIO DIAG", hardware::colors::White, 1);
        char b[40];
        snprintf(b, sizeof(b), "SR %u  BLOCK 128", 48000U); display_.drawText(10, 26, b, hardware::colors::LightGray, 1);
        snprintf(b, sizeof(b), "AVG %uus  P99 %uus", (unsigned)state.avgBlockTimeUs, (unsigned)state.p99BlockTimeUs); display_.drawText(10, 42, b, hardware::colors::LightGray, 1);
        snprintf(b, sizeof(b), "MAX %uus  LOAD %2.1f%%", (unsigned)state.maxBlockTimeUs, state.cpuLoadPercent); display_.drawText(10, 58, b, hardware::colors::LightGray, 1);
        snprintf(b, sizeof(b), "DLINE %u TMO %u", (unsigned)state.deadlineMisses, (unsigned)state.writeTimeouts); display_.drawText(10, 74, b, hardware::colors::LightGray, 1);
        snprintf(b, sizeof(b), "TXERR %u SHORT %u", (unsigned)state.txErrors, (unsigned)state.shortWrites); display_.drawText(10, 90, b, hardware::colors::LightGray, 1);
        snprintf(b, sizeof(b), "PEAK %.2f OUT %.2f", state.preLimiterPeak, state.postLimiterPeak); display_.drawText(10, 106, b, hardware::colors::Cyan, 1);
        snprintf(b, sizeof(b), "GR %.1f M%.1f >.1:%u >1:%u C%u", state.currentGainReductionDb, state.maxGainReductionDb, (unsigned)state.gainReductionOver0p1DbSamples, (unsigned)state.gainReductionOver1DbSamples, (unsigned)state.hardClampCount); display_.drawText(10, 122, b, hardware::colors::Gray, 1);
    } else {
        renderStatus(state);
    }
#if POCKETPAN_UI_TIMING_ACTIVE
    const int64_t tFb1 = esp_timer_get_time();
#endif
#if defined(POCKETPAN_UI_PARTIAL) && POCKETPAN_UI_PARTIAL
    if (state.mode == UiScreenMode::Status) {
        if (noTransfer_) {
            // Displayed content is identical: no draw above and no transfer.
        } else if (dirtyY1_ > dirtyY0_) {
            display_.updateRows(dirtyY0_, dirtyY1_);
        } else {
            display_.update();
        }
    } else {
        display_.update();
    }
#else
    display_.update();
#endif
#if POCKETPAN_UI_TIMING_ACTIVE
    lastFramebufferUs_ = static_cast<uint32_t>(tFb1 - tFb0);
    lastLcdUs_ = static_cast<uint32_t>(esp_timer_get_time() - tFb1);
#endif
#if defined(POCKETPAN_UI_DIRTY) && POCKETPAN_UI_DIRTY
    buildSignature(state, lastSignature_, sizeof(lastSignature_));
    hasRendered_ = true;
#endif
}

#if defined(POCKETPAN_UI_PARTIAL) && POCKETPAN_UI_PARTIAL
void UiRenderer::drawStatusField(int x, int y, const char* text, uint16_t color, uint16_t bg,
                                 int scale, char* cache, size_t cap, bool force) {
    if (!force && std::strcmp(text, cache) == 0) return;
    const int h = 8 * scale;
    if (!force) {
        // Erase the wider of the old/new strings so no glyph is left behind.
        const size_t oldLen = std::strlen(cache);
        const size_t newLen = std::strlen(text);
        const size_t cols = oldLen > newLen ? oldLen : newLen;
        display_.fillRect(x, y, static_cast<int>(cols * static_cast<size_t>(6 * scale)) + 2, h, bg);
    }
    display_.drawText(x, y, text, color, scale);
    std::snprintf(cache, cap, "%s", text);
    if (y < dirtyY0_) dirtyY0_ = y;
    if (y + h > dirtyY1_) dirtyY1_ = y + h;
}

void UiRenderer::renderStatus(const UiState& state) {
    const bool full = !partialValid_ || partialMode_ != UiScreenMode::Status;
    dirtyY0_ = hardware::Display::kHeight;
    dirtyY1_ = 0;
    noTransfer_ = false;

    if (full) {
#if !POCKETPAN_UI_SKIP_FULL_CLEAR
        display_.clear(hardware::colors::Background);
#endif
        // Static background: drawn once per mode entry, never per frame.
        display_.fillRect(0, 0, hardware::Display::kWidth, 20, hardware::colors::DarkGray);
        display_.drawText(8, 6, "POCKET PAN", hardware::colors::Cyan, 1);
        display_.drawFastHLine(0, 20, hardware::Display::kWidth, hardware::colors::Gray);
        display_.drawText(12, 28, "PRESET", hardware::colors::Gray, 1);
        display_.drawText(140, 28, "ROOT", hardware::colors::Gray, 1);
        display_.drawFastHLine(8, 62, hardware::Display::kWidth - 16, hardware::colors::DarkGray);
        display_.drawText(140, 70, "AUDIO   48K", hardware::colors::LightGray, 1);
        display_.drawFastHLine(0, 115, hardware::Display::kWidth, hardware::colors::DarkGray);
        display_.drawText(12, 122, "BOOT: SHORT DIAG / HOLD MODEL", hardware::colors::Gray, 1);
    }

    const bool force = full;
    drawStatusField(175, 6, state.bleStatus,
                    state.bleStatus[4] == 'O' ? hardware::colors::Green : hardware::colors::Orange,
                    hardware::colors::DarkGray, 1, lastBle_, sizeof(lastBle_), force);
    drawStatusField(12, 40, state.presetName, hardware::colors::White,
                    hardware::colors::Background, 2, lastPreset_, sizeof(lastPreset_), force);
    drawStatusField(140, 40, state.rootNoteName, hardware::colors::Yellow,
                    hardware::colors::Background, 2, lastRoot_, sizeof(lastRoot_), force);
    char lineBuf[32];
    snprintf(lineBuf, sizeof(lineBuf), "VOICES  %u/%u", state.activeVoices, state.maxVoices);
    drawStatusField(12, 70, lineBuf, hardware::colors::LightGray,
                    hardware::colors::Background, 1, lastVoices_, sizeof(lastVoices_), force);
    snprintf(lineBuf, sizeof(lineBuf), "CPU     %2.0f%%", state.cpuLoadPercent);
    drawStatusField(12, 88, lineBuf,
                    state.cpuLoadPercent > 75.0f ? hardware::colors::Red : hardware::colors::LightGray,
                    hardware::colors::Background, 1, lastCpu_, sizeof(lastCpu_), force);
    snprintf(lineBuf, sizeof(lineBuf), "DLINE   %u", static_cast<unsigned>(state.deadlineMisses));
    drawStatusField(140, 88, lineBuf,
                    state.deadlineMisses > 0 ? hardware::colors::Red : hardware::colors::LightGray,
                    hardware::colors::Background, 1, lastDline_, sizeof(lastDline_), force);

    partialValid_ = true;
    partialMode_ = UiScreenMode::Status;
    if (full) { dirtyY0_ = 0; dirtyY1_ = 0; }            // force full transfer
    else if (dirtyY1_ <= dirtyY0_) noTransfer_ = true;   // nothing changed
}
#else
void UiRenderer::renderStatus(const UiState& state) {
#if !POCKETPAN_UI_SKIP_FULL_CLEAR
    display_.clear(hardware::colors::Background);
#endif

    // Header banner
    display_.fillRect(0, 0, hardware::Display::kWidth, 20, hardware::colors::DarkGray);
    display_.drawText(8, 6, "POCKET PAN", hardware::colors::Cyan, 1);
    display_.drawText(175, 6, state.bleStatus,
                      state.bleStatus[4] == 'O' ? hardware::colors::Green : hardware::colors::Orange, 1);

    display_.drawFastHLine(0, 20, hardware::Display::kWidth, hardware::colors::Gray);

    // Active Preset and Note info
    display_.drawText(12, 28, "PRESET", hardware::colors::Gray, 1);
    display_.drawText(12, 40, state.presetName, hardware::colors::White, 2);

    display_.drawText(140, 28, "ROOT", hardware::colors::Gray, 1);
    display_.drawText(140, 40, state.rootNoteName, hardware::colors::Yellow, 2);

    display_.drawFastHLine(8, 62, hardware::Display::kWidth - 16, hardware::colors::DarkGray);

    // Stats Grid
    char lineBuf[32];

    // Voices
    snprintf(lineBuf, sizeof(lineBuf), "VOICES  %u/%u", state.activeVoices, state.maxVoices);
    display_.drawText(12, 70, lineBuf, hardware::colors::LightGray, 1);

    // Audio Rate
    display_.drawText(140, 70, "AUDIO   48K", hardware::colors::LightGray, 1);

    // CPU Load
    snprintf(lineBuf, sizeof(lineBuf), "CPU     %2.0f%%", state.cpuLoadPercent);
    display_.drawText(12, 88, lineBuf,
                      state.cpuLoadPercent > 75.0f ? hardware::colors::Red : hardware::colors::LightGray, 1);

    // Deadline Misses
    snprintf(lineBuf, sizeof(lineBuf), "DLINE   %u", static_cast<unsigned>(state.deadlineMisses));
    display_.drawText(140, 88, lineBuf,
                      state.deadlineMisses > 0 ? hardware::colors::Red : hardware::colors::LightGray, 1);

    // Footer tip
    display_.drawFastHLine(0, 115, hardware::Display::kWidth, hardware::colors::DarkGray);
    display_.drawText(12, 122, "BOOT: SHORT DIAG / HOLD MODEL", hardware::colors::Gray, 1);
}
#endif

void UiRenderer::renderDiagnostic(const UiState& state) {
#if !POCKETPAN_UI_SKIP_FULL_CLEAR
    display_.clear(0x0000); // Black
#endif

    // Header banner
    display_.fillRect(0, 0, hardware::Display::kWidth, 18, hardware::colors::Red);
    display_.drawText(8, 5, "MIDI DIAGNOSTIC MONITOR", hardware::colors::White, 1);
    display_.drawFastHLine(0, 18, hardware::Display::kWidth, hardware::colors::Gray);

    char buf[40];

    // Note Number & Name & Velocity
    snprintf(buf, sizeof(buf), "NOTE %u (%s)  VEL %u", state.lastNoteNumber, state.rootNoteName, state.lastVelocity);
    display_.drawText(10, 24, buf, hardware::colors::Yellow, 1);

    // Pressure & Event Type
    snprintf(buf, sizeof(buf), "PRESS %u  TYPE %s", state.lastPressure, state.lastEventType);
    display_.drawText(10, 40, buf, hardware::colors::Cyan, 1);

    // 13-bit Timestamp & Raw Bytes Hex
    snprintf(buf, sizeof(buf), "TS13 %u  HEX %02X %02X %02X",
             state.lastTimestamp13, state.lastRawBytes[0], state.lastRawBytes[1], state.lastRawBytes[2]);
    display_.drawText(10, 56, buf, hardware::colors::LightGray, 1);

    // MIDI Queue HighWater & Drops
    snprintf(buf, sizeof(buf), "QUEUE HWM %u  DROP %u",
             static_cast<unsigned>(state.midiHighWater), static_cast<unsigned>(state.midiDrops));
    display_.drawText(10, 72, buf, state.midiDrops > 0 ? hardware::colors::Red : hardware::colors::Green, 1);

    snprintf(buf, sizeof(buf), "CONN %.2fms LAT %u RSSI %d",
             state.bleIntervalUnits * 1.25f, state.bleLatency, state.bleRssi);
    display_.drawText(10, 80, buf, hardware::colors::LightGray, 1);
    snprintf(buf, sizeof(buf), "RECN %u DISC %u", (unsigned)state.bleReconnects, state.bleLastDisconnectReason);
    display_.drawText(10, 88, buf, hardware::colors::LightGray, 1);

    // Audio Engine stats
    snprintf(buf, sizeof(buf), "DSP %uus (CPU %2.0f%%) DL %u",
             static_cast<unsigned>(state.avgBlockTimeUs), state.cpuLoadPercent, static_cast<unsigned>(state.deadlineMisses));
    display_.drawText(10, 102, buf, state.deadlineMisses > 0 ? hardware::colors::Red : hardware::colors::LightGray, 1);

    // Bottom status
    display_.drawFastHLine(0, 114, hardware::Display::kWidth, hardware::colors::DarkGray);
    snprintf(buf, sizeof(buf), "%s  (BOOT: TOGGLE)",
             state.bleStatus);
    display_.drawText(10, 120, buf,
                      state.bleStatus[4] == 'O' ? hardware::colors::Green : hardware::colors::Gray, 1);
}

#if defined(POCKETPAN_UI_DIRTY) && POCKETPAN_UI_DIRTY
// The signature is the exact sequence of formatted strings handed to drawText()
// for the active screen.  Comparing signatures is therefore equivalent to
// comparing the drawn glyphs (the static text and every geometric primitive are
// constants within a screen mode).
size_t UiRenderer::buildSignature(const UiState& state, char* out, size_t cap) {
    if (cap == 0) return 0;
    size_t used = 0;
    auto add = [&](const char* fmt, auto... args) {
        if (used >= cap) return;
        const int n = std::snprintf(out + used, cap - used, fmt, args...);
        if (n > 0) used += static_cast<size_t>(n);
    };
    if (state.mode == UiScreenMode::MidiDiagnostic) {
        add("%d|%u (%s) %u|%u %s|%u %02X%02X%02X|%u %u|%.2f %u %d|%u %u|%u %2.0f %u|%s",
            static_cast<int>(state.mode), state.lastNoteNumber, state.rootNoteName, state.lastVelocity,
            state.lastPressure, state.lastEventType, state.lastTimestamp13,
            state.lastRawBytes[0], state.lastRawBytes[1], state.lastRawBytes[2],
            static_cast<unsigned>(state.midiHighWater), static_cast<unsigned>(state.midiDrops),
            state.bleIntervalUnits * 1.25f, state.bleLatency, state.bleRssi,
            static_cast<unsigned>(state.bleReconnects), state.bleLastDisconnectReason,
            static_cast<unsigned>(state.avgBlockTimeUs), state.cpuLoadPercent,
            static_cast<unsigned>(state.deadlineMisses), state.bleStatus);
    } else if (state.mode == UiScreenMode::AudioDiagnostic) {
        add("%d|%u %u|%u %2.1f|%u %u|%u %u|%.2f %.2f|%.1f %.1f %u %u %u",
            static_cast<int>(state.mode), static_cast<unsigned>(state.avgBlockTimeUs),
            static_cast<unsigned>(state.p99BlockTimeUs), static_cast<unsigned>(state.maxBlockTimeUs),
            state.cpuLoadPercent, static_cast<unsigned>(state.deadlineMisses),
            static_cast<unsigned>(state.writeTimeouts), static_cast<unsigned>(state.txErrors),
            static_cast<unsigned>(state.shortWrites), state.preLimiterPeak, state.postLimiterPeak,
            state.currentGainReductionDb, state.maxGainReductionDb,
            static_cast<unsigned>(state.gainReductionOver0p1DbSamples),
            static_cast<unsigned>(state.gainReductionOver1DbSamples),
            static_cast<unsigned>(state.hardClampCount));
    } else {
        add("%d|%s|%s|%s|%u/%u|%2.0f|%u",
            static_cast<int>(state.mode), state.presetName, state.rootNoteName, state.bleStatus,
            state.activeVoices, state.maxVoices, state.cpuLoadPercent,
            static_cast<unsigned>(state.deadlineMisses));
    }
    if (used >= cap) out[cap - 1] = 0;
    return used;
}
#endif

#if defined(POCKETPAN_UI_URGENT) && POCKETPAN_UI_URGENT
namespace {
constexpr uint64_t kFnvOffset = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

inline uint64_t hashBytes(uint64_t h, const void* data, size_t n) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= kFnvPrime;
    }
    return h;
}
template <typename T>
inline uint64_t hashValue(uint64_t h, T value) {
    return hashBytes(h, &value, sizeof(T));
}
inline uint64_t hashText(uint64_t h, const char* text) {
    for (const char* p = text; *p; ++p) {
        h ^= static_cast<uint8_t>(*p);
        h *= kFnvPrime;
    }
    return h;
}
} // namespace

uint64_t UiRenderer::urgentHash(const UiState& state) {
    uint64_t h = kFnvOffset;
    h = hashValue(h, static_cast<uint8_t>(state.mode));
    h = hashValue(h, state.lastNoteNumber);
    h = hashValue(h, state.lastVelocity);
    h = hashValue(h, state.lastPressure);
    h = hashValue(h, state.lastTimestamp13);
    h = hashBytes(h, state.lastRawBytes, sizeof(state.lastRawBytes));
    h = hashValue(h, static_cast<uint8_t>(state.midiDrops > 0 ? 1 : 0));
    h = hashText(h, state.presetName);
    h = hashText(h, state.rootNoteName);
    h = hashText(h, state.bleStatus);
    h = hashText(h, state.lastEventType);
    h = hashValue(h, state.activeVoices);
    h = hashValue(h, state.maxVoices);
    // Connection details shown on the MIDI monitor are input-feel fields.
    h = hashValue(h, state.bleIntervalUnits);
    h = hashValue(h, state.bleLatency);
    h = hashValue(h, state.bleReconnects);
    h = hashValue(h, state.bleLastDisconnectReason);
    return h;
}

uint64_t UiRenderer::telemetryHash(const UiState& state) {
    uint64_t h = kFnvOffset;
    h = hashValue(h, state.cpuLoadPercent);
    h = hashValue(h, state.avgBlockTimeUs);
    h = hashValue(h, state.p99BlockTimeUs);
    h = hashValue(h, state.maxBlockTimeUs);
    h = hashValue(h, state.deadlineMisses);
    h = hashValue(h, state.writeTimeouts);
    h = hashValue(h, state.txErrors);
    h = hashValue(h, state.shortWrites);
    h = hashValue(h, state.bleRssi);
    h = hashValue(h, state.internalHeapFree);
    h = hashValue(h, state.largestInternalBlock);
    h = hashValue(h, state.preLimiterPeak);
    h = hashValue(h, state.postLimiterPeak);
    h = hashValue(h, state.midiDrops);
    h = hashValue(h, state.midiHighWater);
    h = hashValue(h, state.gainReductionOver1DbSamples);
    h = hashValue(h, state.gainReductionOver0p1DbSamples);
    h = hashValue(h, state.hardClampCount);
    h = hashValue(h, state.currentGainReductionDb);
    h = hashValue(h, state.maxGainReductionDb);
    return h;
}

bool UiRenderer::shouldRender(const UiState& state, uint32_t nowMs, uint32_t telemetryPeriodMs) {
    const uint64_t urgent = urgentHash(state);
    const uint64_t telemetry = telemetryHash(state);
    const bool urgentChanged = !hasUrgent_ || urgent != urgentHash_;
    const bool telemetryChanged = !hasTelemetry_ || telemetry != telemetryHash_;

    if (urgentChanged ||
        (telemetryChanged && (nowMs - lastTelemetryRenderMs_) >= telemetryPeriodMs)) {
        urgentHash_ = urgent;
        telemetryHash_ = telemetry;
        hasUrgent_ = true;
        hasTelemetry_ = true;
        lastTelemetryRenderMs_ = nowMs;
        if (urgentChanged) ++urgentRenderCount_; else ++telemetryRenderCount_;
        return true;
    }
    ++skipRenderCount_;
    return false;
}
#endif

} // namespace pocketpan::ui
