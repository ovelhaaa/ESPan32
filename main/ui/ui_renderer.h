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
};

} // namespace pocketpan::ui
