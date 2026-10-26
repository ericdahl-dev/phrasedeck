#include <unity.h>

#include "factory_reset.h"

void setUp() {}
void tearDown() {}

using factory_reset::Hold;

// A finger on the top-left corner from power-on, held 10 s, erases the panel. Anything else boots.
void test_ten_seconds_on_the_top_left_corner_resets() {
    TEST_ASSERT(factory_reset::check(true, 10, 10, 0) == Hold::Holding);
    TEST_ASSERT(factory_reset::check(true, 10, 10, 9999) == Hold::Holding);
    TEST_ASSERT(factory_reset::check(true, 10, 10, 10000) == Hold::Reset);
}

void test_no_finger_lifting_it_or_another_spot_boots() {
    TEST_ASSERT(factory_reset::check(false, 10, 10, 0) == Hold::Boot);
    TEST_ASSERT(factory_reset::check(false, 10, 10, 5000) == Hold::Boot);  // lifted early
    TEST_ASSERT(factory_reset::check(true, 400, 240, 0) == Hold::Boot);    // middle of the glass
    TEST_ASSERT(factory_reset::check(true, 10, 150, 5000) == Hold::Boot);  // slid off the corner
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_ten_seconds_on_the_top_left_corner_resets);
    RUN_TEST(test_no_finger_lifting_it_or_another_spot_boots);
    return UNITY_END();
}
