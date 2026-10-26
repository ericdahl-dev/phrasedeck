#include "hid_keys.h"

namespace hid_keys {

namespace {

// The key for one printable ASCII character, or code 0 when a US keyboard has none.
Key key(char c) {
    if (c >= 'a' && c <= 'z') return {0, uint8_t(0x04 + (c - 'a'))};
    if (c >= 'A' && c <= 'Z') return {kShift, uint8_t(0x04 + (c - 'A'))};
    if (c >= '1' && c <= '9') return {0, uint8_t(0x1E + (c - '1'))};
    if (c == '0') return {0, 0x27};
    if (c == ' ') return {0, 0x2C};
    // The other keys, unshifted then shifted, in HID usage order from 0x2D.
    static const char plain[] = "-=[]\\\0;'`,./";
    static const char shifted[] = "_+{}|\0:\"~<>?";
    for (uint8_t i = 0; i < sizeof plain - 1; i++) {
        if (plain[i] == '\0') continue;  // 0x32 is the non-US # key
        if (c == plain[i]) return {0, uint8_t(0x2D + i)};
        if (c == shifted[i]) return {kShift, uint8_t(0x2D + i)};
    }
    static const char digitsShifted[] = "!@#$%^&*()";
    for (uint8_t i = 0; i < 10; i++) {
        if (c == digitsShifted[i]) return {kShift, uint8_t(0x1E + i)};
    }
    return {0, 0};
}

// The character starting at text[i], for the panel to name: its whole UTF-8 sequence, or U+00XX
// for an ASCII control.
std::string named(const std::string& text, size_t i) {
    auto b = static_cast<unsigned char>(text[i]);
    if (b < 0x80) {
        static const char hex[] = "0123456789ABCDEF";
        return std::string("U+00") + hex[b >> 4] + hex[b & 0xF];
    }
    size_t end = i + 1;
    while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) end++;
    return text.substr(i, end - i);
}

}  // namespace

Plan plan(const std::string& text, bool enter) {
    constexpr uint8_t kEnter = 0x28, kTab = 0x2B;
    Plan p;
    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') continue;  // "\r\n" is one newline
        if (c == '\n' || c == '\r') {
            p.keys.push_back({kShift, kEnter});
            continue;
        }
        Key k = c == '\t' ? Key{0, kTab} : key(c);
        if (k.code == 0) {
            Plan refused;
            refused.bad = named(text, i);
            return refused;
        }
        p.keys.push_back(k);
    }
    if (enter) p.keys.push_back({0, kEnter});
    p.ok = true;
    return p;
}

}  // namespace hid_keys
