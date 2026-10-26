// GT911 capacitive touch over raw I2C, polled.
//
// Two things make this board's touch worth its own module.
//
// First, the GT911 latches its I2C address from the state of its interrupt line
// during reset: low gives 0x5D, high gives 0x14. Reset is not an ESP32 GPIO
// here - it belongs to the companion MCU - so the sequence is: hold the
// interrupt pin low, ask panel_mcu to reset, wait, release. Both addresses are
// probed afterwards so a mis-latched controller announces itself instead of
// presenting as "touch does not work".
//
// Second, polling this bus too eagerly breaks the display. Blocking GT911 reads
// starve the RGB panel's DMA and produce visible shake; this is Elecrow's own
// issue #8. touch_poll() therefore rate-limits itself to board::TOUCH_POLL_MS
// and is safe to call every loop iteration.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#pragma once

#include <cstdint>

// Runs the address-latching reset sequence, probes both addresses, and
// registers an LVGL pointer device. Wire.begin() and panel_mcu::begin() must
// have run first.
//
// Returns false if neither address answered. Not fatal: the display is useful
// without touch.
bool touch_init();

// The address that answered, or 0 if none did. Log it - a controller that came
// up at 0x14 is the single most likely touch fault on this family of boards.
uint8_t touch_address();

// Reads the controller at most once per board::TOUCH_POLL_MS. Safe and intended
// to be called every loop iteration.
void touch_poll();

// True while a finger is down, as of the last poll.
bool touch_pressed();

// Where that finger is, in panel coordinates, as of the last poll. Only
// meaningful while touch_pressed() is true; otherwise these hold wherever the
// last press was. Published so the application can tell one region of the glass
// from another - the driver has always tracked them for LVGL.
int16_t touch_x();
int16_t touch_y();
