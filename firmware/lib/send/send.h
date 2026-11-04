// A Send: one tap's attempt to type a Button's Phrase on a Computer (CONTEXT.md). This module owns
// what a Send does and what it means: the route to the Helper and its spoofer retry, the Bluetooth
// key loop with Stop and the release on a stalled link, and the words and color for Typed, Not
// typed and Maybe typed. It reaches the Computer through two seams, so it's host-tested with fakes:
// HelperTransport (TLS on the panel, helper_client) and Keyboard (NimBLE HID, ble_keyboard).
#pragma once

#include <ArduinoJson.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "nearby.h"

namespace send {

enum class Outcome { Typed, NotTyped, MaybeTyped };

// What a Send says on the panel: the outcome, for the color, and the words for the status line.
struct Result {
    Outcome outcome = Outcome::NotTyped;
    std::string message;
};

// Like the iPhone: green Typed, orange Maybe typed (it stays until the next tap), red Not typed.
enum class Color { Green, Orange, Red };
Color color(Outcome o);

// What a Button sends: its Phrase, which panel Phrase and edit it is (key and edit, for the
// Helper's edit notice, #98; an empty key for none), and Send Enter (#171).
struct Phrase {
    std::string text;
    std::string key;
    uint32_t edit = 0;
    bool enter = false;
};

// sent: the first byte of the request was written (TLS up and the Pin matched); a write that fails
// partway still counts. status: /type's HTTP status, or 0 or below when no reply came back. 500 means typing failed partway; every other error means nothing was
// typed. With no reply, only a request that went out may have typed something.
inline Outcome outcome(bool sent, int status) {
    if (status == 200) return Outcome::Typed;
    if (status == 500 || (sent && status <= 0)) return Outcome::MaybeTyped;
    return Outcome::NotTyped;
}

// /type's JSON body. phrase and edit let the Helper notice the first Send of an edited Phrase
// (#98); an empty phrase leaves both out. enter asks the Helper to press Enter after the text
// (Send Enter, #171); a "\n" in the text wouldn't, since the Helper types that as Shift+Enter.
inline std::string typeBody(const std::string& text, const std::string& phrase, uint32_t edit, bool enter = false) {
    JsonDocument doc;
    doc["text"] = text;
    if (!phrase.empty()) {
        doc["phrase"] = phrase;
        doc["edit"] = edit;
    }
    if (enter) doc["enter"] = true;
    std::string json;
    serializeJson(doc, json);
    return json;
}

// The paired Computer, from the panel NVS: where pairing found it, its whole Pin and the Token.
struct Paired {
    std::string name, address;
    int port = 0;
    std::string pin, token;
    bool ok() const { return !token.empty() && pin.size() == 64; }
};

// One POST /type to a Helper (helper_client::Reply on the panel).
struct Reply {
    bool sent = false;         // the request was written; only then could the Helper act on it
    bool pinMismatch = false;  // the cert failed allow(), so nothing was written
    int status = 0;            // HTTP status, 0 or below when no reply came back
    std::string body;
};

// The seam to the Helper. post() writes json to address:port's /type with token as the bearer, but
// only once allow(the handshake cert's SHA-256, lowercase hex) says yes.
class HelperTransport {
  public:
    virtual ~HelperTransport() = default;
    virtual Reply post(const std::string& address, int port, const std::string& json, const std::string& token,
                       const std::function<bool(const std::string& certSha256)>& allow) = 0;
};

// A Send over the Home network route to the paired Computer. It goes only with a pairing and the
// panel on its Wi-Fi, to the Nearby Helper whose fp is the Pin (the address can change) or else to
// the address from pairing, and only to a cert that is the whole Pin. A spoofer can claim the fp:
// its cert fails, and the address from pairing is tried once.
Result toHelper(const Phrase& phrase, const Paired& paired, bool wifiUp, const std::vector<nearby::Helper>& nearby,
                HelperTransport& transport);

// The seam to the Bluetooth keyboard (ble_keyboard on NimBLE), and the clock its key loop runs on.
// All on the one task that sends.
class Keyboard {
  public:
    virtual ~Keyboard() = default;
    virtual bool ready() = 0;                             // the paired Computer is connected
    virtual bool report(uint8_t mod, uint8_t code) = 0;   // one HID report; false if the stack didn't take it
    virtual unsigned disconnects() = 0;                   // bumps when the paired Computer disconnects
    virtual uint32_t now() = 0;                           // millis()
    virtual void yield() = 0;                             // between key steps: vTaskDelay(1)
    virtual void sleep(uint32_t ms) = 0;                  // between release offers
};

// For the PANEL_BLE_DEBUG log: reports taken and how long the keys took, release offers not counted.
struct KeyStats {
    unsigned reports = 0;
    size_t keys = 0;
    uint32_t tookMs = 0;
};

// A Send in the Bluetooth Mode (#94): the Phrase as keystrokes on a US layout, one report per
// keyIntervalMs (#174). stop is the Stop button: read, and cleared, between keys. A disconnect
// ends it at once. A stalled link gives up after a second, then keeps offering a release for up to
// 6 s while a key may still be down, so the Computer can't auto-repeat it. Sent is Typed, but says
// "Sent as keystrokes" since the panel can't see what landed; stopped partway is Maybe typed (#177).
Result overBluetooth(const Phrase& phrase, Keyboard& keyboard, std::atomic<bool>& stop, uint32_t keyIntervalMs,
                     KeyStats* stats = nullptr);

}  // namespace send
