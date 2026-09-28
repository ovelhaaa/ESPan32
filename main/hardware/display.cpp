#include "display.h"
#include "../diag/ui_audio_sync.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

namespace pocketpan::hardware {

namespace {
constexpr const char* kTag = "Display";

// Complete 5x7 ASCII character font table (ASCII 32 to 126)
const uint8_t kFont5x7[95][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // 32 ' '
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // 33 '!'
    {0x00, 0x07, 0x00, 0x07, 0x00}, // 34 '"'
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // 35 '#'
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // 36 '$'
    {0x23, 0x13, 0x08, 0x64, 0x62}, // 37 '%'
    {0x36, 0x49, 0x55, 0x22, 0x50}, // 38 '&'
    {0x00, 0x05, 0x03, 0x00, 0x00}, // 39 '''
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // 40 '('
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // 41 ')'
    {0x14, 0x08, 0x3E, 0x08, 0x14}, // 42 '*'
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // 43 '+'
    {0x00, 0x50, 0x30, 0x00, 0x00}, // 44 ','
    {0x08, 0x08, 0x08, 0x08, 0x08}, // 45 '-'
    {0x00, 0x60, 0x60, 0x00, 0x00}, // 46 '.'
    {0x20, 0x10, 0x08, 0x04, 0x02}, // 47 '/'
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 48 '0'
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 49 '1'
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 50 '2'
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 51 '3'
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 52 '4'
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 53 '5'
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 54 '6'
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 55 '7'
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 56 '8'
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 57 '9'
    {0x00, 0x36, 0x36, 0x00, 0x00}, // 58 ':'
    {0x00, 0x56, 0x36, 0x00, 0x00}, // 59 ';'
    {0x08, 0x14, 0x22, 0x41, 0x00}, // 60 '<'
    {0x14, 0x14, 0x14, 0x14, 0x14}, // 61 '='
    {0x00, 0x41, 0x22, 0x14, 0x08}, // 62 '>'
    {0x02, 0x01, 0x51, 0x09, 0x06}, // 63 '?'
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // 64 '@'
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // 65 'A'
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // 66 'B'
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // 67 'C'
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // 68 'D'
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // 69 'E'
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // 70 'F'
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // 71 'G'
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // 72 'H'
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // 73 'I'
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // 74 'J'
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // 75 'K'
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // 76 'L'
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // 77 'M'
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // 78 'N'
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // 79 'O'
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // 80 'P'
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // 81 'Q'
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // 82 'R'
    {0x46, 0x49, 0x49, 0x49, 0x31}, // 83 'S'
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // 84 'T'
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // 85 'U'
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // 86 'V'
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // 87 'W'
    {0x63, 0x14, 0x08, 0x14, 0x63}, // 88 'X'
    {0x07, 0x08, 0x70, 0x08, 0x07}, // 89 'Y'
    {0x61, 0x51, 0x49, 0x45, 0x43}, // 90 'Z'
    {0x00, 0x7F, 0x41, 0x41, 0x00}, // 91 '['
    {0x02, 0x04, 0x08, 0x10, 0x20}, // 92 '\'
    {0x00, 0x41, 0x41, 0x7F, 0x00}, // 93 ']'
    {0x04, 0x02, 0x01, 0x02, 0x04}, // 94 '^'
    {0x40, 0x40, 0x40, 0x40, 0x40}, // 95 '_'
    {0x00, 0x01, 0x02, 0x04, 0x00}, // 96 '`'
    {0x20, 0x54, 0x54, 0x54, 0x78}, // 97 'a'
    {0x7F, 0x48, 0x44, 0x44, 0x38}, // 98 'b'
    {0x38, 0x44, 0x44, 0x44, 0x20}, // 99 'c'
    {0x38, 0x44, 0x44, 0x48, 0x7F}, // 100 'd'
    {0x38, 0x54, 0x54, 0x54, 0x18}, // 101 'e'
    {0x08, 0x7E, 0x09, 0x01, 0x02}, // 102 'f'
    {0x0C, 0x52, 0x52, 0x52, 0x3E}, // 103 'g'
    {0x7F, 0x08, 0x04, 0x04, 0x78}, // 104 'h'
    {0x00, 0x44, 0x7D, 0x40, 0x00}, // 105 'i'
    {0x20, 0x40, 0x44, 0x3D, 0x00}, // 106 'j'
    {0x7F, 0x10, 0x28, 0x44, 0x00}, // 107 'k'
    {0x00, 0x41, 0x7F, 0x40, 0x00}, // 108 'l'
    {0x7C, 0x04, 0x18, 0x04, 0x78}, // 109 'm'
    {0x7C, 0x08, 0x04, 0x04, 0x78}, // 110 'n'
    {0x38, 0x44, 0x44, 0x44, 0x38}, // 111 'o'
    {0x7C, 0x14, 0x14, 0x14, 0x08}, // 112 'p'
    {0x08, 0x14, 0x14, 0x18, 0x7C}, // 113 'q'
    {0x7C, 0x08, 0x04, 0x04, 0x08}, // 114 'r'
    {0x48, 0x54, 0x54, 0x54, 0x20}, // 115 's'
    {0x04, 0x3F, 0x44, 0x40, 0x20}, // 116 't'
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, // 117 'u'
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, // 118 'v'
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, // 119 'w'
    {0x44, 0x28, 0x10, 0x28, 0x44}, // 120 'x'
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, // 121 'y'
    {0x44, 0x64, 0x54, 0x4C, 0x44}, // 122 'z'
    {0x00, 0x08, 0x36, 0x41, 0x00}, // 123 '{'
    {0x00, 0x00, 0x7F, 0x00, 0x00}, // 124 '|'
    {0x00, 0x41, 0x36, 0x08, 0x00}, // 125 '}'
    {0x08, 0x08, 0x2A, 0x1C, 0x08}, // 126 '~'
};

inline void drawPixelInBuffer(uint8_t* buffer, int width, int height, int x, int y, uint16_t color) {
    if (!buffer || x < 0 || x >= width || y < 0 || y >= height) return;
    const uint8_t high = static_cast<uint8_t>(color >> 8);
    const uint8_t low  = static_cast<uint8_t>(color & 0xFF);
    const size_t idx = (static_cast<size_t>(y) * width + x) * 2;
    buffer[idx]     = high;
    buffer[idx + 1] = low;
}

inline void fillRectInBuffer(uint8_t* buffer, int width, int height, int x, int y, int w, int h, uint16_t color) {
    if (!buffer || w <= 0 || h <= 0) return;
    const uint8_t high = static_cast<uint8_t>(color >> 8);
    const uint8_t low  = static_cast<uint8_t>(color & 0xFF);

    for (int j = 0; j < h; ++j) {
        const int py = y + j;
        if (py < 0 || py >= height) continue;
        for (int i = 0; i < w; ++i) {
            const int px = x + i;
            if (px < 0 || px >= width) continue;
            const size_t idx = (static_cast<size_t>(py) * width + px) * 2;
            buffer[idx]     = high;
            buffer[idx + 1] = low;
        }
    }
}

inline void drawCharInBuffer(uint8_t* buffer, int width, int height, int x, int y, char c, uint16_t color, int scale) {
    if (c < 32 || c > 126) c = ' ';
    const int fontIdx = c - 32;

    for (int col = 0; col < 5; ++col) {
        const uint8_t line = kFont5x7[fontIdx][col];
        for (int row = 0; row < 7; ++row) {
            if (line & (1 << row)) {
                if (scale == 1) {
                    drawPixelInBuffer(buffer, width, height, x + col, y + row, color);
                } else {
                    fillRectInBuffer(buffer, width, height, x + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
    }
}

inline void drawTextInBuffer(uint8_t* buffer, int width, int height, int x, int y, const char* text, uint16_t color, int scale) {
    if (!text || !buffer) return;
    int cursorX = x;
    const int charWidth = 6 * scale;

    while (*text) {
        if (*text == '\n') {
            y += 8 * scale;
            cursorX = x;
        } else {
            drawCharInBuffer(buffer, width, height, cursorX, y, *text, color, scale);
            cursorX += charWidth;
        }
        text++;
    }
}

// Callback invoked in ISR context when LCD SPI GDMA finishes color transfer
static bool onLcdColorTransDone(esp_lcd_panel_io_handle_t panel_io,
                                esp_lcd_panel_io_event_data_t *edata,
                                void *user_ctx) {
    (void)panel_io;
    (void)edata;
    Display* disp = static_cast<Display*>(user_ctx);
    if (disp) {
        disp->handleTransferDone();
    }
    return false;
}

} // namespace

// TileSurface implementation
void TileSurface::clear(uint16_t color) {
    fillRectInBuffer(buffer_, width_, height_, 0, 0, width_, height_, color);
}

void TileSurface::fillRect(int x, int y, int w, int h, uint16_t color) {
    fillRectInBuffer(buffer_, width_, height_, x, y, w, h, color);
}

void TileSurface::drawPixel(int x, int y, uint16_t color) {
    drawPixelInBuffer(buffer_, width_, height_, x, y, color);
}

void TileSurface::drawFastHLine(int x, int y, int w, uint16_t color) {
    fillRect(x, y, w, 1, color);
}

void TileSurface::drawFastVLine(int x, int y, int h, uint16_t color) {
    fillRect(x, y, 1, h, color);
}

void TileSurface::drawRect(int x, int y, int w, int h, uint16_t color) {
    drawFastHLine(x, y, w, color);
    drawFastHLine(x, y + h - 1, w, color);
    drawFastVLine(x, y, h, color);
    drawFastVLine(x + w - 1, y, h, color);
}

void TileSurface::drawChar(int x, int y, char c, uint16_t color, int scale) {
    drawCharInBuffer(buffer_, width_, height_, x, y, c, color, scale);
}

void TileSurface::drawText(int x, int y, const char* text, uint16_t color, int scale) {
    drawTextInBuffer(buffer_, width_, height_, x, y, text, color, scale);
}

// Display implementation
Display::~Display() {
    if (internalAlloc_ && framebuffer_) {
        heap_caps_free(framebuffer_);
        framebuffer_ = nullptr;
    }
    if (scratchInternalAlloc_ && scratchBuffer_) {
        heap_caps_free(scratchBuffer_);
        scratchBuffer_ = nullptr;
    }
}

void Display::handleTransferDone() {
    const int64_t now = esp_timer_get_time();
    lastTransferDurationUs_ = static_cast<uint32_t>(now - submitTimeUs_);
    dmaActive_.store(false, std::memory_order_release);
    pocketpan::diag::gUiAudioCorrelation.lcdTransferActive.store(false, std::memory_order_release);
    totalTransferCount_.fetch_add(1, std::memory_order_relaxed);
}

bool Display::init(uint8_t* framebuffer) {
    if (initialized_) return true;

    // Phase A.1: Document framebuffer allocation and heap capabilities
    const size_t freeIntBefore = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t maxIntBefore = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    const size_t freePsramBefore = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(kTag, "[DISP_MEM_PRE] before: int_free=%u int_largest=%u psram_free=%u",
             (unsigned)freeIntBefore, (unsigned)maxIntBefore, (unsigned)freePsramBefore);

    if (framebuffer) {
        framebuffer_ = framebuffer;
        internalAlloc_ = false;
    } else {
#if defined(POCKETPAN_UI_PSRAM) && POCKETPAN_UI_PSRAM
        framebuffer_ = static_cast<uint8_t*>(heap_caps_malloc(kFrameBufferSize, MALLOC_CAP_SPIRAM));
#else
        framebuffer_ = static_cast<uint8_t*>(heap_caps_malloc(kFrameBufferSize,
                                                              MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
        if (!framebuffer_) {
            ESP_LOGW(kTag, "Internal SRAM (64800 B) exhausted; falling back to PSRAM");
            framebuffer_ = static_cast<uint8_t*>(heap_caps_malloc(kFrameBufferSize, MALLOC_CAP_SPIRAM));
        }
#endif
        internalAlloc_ = true;
    }

    if (!framebuffer_) {
        ESP_LOGE(kTag, "Failed to allocate display framebuffer (neither internal nor PSRAM)");
        return false;
    }

    // Allocate preallocated scratch buffer in internal DMA SRAM for rect/tile operations
    scratchBuffer_ = static_cast<uint8_t*>(heap_caps_malloc(kScratchBufferSize,
                                                            MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    scratchInternalAlloc_ = (scratchBuffer_ != nullptr);
    if (!scratchBuffer_) {
        ESP_LOGW(kTag, "Failed to allocate display scratch buffer; falling back to direct operations");
    }

    const size_t freeIntAfter = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t maxIntAfter = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    const size_t freePsramAfter = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    ESP_LOGI(kTag, "[DISP_MEM] framebuffer=%p (size=%u, align=%u, internal=%d, dma=%d, psram=%d)",
             framebuffer_, (unsigned)kFrameBufferSize, (unsigned)((uintptr_t)framebuffer_ & 0xF),
             esp_ptr_internal(framebuffer_), esp_ptr_dma_capable(framebuffer_),
             (framebuffer_ && !esp_ptr_internal(framebuffer_)) ? 1 : 0);
    ESP_LOGI(kTag, "[DISP_MEM] scratch=%p (size=%u, align=%u, internal=%d, dma=%d)",
             scratchBuffer_, (unsigned)kScratchBufferSize, (unsigned)((uintptr_t)scratchBuffer_ & 0xF),
             esp_ptr_internal(scratchBuffer_), esp_ptr_dma_capable(scratchBuffer_));
    ESP_LOGI(kTag, "[DISP_MEM] before: int_free=%u int_largest=%u psram_free=%u",
             (unsigned)freeIntBefore, (unsigned)maxIntBefore, (unsigned)freePsramBefore);
    ESP_LOGI(kTag, "[DISP_MEM] after:  int_free=%u int_largest=%u psram_free=%u",
             (unsigned)freeIntAfter, (unsigned)maxIntAfter, (unsigned)freePsramAfter);

    // 1. Initialize SPI bus (SPI2)
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = board::tft::spi::kMosiGpio;
    buscfg.miso_io_num = -1;
    buscfg.sclk_io_num = board::tft::spi::kSclkGpio;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = static_cast<int>(kFrameBufferSize);
    buscfg.flags = SPICOMMON_BUSFLAG_MASTER;

    esp_err_t err = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to initialize SPI bus: %s", esp_err_to_name(err));
        return false;
    }

    // 2. Initialize LCD Panel IO with transfer completion callback (Phase A.2)
    esp_lcd_panel_io_handle_t ioHandle = nullptr;
    esp_lcd_panel_io_spi_config_t ioConfig = {};
    ioConfig.cs_gpio_num = board::tft::spi::kCsGpio;
    ioConfig.dc_gpio_num = board::tft::kDcGpio;
    ioConfig.spi_mode = 0;
    ioConfig.pclk_hz = board::tft::kSpiClockHz; // 40 MHz
    ioConfig.trans_queue_depth = 10;
    ioConfig.on_color_trans_done = onLcdColorTransDone;
    ioConfig.user_ctx = this;
    ioConfig.lcd_cmd_bits = 8;
    ioConfig.lcd_param_bits = 8;

    err = esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(SPI2_HOST), &ioConfig, &ioHandle);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to create panel IO: %s", esp_err_to_name(err));
        return false;
    }

    // 3. Initialize ST7789 panel device
    esp_lcd_panel_dev_config_t panelConfig = {};
    panelConfig.reset_gpio_num = board::tft::kResetGpio;
    panelConfig.rgb_endian = LCD_RGB_ENDIAN_RGB;
    panelConfig.data_endian = LCD_RGB_DATA_ENDIAN_BIG;
    panelConfig.bits_per_pixel = 16;

    err = esp_lcd_new_panel_st7789(ioHandle, &panelConfig, &panelHandle_);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to create ST7789 panel: %s", esp_err_to_name(err));
        return false;
    }

    // 4. Power gate for Adafruit Feather ESP32-S3 TFT / TENSTAR board (GPIO21 HIGH)
    gpio_config_t pwr_cfg = {
        .pin_bit_mask = 1ULL << board::tft::kPowerGpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&pwr_cfg);
    gpio_set_level(static_cast<gpio_num_t>(board::tft::kPowerGpio), 1);
    vTaskDelay(pdMS_TO_TICKS(100)); // Stabilization delay

    // 5. Hardware sequence
    esp_lcd_panel_reset(panelHandle_);
    esp_lcd_panel_init(panelHandle_);
    esp_lcd_panel_invert_color(panelHandle_, true);
    esp_lcd_panel_swap_xy(panelHandle_, true);
    esp_lcd_panel_mirror(panelHandle_, false, true);
    esp_lcd_panel_set_gap(panelHandle_, board::tft::kGapX, board::tft::kGapY); // (40, 53)
    esp_lcd_panel_disp_on_off(panelHandle_, true);

    // 6. Backlight turn on (GPIO45)
    gpio_config_t bk_cfg = {
        .pin_bit_mask = 1ULL << board::tft::kBacklightGpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&bk_cfg);
    gpio_set_level(static_cast<gpio_num_t>(board::tft::kBacklightGpio), 1);

    clear(colors::Black);
    update();

    initialized_ = true;
    ESP_LOGI(kTag, "ST7789 240x135 display initialized successfully (SPI 40MHz, GDMA callback armed)");
    return true;
}

void Display::update() {
    if (!panelHandle_ || !framebuffer_) return;
#if defined(POCKETPAN_TAIL_NO_LCD_TRANSFER) && POCKETPAN_TAIL_NO_LCD_TRANSFER
    return;
#else
    submitTimeUs_ = esp_timer_get_time();
    dmaActive_.store(true, std::memory_order_release);
    pocketpan::diag::gUiAudioCorrelation.lcdTransferActive.store(true, std::memory_order_release);
    totalPixelsTransferred_.fetch_add(static_cast<uint64_t>(kWidth) * kHeight, std::memory_order_relaxed);

    const int64_t t0 = esp_timer_get_time();
    esp_lcd_panel_draw_bitmap(panelHandle_, 0, 0, kWidth, kHeight, framebuffer_);
    lastSubmitDurationUs_ = static_cast<uint32_t>(esp_timer_get_time() - t0);
#endif
}

void Display::updateRows(int y0, int y1) {
    if (!panelHandle_ || !framebuffer_) return;
#if defined(POCKETPAN_TAIL_NO_LCD_TRANSFER) && POCKETPAN_TAIL_NO_LCD_TRANSFER
    (void)y0; (void)y1;
    return;
#else
    if (y0 < 0) y0 = 0;
    if (y1 > kHeight) y1 = kHeight;
    if (y1 <= y0) return;

    submitTimeUs_ = esp_timer_get_time();
    dmaActive_.store(true, std::memory_order_release);
    pocketpan::diag::gUiAudioCorrelation.lcdTransferActive.store(true, std::memory_order_release);
    const size_t pixels = static_cast<size_t>(kWidth) * (y1 - y0);
    totalPixelsTransferred_.fetch_add(pixels, std::memory_order_relaxed);

    const int64_t t0 = esp_timer_get_time();
    esp_lcd_panel_draw_bitmap(panelHandle_, 0, y0, kWidth, y1,
                              framebuffer_ + static_cast<size_t>(y0) * kWidth * 2);
    lastSubmitDurationUs_ = static_cast<uint32_t>(esp_timer_get_time() - t0);
#endif
}

void Display::updateRect(int x, int y, int w, int h, const uint8_t* pixels) {
    if (!panelHandle_ || !pixels) return;
#if defined(POCKETPAN_TAIL_NO_LCD_TRANSFER) && POCKETPAN_TAIL_NO_LCD_TRANSFER
    (void)x; (void)y; (void)w; (void)h;
    return;
#else
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > kWidth) w = kWidth - x;
    if (y + h > kHeight) h = kHeight - y;
    if (w <= 0 || h <= 0) return;

    submitTimeUs_ = esp_timer_get_time();
    dmaActive_.store(true, std::memory_order_release);
    pocketpan::diag::gUiAudioCorrelation.lcdTransferActive.store(true, std::memory_order_release);
    totalPixelsTransferred_.fetch_add(static_cast<uint64_t>(w) * h, std::memory_order_relaxed);

    const int64_t t0 = esp_timer_get_time();
    esp_lcd_panel_draw_bitmap(panelHandle_, x, y, x + w, y + h, pixels);
    lastSubmitDurationUs_ = static_cast<uint32_t>(esp_timer_get_time() - t0);
#endif
}

void Display::updateRectFromFramebuffer(int x, int y, int w, int h) {
    if (!panelHandle_ || !framebuffer_) return;
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > kWidth) w = kWidth - x;
    if (y + h > kHeight) h = kHeight - y;
    if (w <= 0 || h <= 0) return;

    if (x == 0 && w == kWidth) {
        updateRows(y, y + h);
        return;
    }

    if (!scratchBuffer_) {
        // Fallback: update bounding rows if no scratch buffer
        updateRows(y, y + h);
        return;
    }

    const size_t rectBytes = static_cast<size_t>(w) * h * 2;
    if (rectBytes > kScratchBufferSize) {
        // Larger than scratch buffer: fallback to rows
        updateRows(y, y + h);
        return;
    }

    // Pack non-contiguous framebuffer rectangle lines into contiguous scratchBuffer
    for (int r = 0; r < h; ++r) {
        const size_t fbOffset = (static_cast<size_t>(y + r) * kWidth + x) * 2;
        const size_t scOffset = static_cast<size_t>(r) * w * 2;
        std::memcpy(scratchBuffer_ + scOffset, framebuffer_ + fbOffset, w * 2);
    }

    updateRect(x, y, w, h, scratchBuffer_);
}

void Display::benchmarkTransfers() {
    if (!panelHandle_ || !framebuffer_ || !scratchBuffer_) return;
    ESP_LOGI(kTag, "[LCDMEAS] Starting transfer benchmark (SPI2 40 MHz)...");

    // 1. Full frame: 240 x 135 (32400 pixels = 64800 bytes)
    clear(colors::Background);
    int64_t tSub0 = esp_timer_get_time();
    update();
    uint32_t subUs = static_cast<uint32_t>(esp_timer_get_time() - tSub0);
    while (isTransferActive()) { esp_rom_delay_us(10); }
    uint32_t dmaUs = lastTransferDurationUs();
    ESP_LOGI(kTag, "[LCDMEAS] full_frame 240x135 (64800 B): submit=%u us, dma=%u us",
             (unsigned)subUs, (unsigned)dmaUs);

    vTaskDelay(pdMS_TO_TICKS(50));

    // 2. Partial row band: 240 x 20 (4800 pixels = 9600 bytes)
    tSub0 = esp_timer_get_time();
    updateRows(0, 20);
    subUs = static_cast<uint32_t>(esp_timer_get_time() - tSub0);
    while (isTransferActive()) { esp_rom_delay_us(10); }
    dmaUs = lastTransferDurationUs();
    ESP_LOGI(kTag, "[LCDMEAS] row_band 240x20 (9600 B): submit=%u us, dma=%u us",
             (unsigned)subUs, (unsigned)dmaUs);

    vTaskDelay(pdMS_TO_TICKS(50));

    // 3. Small rect: 80 x 16 (1280 pixels = 2560 bytes)
    tSub0 = esp_timer_get_time();
    updateRect(12, 40, 80, 16, scratchBuffer_);
    subUs = static_cast<uint32_t>(esp_timer_get_time() - tSub0);
    while (isTransferActive()) { esp_rom_delay_us(10); }
    dmaUs = lastTransferDurationUs();
    ESP_LOGI(kTag, "[LCDMEAS] small_rect 80x16 (2560 B): submit=%u us, dma=%u us",
             (unsigned)subUs, (unsigned)dmaUs);
}

void Display::clear(uint16_t color) {
    fillRectInBuffer(framebuffer_, kWidth, kHeight, 0, 0, kWidth, kHeight, color);
}

void Display::drawPixel(int x, int y, uint16_t color) {
    drawPixelInBuffer(framebuffer_, kWidth, kHeight, x, y, color);
}

void Display::fillRect(int x, int y, int w, int h, uint16_t color) {
    fillRectInBuffer(framebuffer_, kWidth, kHeight, x, y, w, h, color);
}

void Display::drawFastHLine(int x, int y, int w, uint16_t color) {
    fillRect(x, y, w, 1, color);
}

void Display::drawFastVLine(int x, int y, int h, uint16_t color) {
    fillRect(x, y, 1, h, color);
}

void Display::drawRect(int x, int y, int w, int h, uint16_t color) {
    drawFastHLine(x, y, w, color);
    drawFastHLine(x, y + h - 1, w, color);
    drawFastVLine(x, y, h, color);
    drawFastVLine(x + w - 1, y, h, color);
}

void Display::drawChar(int x, int y, char c, uint16_t color, int scale) {
    drawCharInBuffer(framebuffer_, kWidth, kHeight, x, y, c, color, scale);
}

void Display::drawText(int x, int y, const char* text, uint16_t color, int scale) {
    drawTextInBuffer(framebuffer_, kWidth, kHeight, x, y, text, color, scale);
}

} // namespace pocketpan::hardware

#else

namespace pocketpan::hardware {

TileSurface::TileSurface(uint8_t* buffer, int width, int height)
    : buffer_(buffer), width_(width), height_(height) {}
void TileSurface::clear(uint16_t) {}
void TileSurface::fillRect(int, int, int, int, uint16_t) {}
void TileSurface::drawPixel(int, int, uint16_t) {}
void TileSurface::drawFastHLine(int, int, int, uint16_t) {}
void TileSurface::drawFastVLine(int, int, int, uint16_t) {}
void TileSurface::drawRect(int, int, int, int, uint16_t) {}
void TileSurface::drawChar(int, int, char, uint16_t, int) {}
void TileSurface::drawText(int, int, const char*, uint16_t, int) {}

Display::~Display() = default;
bool Display::init(uint8_t*) { initialized_ = true; return true; }
void Display::update() {}
void Display::updateRows(int, int) {}
void Display::updateRect(int, int, int, int, const uint8_t*) {}
void Display::updateRectFromFramebuffer(int, int, int, int) {}
void Display::handleTransferDone() {}
void Display::benchmarkTransfers() {}
void Display::clear(uint16_t) {}
void Display::drawPixel(int, int, uint16_t) {}
void Display::fillRect(int, int, int, int, uint16_t) {}
void Display::drawFastHLine(int, int, int, uint16_t) {}
void Display::drawFastVLine(int, int, int, uint16_t) {}
void Display::drawRect(int, int, int, int, uint16_t) {}
void Display::drawChar(int, int, char, uint16_t, int) {}
void Display::drawText(int, int, const char*, uint16_t, int) {}

} // namespace pocketpan::hardware

#endif
