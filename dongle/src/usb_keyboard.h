// The dongle's USB side (#198): TinyUSB on the S3's native port, a HID keyboard (boot protocol,
// 1 ms polling) next to the CDC serial port.
#pragma once

#include "send.h"

namespace usb_keyboard {

// Watches the USB events and readies the keyboard's report path. Call once from setup().
void start();

// send::Keyboard over USB: ready() while the Computer has the dongle configured and awake; a
// report waits for the host to take it (one per 1 ms frame) or 20 ms; disconnects() bumps when the
// cable comes out or the Computer suspends the bus (sleep).
class Port : public send::Keyboard {
  public:
    bool ready() override;
    bool report(uint8_t mod, uint8_t code) override;
    unsigned disconnects() override;
    uint32_t now() override;
    void yield() override;
    void sleep(uint32_t ms) override;

  private:
    bool reported_ = false;  // a report went since the last yield()
};

}  // namespace usb_keyboard
