#include <unity.h>

#include "ble_params.h"

void setUp() {}
void tearDown() {}

// Units as NimBLE gives them: the interval in 1.25 ms, the supervision timeout in 10 ms.
constexpr uint16_t kMax = 12;  // 15 ms, the panel's PANEL_BLE_CONN_MAX

// A host that's already fast and already drops a stalled link within 2 s keeps its own parameters,
// so a Mac at 11.25 ms isn't slowed to 15 ms.
void test_a_fast_host_with_a_short_timeout_is_left_alone() {
    TEST_ASSERT_FALSE(ble_params::shouldRequest(9, 200, kMax));   // 11.25 ms, 2 s
    TEST_ASSERT_FALSE(ble_params::shouldRequest(12, 200, kMax));  // 15 ms, 2 s
    TEST_ASSERT_FALSE(ble_params::shouldRequest(12, 42, kMax));   // 15 ms, 420 ms
}

// A slow interval makes every report wait for it (#174): ask for a faster one.
void test_a_slow_interval_is_asked_to_speed_up() {
    TEST_ASSERT_TRUE(ble_params::shouldRequest(24, 42, kMax));   // 30 ms, BlueZ's start
    TEST_ASSERT_TRUE(ble_params::shouldRequest(13, 200, kMax));  // 16.25 ms
}

// A fast interval with a long supervision timeout still gets the request (#187): the host would
// otherwise hold a key down through a stalled link for up to 32 s, long enough to auto-repeat it.
void test_a_fast_host_with_a_long_timeout_is_asked_for_2_s() {
    TEST_ASSERT_TRUE(ble_params::shouldRequest(9, 201, kMax));    // 11.25 ms, 2.01 s
    TEST_ASSERT_TRUE(ble_params::shouldRequest(12, 500, kMax));   // 15 ms, 5 s
    TEST_ASSERT_TRUE(ble_params::shouldRequest(6, 3200, kMax));   // 7.5 ms, 32 s
}

void test_the_timeout_asked_for_is_2_s() { TEST_ASSERT_EQUAL_UINT16(200, ble_params::kSupervisionTimeout); }

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_fast_host_with_a_short_timeout_is_left_alone);
    RUN_TEST(test_a_slow_interval_is_asked_to_speed_up);
    RUN_TEST(test_a_fast_host_with_a_long_timeout_is_asked_for_2_s);
    RUN_TEST(test_the_timeout_asked_for_is_2_s);
    return UNITY_END();
}
