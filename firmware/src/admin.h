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

// Each has its own lockout, so wrong tries from the network can't lock the owner out of the glass.
enum class From { Panel, Web };

// Ok clears that lockout. Wait means too many wrong tries, or the try couldn't be counted in NVS:
// waitMs says how long, and the password isn't even checked. Wrong when no password is set.
Check check(const std::string& password, uint32_t& waitMs, From from = From::Panel);

}  // namespace admin
