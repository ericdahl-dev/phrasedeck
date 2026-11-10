// The dongle's GATT protocol (#198): how a phone writes a Phrase to the Send characteristic, and
// what the Result characteristic notifies back. One GATT write is one frame; multi-byte numbers
// are little-endian.
//
//   Begin  01 id flags lenLo lenHi token[16] text...   flags bit 0: Send Enter
//   Data   02 id text...                               appended in order until len bytes are in
//   Stop   03 id                                       cancels Send id, received or typing
//
//   Result (notify)  id code detail...                  detail: the refused character, UTF-8
//
// A Begin carries as much text as fits after its header, so a short Phrase is one write. Each
// frame must fit one write: ATT_MTU - 3 bytes (iOS gives 182). The id is the phone's, to match a
// Result to its Send; a new Begin replaces a Send that never finished.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace phrase_link {

using Token = std::array<uint8_t, 16>;

// The longest Phrase the dongle keeps, in UTF-8 bytes.
constexpr size_t kMaxPhrase = 8192;

// What became of a Send. In the app's words: Typed, Maybe typed (some keys may have reached the
// Computer: check before sending again), and Not typed for every other code (nothing was typed).
enum class Code : uint8_t {
    Typed = 0x00,
    MaybeTyped = 0x01,  // stopped partway: a Stop, the USB cable pulled, the Computer asleep
    NotTyped = 0x02,    // no Computer on USB, or stopped before the first key
    Refused = 0x03,     // a character a US keyboard can't type; the detail names it
    BadToken = 0x04,
    Busy = 0x05,        // another Send is typing
    BadFrame = 0x06,
    TooLong = 0x07,
};

struct Event {
    enum class Kind {
        None,   // a frame of a Send still coming in, or nothing to act on
        Ready,  // the Send is in: type text() (until the next feed)
        Done,   // the Send ended here, not typed: notify code
        Stop,   // a Stop for a Send that isn't coming in: cancel it if it's typing
    };
    Kind kind = Kind::None;
    uint8_t id = 0;
    bool enter = false;
    Code code = Code::Typed;  // Done: why the Send ended here
};

// Puts a Send together from its frames. Not thread-safe: one task feeds it.
class Receiver {
  public:
    Receiver() { text_.reserve(kMaxPhrase); }
    // One GATT write. token is the one the dongle issued; a Begin with another is Bad token.
    Event feed(const uint8_t* data, size_t len, const Token& token);
    const std::string& text() const { return text_; }

  private:
    Event append(const uint8_t* bytes, size_t n);
    void drop();

    bool receiving_ = false;
    std::string text_;
    uint8_t id_ = 0;
    bool enter_ = false;
    size_t length_ = 0;
};

// The writes for one Send, each at most maxWrite bytes (at least 22). For tests and tools; the
// app does the same.
std::vector<std::vector<uint8_t>> frames(uint8_t id, const std::string& text, bool enter, const Token& token,
                                         size_t maxWrite);

// The Result notify for Send id.
std::vector<uint8_t> result(uint8_t id, Code code, const std::string& detail);

// Compares in constant time, so a wrong Token's timing doesn't say how much of it was right.
bool sameToken(const uint8_t* given, const Token& want);

}  // namespace phrase_link
