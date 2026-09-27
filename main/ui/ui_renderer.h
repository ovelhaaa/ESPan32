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
