// Typing a Phrase on the dongle's USB keyboard (#198), and the speed test's numbers.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "hid_keys.h"
#include "phrase_link.h"
#include "send.h"

namespace usb_typing {

struct Outcome {
    phrase_link::Code code = phrase_link::Code::NotTyped;
    std::string bad;  // Refused: the character a US keyboard can't type
};

Outcome type(const std::string& text, bool enter, send::Keyboard& usb, std::atomic<bool>& stop, uint32_t intervalMs,
             send::KeyStats* stats = nullptr);

std::string speedLine(const send::KeyStats& stats, uint32_t intervalMs);

std::string speedPhrase(size_t n);

}  // namespace usb_typing
