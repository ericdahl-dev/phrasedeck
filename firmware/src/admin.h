// The panel's admin password (#98): guards Settings on the touchscreen and, later, the web editor.
// Stored in the panel NVS as salt + iterations + PBKDF2-HMAC-SHA256; wrong tries lock out
// (lib/lockout), and the count survives a reboot.
#pragma once

#include <cstdint>
#include <string>

namespace admin {

// Loads the record and the lockout count. Call once, after panel_store::begin().
void begin();

bool isSet();

// Sets or replaces the password. False if it's too short or couldn't be saved.
bool set(const std::string& password);

enum class Check { Ok, Wrong, Wait };

// Ok clears the lockout. Wait means too many wrong tries: waitMs says how long; the password
// isn't even checked.
Check check(const std::string& password, uint32_t& waitMs);

}  // namespace admin
