// Wi-Fi and the Bonjour browse, on their own FreeRTOS task so the LVGL loop never blocks on the
// network (#91 review). The UI reads a snapshot.
#pragma once

#include <cstdint>
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
    unsigned messageSeq = 0;  // bumps once per handled request, even when message repeats
    bool ok = false;          // the last request worked: paired, Typed, or Sent as keystrokes
    bool maybe = false;       // the last Send was Maybe typed (never over Bluetooth)
    bool wifiConfigured = false;
    bool wifiUp = false;
    std::string ip;
    std::vector<Helper> nearby;
    unsigned version = 0;  // bumps on every change
    // The Bluetooth keyboard Mode (#94). These are read live, so they're current in any snapshot.
    bool bluetooth = false;     // Sends go as keystrokes over Bluetooth, not through phrased
    bool bleReady = false;      // the paired Computer is connected
    bool blePairing = false;    // the pairing window is open
    int32_t passkey = -1;       // to show while a Computer pairs, or -1
};

// Starts the task. Does nothing but report "not configured" when the build has no Wi-Fi.
// False if the task or its lock couldn't be created.
bool start();

// Bumps on every change; cheap to poll before taking a snapshot.
unsigned version();

Snapshot snapshot();

// Pairs with Helper h (from a snapshot) using the Pairing code and the short Pin from
// `phrased pair`. The code is sent only if that Helper's cert matches the short Pin.
// False if the request couldn't be queued.
bool pair(const Helper& h, const std::string& code, const std::string& shortPin);

// Types text on the paired Computer, checked against its whole Pin. phrase and edit name the
// panel Phrase for the Helper's edit notice; leave phrase empty for none. False if not queued.
bool type(const std::string& text, const std::string& phrase = "", uint32_t edit = 0);

// The Mode, stored in the panel NVS. A change takes effect at the next restart. False if not saved.
bool setBluetooth(bool on);

// Stops a Bluetooth Send partway, releasing any key that's down. Any task.
void cancelBluetoothSend();

// Lets a new Computer pair over Bluetooth for ms, replacing the paired one. Bluetooth Mode only.
void openBluetoothPairing(uint32_t ms);
void closeBluetoothPairing();

}  // namespace net
