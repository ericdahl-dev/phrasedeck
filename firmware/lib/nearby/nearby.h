// A Helper found over Bonjour (`_phrasedeck._tcp`). Nothing from Bonjour is trusted: the TXT only
// says a Helper is Nearby; its address is checked against the Pin like any other
// (docs/launch/lan-threat-checklist.md).
#pragma once

#include <map>
#include <string>

namespace nearby {

struct Computer {
    int port = 0;
    std::string fp;  // 64 lowercase hex: the SHA-256 of the Helper's certificate
    std::string ts;  // tailnet name, when Tailscale is up
};

// Reads a Helper's TXT. False unless `v` is 1, `port` is 1 to 65535 and `fp` is 64 hex characters.
bool parse(const std::map<std::string, std::string>& txt, Computer& out);

}  // namespace nearby
