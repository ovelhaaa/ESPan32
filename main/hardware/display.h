#pragma once

#include <cstdint>
#include <cstddef>
#include "board_config.h"

#ifdef ESP_PLATFORM
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#endif

namespace pocketpan::hardware {

// Standard RGB565 Colors
namespace colors {
constexpr uint16_t Black       = 0x0000;
constexpr uint16_t White       = 0xFFFF;
constexpr uint16_t DarkGray    = 0x18E3;
constexpr uint16_t Gray        = 0x7BEF;
constexpr uint16_t LightGray   = 0xC618;
constexpr uint16_t Red         = 0xF800;
constexpr uint16_t Green       = 0x07E0;
constexpr uint16_t Blue        = 0x001F;
constexpr uint16_t Cyan        = 0x07FF;
constexpr uint16_t Yellow      = 0xFFE0;
constexpr uint16_t Orange      = 0xFD20;
constexpr uint16_t Purple      = 0x780F;
constexpr uint16_t Background  = 0x0821; // Deep dark navy
constexpr uint16_t Text        = 0xFFFF;
constexpr uint16_t Accent      = 0x07FF; // Cyan
} // namespace colors

class Display {
public:
    static constexpr uint16_t kWidth = board::tft::kWidth;
    static constexpr uint16_t kHeight = board::tft::kHeight;
    static constexpr size_t kFrameBufferSize = kWidth * kHeight * sizeof(uint16_t); // 64800 bytes

    Display() = default;
    ~Display();

    bool init(uint8_t* framebuffer = nullptr);
    void update();

    // Graphics primitives
    void clear(uint16_t color = colors::Black);
    void fillRect(int x, int y, int w, int h, uint16_t color);
    void drawPixel(int x, int y, uint16_t color);
    void drawFastHLine(int x, int y, int w, uint16_t color);
    void drawFastVLine(int x, int y, int h, uint16_t color);
    void drawRect(int x, int y, int w, int h, uint16_t color);

    // Font rendering (standard 5x7 ASCII font with scale)
    void drawChar(int x, int y, char c, uint16_t color, int scale = 1);
    void drawText(int x, int y, const char* text, uint16_t color, int scale = 1);

    uint8_t* getFramebuffer() const { return framebuffer_; }

private:
#ifdef ESP_PLATFORM
    esp_lcd_panel_handle_t panelHandle_ = nullptr;
#endif
    uint8_t* framebuffer_ = nullptr;
    bool internalAlloc_ = false;
    bool initialized_ = false;
};

} // namespace pocketpan::hardware
