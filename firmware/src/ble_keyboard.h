// The panel as a Bluetooth keyboard (#94): a HID-over-GATT keyboard on NimBLE. Keystrokes go only
// to the one Computer paired through the Settings window (bt_pairing), over an encrypted,
// passkey-authenticated link.
#pragma once

#include <cstdint>

namespace ble_keyboard {

// Starts the stack, the HID service and advertising. False if it couldn't.
bool start();

// A paired Computer is connected over an encrypted link, alone: keystrokes can go.
bool ready();

// Sends one keyboard report (mod, code) and returns whether the stack took it. False when not
// ready(). Call from one task only (the net task).
bool send(uint8_t mod, uint8_t code);

// Bumps on every disconnect, so a send loop can drop() its Sender.
unsigned disconnects();

// Lets a new Computer pair for ms, replacing the one paired now. Advertises meanwhile.
void openPairing(uint32_t ms);
void closePairing();
bool pairingOpen();

// The 6-digit passkey to show while a Computer pairs, or -1 for none.
int32_t passkey();

// Bumps on every change to ready(), pairingOpen() or passkey().
unsigned version();

}  // namespace ble_keyboard
