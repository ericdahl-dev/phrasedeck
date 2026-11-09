#include "usb_typing.h"

#include <cstdio>

namespace usb_typing {

Outcome type(const std::string& text, bool enter, send::Keyboard& usb, std::atomic<bool>& stop, uint32_t intervalMs,
             send::KeyStats* stats) {
    // Planned here as well as in overBluetooth, to name the character in the Result.
    const hid_keys::Plan plan = hid_keys::plan(text, enter);
    if (!plan.ok) return {phrase_link::Code::Refused, plan.bad};
    send::Phrase phrase;
    phrase.text = text;
    phrase.enter = enter;
    // The panel's key loop, behind its Keyboard seam: Stop, a disconnect (here the cable or the
    // Computer's sleep), the stalled-report give-up and the release offers all work the same.
    switch (send::overBluetooth(phrase, usb, stop, intervalMs, stats).outcome) {
        case send::Outcome::Typed: return {phrase_link::Code::Typed, ""};
        case send::Outcome::MaybeTyped: return {phrase_link::Code::MaybeTyped, ""};
        case send::Outcome::NotTyped: break;
    }
    return {phrase_link::Code::NotTyped, ""};
}

std::string speedLine(const send::KeyStats& stats, uint32_t intervalMs) {
    const unsigned long perSecond = stats.tookMs == 0 ? 0 : (unsigned long)stats.keys * 1000 / stats.tookMs;
    const double perMs = stats.tookMs == 0 ? 0.0 : double(stats.reports) / stats.tookMs;
    char line[160];
    snprintf(line, sizeof line, "speed: %lu chars, %u reports in %lu ms: %lu chars/s, %.2f reports/ms, key interval %lu ms",
             (unsigned long)stats.keys, stats.reports, (unsigned long)stats.tookMs, perSecond, perMs,
             (unsigned long)intervalMs);
    return line;
}

std::string speedPhrase(size_t n) {
    // 58 characters: capitals and punctuation (Shift changes) and the same key twice ("ll", "22"),
    // the cases a host or a pacing bug would get wrong first.
    const std::string pattern = "Hello! The quick brown fox jumps over 22 lazy dogs: 012345";
    std::string out;
    out.reserve(n + 64);
    char number[8];
    for (unsigned line = 1; out.size() < n; line++) {
        snprintf(number, sizeof number, "%04u ", line % 10000);
        out += number;
        out += pattern;
        out += '\n';
    }
    out.resize(n);
    return out;
}

}  // namespace usb_typing
