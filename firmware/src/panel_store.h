// The "panel" NVS partition (partitions.csv): the Pin, Token, and later the panel's TLS key and
// cert and the admin password hash (#98). Not encrypted: anyone with the board on USB can read it
// (docs/launch/lan-threat-checklist.md).
#pragma once

namespace panel_store {

constexpr const char* kPartition = "panel";

// Opens the partition. Blank flash, or the old app bytes left at 0x10000 by the table change, init
// fine: NVS erases pages that fail their CRC before using them. It erases the whole partition only
// on NO_FREE_PAGES or NEW_VERSION_FOUND; the latter after a firmware downgrade, which loses the
// pairing, so the panel must pair again. Any other error leaves it alone.
bool begin();

// Factory reset: erases the whole partition. Call before begin().
bool erase();

}  // namespace panel_store
