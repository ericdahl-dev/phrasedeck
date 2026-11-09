// The phone link (#199): a phone sends a Phrase to the panel over a second Bluetooth LE connection,
// to a custom GATT service, and the panel types it on the Computer over its HID link. This is the
// wire format and the trust rules, free of NimBLE so they're host-tested.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "send.h"

namespace phone_link {

// The app-level secret a phone gets once, right after it bonds, and shows with every Send.
constexpr size_t kTokenLen = 16;
using Token = std::array<uint8_t, kTokenLen>;

// A Phrase is at most this many bytes of UTF-8, as on the panel's Buttons (phrases::kMaxText).
constexpr size_t kMaxText = 2000;

// The Phrase characteristic takes a Send as one or more writes (write with response, in order):
//   Begin: 0x01 (or 0x02: Send Enter after it), id, length (2 bytes, little endian), Token (16), text...
//   More:  0x03, id, offset of this text in the Phrase (2 bytes, little endian), text...
// The Send is complete when the text reaches its length. A new Begin drops one that isn't. A Begin
// is 20 bytes before its text, so it fits the smallest ATT MTU (23, 20-byte writes) with no text.
constexpr uint8_t kBegin = 0x01;
constexpr uint8_t kBeginEnter = 0x02;
constexpr uint8_t kMore = 0x03;
constexpr size_t kBeginHeader = 4 + kTokenLen;
constexpr size_t kMoreHeader = 4;

// One Send as GATT writes of at most maxWrite bytes each (the ATT MTU less 3). Empty if text is
// too long or maxWrite too small to carry any text. The app does this; the tests use it too.
std::vector<std::vector<uint8_t>> frames(uint8_t id, const Token& token, const std::string& text, bool enter,
                                         size_t maxWrite);

// More: keep writing. Done: a whole Send is here. Bad: the write made no sense (a wrong offset, a
// different id, too much text): the partial Send is dropped and nothing is typed.
enum class Fed { More, Done, Bad };

// Puts a Send back together from its writes, on the panel. One per phone connection.
class Assembler {
  public:
    Fed feed(const uint8_t* data, size_t len);
    // After Done.
    uint8_t id() const { return id_; }
    bool enter() const { return enter_; }
    const Token& token() const { return token_; }
    const std::string& text() const { return text_; }

  private:
    Fed append(const uint8_t* data, size_t len);
    Fed fail();

    uint8_t id_ = 0;
    bool enter_ = false;
    Token token_{};
    std::string text_;
    size_t total_ = 0;
    bool open_ = false;  // a Begin came and the text isn't whole yet
};

// What the panel knows about the connection a write came on (NimBLE's ble_gap_conn_desc, plus the
// panel's own records).
struct Link {
    bool phoneIdentity = false;  // it connected to the phone identity, not the Computer's (HID) one
    bool encrypted = false;
    bool authenticated = false;  // paired with the passkey, so no one in the middle
    bool bonded = false;
    uint8_t keySize = 0;
    bool storedPhone = false;    // its identity address and key are the phone paired in the window
};

// The link may write at all: the phone identity, bonded with the passkey, a 128-bit key, and the
// phone paired in the window. Checked on every write, before a Send is whole.
bool trusted(const Link& link);

enum class Verdict { Send, NotTrusted, WrongToken };

// Whether a whole Send may go to the Computer: only from the bonded phone, over its passkey-paired
// link, with the Token the panel gave it. The Token is compared in constant time, and only once the
// link is trusted. An all-zero Token (none issued) never matches.
Verdict check(const Link& link, const Token& sent, const Token& issued);

// The Token characteristic hands the Token out once, to the connection that just paired with the
// passkey in a phone pairing window, and only for a minute after. now is millis().
constexpr uint32_t kTokenOfferMs = 60000;

class TokenOffer {
  public:
    void offer(uint16_t handle, uint32_t now) {
        handle_ = handle;
        at_ = now;
        open_ = true;
    }
    // True once, for that handle, in time; it's gone after.
    bool take(uint16_t handle, uint32_t now) {
        const bool ok = open_ && handle == handle_ && now - at_ < kTokenOfferMs;
        if (ok) open_ = false;
        return ok;
    }
    // The connection went: a new one can get its handle.
    void drop(uint16_t handle) {
        if (handle == handle_) open_ = false;
    }

  private:
    uint16_t handle_ = 0;
    uint32_t at_ = 0;
    bool open_ = false;
};

// The Status characteristic's notify after a Send: id, outcome, then the words for the status line
// (UTF-8, cut on a character boundary to fit maxLen). Refusals (Busy, Not trusted, Wrong token, a bad
// write) are Not typed.
constexpr uint8_t kTyped = 0;
constexpr uint8_t kNotTyped = 1;
constexpr uint8_t kMaybeTyped = 2;
std::vector<uint8_t> status(uint8_t id, send::Outcome outcome, const std::string& message, size_t maxLen);

// Which identity has the one advertising set. keyboardWants: ble_keyboard would advertise now (no
// Computer connected, or its window open).
enum class Advertise { None, Keyboard, Phone };
Advertise advertise(bool keyboardWants, bool phoneConnected, bool phoneBonded, bool phoneWindowOpen);

}  // namespace phone_link
