// Implementation of the panel driver and the LVGL 9 binding.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include "display_driver.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

LGFX display_lcd;

namespace {

lv_display_t* g_disp = nullptr;

// Ten lines of partial rendering. Full double buffering at this size would be
// 1.5 MB, which PSRAM could afford but which buys nothing here: the UI redraws
// small regions - two numbers and two charts - not whole frames.
constexpr int   DRAW_LINES = 10;
constexpr size_t DRAW_PIXELS =
    static_cast<size_t>(board::LCD_WIDTH) * DRAW_LINES;

void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    const int32_t w = area->x2 - area->x1 + 1;
    const int32_t h = area->y2 - area->y1 + 1;

    display_lcd.pushImageDMA(area->x1, area->y1, w, h,
                             reinterpret_cast<lgfx::rgb565_t*>(px_map));
    lv_display_flush_ready(disp);
}

uint32_t tick_cb() { return millis(); }

}  // namespace

LGFX::LGFX() {
    {
        auto cfg = bus_.config();

        cfg.panel = &panel_;

        cfg.pin_d0  = board::LCD_B0;
        cfg.pin_d1  = board::LCD_B1;
        cfg.pin_d2  = board::LCD_B2;
        cfg.pin_d3  = board::LCD_B3;
        cfg.pin_d4  = board::LCD_B4;
        cfg.pin_d5  = board::LCD_G0;
        cfg.pin_d6  = board::LCD_G1;
        cfg.pin_d7  = board::LCD_G2;
        cfg.pin_d8  = board::LCD_G3;
        cfg.pin_d9  = board::LCD_G4;
        cfg.pin_d10 = board::LCD_G5;
        cfg.pin_d11 = board::LCD_R0;
        cfg.pin_d12 = board::LCD_R1;
        cfg.pin_d13 = board::LCD_R2;
        cfg.pin_d14 = board::LCD_R3;
        cfg.pin_d15 = board::LCD_R4;

        cfg.pin_henable = board::LCD_DE;
        cfg.pin_vsync   = board::LCD_VSYNC;
        cfg.pin_hsync   = board::LCD_HSYNC;
        cfg.pin_pclk    = board::LCD_PCLK;
        cfg.freq_write  = board::LCD_PCLK_HZ;

        cfg.hsync_polarity    = board::LCD_HSYNC_POLARITY;
        cfg.hsync_front_porch = board::LCD_HSYNC_FRONT_PORCH;
        cfg.hsync_pulse_width = board::LCD_HSYNC_PULSE_WIDTH;
        cfg.hsync_back_porch  = board::LCD_HSYNC_BACK_PORCH;

        cfg.vsync_polarity    = board::LCD_VSYNC_POLARITY;
        cfg.vsync_front_porch = board::LCD_VSYNC_FRONT_PORCH;
        cfg.vsync_pulse_width = board::LCD_VSYNC_PULSE_WIDTH;
        cfg.vsync_back_porch  = board::LCD_VSYNC_BACK_PORCH;

        cfg.pclk_idle_high = board::LCD_PCLK_IDLE_HIGH;

        bus_.config(cfg);
    }
    {
        auto cfg = panel_.config();
        cfg.memory_width  = board::LCD_WIDTH;
        cfg.memory_height = board::LCD_HEIGHT;
        cfg.panel_width   = board::LCD_WIDTH;
        cfg.panel_height  = board::LCD_HEIGHT;
        cfg.offset_x      = 0;
        cfg.offset_y      = 0;
        panel_.config(cfg);
    }

    panel_.setBus(&bus_);
    setPanel(&panel_);
}

bool display_init() {
    if (!display_lcd.init()) {
        Serial.println("display: lcd.init() failed");
        return false;
    }
    display_lcd.setColorDepth(16);

    lv_init();
    lv_tick_set_cb(tick_cb);

    // The draw buffers go in INTERNAL DMA-capable RAM, not PSRAM, and that is
    // a display-quality decision rather than a memory one.
    //
    // The RGB panel scans its framebuffer out of PSRAM continuously. Put the
    // draw buffer in PSRAM too and every flush both reads and writes PSRAM,
    // competing with that scanout for the same bus and starving its DMA. On
    // real hardware that shows up as occasional jitter, worse while the touch
    // controller is also active. From internal RAM a flush only writes to
    // PSRAM, halving the contention. Two 16 KB buffers is affordable against
    // roughly 140 KB free.
    //
    // Falls back to PSRAM if internal memory is short, because a jittery
    // display beats no display. The fallback must NOT ask for MALLOC_CAP_DMA:
    // external RAM on the ESP32-S3 is never registered DMA-capable, so every
    // such request returns NULL.
    const size_t bytes = DRAW_PIXELS * sizeof(uint16_t);
    bool  internal = true;
    void* buf1 = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    void* buf2 = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (buf1 == nullptr || buf2 == nullptr) {
        heap_caps_free(buf1);
        heap_caps_free(buf2);
        internal = false;
        buf1 = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
        buf2 = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    }
    if (buf1 == nullptr || buf2 == nullptr) {
        heap_caps_free(buf1);
        heap_caps_free(buf2);
        Serial.println("display: draw buffer allocation failed");
        Serial.println("display: check that the build selects OPI PSRAM,");
        Serial.println("display: and that PSRAM initialized - this asks for");
        Serial.printf("display: 2 x %u bytes of external RAM\n",
                      static_cast<unsigned>(bytes));
        return false;
    }

    g_disp = lv_display_create(board::LCD_WIDTH, board::LCD_HEIGHT);
    if (g_disp == nullptr) {
        heap_caps_free(buf1);
        heap_caps_free(buf2);
        Serial.println("display: lv_display_create failed");
        return false;
    }
    lv_display_set_flush_cb(g_disp, flush_cb);
    lv_display_set_buffers(g_disp, buf1, buf2, bytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    Serial.printf("display: %dx%d up, pclk %u Hz, draw buffers in %s\n",
                  board::LCD_WIDTH, board::LCD_HEIGHT,
                  static_cast<unsigned>(board::LCD_PCLK_HZ),
                  internal ? "internal RAM" : "PSRAM (expect jitter)");
    return true;
}

lv_display_t* display_lvgl() { return g_disp; }
