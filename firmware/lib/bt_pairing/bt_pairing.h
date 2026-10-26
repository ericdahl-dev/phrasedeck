// Who may pair with the panel as a Bluetooth keyboard (#94). Everything it types goes to the paired
// Computer, and a Phrase can be a secret, so a new Computer pairs only while a window opened from
// Settings (behind the admin password) is open, and then it replaces any other: one Computer.
#pragma once

#include <cstdint>

namespace bt_pairing {

enum class Bond { Keep, Reject, ReplaceOthers };

// After a link is encrypted. newPairing: a passkey was just shown for it. knownComputer: its
// identity address is the one stored for the paired Computer.
inline Bond onPaired(bool newPairing, bool knownComputer, bool windowOpen) {
    if (newPairing) return windowOpen ? Bond::ReplaceOthers : Bond::Reject;
    return knownComputer ? Bond::Keep : Bond::Reject;
}

// How long a connection has to encrypt before dropIdle() ends it.
constexpr uint32_t kGraceMs = 10000;

// Whether to end a connection that isn't encrypted: past its grace, with no window open and no
// passkey showing for it.
inline bool dropIdle(bool encrypted, bool showingPasskey, bool windowOpen, bool graceOpen) {
    return !encrypted && !showingPasskey && !windowOpen && !graceOpen;
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
    // Ran out without close(): time to stop advertising.
    bool lapsed(uint32_t now) const { return open_ && !isOpen(now); }

  private:
    uint32_t opened_ = 0;
    uint32_t ms_ = 0;
    bool open_ = false;
};

}  // namespace bt_pairing
