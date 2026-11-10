// The dongle's Bluetooth side (#198): a NimBLE peripheral with one GATT service, bonded to one phone.
//
//   Service  6b1d0000-5e2a-4c6e-9a51-2f1c8d7e4a10
//   Send     6b1d0001-...  write, encrypted with MITM: the frames in phrase_link.h
//   Result   6b1d0002-...  notify: one Result per Send (phrase_link::result)
//   Token    6b1d0003-...  read, encrypted with MITM: the Token as hex, for 2 minutes after the bond
//
// Only the bonded phone's connection can write a Send or read the Token; others' writes are dropped.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "phrase_link.h"

namespace ble_link {

// Called on the NimBLE host task for each Send write from the phone, with the Token to check it
// against. Must not block.
using OnWrite = void (*)(const uint8_t* data, size_t len, const phrase_link::Token& token);

bool start(OnWrite onWrite);

// Notifies the phone of a Send's Result. False if no phone is connected (the Result is lost).
bool notify(const std::vector<uint8_t>& result);

// For the status log.
bool hasPhone();
bool phoneConnected();

// The BOOT long press and the serial `forget`, both: erases every bond and the phone with its
// Token now, waits (up to 30 s) for BOOT on bootPin to come up, then restarts. -1: no button to
// wait for. Returns, false, only if something couldn't be erased.
bool forgetPhoneAndRestart(int bootPin);

}  // namespace ble_link
