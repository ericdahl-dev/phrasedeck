// The "panel" NVS partition (partitions.csv): the Pin, Token, and later the panel's TLS key and
// cert and the admin password hash (#98). Not encrypted: anyone with the board on USB can read it
// (docs/launch/lan-threat-checklist.md).
#pragma once

namespace panel_store {

constexpr const char* kPartition = "panel";

// Opens the partition. Erases it only when it's blank or from a newer NVS format (the first boot
// after this table, when it still holds old app bytes); any other error leaves it alone.
bool begin();

}  // namespace panel_store
