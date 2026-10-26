#include "pairing.h"

#include <cctype>

namespace pairing {

bool matchesShortPin(const std::string& typed, const std::string& certSha256) {
    std::string pin;
    for (char ch : typed) {
        if (ch == ' ') continue;
        if (!std::isxdigit(static_cast<unsigned char>(ch))) return false;
        pin += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return pin.size() == 16 && certSha256.size() == 64 && certSha256.compare(0, 16, pin) == 0;
}

}  // namespace pairing
