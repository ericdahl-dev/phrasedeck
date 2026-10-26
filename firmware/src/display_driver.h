// Panel bring-up for the Elecrow CrowPanel Advance 7.0-HMI.
//
// Wraps the 800x480 16-bit RGB565 IPS panel in a LovyanGFX device and hands
// LVGL 9 a pair of draw buffers (internal RAM, PSRAM if that's short).
//
// This driver does NOT own the backlight. On this board the backlight is an
// I2C command to a companion microcontroller; see panel_mcu.h. Elecrow's own
// driver has the LovyanGFX Light_PWM block commented out for the same reason.
//
// Every pin, timing and clock value comes from board_pins.h. Nothing here
// hardcodes a GPIO number.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#pragma once

#include <LovyanGFX.hpp>
// LovyanGFX.hpp does not pull in the RGB parallel bus and panel; they are
// ESP32-S3 specific and have to be asked for by name.
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lvgl.h>

#include "board_pins.h"

class LGFX : public lgfx::LGFX_Device {
  public:
    LGFX();

  private:
    lgfx::Bus_RGB   bus_;
    lgfx::Panel_RGB panel_;
};

// The single panel instance. Defined in display_driver.cpp.
extern LGFX display_lcd;

// Initializes LVGL, brings up the panel, allocates the draw buffers and
// registers the LVGL display. Also installs a millis()-backed LVGL tick source,
// so the caller only has to pump lv_timer_handler().
//
// Does NOT turn the backlight on - that belongs to panel_mcu and must happen
// after this returns.
//
// Returns false if the panel or, far more likely, the PSRAM allocation failed.
// A false return is fatal: there is nothing to draw on. At 800x480 and 16 bpp a
// full frame is 768 KB and cannot come from internal SRAM, so a build that
// omits PSRAM or selects quad instead of octal fails here rather than at
// compile time.
bool display_init();

// The LVGL display registered by display_init(), or nullptr before it runs.
lv_display_t* display_lvgl();
