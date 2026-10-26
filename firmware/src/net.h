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

}  // namespace net
