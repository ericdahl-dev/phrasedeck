#include "web.h"

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
    size_t start = 0;
    while (start <= body.size()) {
        size_t end = body.find('&', start);
        if (end == std::string::npos) end = body.size();
        const std::string pair = body.substr(start, end - start);
        if (pair.compare(0, name.size() + 1, name + "=") == 0) {
            std::string out;
            for (size_t i = name.size() + 1; i < pair.size(); ++i) {
                if (pair[i] == '+') {
                    out += ' ';
                } else if (pair[i] == '%') {
                    if (i + 2 >= pair.size()) return "";
                    const int hi = hexValue(pair[i + 1]), lo = hexValue(pair[i + 2]);
                    if (hi < 0 || lo < 0) return "";
                    out += static_cast<char>(hi * 16 + lo);
                    i += 2;
                } else {
                    out += pair[i];
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
        if (cookieHeader.compare(start, 2, "s=") == 0) {
            const std::string t = cookieHeader.substr(start + 2, end - start - 2);
            return isToken(t) ? t : "";
        }
        start = end + 1;
    }
    return "";
}

void startSession(Session& s, const std::string& token, uint32_t nowMs) {
    s.token = token;
    s.lastMs = nowMs;
}

void endSession(Session& s) { s = Session{}; }

void touch(Session& s, uint32_t nowMs) { s.lastMs = nowMs; }

bool sessionValid(const Session& s, const std::string& token, uint32_t nowMs) {
    if (!isToken(s.token) || !isToken(token) || nowMs - s.lastMs >= kSessionMs) return false;  // wrap-safe
    unsigned char diff = 0;
    for (size_t i = 0; i < kTokenLen; ++i) diff |= static_cast<unsigned char>(s.token[i] ^ token[i]);
    return diff == 0;
}

}  // namespace web
