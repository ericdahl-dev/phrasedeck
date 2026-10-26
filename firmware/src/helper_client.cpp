#include "helper_client.h"

#include <Arduino.h>
#include <NetworkClientSecure.h>

#include "pairing.h"

namespace helper_client {
namespace {

constexpr uint32_t kTimeoutMs = 10000;
constexpr size_t kMaxBody = 4096;

// Reads one line (without CR LF), or returns false on timeout or close.
bool readLine(NetworkClientSecure& c, std::string& line, uint32_t deadline) {
    line.clear();
    while (millis() < deadline) {
        if (!c.available()) {
            if (!c.connected()) return false;
            delay(5);
            continue;
        }
        const char ch = static_cast<char>(c.read());
        if (ch == '\n') {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return true;
        }
        if (line.size() < 1024) line += ch;
    }
    return false;
}

}  // namespace

Reply post(const std::string& ip, int port, const char* path, const std::string& json,
           const std::string& bearer, const std::function<bool(const std::string&)>& allow) {
    Reply r;
    NetworkClientSecure c;
    c.setInsecure();  // no CA here: the Pin check below is the trust decision, before any write
    c.setHandshakeTimeout(kTimeoutMs / 1000);
    if (!c.connect(ip.c_str(), port, kTimeoutMs)) return r;

    uint8_t digest[32];
    if (!c.getFingerprintSHA256(digest)) {
        c.stop();
        return r;
    }
    r.certSha256 = pairing::hexLower(digest);
    if (!allow(r.certSha256)) {
        r.pinMismatch = true;
        c.stop();
        return r;
    }

    std::string req = std::string("POST ") + path + " HTTP/1.1\r\nHost: " + ip + ":" + std::to_string(port) +
                      "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(json.size()) +
                      "\r\nConnection: close\r\n";
    if (!bearer.empty()) req += "Authorization: Bearer " + bearer + "\r\n";
    req += "\r\n" + json;
    const size_t wrote = c.write(reinterpret_cast<const uint8_t*>(req.data()), req.size());
    r.sent = wrote > 0;  // even a partial write may have reached the Helper
    if (wrote != req.size()) {
        c.stop();
        return r;
    }

    const uint32_t deadline = millis() + kTimeoutMs;
    std::string line;
    if (!readLine(c, line, deadline) || line.compare(0, 5, "HTTP/") != 0 || line.size() < 12) {
        c.stop();
        return r;
    }
    r.status = atoi(line.c_str() + 9);  // the Helper answered, whatever happens to the body
    size_t length = kMaxBody;
    while (readLine(c, line, deadline) && !line.empty()) {
        if (strncasecmp(line.c_str(), "Content-Length:", 15) == 0) {
            length = std::min(kMaxBody, static_cast<size_t>(strtoul(line.c_str() + 15, nullptr, 10)));
        }
    }
    while (r.body.size() < length && millis() < deadline) {
        if (c.available()) {
            r.body += static_cast<char>(c.read());
        } else if (!c.connected()) {
            break;
        } else {
            delay(5);
        }
    }
    c.stop();
    return r;
}

}  // namespace helper_client
