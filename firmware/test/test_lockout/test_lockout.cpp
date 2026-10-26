#include <unity.h>

#include "lockout.h"

void setUp() {}
void tearDown() {}

// Wrong admin passwords (#98): 5 free tries, then a wait that doubles from 30 s up to 15 min.
// The count is what's saved, so a crash or reboot can't reset it; times are ms since boot.
void test_five_tries_then_a_doubling_wait_capped_at_15_minutes() {
    lockout::State s;
    for (int i = 0; i < 4; ++i) lockout::failed(s, 1000);
    TEST_ASSERT_EQUAL_UINT32(0, lockout::waitMs(s, 1000));
    lockout::failed(s, 1000);  // 5th
    TEST_ASSERT_EQUAL_UINT32(30000, lockout::waitMs(s, 1000));
    TEST_ASSERT_EQUAL_UINT32(10000, lockout::waitMs(s, 21000));
    TEST_ASSERT_EQUAL_UINT32(0, lockout::waitMs(s, 31000));
    lockout::failed(s, 31000);  // 6th
    TEST_ASSERT_EQUAL_UINT32(60000, lockout::waitMs(s, 31000));
    for (int i = 0; i < 20; ++i) lockout::failed(s, 31000);
    TEST_ASSERT_EQUAL_UINT32(15u * 60 * 1000, lockout::waitMs(s, 31000));
}

// A right password, or the owner clearing it on the touchscreen, ends the lockout.
void test_success_clears_it() {
    lockout::State s;
    for (int i = 0; i < 7; ++i) lockout::failed(s, 0);
    lockout::clear(s);
    TEST_ASSERT_EQUAL_UINT32(0, lockout::waitMs(s, 0));
    TEST_ASSERT_EQUAL_UINT32(0, s.failures);
}

// After a reboot the clock restarts but the saved count doesn't: the full wait applies again.
void test_after_a_reboot_the_wait_starts_over_from_boot() {
    const lockout::State saved = lockout::loaded(6, 500);  // count from NVS, at 500 ms after boot
    TEST_ASSERT_EQUAL_UINT32(60000, lockout::waitMs(saved, 500));
    TEST_ASSERT_EQUAL_UINT32(0, lockout::waitMs(lockout::loaded(3, 500), 500));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_five_tries_then_a_doubling_wait_capped_at_15_minutes);
    RUN_TEST(test_success_clears_it);
    RUN_TEST(test_after_a_reboot_the_wait_starts_over_from_boot);
    return UNITY_END();
}
