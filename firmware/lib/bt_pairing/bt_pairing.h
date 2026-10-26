// Who may pair with the panel as a Bluetooth keyboard (#94). Everything it types goes to the paired
// Computer, and a Phrase can be a secret, so a new Computer pairs only while a window opened from
// Settings (behind the admin password) is open, and then it replaces any other: one Computer.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace bt_pairing {

enum class Bond { Keep, Reject, ReplaceOthers };

// After a pairing completes. newBond: this Computer wasn't paired before.
inline Bond onPaired(bool newBond, bool windowOpen) {
    if (!newBond) return Bond::Keep;
    return windowOpen ? Bond::ReplaceOthers : Bond::Reject;
}

// The pairing window. now is millis(); unsigned subtraction keeps it right across the wrap.
class Window {
  public:
    void open(uint32_t now, uint32_t ms) {
        opened_ = now;
        ms_ = ms;
        open_ = true;
    }
    void close() { open_ = false; }
    bool isOpen(uint32_t now) const { return open_ && now - opened_ < ms_; }

  private:
    uint32_t opened_ = 0;
    uint32_t ms_ = 0;
    bool open_ = false;
};

// The passkey as the Computer asks for it: 6 digits, leading zeros kept. Empty for none (-1).
inline std::string passkeyText(int32_t passkey) {
    if (passkey < 0) return "";
    char s[8];
    snprintf(s, sizeof s, "%06ld", static_cast<long>(passkey % 1000000));
    return s;
}

// The link in one word or two, for the status line.
inline const char* status(bool ready, bool pairing) {
    return ready ? "Connected" : pairing ? "Pairing" : "Not connected";
}

}  // namespace bt_pairing
