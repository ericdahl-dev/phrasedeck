#include <unity.h>

#include "nearby.h"

void setUp() {}
void tearDown() {}

// A Helper's Bonjour TXT (`v`, `port`, `fp`, maybe `ts`) is enough to show it as Nearby.
void test_a_helpers_txt_is_nearby() {
    nearby::Computer c;
    TEST_ASSERT_TRUE(nearby::parse({{"v", "1"}, {"port", "47812"},
                                    {"fp", "3F9A0C1277BE41D0" "00112233445566778899AABBCCDDEEFF" "0011223344556677"},
                                    {"ts", "omarchy-black2.tail1234.ts.net"}},
                                   c));
    TEST_ASSERT_EQUAL(47812, c.port);
    TEST_ASSERT_EQUAL_STRING("3f9a0c1277be41d000112233445566778899aabbccddeeff0011223344556677", c.fp.c_str());
    TEST_ASSERT_EQUAL_STRING("omarchy-black2.tail1234.ts.net", c.ts.c_str());
}

// A spoofed or malformed TXT never shows up as a Helper.
void test_a_bad_txt_is_not_nearby() {
    const std::string fp(64, 'a');
    nearby::Computer c;
    TEST_ASSERT_FALSE(nearby::parse({{"v", "2"}, {"port", "47812"}, {"fp", fp}}, c));
    TEST_ASSERT_FALSE(nearby::parse({{"port", "47812"}, {"fp", fp}}, c));
    TEST_ASSERT_FALSE(nearby::parse({{"v", "1"}, {"port", "0"}, {"fp", fp}}, c));
    TEST_ASSERT_FALSE(nearby::parse({{"v", "1"}, {"port", "65536"}, {"fp", fp}}, c));
    TEST_ASSERT_FALSE(nearby::parse({{"v", "1"}, {"port", "47812x"}, {"fp", fp}}, c));
    TEST_ASSERT_FALSE(nearby::parse({{"v", "1"}, {"port", "47812"}, {"fp", fp.substr(1)}}, c));
    TEST_ASSERT_FALSE(nearby::parse({{"v", "1"}, {"port", "47812"}, {"fp", fp.substr(1) + "g"}}, c));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_helpers_txt_is_nearby);
    RUN_TEST(test_a_bad_txt_is_not_nearby);
    return UNITY_END();
}
