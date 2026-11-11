// Wi-Fi and the Bonjour browse, on their own FreeRTOS task so the LVGL loop never blocks on the
// network (#91 review). The UI reads a snapshot.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "nearby.h"
#include "request_channel.h"
#include "send.h"

namespace net {

using Helper = nearby::Helper;
// The network and the Bluetooth keyboard as one moment (request_channel): version bumps on every
// change, a finished request included.
using Snapshot = request_channel::Snapshot;
// A pair or a Send in flight, from pair() or type(); kBusy when one already is.
using Ticket = request_channel::Ticket;
using Result = request_channel::Result;
constexpr Ticket kBusy = request_channel::kBusy;

// Starts the task. Without Wi-Fi in the build, or once the Wi-Fi hasn't joined for 30 s, it
// starts the panel's own network, and keeps trying the Wi-Fi. False if the task or its lock
// couldn't be created.
bool start();

// Bumps on every change; cheap to poll before taking a snapshot.
unsigned version();

Snapshot snapshot();

// Pairs with Helper h (from a snapshot) using the Pairing code and the short Pin from
// `phrased pair`. The code is sent only if that Helper's cert matches the short Pin.
// kBusy while another request is in flight.
Ticket pair(const Helper& h, const std::string& code, const std::string& shortPin);

// Types text on the paired Computer, checked against its whole Pin. phrase and edit name the
// panel Phrase for the Helper's edit notice; leave phrase empty for none. enter presses Enter after
// the text (Send Enter, #171), in either Mode. kBusy while another request is in flight.
Ticket type(const std::string& text, const std::string& phrase = "", uint32_t edit = 0, bool enter = false);

// True once ticket t has finished, with how it went. Only the last finished ticket has a result.
bool result(Ticket t, Result& out);

// A pair or a Send is in flight, whoever asked for it.
bool busy();

// Stops ticket t's Bluetooth Send partway, releasing any key that's down; nothing once t has
// finished. It still finishes with a result. Any task.
void stop(Ticket t);

// The Mode, stored in the panel NVS. A change takes effect at the next restart. False if not saved.
bool setBluetooth(bool on);

// Lets a new Computer pair over Bluetooth for ms, replacing the paired one. Bluetooth Mode only.
void openBluetoothPairing(uint32_t ms);
void closeBluetoothPairing();

}  // namespace net
