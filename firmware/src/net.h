// Wi-Fi and the Bonjour browse, on their own FreeRTOS task so the LVGL loop never blocks on the
// network (#91 review). The UI reads a snapshot.
#pragma once

#include <string>
#include <vector>

#include "nearby.h"

namespace net {

struct Helper {
    std::string name;     // Bonjour instance host, e.g. phrased-omarchy-black2
    std::string address;  // IPv4 the browse resolved to
    nearby::Computer txt;
};

struct Snapshot {
    std::string paired;   // the paired Computer's name, empty when not paired
    std::string message;  // the last pairing or Send result, for the status line
    bool wifiConfigured = false;
    bool wifiUp = false;
    std::string ip;
    std::vector<Helper> nearby;
    unsigned version = 0;  // bumps on every change
};

// Starts the task. Does nothing but report "not configured" when the build has no Wi-Fi.
// False if the task or its lock couldn't be created.
bool start();

// Bumps on every change; cheap to poll before taking a snapshot.
unsigned version();

Snapshot snapshot();

// Pairs with Nearby Helper `index` (as of the last snapshot) using the Pairing code and the short
// Pin from `phrased pair`. The code is sent only if that Helper's cert matches the short Pin.
void pair(size_t index, const std::string& code, const std::string& shortPin);

// Types text on the paired Computer, checked against its whole Pin.
void type(const std::string& text);

}  // namespace net
