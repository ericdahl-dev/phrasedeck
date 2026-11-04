// The panel's own Wi-Fi network (#165): when the panel can't join its Wi-Fi, it starts a WPA2
// network of its own so the web editor still works. Kept free of Arduino so it's host-tested.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace own_network {

// "Phrasedeck-" and the last 4 hex digits of the MAC, in capitals.
std::string name(const uint8_t mac[6]);

// Fills out with random bytes: esp_fill_random on the panel.
using Fill = std::function<void(uint8_t* out, size_t len)>;

// Lowercase letters and digits without l, o, 0 and 1: 32 of them, so a byte maps without bias.
extern const char kAlphabet[];
constexpr size_t kPasswordLen = 12;

// A new network password: kPasswordLen characters from kAlphabet.
std::string makePassword(const Fill& fill);

// Where the password is kept: the panel NVS on the panel, so a factory reset erases it.
struct Store {
    std::function<std::string()> get;               // empty if none
    std::function<bool(const std::string&)> put;    // false if not saved
};

// The kept password, or a new one, saved, the first time (or after a factory reset). A stored
// value that isn't one of ours is replaced. Empty if a new one couldn't be saved.
std::string loadOrMakePassword(const Store& store, const Fill& fill);

constexpr uint32_t kJoinWaitMs = 30000;       // the panel's Wi-Fi gets this long to join first
constexpr uint32_t kRetryMs = 60000;          // then it's tried this often while the network is up
constexpr uint32_t kRetryBusyMs = 5 * 60000;  // or this often while someone is on the network

// Whether the panel's own network should be up. Ask on every pass of the net task, with the time
// since boot: up at once without Wi-Fi in the build, else once the Wi-Fi has been down
// kJoinWaitMs (since boot, or since it was last joined); down as soon as it joins.
class Decision {
public:
    bool wanted(bool wifiInBuild, bool joined, uint64_t nowMs);

private:
    uint64_t joinedMs_ = 0;  // the last time it was joined; boot counts as one
};

// Whether to try the panel's Wi-Fi again while its own network is up. Each try scans every
// channel, which stalls the network for a few seconds; clients is how many are on it.
bool retryDue(uint64_t nowMs, uint64_t lastTryMs, unsigned clients);

// The panel's address on its own network, and the editor's: https://192.168.4.1/
constexpr const char* kAddress = "192.168.4.1";

// The address the web editor answers to (web::hostAllowed, web::originAllowed): joinedIp when the
// panel is on its Wi-Fi, kAddress only while its own network is up, else none ("").
std::string editorAddress(const std::string& joinedIp, bool networkUp);

// What a phone camera reads to join the network: WIFI:T:WPA;S:<name>;P:<password>;; with
// \ ; , : and " escaped.
std::string joinQr(const std::string& name, const std::string& password);

// Why a pairing can't go yet: phrased is reachable only over the panel's Wi-Fi. Empty when it can.
// A Send's words for the same are the Send module's (send::toHelper).
std::string offWifi(bool wifiUp);

}  // namespace own_network
