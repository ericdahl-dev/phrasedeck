// Implementation of the companion MCU driver.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include "panel_mcu.h"

#include <Arduino.h>
#include <Wire.h>

#include "board_pins.h"

namespace panel_mcu {
namespace {

// V1.3 and later.
constexpr uint8_t CMD_V13_BRIGHTEST = 0;
constexpr uint8_t CMD_V13_DIMMEST   = 244;
constexpr uint8_t CMD_V13_OFF       = 245;
constexpr uint8_t CMD_V13_TOUCH     = 250;

// V1.2.
constexpr uint8_t CMD_V12_ON    = 0x10;
constexpr uint8_t CMD_V12_OFF   = 0x05;
constexpr uint8_t CMD_V12_TOUCH = 0x19;

int g_revision = 0;

// A single bare byte, no register address. Returns true if the device ACKed.
bool send(uint8_t command) {
    Wire.beginTransmission(board::PANEL_MCU_ADDR);
    Wire.write(command);
    return Wire.endTransmission() == 0;
}

bool present() {
    Wire.beginTransmission(board::PANEL_MCU_ADDR);
    return Wire.endTransmission() == 0;
}

}  // namespace

bool begin() {
    g_revision = 0;

    if (!present()) {
        Serial.printf("panel_mcu: nothing at 0x%02X.\n", board::PANEL_MCU_ADDR);
        Serial.println("panel_mcu: a V1.0 board has an expander at 0x18 instead,");
        Serial.println("panel_mcu: which this firmware does not support.");
        return false;
    }

    // The device ACKs any byte, so probing cannot distinguish the revisions by
    // return value. Trust the compile-time setting, and where it is the default
    // prefer V1.3+ because that is what current stock ships.
    g_revision = CROWPANEL_ADVANCE_REV;
    Serial.printf("panel_mcu: found at 0x%02X, driving it as V%d.%d\n",
                  board::PANEL_MCU_ADDR, g_revision / 100, (g_revision / 10) % 10);
    Serial.println("panel_mcu: if the screen stays dark, check the silkscreen and");
    Serial.println("panel_mcu: set CROWPANEL_ADVANCE_REV in board_pins.h.");
    return true;
}

int detectedRevision() { return g_revision; }

void backlightOn() { backlightLevel(100); }

void backlightOff() {
#if CROWPANEL_ADVANCE_REV >= 130
    send(CMD_V13_OFF);
#else
    send(CMD_V12_OFF);
#endif
}

void backlightLevel(uint8_t percent) {
    if (percent > 100) percent = 100;

    if (percent == 0) {
        backlightOff();
        return;
    }

#if CROWPANEL_ADVANCE_REV >= 130
    // 0 is brightest and 244 is dimmest, so the scale runs backwards from every
    // intuition about brightness values.
    const uint32_t span  = CMD_V13_DIMMEST - CMD_V13_BRIGHTEST;
    const uint8_t  value = static_cast<uint8_t>(span - (span * percent) / 100);
    send(value);
#else
    // V1.2 accepts 0x05 to 0x10, and dimming requires sending 0x10 first.
    send(CMD_V12_ON);
    const uint32_t span  = CMD_V12_ON - CMD_V12_OFF;
    const uint8_t  value = static_cast<uint8_t>(CMD_V12_OFF + (span * percent) / 100);
    send(value);
#endif
}

void activateTouch() {
#if CROWPANEL_ADVANCE_REV >= 130
    send(CMD_V13_TOUCH);
#else
    send(CMD_V12_TOUCH);
#endif
}

}  // namespace panel_mcu
