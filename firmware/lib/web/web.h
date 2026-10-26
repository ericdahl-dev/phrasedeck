// The panel's web editor (#98): request checks and the login session, kept free of Arduino and
// httpd so they're host-tested. The threat model is in #98's AppSec review.
#pragma once

#include <cstdint>
#include <string>

namespace web {

constexpr uint32_t kSessionMs = 15u * 60 * 1000;  // ends after 15 minutes without a request
constexpr size_t kTokenLen = 32;                  // 16 random bytes as lowercase hex

// True only for the panel's own address, with or without :443 (DNS rebinding).
bool hostAllowed(const std::string& host, const std::string& ip);

// True only for https://<ip>, the panel's own page. Every POST needs it (CSRF).
bool originAllowed(const std::string& origin, const std::string& ip);

// One field of an application/x-www-form-urlencoded body, decoded; empty if missing or malformed.
std::string formValue(const std::string& body, const std::string& name);

// The "s" cookie's value if it's a well-formed token, else empty.
std::string cookieToken(const std::string& cookieHeader);

// One session at a time: a new login replaces the old one.
struct Session {
    std::string token;  // empty: none
    uint32_t lastMs = 0;
};

void startSession(Session& s, const std::string& token, uint32_t nowMs);
void endSession(Session& s);
void touch(Session& s, uint32_t nowMs);

// Constant time in the token; false once kSessionMs have passed since the last touch.
bool sessionValid(const Session& s, const std::string& token, uint32_t nowMs);

}  // namespace web
