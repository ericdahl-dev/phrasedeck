// Who may pair with the panel as a Bluetooth keyboard (#94). Everything it types goes to the paired
// Computer, and a Phrase can be a secret, so a new Computer pairs only while a window opened from
// Settings (behind the admin password) is open, and then it replaces any other: one Computer.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace bt_pairing {

enum class Bond { Keep, Reject, ReplaceOthers };

// After a link is encrypted. newPairing: a passkey was just shown for it, which only happens inside
// a window, so it stands even if the window ran out while the passkey was being typed.
// knownComputer: its identity address is the one stored for the paired Computer.
inline Bond onPaired(bool newPairing, bool knownComputer) {
    if (newPairing) return Bond::ReplaceOthers;
    return knownComputer ? Bond::Keep : Bond::Reject;
}

// How long a connection has to encrypt before dropIdle() ends it.
constexpr uint32_t kGraceMs = 10000;

// Whether to end a connection that isn't encrypted: past its grace, with no window open and no
// passkey showing for it.
inline bool dropIdle(bool encrypted, bool showingPasskey, bool windowOpen, bool graceOpen) {
    return !encrypted && !showingPasskey && !windowOpen && !graceOpen;
}

// Whether to advertise to the stored Computer only (#144): a nearby device that can't connect can't
// start the repeat pairing that makes the Bluetooth library delete the Computer's bond. Off while a
// window is open, so a new Computer can pair.
inline bool allowListOnly(bool hasComputer, bool windowOpen) { return hasComputer && !windowOpen; }

// A resolvable private address: random (type 1), its top two bits 01. val is little-endian.
inline bool isRpa(uint8_t type, const uint8_t val[6]) { return type == 1 && (val[5] >> 6) == 1; }

// Whether the stored Computer is matched only by the private addresses its IRK makes (network
// privacy), not also by its identity address sent in the clear, which anyone can copy (#144). Needs
// a real IRK, and the Computer seen connecting from such addresses: many stacks send an IRK yet
// connect from their public address, and network mode would lock those out.
inline bool networkPrivacy(bool irkPresent, const uint8_t irk[16], bool seenRpa) {
    if (!irkPresent || !seenRpa) return false;
    for (int i = 0; i < 16; ++i) {
        if (irk[i] != 0) return true;
    }
    return false;
}

// Whether a store write goes ahead (#144). securityKeys: a pairing's keys. windowPairing: a passkey is
// showing, which only happens in a window. Without this, a Just Works pairing that claims the
// Computer's identity address overwrites its keys, and rejecting it then deletes them.
inline bool saveToStore(bool securityKeys, bool windowPairing) { return !securityKeys || windowPairing; }

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
