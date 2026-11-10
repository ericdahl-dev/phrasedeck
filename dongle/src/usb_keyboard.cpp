#include "usb_keyboard.h"

#include <Arduino.h>
#include <USB.h>
#include <USBHIDKeyboard.h>

#include <atomic>

#include "tusb.h"

namespace usb_keyboard {

// The IDs the Computer sees, as the README gives them. They come from the esp32s3 variant's
// pins_arduino.h, so a core update that changes them fails the build here.
static_assert(USB_VID == 0x303A, "README: USB vendor 0x303A");
static_assert(USB_PID == 0x1001, "README: USB product 0x1001");

namespace {

// USBHIDKeyboard adds the boot keyboard interface and its report descriptor. Its sendReport()
// returns nothing, so reports go through a second USBHID handle on the same (static) TinyUSB HID
// state: its SendReport() says whether the host took the report.
USBHIDKeyboard g_keyboard;
USBHID g_hid(HID_ITF_PROTOCOL_KEYBOARD);

std::atomic<unsigned> g_disconnects{0};

void onUsbEvent(void*, esp_event_base_t base, int32_t id, void*) {
    if (base != ARDUINO_USB_EVENTS) return;
    switch (id) {
        case ARDUINO_USB_STARTED_EVENT: Serial.println("usb: configured by the Computer"); break;
        case ARDUINO_USB_STOPPED_EVENT:
            g_disconnects.fetch_add(1);
            break;
        case ARDUINO_USB_SUSPEND_EVENT:
            g_disconnects.fetch_add(1);
            break;
        case ARDUINO_USB_RESUME_EVENT: break;
        default: break;
    }
}

}  // namespace

// The keyboard interface was added by g_keyboard's constructor, before the core started USB (CDC on
// boot starts it before setup()); the names are build flags for the same reason.
void start() {
    USB.onEvent(onUsbEvent);
    g_keyboard.begin();
    g_hid.begin();
}

bool Port::ready() { return tud_mounted() && !tud_suspended(); }

bool Port::report(uint8_t mod, uint8_t code) {
    hid_keyboard_report_t r{};
    r.modifier = mod;
    r.keycode[0] = code;
    const bool ok = g_hid.SendReport(HID_REPORT_ID_KEYBOARD, &r, sizeof r, 20);
    reported_ = reported_ || ok;
    return ok;
}

unsigned Port::disconnects() { return g_disconnects.load(); }

uint32_t Port::now() { return millis(); }

// A report that went already blocked until the host polled (a frame), letting other tasks run, so
// the loop goes on at once: one report per frame at a key interval of 0. Otherwise wait a tick.
void Port::yield() {
    if (!reported_) vTaskDelay(1);
    reported_ = false;
}

void Port::sleep(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms < 1 ? 1 : ms)); }

}  // namespace usb_keyboard
