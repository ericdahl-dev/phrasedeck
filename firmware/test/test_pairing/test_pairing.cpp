#include <unity.h>

#include "pairing.h"

void setUp() {}
void tearDown() {}

// The keypad builds the 6-digit Pairing code; Back removes the last digit; the code is complete
// at exactly 6, and further digits are ignored.
void test_the_keypad_builds_a_six_digit_code() {
    pairing::CodeEntry e;
    for (char d : std::string("12345")) e.digit(d);
    TEST_ASSERT_FALSE(e.complete());
    e.back();
    e.digit('9');
    e.digit('0');
    TEST_ASSERT_TRUE(e.complete());
    e.digit('7');
    TEST_ASSERT_EQUAL_STRING("123490", e.code().c_str());
    e.back();
    TEST_ASSERT_FALSE(e.complete());
    TEST_ASSERT_EQUAL_STRING("12349", e.code().c_str());
}

// The short Pin from `phrased pair` (16 hex, spaces and case ignored) must start the handshake
// certificate's SHA-256, or the Pairing code isn't sent.
void test_the_code_goes_only_to_a_cert_matching_the_short_pin() {
    const std::string cert = "5597416a434cd222035b408fa54b84ab02cc0e190f3ca4dc251c67233bf1cfaa";
    TEST_ASSERT_TRUE(pairing::matchesShortPin("5597 416a 434c d222", cert));
    TEST_ASSERT_TRUE(pairing::matchesShortPin("5597416A434CD222", cert));
    TEST_ASSERT_FALSE(pairing::matchesShortPin("5597 416a 434c d223", cert));
    TEST_ASSERT_FALSE(pairing::matchesShortPin("5597 416a 434c", cert));          // too short
    TEST_ASSERT_FALSE(pairing::matchesShortPin("5597 416a 434c d222 03", cert));  // too long
    TEST_ASSERT_FALSE(pairing::matchesShortPin("", cert));
    TEST_ASSERT_FALSE(pairing::matchesShortPin("5597 416a 434c d222", ""));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_the_keypad_builds_a_six_digit_code);
    RUN_TEST(test_the_code_goes_only_to_a_cert_matching_the_short_pin);
    return UNITY_END();
}
