// Pin assignments for the Elecrow CrowPanel Advance 7.0-HMI.
//
// SKU DIS02170A, ESP32-S3-WROOM-1-N16R8.
//
// Every value here is quoted from Elecrow's own LovyanGFX_Driver.h and
// cross-checked against the net names in their V1.5 Eagle schematic. See
// docs/HARDWARE.md for sources and for the places where Elecrow's own
// documentation contradicts itself.
//
// DO NOT copy values from the sibling CrowPanel 7.0 HMI project. That board is
// a different product: different I2C pins, different porches, a quarter of the
// flash, and a backlight on a GPIO rather than behind an I2C command.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#pragma once

#include <cstdint>

// Board revision. The backlight encoding, the touch activation command and the
// pixel clock all depend on it, and it is printed on the silkscreen.
//   130 = V1.3, V1.4, V1.5   (current stock)
//   120 = V1.2
// Set this once the physical board is in hand and its silkscreen has been read.
// It cannot be detected at run time: the companion MCU acknowledges any byte,
// so a write can never report whether it was understood. panel_mcu logs which
// revision it is driving at startup - check that against the silkscreen.
#ifndef CROWPANEL_ADVANCE_REV
#define CROWPANEL_ADVANCE_REV 130
#endif

namespace board {

// ---------------------------------------------------------------------------
// Display: 16-bit RGB565 parallel. Identical in every revision, V1.0 to V1.5.
// ---------------------------------------------------------------------------
// Elecrow name the schematic nets in RGB888 bit positions, so the lowest red
// line is called R3 and the highest R7. That is correct: a 16-bit bus feeding a
// 6-bit-per-channel panel lands on R3-R7, G2-G7 and B3-B7, and the panel's
// unused low bits are tied to ground. Do not renumber them.
constexpr int LCD_R0 = 7;    // net IO7_R3
constexpr int LCD_R1 = 17;   // net IO17_R4
constexpr int LCD_R2 = 18;   // net IO18_R5
constexpr int LCD_R3 = 3;    // net IO3_R6   - also a strapping pin
constexpr int LCD_R4 = 46;   // net IO46_R7  - also a strapping pin

constexpr int LCD_G0 = 9;    // net IO9_G2
constexpr int LCD_G1 = 10;   // net IO10_G3
constexpr int LCD_G2 = 11;   // net IO11_G4
constexpr int LCD_G3 = 12;   // net IO12_G5
constexpr int LCD_G4 = 13;   // net IO13_G6
constexpr int LCD_G5 = 14;   // net IO14_G7

constexpr int LCD_B0 = 21;   // net IO21_B3
constexpr int LCD_B1 = 47;   // net IO47_B4
constexpr int LCD_B2 = 48;   // net IO48_B5  - also a strapping pin
constexpr int LCD_B3 = 45;   // net IO45_B6  - also a strapping pin
constexpr int LCD_B4 = 38;   // net IO38_B7

constexpr int LCD_DE    = 42;  // net IO42_DE
constexpr int LCD_VSYNC = 41;  // net IO41_VSYNC
constexpr int LCD_HSYNC = 40;  // net IO40_HSYNC
constexpr int LCD_PCLK  = 39;  // net IO39_CLK_DCLK

constexpr int LCD_WIDTH  = 800;
constexpr int LCD_HEIGHT = 480;

// Elecrow LOWERED the pixel clock between V1.2 and V1.3. Read verbatim from
// their three driver files. This single fact reconciles most of the
// contradictory advice in circulation.
#if CROWPANEL_ADVANCE_REV >= 130
constexpr uint32_t LCD_PCLK_HZ = 16000000;
#else
constexpr uint32_t LCD_PCLK_HZ = 21000000;
#endif

// Porches are identical in every revision, and both axes use the same values.
// That looks like a copy-paste error and is not.
constexpr int LCD_HSYNC_POLARITY    = 0;
constexpr int LCD_HSYNC_FRONT_PORCH = 8;
constexpr int LCD_HSYNC_PULSE_WIDTH = 4;
constexpr int LCD_HSYNC_BACK_PORCH  = 8;

constexpr int LCD_VSYNC_POLARITY    = 0;
constexpr int LCD_VSYNC_FRONT_PORCH = 8;
constexpr int LCD_VSYNC_PULSE_WIDTH = 4;
constexpr int LCD_VSYNC_BACK_PORCH  = 8;

constexpr bool LCD_PCLK_IDLE_HIGH = true;

// ---------------------------------------------------------------------------
// Backlight: NOT A GPIO.
// ---------------------------------------------------------------------------
// The MT9201 boost is driven by a companion microcontroller. You turn the
// backlight on by sending an I2C command. See panel_mcu.h. Elecrow's own
// driver has the LovyanGFX Light_PWM block commented out entirely, including
// `// cfg.pin_bl = GPIO_NUM_2;` - there is no LovyanGFX-managed backlight here.

// ---------------------------------------------------------------------------
// I2C: touch, the companion MCU, the real-time clock, and the external header.
// ---------------------------------------------------------------------------
constexpr int      I2C_SDA = 15;
constexpr int      I2C_SCL = 16;
constexpr uint32_t I2C_HZ  = 400000;

// GT911 capacitive touch, 5-point. It latches its address from the state of its
// interrupt line during reset, so probe both rather than assuming.
constexpr uint8_t GT911_ADDR_PRIMARY = 0x5D;  // INT low at reset
constexpr uint8_t GT911_ADDR_BACKUP  = 0x14;  // INT high at reset

// The GT911 interrupt is wired to this pin even though the driver polls. It is
// held low across reset to force address 0x5D.
constexpr int TP_INT = 1;

// STC8H1K28 companion MCU: backlight, touch reset, buzzer, amplifier control
// and battery charger status. V1.2 and later. On V1.0 this is an I/O expander
// at 0x18 instead, which this firmware does not support.
constexpr uint8_t PANEL_MCU_ADDR = 0x30;

// PCF8563 real-time clock with a CR1220 backup cell. Unused by this firmware,
// recorded so nothing else claims the address. Note that 0x51 comes from the
// part's datasheet: no Elecrow code touches the RTC.
constexpr uint8_t RTC_ADDR = 0x51;

// ---------------------------------------------------------------------------
// Timing.
// ---------------------------------------------------------------------------
// Blocking GT911 reads on the shared I2C bus starve the RGB panel's DMA and
// produce visible display shake. This is Elecrow's own issue #8. Poll on a
// fixed interval, never per-loop.
constexpr uint32_t TOUCH_POLL_MS = 20;

// ---------------------------------------------------------------------------
// Unused here, recorded so nothing else claims them by accident.
// ---------------------------------------------------------------------------
// GPIO4/5/6 are microSD and the I2S amplifier, multiplexed by a CH486F analog
// switch under the S0/S1 DIP switches. GPIO19/20 are the PDM microphone, the
// radio socket and UART1, behind a second switch. GPIO43/44 are UART0 to the
// CH340K. GPIO0 is the boot strapping pin and the auto-download circuit.
//
// GPIO2 and GPIO8 are the ONLY genuinely free signal pins, and only on V1.3+.
// On V1.0 and V1.2 GPIO2 is the I2S microphone's word-select and GPIO8 is the
// buzzer.
constexpr int FREE_GPIO_A = 2;   // V1.3+ only
constexpr int FREE_GPIO_B = 8;   // V1.3+ only

// GPIO26-32 are the SPI flash and GPIO33-37 are consumed by the octal PSRAM on
// this R8 module. Neither group is usable. GPIO22-25 do not exist on the S3.

}  // namespace board
