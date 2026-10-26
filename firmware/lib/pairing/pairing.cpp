#include "pairing.h"

#include <ArduinoJson.h>

#include <cctype>

namespace pairing {

bool matchesShortPin(const std::string& typed, const std::string& certSha256) {
    std::string pin;
    for (char ch : typed) {
        if (ch == ' ') continue;
        if (!std::isxdigit(static_cast<unsigned char>(ch))) return false;
        pin += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (pin.size() != 16 || certSha256.size() != 64) return false;
    // Constant time, so how long a wrong Pin takes doesn't say how much of it was right.
    unsigned char diff = 0;
    for (size_t i = 0; i < 16; ++i) diff |= static_cast<unsigned char>(pin[i] ^ certSha256[i]);
    return diff == 0;
}

size_t pinDigits(const std::string& typed) {
    size_t n = 0;
    for (char ch : typed) n += std::isxdigit(static_cast<unsigned char>(ch)) ? 1 : 0;
    return n;
}

bool readyToPair(const std::string& code, const std::string& shortPin) {
    for (char ch : code) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) return false;
    }
    return code.size() == 6 && pinDigits(shortPin) == 16;
}

bool matchesPin(const std::string& pin, const std::string& certSha256) {
    if (pin.size() != 64 || certSha256.size() != 64) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < 64; ++i) diff |= static_cast<unsigned char>(pin[i] ^ certSha256[i]);
    return diff == 0;
}

bool readPairReply(int status, const std::string& body, const std::string& checkedCert, std::string& token) {
    if (status != 200) return false;
    JsonDocument doc;
    if (deserializeJson(doc, body)) return false;
    const char* t = doc["token"] | "";
    const char* cert = doc["certSHA256"] | "";
    if (*t == '\0' || !matchesPin(checkedCert, cert)) return false;
    token = t;
    return true;
}

std::string hexLower(const uint8_t digest[32]) {
    static const char kHex[] = "0123456789abcdef";
    std::string out(64, '0');
    for (int i = 0; i < 32; ++i) {
        out[2 * i] = kHex[digest[i] >> 4];
        out[2 * i + 1] = kHex[digest[i] & 0x0F];
    }
    return out;
}

}  // namespace pairing
