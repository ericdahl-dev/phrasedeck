// Rotate screen 180° (#190), kept in the panel NVS so it survives a restart and a factory reset
// turns it off. The logic is lib/screen_rotation; this is its Preferences glue.
#pragma once

namespace screen_setting {

// Whether the screen is rotated. Off if it was never saved or the panel NVS can't be read.
bool load();

// False if it couldn't be saved.
bool save(bool rotated);

}  // namespace screen_setting
