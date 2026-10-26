#include <unity.h>

#include "pairing.h"

void setUp() {}
void tearDown() {}

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

// The cert's SHA-256 digest becomes the lowercase hex the Helper prints and the Pin compares with.
void test_a_digest_is_lowercase_hex() {
    uint8_t digest[32];
    for (int i = 0; i < 32; ++i) digest[i] = static_cast<uint8_t>(i * 8 + 0x0A);
    TEST_ASSERT_EQUAL_STRING("0a121a222a323a424a525a626a727a828a929aa2aab2bac2cad2dae2eaf2fa02",
                             pairing::hexLower(digest).c_str());
}

// A 200 from /pair gives the Token. Its certSHA256 must be the cert the panel just checked, or
// the reply came from somewhere else and nothing is stored.
void test_a_pair_reply_gives_the_token_for_the_checked_cert() {
    const std::string fp = "5597416a434cd222035b408fa54b84ab02cc0e190f3ca4dc251c67233bf1cfaa";
    std::string token;
    TEST_ASSERT_TRUE(pairing::readPairReply(
        200, "{\"token\":\"tok-123\",\"certSHA256\":\"" + fp + "\",\"lanPort\":47812}", fp, token));
    TEST_ASSERT_EQUAL_STRING("tok-123", token.c_str());

    std::string other = fp;
    other[63] = '0';
    TEST_ASSERT_FALSE(pairing::readPairReply(
        200, "{\"token\":\"tok-123\",\"certSHA256\":\"" + other + "\"}", fp, token));
    TEST_ASSERT_FALSE(pairing::readPairReply(200, "{\"token\":\"tok-123\"}", fp, token));
    TEST_ASSERT_FALSE(pairing::readPairReply(200, "{\"certSHA256\":\"" + fp + "\"}", fp, token));
    TEST_ASSERT_FALSE(pairing::readPairReply(401, "{\"error\":\"wrong code\"}", fp, token));
    TEST_ASSERT_FALSE(pairing::readPairReply(200, "not json", fp, token));
}

// Every later request compares the whole Pin, not the short one.
void test_later_requests_need_the_whole_pin() {
    const std::string fp = "5597416a434cd222035b408fa54b84ab02cc0e190f3ca4dc251c67233bf1cfaa";
    std::string other = fp;
    other[63] = '0';
    TEST_ASSERT_TRUE(pairing::matchesPin(fp, fp));
    TEST_ASSERT_FALSE(pairing::matchesPin(fp, other));
    TEST_ASSERT_FALSE(pairing::matchesPin(fp, fp.substr(0, 16)));
    TEST_ASSERT_FALSE(pairing::matchesPin("", ""));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_the_code_goes_only_to_a_cert_matching_the_short_pin);
    RUN_TEST(test_a_digest_is_lowercase_hex);
    RUN_TEST(test_a_pair_reply_gives_the_token_for_the_checked_cert);
    RUN_TEST(test_later_requests_need_the_whole_pin);
    return UNITY_END();
}
