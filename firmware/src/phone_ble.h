// The phone link spike (#199), only with -DPANEL_PHONE_BLE (env crowpanel-phone): in Bluetooth Mode,
// a phone connects to a second identity of the panel (its own random static address, advertising only
// the Phrase service, no HID) and writes a Phrase. The panel Sends it through the request channel and
// the Send module (#180, #179), as a tap would, and notifies Typed, Not typed or Maybe typed.
//
// The phone's connections never reach ble_keyboard: they start from the phone identity's own
// advertising, so NimBLE gives their events to this module. Only the advertising set is shared
// (legacy advertising, one set: see ble_keyboard::refreshAdvertising).
#pragma once

#include <cstdint>

namespace phone_ble {

// Sets the phone identity's address and registers the Phrase service. From ble_keyboard::start(),
// after its own services and before advertising starts. False if it couldn't.
bool start();

// The bonded phone's identity address, so ble_keyboard's bond cleanup keeps it. False for none.
bool bondedPhone(uint8_t type_and_addr[7]);

// For ble_keyboard::refreshAdvertising().
bool connected();
bool bonded();
bool windowOpen();
// Advertises the phone identity. False if the stack refused.
bool startAdvertising();

// Lets one phone pair for ms, replacing the bonded one. STUB: a real build opens this from Settings
// behind the admin password, like the Computer's window; the spike opens it from serial (`phonepair`).
void openWindow(uint32_t ms);

// The 6-digit passkey to show while a phone pairs, or -1.
int32_t passkey();

}  // namespace phone_ble
