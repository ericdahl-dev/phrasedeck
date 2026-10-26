// GT911 touch decoding, apart from the I2C so it runs in host tests.
#pragma once

#include <cstdint>

namespace gt911 {

struct Touch {
    bool pressed = false;
    int16_t x = 0;
    int16_t y = 0;
    uint8_t failedReads = 0;  // in a row
};

// Failed reads in a row before a press is released: one glitch mid-press would otherwise
// release and press again, which LVGL sees as a second tap.
constexpr uint8_t kReleaseAfterFailedReads = 3;

// Updates t from one read of registers 0x814E..0x8153 (status, reserved, X lo/hi, Y lo/hi).
// readOk is false when the I2C read failed; kReleaseAfterFailedReads of those in a row release,
// so a finger is never stuck down.
// A result the controller hasn't marked fresh (status bit 7) leaves t as it was. Points are
// clamped to the panel, so a glitched read never lands outside every object.
inline void decode(const uint8_t buf[6], bool readOk, int16_t width, int16_t height, Touch& t) {
    if (!readOk) {
        if (t.failedReads < kReleaseAfterFailedReads) ++t.failedReads;
        if (t.failedReads >= kReleaseAfterFailedReads) t.pressed = false;
        return;
    }
    t.failedReads = 0;
    if ((buf[0] & 0x80) == 0) return;
    if ((buf[0] & 0x0F) == 0) {
        t.pressed = false;
        return;
    }
    const int x = buf[2] | (buf[3] << 8);
    const int y = buf[4] | (buf[5] << 8);
    t.x = static_cast<int16_t>(x >= width ? width - 1 : x);
    t.y = static_cast<int16_t>(y >= height ? height - 1 : y);
    t.pressed = true;
}

}  // namespace gt911
