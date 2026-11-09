#include <unity.h>

#include <string>

#include "dongle_trust.h"

void setUp() {}
void tearDown() {}

using dongle_trust::Hold;

// Holding BOOT for 5 s forgets the phone: it fires once, at 5 s, however long the button stays down.
void test_holding_boot_five_seconds_fires_once() {
    Hold h(5000);
    TEST_ASSERT_FALSE(h.update(true, 1000));
    TEST_ASSERT_FALSE(h.update(true, 5999));
    TEST_ASSERT_TRUE(h.update(true, 6000));
    TEST_ASSERT_FALSE(h.update(true, 9000));
    TEST_ASSERT_FALSE(h.update(false, 9100));
}

// Letting go before 5 s starts the count again, so taps never add up to a reset.
void test_a_shorter_press_does_nothing_and_starts_over() {
    Hold h(5000);
    h.update(true, 0);
    TEST_ASSERT_FALSE(h.update(true, 4900));
    TEST_ASSERT_FALSE(h.update(false, 4950));
    TEST_ASSERT_FALSE(h.update(true, 5000));
    TEST_ASSERT_FALSE(h.update(true, 9999));
    TEST_ASSERT_TRUE(h.update(true, 10000));
}

// millis() wraps after 49 days; the hold still times right across it.
void test_the_hold_times_right_across_the_millis_wrap() {
    Hold h(5000);
    h.update(true, 0xFFFFF000u);
    TEST_ASSERT_FALSE(h.update(true, 0x00000100u));
    TEST_ASSERT_TRUE(h.update(true, 0x00000388u));
}

static const phrase_link::Token kToken = {0x00, 0x01, 0x0a, 0xff, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 0xab};

// The Token reads as 32 lowercase hex digits: nRF Connect shows it as text, and the same digits
// pasted as hex go into a Begin.
void test_the_token_reads_as_hex_only_by_the_phone_in_its_window() {
    TEST_ASSERT_EQUAL_STRING("00010aff0405060708090a0b0c0d0eab",
                             dongle_trust::tokenText(kToken, true, true).c_str());
    TEST_ASSERT_EQUAL_STRING("", dongle_trust::tokenText(kToken, false, true).c_str());  // a stranger
    TEST_ASSERT_EQUAL_STRING("", dongle_trust::tokenText(kToken, true, false).c_str());  // too late
}

// A dongle takes a new phone only while it has none: new, or after the BOOT reset. Then the
// passkey shows on USB serial. With a phone, every new pairing is ended before its passkey.
void test_only_a_dongle_with_no_phone_takes_a_new_one() {
    TEST_ASSERT_TRUE(dongle_trust::acceptsNewPhone(false));
    TEST_ASSERT_FALSE(dongle_trust::acceptsNewPhone(true));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_holding_boot_five_seconds_fires_once);
    RUN_TEST(test_a_shorter_press_does_nothing_and_starts_over);
    RUN_TEST(test_the_hold_times_right_across_the_millis_wrap);
    RUN_TEST(test_the_token_reads_as_hex_only_by_the_phone_in_its_window);
    RUN_TEST(test_only_a_dongle_with_no_phone_takes_a_new_one);
    return UNITY_END();
}
