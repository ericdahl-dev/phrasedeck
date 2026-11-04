#include "own_network.h"

#include <cstdio>
#include <cstring>

namespace own_network {

const char kAlphabet[] = "abcdefghijkmnpqrstuvwxyz23456789";

namespace {

// One this firmware made: the right length, every character from kAlphabet.
bool ours(const std::string& pw) {
    if (pw.size() != kPasswordLen) return false;
    for (char c : pw) {
        if (c == '\0' || strchr(kAlphabet, c) == nullptr) return false;
    }
    return true;
}

}  // namespace

std::string name(const uint8_t mac[6]) {
    char out[sizeof "Phrasedeck-XXXX"];
    snprintf(out, sizeof out, "Phrasedeck-%02X%02X", mac[4], mac[5]);
    return out;
}

std::string makePassword(const Fill& fill) {
    static_assert(sizeof kAlphabet - 1 == 32, "a byte's low 5 bits pick a character without bias");
    uint8_t raw[kPasswordLen];
    fill(raw, sizeof raw);
    std::string out(kPasswordLen, '\0');
    for (size_t i = 0; i < kPasswordLen; ++i) {
        out[i] = kAlphabet[raw[i] & 31];
        raw[i] = 0;
    }
    return out;
}

std::string loadOrMakePassword(const Store& store, const Fill& fill) {
    const std::string kept = store.get();
    if (ours(kept)) return kept;
    const std::string pw = makePassword(fill);
    return store.put(pw) ? pw : "";
}

bool Decision::wanted(bool wifiInBuild, bool joined, uint64_t nowMs) {
    if (!wifiInBuild) return true;
    if (joined) {
        joinedMs_ = nowMs;
        return false;
    }
    return nowMs - joinedMs_ >= kJoinWaitMs;
}

bool retryDue(uint64_t nowMs, uint64_t lastTryMs, unsigned clients) {
    return nowMs - lastTryMs >= (clients > 0 ? kRetryBusyMs : kRetryMs);
}

std::string editorAddress(const std::string& joinedIp, bool networkUp) {
    if (!joinedIp.empty()) return joinedIp;
    return networkUp ? kAddress : "";
}

namespace {

std::string qrEscaped(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (strchr("\\;,:\"", c) != nullptr) out += '\\';
        out += c;
    }
    return out;
}

}  // namespace

std::string joinQr(const std::string& name, const std::string& password) {
    return "WIFI:T:WPA;S:" + qrEscaped(name) + ";P:" + qrEscaped(password) + ";;";
}

std::string offWifi(bool wifiUp) { return wifiUp ? "" : "Not paired: the panel isn't on its Wi-Fi"; }

}  // namespace own_network
