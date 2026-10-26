#include "nearby.h"

#include <cctype>

namespace nearby {
namespace {

std::string get(const std::map<std::string, std::string>& txt, const char* key) {
    auto it = txt.find(key);
    return it == txt.end() ? std::string() : it->second;
}

}  // namespace

bool parse(const std::map<std::string, std::string>& txt, Computer& out) {
    if (get(txt, "v") != "1") return false;

    const std::string port = get(txt, "port");
    if (port.empty() || port.size() > 5) return false;
    long n = 0;
    for (char ch : port) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) return false;
        n = n * 10 + (ch - '0');
    }
    if (n < 1 || n > 65535) return false;

    std::string fp = get(txt, "fp");
    if (fp.size() != 64) return false;
    for (char& ch : fp) {
        if (!std::isxdigit(static_cast<unsigned char>(ch))) return false;
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }

    out.port = static_cast<int>(n);
    out.fp = fp;
    out.ts = get(txt, "ts");
    return true;
}

}  // namespace nearby
