// One HTTPS request to a Helper's Home network listener, with the certificate checked BEFORE
// anything is written: the caller's check sees the handshake cert's SHA-256 (lowercase hex) and
// says whether this Helper may get the request (the short Pin while pairing, the whole Pin after).
// docs/launch/lan-threat-checklist.md, "Panel pairing". Runs on the net task.
#pragma once

#include <functional>
#include <string>

namespace helper_client {

struct Reply {
    bool sent = false;        // the request was written; only then could the Helper act on it
    bool pinMismatch = false;  // the cert failed the check, so nothing was written
    int status = 0;            // HTTP status, 0 when no reply came back
    std::string body;          // at most 4 KB
    std::string certSha256;    // of the handshake cert
};

Reply post(const std::string& ip, int port, const char* path, const std::string& json,
           const std::string& bearer, const std::function<bool(const std::string& certSha256)>& allow);

}  // namespace helper_client
