#include <unity.h>

#include "screen_rotation.h"

void setUp() {}
void tearDown() {}

constexpr int W = 800, H = 480;

// Upside down, a finger on the glass's top-left corner is on the screen's bottom-right corner.
void test_a_touch_is_mirrored_through_the_center_when_rotated() {
    screen_rotation::Point p = screen_rotation::touch(0, 0, W, H, true);
    TEST_ASSERT_EQUAL(W - 1, p.x);
    TEST_ASSERT_EQUAL(H - 1, p.y);
    p = screen_rotation::touch(W - 1, H - 1, W, H, true);
    TEST_ASSERT_EQUAL(0, p.x);
    TEST_ASSERT_EQUAL(0, p.y);
    p = screen_rotation::touch(100, 40, W, H, true);
    TEST_ASSERT_EQUAL(699, p.x);
    TEST_ASSERT_EQUAL(439, p.y);
}

void test_a_touch_is_unchanged_when_not_rotated() {
    const screen_rotation::Point p = screen_rotation::touch(100, 40, W, H, false);
    TEST_ASSERT_EQUAL(100, p.x);
    TEST_ASSERT_EQUAL(40, p.y);
}

// The panel NVS: what the panel keeps between restarts. A factory reset empties it.
struct FakeNvs {
    int saved = -1;  // none
    bool full = false;
    screen_rotation::Store store() {
        return {[this] { return saved; },
                [this](uint8_t v) {
                    if (full) return false;
                    saved = v;
                    return true;
                }};
    }
};

// A new panel, or one after a factory reset, isn't rotated.
void test_the_setting_is_off_by_default() {
    FakeNvs nvs;
    TEST_ASSERT_FALSE(screen_rotation::load(nvs.store()));
}

// The choice survives a restart.
void test_the_setting_is_saved_and_loaded() {
    FakeNvs nvs;
    TEST_ASSERT_TRUE(screen_rotation::save(nvs.store(), true));
    TEST_ASSERT_TRUE(screen_rotation::load(nvs.store()));
    TEST_ASSERT_TRUE(screen_rotation::save(nvs.store(), false));
    TEST_ASSERT_FALSE(screen_rotation::load(nvs.store()));
}

// A value that isn't one this firmware saves leaves the screen the right way up.
void test_a_stored_value_that_isnt_ours_is_off() {
    FakeNvs nvs;
    nvs.saved = 7;
    TEST_ASSERT_FALSE(screen_rotation::load(nvs.store()));
}

void test_a_failed_save_says_so() {
    FakeNvs nvs;
    nvs.full = true;
    TEST_ASSERT_FALSE(screen_rotation::save(nvs.store(), true));
    TEST_ASSERT_FALSE(screen_rotation::load(nvs.store()));
}

// A tiny screen: what LVGL draws (logical) and what the glass shows (the framebuffer).
constexpr int SW = 8, SH = 4;
uint16_t logical(int x, int y) { return static_cast<uint16_t>(y * 100 + x); }

// One flush, as the panel's flush callback does it: LVGL's pixels for area a, row after row, then
// written to the framebuffer at the area flush() returns.
void flushTo(uint16_t glass[SH][SW], screen_rotation::Area a, bool rotated) {
    const int w = a.x2 - a.x1 + 1, h = a.y2 - a.y1 + 1;
    uint16_t px[SW * SH];
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) px[y * w + x] = logical(a.x1 + x, a.y1 + y);
    const screen_rotation::Area to = screen_rotation::flush(a, px, SW, SH, rotated);
    TEST_ASSERT_EQUAL(w, to.x2 - to.x1 + 1);
    TEST_ASSERT_EQUAL(h, to.y2 - to.y1 + 1);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) glass[to.y1 + y][to.x1 + x] = px[y * w + x];
}

// Upside down, the glass shows each pixel mirrored through the center, flushed in pieces as
// PARTIAL render mode does.
void test_the_picture_is_turned_half_a_turn_when_rotated() {
    uint16_t glass[SH][SW] = {};
    flushTo(glass, {0, 0, 7, 0}, true);  // a whole row
    flushTo(glass, {0, 1, 2, 3}, true);  // and blocks of the rest
    flushTo(glass, {3, 1, 7, 2}, true);
    flushTo(glass, {3, 3, 7, 3}, true);
    for (int y = 0; y < SH; ++y)
        for (int x = 0; x < SW; ++x) TEST_ASSERT_EQUAL(logical(SW - 1 - x, SH - 1 - y), glass[y][x]);
}

void test_the_picture_is_unchanged_when_not_rotated() {
    uint16_t glass[SH][SW] = {};
    flushTo(glass, {0, 0, 7, 1}, false);
    flushTo(glass, {0, 2, 4, 3}, false);
    flushTo(glass, {5, 2, 7, 3}, false);
    for (int y = 0; y < SH; ++y)
        for (int x = 0; x < SW; ++x) TEST_ASSERT_EQUAL(logical(x, y), glass[y][x]);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_touch_is_mirrored_through_the_center_when_rotated);
    RUN_TEST(test_a_touch_is_unchanged_when_not_rotated);
    RUN_TEST(test_the_setting_is_off_by_default);
    RUN_TEST(test_the_setting_is_saved_and_loaded);
    RUN_TEST(test_a_stored_value_that_isnt_ours_is_off);
    RUN_TEST(test_a_failed_save_says_so);
    RUN_TEST(test_the_picture_is_turned_half_a_turn_when_rotated);
    RUN_TEST(test_the_picture_is_unchanged_when_not_rotated);
    return UNITY_END();
}
