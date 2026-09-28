#pragma once

#include "ui_state.h"
#include "../hardware/display.h"

#ifndef POCKETPAN_UI_ARCH
// 0 = F0 (legacy M6.3.7 partial row band: updateRows)
// 1 = F1 (full framebuffer + true rect transfer)
// 2 = F2 (tile/scratch render + true rect transfer)
// 3 = F3 (PSRAM framebuffer + tile/scratch render)
#define POCKETPAN_UI_ARCH 2
#endif

namespace pocketpan::ui {

class UiRenderer {
public:
    explicit UiRenderer(hardware::Display& display) : display_(display) {}

    void render(const UiState& state);

    uint32_t fieldUpdateCount() const { return fieldUpdateCount_; }

private:
    void renderStatus(const UiState& state);
    void renderDiagnostic(const UiState& state);

    hardware::Display& display_;
    uint32_t fieldUpdateCount_ = 0;

#if defined(POCKETPAN_UI_URGENT) && POCKETPAN_UI_URGENT
    bool hasUrgent_ = false;
    bool hasTelemetry_ = false;
    uint32_t lastTelemetryRenderMs_ = 0;
    uint64_t urgentHash_ = 0;
    uint64_t telemetryHash_ = 0;
    uint32_t urgentRenderCount_ = 0;
    uint32_t telemetryRenderCount_ = 0;
    uint32_t skipRenderCount_ = 0;
public:
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
    uint32_t lastFramebufferUs() const { return lastFramebufferUs_; }
    uint32_t lastLcdUs() const { return lastLcdUs_; }
private:
    uint32_t lastFramebufferUs_ = 0;
    uint32_t lastLcdUs_ = 0;
#endif

#if defined(POCKETPAN_UI_DIRTY) && POCKETPAN_UI_DIRTY
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

    // Status screen state caches
    bool partialValid_ = false;
    UiScreenMode partialMode_ = UiScreenMode::Status;
    int dirtyY0_ = 0, dirtyY1_ = 0;
    bool noTransfer_ = false;
    char lastBle_[16] = {0};
    char lastPreset_[16] = {0};
    char lastRoot_[8] = {0};
    char lastVoices_[24] = {0};
    char lastCpu_[24] = {0};
    char lastDline_[24] = {0};
    uint16_t lastBleColor_ = 0;
    uint16_t lastCpuColor_ = 0;
    uint16_t lastDlineColor_ = 0;

    // F0 / U0 helper
    void drawStatusFieldRowBand(int x, int y, const char* text, uint16_t color, uint16_t bg,
                                int scale, char* cache, size_t cap, bool force);

    // F1 / U1 helper
    void drawStatusFieldRectFb(int x, int y, int w, int h, const char* text, uint16_t color,
                               uint16_t bg, int scale, char* cache, size_t cap,
                               uint16_t* lastColor, bool force);

    // F2 / U2 helper
    void drawStatusFieldTile(int x, int y, int w, int h, const char* text, uint16_t color,
                             uint16_t bg, int scale, char* cache, size_t cap,
                             uint16_t* lastColor, uint8_t* tileMem, bool force);
};

} // namespace pocketpan::ui
