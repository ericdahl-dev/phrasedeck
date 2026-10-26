#include <unity.h>

#include "gt911.h"

void setUp() {}
void tearDown() {}

// Bytes from register 0x814E: status (bit 7 fresh, low nibble points), reserved, X lo/hi, Y lo/hi.
void test_a_fresh_point_presses_and_is_clamped_to_the_panel() {
    gt911::Touch t;
    const uint8_t at_300_200[6] = {0x81, 0, 0x2C, 0x01, 0xC8, 0x00};
    gt911::decode(at_300_200, true, 800, 480, t);
    TEST_ASSERT_TRUE(t.pressed);
    TEST_ASSERT_EQUAL(300, t.x);
    TEST_ASSERT_EQUAL(200, t.y);

    const uint8_t off_screen[6] = {0x81, 0, 0x40, 0x06, 0xFF, 0x01};  // 1600, 511
    gt911::decode(off_screen, true, 800, 480, t);
    TEST_ASSERT_EQUAL(799, t.x);
    TEST_ASSERT_EQUAL(479, t.y);
}

void test_a_fresh_result_with_no_points_releases() {
    gt911::Touch t{true, 10, 10};
    const uint8_t none[6] = {0x80, 0, 0, 0, 0, 0};
    gt911::decode(none, true, 800, 480, t);
    TEST_ASSERT_FALSE(t.pressed);
}

void test_a_stale_result_keeps_the_last_state() {
    gt911::Touch t{true, 10, 10};
    const uint8_t stale[6] = {0x01, 0, 0, 0, 0, 0};
    gt911::decode(stale, true, 800, 480, t);
    TEST_ASSERT_TRUE(t.pressed);
}

// A failed I2C read must not leave a finger stuck down (#91 review).
void test_a_failed_read_releases() {
    gt911::Touch t{true, 10, 10};
    const uint8_t junk[6] = {0x81, 0, 1, 0, 1, 0};
    gt911::decode(junk, false, 800, 480, t);
    TEST_ASSERT_FALSE(t.pressed);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_fresh_point_presses_and_is_clamped_to_the_panel);
    RUN_TEST(test_a_fresh_result_with_no_points_releases);
    RUN_TEST(test_a_stale_result_keeps_the_last_state);
    RUN_TEST(test_a_failed_read_releases);
    return UNITY_END();
}
