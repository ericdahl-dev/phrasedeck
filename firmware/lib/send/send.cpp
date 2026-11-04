#include "send.h"

#include "hid_keys.h"
#include "pairing.h"

namespace send {
namespace {

const char* const kMaybe = "Maybe typed: check the Computer before tapping again";

// The Helper's own words for an error reply, if it sent any.
std::string errorText(const std::string& body) {
    JsonDocument doc;
    if (deserializeJson(doc, body)) return "";
    return doc["error"] | "";
}

}  // namespace

Color color(Outcome o) {
    switch (o) {
        case Outcome::Typed: return Color::Green;
        case Outcome::MaybeTyped: return Color::Orange;
        case Outcome::NotTyped: return Color::Red;
    }
    return Color::Red;
}

Result toHelper(const Phrase& phrase, const Paired& paired, bool wifiUp, const std::vector<nearby::Helper>& nearby,
                HelperTransport& transport) {
    if (!paired.ok()) return {Outcome::NotTyped, "Not typed: pair with a Computer first"};
    // On its own network, or still joining: phrased is only reachable over the Wi-Fi.
    if (!wifiUp) return {Outcome::NotTyped, "Not typed: the panel isn't on its Wi-Fi"};
    // A paired Computer is found again by its Pin in Bonjour, since its address can change.
    std::string address = paired.address;
    int port = paired.port;
    for (const nearby::Helper& h : nearby) {
        if (pairing::matchesPin(paired.pin, h.txt.fp)) {
            address = h.address;
            port = h.txt.port;
        }
    }
    const std::string json = typeBody(phrase.text, phrase.key, phrase.edit, phrase.enter);
    const auto pinned = [&paired](const std::string& cert) { return pairing::matchesPin(paired.pin, cert); };
    Reply r = transport.post(address, port, json, paired.token, pinned);
    // A Bonjour spoofer can claim the paired Computer's fp; the Pin stops it, then try the
    // address from pairing once, so a spoofer can't block every Send (checklist, Spoofed Bonjour).
    if (r.pinMismatch && (address != paired.address || port != paired.port)) {
        r = transport.post(paired.address, paired.port, json, paired.token, pinned);
    }
    switch (outcome(r.sent, r.status)) {
        case Outcome::Typed: return {Outcome::Typed, "Typed"};
        case Outcome::MaybeTyped: return {Outcome::MaybeTyped, kMaybe};
        case Outcome::NotTyped: break;
    }
    std::string why = r.pinMismatch ? "Pin doesn't match" : errorText(r.body);
    if (why.empty()) why = r.status ? std::to_string(r.status) : "can't reach " + paired.name;
    return {Outcome::NotTyped, "Not typed: " + why};
}

Result overBluetooth(const Phrase& phrase, Keyboard& keyboard, std::atomic<bool>& stop, uint32_t keyIntervalMs,
                     KeyStats* stats) {
    const hid_keys::Plan p = hid_keys::plan(phrase.text, phrase.enter);
    if (!p.ok) return {Outcome::NotTyped, "Not sent: a US keyboard can't type " + p.bad};
    if (!keyboard.ready()) return {Outcome::NotTyped, "Not sent: no Computer connected over Bluetooth"};
    hid_keys::Sender sender(p.keys, keyIntervalMs);
    const unsigned disconnects = keyboard.disconnects();
    unsigned reports = 0;
    const auto notify = [&](const hid_keys::Key& k) {
        const bool ok = keyboard.report(k.mod, k.code);
        reports += ok;
        return ok;
    };
    const uint32_t began = keyboard.now();
    while (!sender.done()) {
        if (keyboard.disconnects() != disconnects) sender.drop();
        if (stop.exchange(false)) sender.cancel();
        sender.step(keyboard.now(), notify);
        keyboard.yield();  // Sender needs a yield between steps
    }
    if (stats != nullptr) *stats = {reports, p.keys.size(), keyboard.now() - began};
    // Gave up partway on a stalled link: a key may still be down on the Computer, which would
    // auto-repeat it once the link recovers. Keep offering a release until it goes or the link does.
    const uint32_t start = keyboard.now();
    while (sender.keyDown() && keyboard.disconnects() == disconnects && keyboard.now() - start < 6000 &&
           !keyboard.report(0, 0)) {
        keyboard.sleep(keyIntervalMs);
    }
    switch (sender.result()) {
        case hid_keys::Sender::Result::Sent: return {Outcome::Typed, "Sent as keystrokes"};
        // The start of the Phrase was typed: tapping again would type it a second time (#177).
        case hid_keys::Sender::Result::Stopped: return {Outcome::MaybeTyped, kMaybe};
        case hid_keys::Sender::Result::NotSent:
        case hid_keys::Sender::Result::Running: break;
    }
    return {Outcome::NotTyped, "Not sent"};
}

}  // namespace send
