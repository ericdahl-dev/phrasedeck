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

}  // namespace dongle_trust
