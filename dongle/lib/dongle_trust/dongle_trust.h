// Who the dongle trusts (#198). Bonding and who may bond are bt_pairing's rules, shared with the
// panel; this adds the BOOT long press that forgets the phone, and the Token as text.
#pragma once

#include <cstdint>
#include <string>

#include "phrase_link.h"

namespace dongle_trust {

// How long after its bond the phone may read the Token. The app reads it at once and keeps it.
constexpr uint32_t kTokenWindowMs = 2 * 60 * 1000;

// The Token characteristic's value for a reader: the Token as 32 lowercase hex digits, but only for
// the phone (the trusted connection) while its window after bonding is open; empty for anyone else.
inline std::string tokenText(const phrase_link::Token& token, bool isPhone, bool windowOpen) {
    if (!isPhone || !windowOpen) return "";
    static const char hex[] = "0123456789abcdef";
    std::string s;
    for (uint8_t b : token) {
        s += hex[b >> 4];
        s += hex[b & 0xF];
    }
    return s;
}

// A new phone may bond only while the dongle has none (new, or reset with BOOT): one phone.
inline bool acceptsNewPhone(bool hasPhone) { return !hasPhone; }

// A button held down for ms. update() is polled with the button's state and millis(); it's true
// once per press, when the press reaches ms. Unsigned subtraction keeps it right across the wrap.
class Hold {
  public:
    explicit Hold(uint32_t ms) : ms_(ms) {}
    bool update(bool pressed, uint32_t now) {
        if (!pressed) {
            down_ = fired_ = false;
            return false;
        }
        if (!down_) {
            down_ = true;
            since_ = now;
        }
        if (fired_ || now - since_ < ms_) return false;
        fired_ = true;
        return true;
    }

  private:
    uint32_t ms_;
    uint32_t since_ = 0;
    bool down_ = false;
    bool fired_ = false;
};

// How long a forget waits for BOOT to come up before it restarts anyway.
constexpr uint32_t kReleaseWaitMs = 30 * 1000;

// What forgetting the phone needs from the dongle. ble_link has the real one.
class Device {
  public:
    virtual ~Device() = default;
    virtual bool eraseBonds() = 0;  // every Bluetooth bond in the store
    virtual bool erasePhone() = 0;  // the phone's address and its Token
    virtual bool bootHeld() = 0;
    virtual uint32_t now() = 0;
    virtual void sleep(uint32_t ms) = 0;
    virtual void restart() = 0;
};

// Forgets the phone: the BOOT long press and the serial `forget` both run this, so they do the
// same thing. The bonds and the phone go now, not on the next boot. The restart then waits for BOOT
// (GPIO0, a strapping pin) to come up, for at most kReleaseWaitMs. False, with no restart, if
// either couldn't be erased.
inline bool forget(Device& d) {
    const bool bonds = d.eraseBonds();
    const bool phone = d.erasePhone();
    if (!bonds || !phone) return false;
    const uint32_t since = d.now();
    while (d.bootHeld() && d.now() - since < kReleaseWaitMs) d.sleep(20);
    d.restart();
    return true;
}

}  // namespace dongle_trust
