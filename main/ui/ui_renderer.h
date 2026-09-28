#pragma once

#include "ui_state.h"
#include "../hardware/display.h"

namespace pocketpan::ui {

class UiRenderer {
public:
    explicit UiRenderer(hardware::Display& display) : display_(display) {}

    void render(const UiState& state);

private:
    void renderStatus(const UiState& state);
    void renderDiagnostic(const UiState& state);

    hardware::Display& display_;

#if defined(POCKETPAN_UI_URGENT) && POCKETPAN_UI_URGENT
    // M6.3.7 Phase D: two independent display signatures.  "Urgent" covers
    // musical/connection/screen state that must feel immediate (<= 1 loop);
    // "telemetry" covers fast-changing readouts (CPU/avg/p99/max/deadline/RSSI/
    // heap) that may refresh slowly.  Signatures are committed only when a
    // redraw is actually performed, so a pending telemetry change is not lost.
    bool hasUrgent_ = false;
    bool hasTelemetry_ = false;
    uint32_t lastTelemetryRenderMs_ = 0;
    // Formatting-free state fingerprints.  The per-loop dirty test must never
    // call snprintf/float formatting: that (not the pixels or the transfer) is
    // what competes with the audio core.  A 64-bit FNV-1a over the raw integer
    // and float-bit representation is cheap and side-effect free.
    uint64_t urgentHash_ = 0;
    uint64_t telemetryHash_ = 0;
    uint32_t urgentRenderCount_ = 0;
    uint32_t telemetryRenderCount_ = 0;
    uint32_t skipRenderCount_ = 0;
public:
    // Returns true when a redraw is due: immediately on an urgent change, or
    // once per telemetry period when only telemetry changed.  Commits both
    // fingerprints only on the frames it returns true.
    bool shouldRender(const UiState& state, uint32_t nowMs, uint32_t telemetryPeriodMs);
    uint32_t urgentRenderCountForTest() const { return urgentRenderCount_; }
    uint32_t telemetryRenderCountForTest() const { return telemetryRenderCount_; }
    uint32_t skipRenderCountForTest() const { return skipRenderCount_; }
private:
    static uint64_t urgentHash(const UiState& state);
    static uint64_t telemetryHash(const UiState& state);
#endif

#if defined(POCKETPAN_UI_TIMING) && POCKETPAN_UI_TIMING
public:
    // Microseconds for the last executed render, split into framebuffer drawing
    // and the LCD submit (Display::update*/updateRows).
    uint32_t lastFramebufferUs() const { return lastFramebufferUs_; }
    uint32_t lastLcdUs() const { return lastLcdUs_; }
private:
    uint32_t lastFramebufferUs_ = 0;
    uint32_t lastLcdUs_ = 0;
#endif

#if defined(POCKETPAN_UI_DIRTY) && POCKETPAN_UI_DIRTY
    // M6.3.6 Phase C candidate (guarded, default off): screen-dirty tracking.
    // The complete render is skipped only when the exact text handed to every
    // drawText() is unchanged, so the framebuffer result is identical to a
    // forced redraw.  Nothing about draw order, geometry or colors changes.
    bool hasRendered_ = false;
    char lastSignature_[192] = {0};
    uint32_t renderCount_ = 0;
    uint32_t skipCount_ = 0;
    static size_t buildSignature(const UiState& state, char* out, size_t cap);
public:
    uint32_t dirtyRenderCountForTest() const { return renderCount_; }
    uint32_t dirtySkipCountForTest() const { return skipCount_; }
private:
#endif

#if defined(POCKETPAN_UI_PARTIAL) && POCKETPAN_UI_PARTIAL
    // M6.3.6 Phase C production candidate: partial redraw of the Status screen.
    // The static background is drawn once; each frame only the fields whose
    // rendered text changed are erased and redrawn, and only their full-width
    // row band is transferred.  Visual result is identical to a full redraw.
    bool partialValid_ = false;
    UiScreenMode partialMode_ = UiScreenMode::Status;
    int dirtyY0_ = 0, dirtyY1_ = 0; // transfer rows [dirtyY0_, dirtyY1_) if y1 > y0
    bool noTransfer_ = false;       // nothing displayed changed
    char lastBle_[16] = {0};
    char lastPreset_[16] = {0};
    char lastRoot_[8] = {0};
    char lastVoices_[24] = {0};
    char lastCpu_[24] = {0};
    char lastDline_[24] = {0};
    void drawStatusField(int x, int y, const char* text, uint16_t color, uint16_t bg,
                         int scale, char* cache, size_t cap, bool force);
#endif
};

} // namespace pocketpan::ui
