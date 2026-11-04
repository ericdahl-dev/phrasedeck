// Rotate screen 180° (#190): the panel can be turned upside down. Kept free of Arduino so it's
// host-tested.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace screen_rotation {

struct Point {
    int x;
    int y;
};

// Where a touch at (x, y) on the glass, from the touch controller, is on the screen as drawn: the
// same point, or mirrored through the center when rotated. The one place touch follows rotation.
inline Point touch(int x, int y, int width, int height, bool rotated) {
    if (!rotated) return {x, y};
    return {width - 1 - x, height - 1 - y};
}

struct Area {
    int x1, y1, x2, y2;  // inclusive, as LVGL's lv_area_t
};

// One flush in LVGL's PARTIAL render mode: px holds area a's pixels, row after row, and the
// return is where on the glass to write them. Rotated, the area is mirrored through the center
// and px is turned half a turn in place, which for a block of pixels row after row is just
// reversing them. No second buffer, and with it off the pixels aren't touched.
inline Area flush(const Area& a, uint16_t* px, int width, int height, bool rotated) {
    if (!rotated) return a;
    const size_t count = static_cast<size_t>(a.x2 - a.x1 + 1) * static_cast<size_t>(a.y2 - a.y1 + 1);
    std::reverse(px, px + count);
    return {width - 1 - a.x2, height - 1 - a.y2, width - 1 - a.x1, height - 1 - a.y1};
}

// Where the choice is kept: the panel NVS on the panel, so a factory reset turns it off.
struct Store {
    std::function<int()> get;         // the saved byte, or -1 if none
    std::function<bool(uint8_t)> put;  // false if not saved
};

constexpr uint8_t kOff = 0;
constexpr uint8_t kOn = 1;

// Whether the screen is rotated: off unless kOn was saved, so a new panel, a factory reset or a
// value this firmware didn't write leaves it the right way up.
inline bool load(const Store& store) { return store.get() == kOn; }

// False if it couldn't be saved.
inline bool save(const Store& store, bool on) { return store.put(on ? kOn : kOff); }

}  // namespace screen_rotation
