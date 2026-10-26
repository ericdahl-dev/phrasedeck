// Implementation of the GT911 driver.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include "touch.h"

#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>

#include "board_pins.h"
#include "panel_mcu.h"

namespace {

// GT911 register map, from the Goodix programming guide.
constexpr uint16_t REG_STATUS   = 0x814E;
constexpr uint16_t REG_POINT1_X = 0x8150;

uint8_t g_addr = 0;

bool g_pressed = false;
int16_t g_x = 0;
int16_t g_y = 0;

uint32_t g_last_poll_ms = 0;

bool readRegs(uint16_t reg, uint8_t* out, size_t len) {
    Wire.beginTransmission(g_addr);
    Wire.write(static_cast<uint8_t>(reg >> 8));
    Wire.write(static_cast<uint8_t>(reg & 0xFF));
    if (Wire.endTransmission(false) != 0) return false;

    if (Wire.requestFrom(static_cast<int>(g_addr), static_cast<int>(len)) !=
        static_cast<int>(len)) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) out[i] = Wire.read();
    return true;
}

bool writeReg(uint16_t reg, uint8_t value) {
    Wire.beginTransmission(g_addr);
    Wire.write(static_cast<uint8_t>(reg >> 8));
    Wire.write(static_cast<uint8_t>(reg & 0xFF));
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool probe(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

void lvgl_read_cb(lv_indev_t*, lv_indev_data_t* data) {
    // Reads cached state only. The actual I2C transaction happens in
    // touch_poll() on its own interval, because LVGL calls this far more often
    // than this bus can afford.
    data->state = g_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->point.x = g_x;
    data->point.y = g_y;
}

}  // namespace

bool touch_init() {
    // Hold the interrupt line low across the reset so the controller latches
    // 0x5D rather than 0x14. Reset itself belongs to the companion MCU.
    pinMode(board::TP_INT, OUTPUT);
    digitalWrite(board::TP_INT, LOW);
    panel_mcu::activateTouch();
    delay(120);
    pinMode(board::TP_INT, INPUT);
    delay(100);

    if (probe(board::GT911_ADDR_PRIMARY)) {
        g_addr = board::GT911_ADDR_PRIMARY;
    } else if (probe(board::GT911_ADDR_BACKUP)) {
        g_addr = board::GT911_ADDR_BACKUP;
        Serial.println("touch: GT911 answered at 0x14, not 0x5D.");
        Serial.println("touch: the address latch did not take. Coordinates");
        Serial.println("touch: will still work; the sequence is suspect.");
    } else {
        Serial.println("touch: no GT911 at 0x5D or 0x14");
        return false;
    }

    Serial.printf("touch: GT911 at 0x%02X\n", g_addr);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lvgl_read_cb);
    return true;
}

uint8_t touch_address() { return g_addr; }

void touch_poll() {
    if (g_addr == 0) return;

    const uint32_t now = millis();
    if (now - g_last_poll_ms < board::TOUCH_POLL_MS) return;
    g_last_poll_ms = now;

    // One transaction, not two. The GT911's status byte and the first touch
    // point are contiguous (0x814E status, 0x8150..0x8153 X and Y), so a single
    // six-byte read fetches both. Two separate reads occupy this bus twice as
    // long, and this bus is shared with the panel's companion MCU while the RGB
    // DMA is scanning - which is Elecrow's own issue #8, visible as jitter
    // while a finger is down.
    uint8_t buf[6];
    if (!readRegs(REG_STATUS, buf, sizeof(buf))) return;

    const uint8_t status = buf[0];

    // Bit 7 means the controller has a fresh result; the low nibble is the
    // number of points.
    if ((status & 0x80) == 0) return;

    if ((status & 0x0F) > 0) {
        const int16_t x =
            static_cast<int16_t>(buf[2] | (static_cast<uint16_t>(buf[3]) << 8));
        const int16_t y =
            static_cast<int16_t>(buf[4] | (static_cast<uint16_t>(buf[5]) << 8));

        // Clamp rather than trust. A glitched read that lands off-screen would
        // otherwise send LVGL a pointer outside every object.
        g_x = x < 0 ? 0 : (x >= board::LCD_WIDTH ? board::LCD_WIDTH - 1 : x);
        g_y = y < 0 ? 0 : (y >= board::LCD_HEIGHT ? board::LCD_HEIGHT - 1 : y);
        g_pressed = true;
    } else {
        g_pressed = false;
    }

    // The status register must be cleared or the controller stops reporting.
    writeReg(REG_STATUS, 0);
}

bool touch_pressed() { return g_pressed; }

int16_t touch_x() { return g_x; }
int16_t touch_y() { return g_y; }
