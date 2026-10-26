#include "web.h"

#include <algorithm>

namespace web {
namespace {

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool isToken(const std::string& t) {
    if (t.size() != kTokenLen) return false;
    for (char c : t) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

}  // namespace

bool hostAllowed(const std::string& host, const std::string& ip) {
    return !ip.empty() && (host == ip || host == ip + ":443");
}

bool originAllowed(const std::string& origin, const std::string& ip) {
    return !ip.empty() && origin == "https://" + ip;
}

std::string formValue(const std::string& body, const std::string& name) {
    // Decodes straight from body into one reserved buffer, so no stray copy of a password is left.
    const std::string key = name + "=";
    size_t start = 0;
    while (start <= body.size()) {
        size_t end = body.find('&', start);
        if (end == std::string::npos) end = body.size();
        if (body.compare(start, key.size(), key) == 0 && start + key.size() <= end) {
            std::string out;
            out.reserve(end - start);
            for (size_t i = start + key.size(); i < end; ++i) {
                if (body[i] == '+') {
                    out += ' ';
                } else if (body[i] == '%') {
                    const int hi = i + 2 < end ? hexValue(body[i + 1]) : -1;
                    const int lo = i + 2 < end ? hexValue(body[i + 2]) : -1;
                    if (hi < 0 || lo < 0) {
                        std::fill(out.begin(), out.end(), '\0');
                        return "";
                    }
                    out += static_cast<char>(hi * 16 + lo);
                    i += 2;
                } else {
                    out += body[i];
                }
            }
            return out;
        }
        start = end + 1;
    }
    return "";
}

std::string cookieToken(const std::string& cookieHeader) {
    size_t start = 0;
    while (start < cookieHeader.size()) {
        while (start < cookieHeader.size() && cookieHeader[start] == ' ') ++start;
        size_t end = cookieHeader.find(';', start);
        if (end == std::string::npos) end = cookieHeader.size();
        static const std::string kName = "__Host-s=";
        if (cookieHeader.compare(start, kName.size(), kName) == 0) {
            const std::string t = cookieHeader.substr(start + kName.size(), end - start - kName.size());
            return isToken(t) ? t : "";
        }
        start = end + 1;
    }
    return "";
}

void startSession(Session& s, const std::string& token, uint32_t nowMs) {
    s.token = token;
    s.lastMs = s.startMs = nowMs;
}

void endSession(Session& s) { s = Session{}; }

void touch(Session& s, uint32_t nowMs) { s.lastMs = nowMs; }

bool sessionValid(const Session& s, const std::string& token, uint32_t nowMs) {
    if (!isToken(s.token) || !isToken(token) || nowMs - s.lastMs >= kSessionMs ||
        nowMs - s.startMs >= kSessionMaxMs)
        return false;  // wrap-safe
    unsigned char diff = 0;
    for (size_t i = 0; i < kTokenLen; ++i) diff |= static_cast<unsigned char>(s.token[i] ^ token[i]);
    return diff == 0;
}

}  // namespace web
