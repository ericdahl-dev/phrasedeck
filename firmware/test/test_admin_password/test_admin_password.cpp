#include <unity.h>

#include <cstring>

#include "admin_password.h"

void setUp() {}
void tearDown() {}

// A stand-in for PBKDF2 (mbedTLS isn't in the host build): mixes password, salt and iterations.
void fakeKdf(const std::string& pw, const uint8_t* salt, size_t saltLen, uint32_t iterations, uint8_t* out) {
    for (size_t i = 0; i < admin_password::kHashLen; ++i) {
        out[i] = static_cast<uint8_t>(iterations + i);
        for (char c : pw) out[i] = static_cast<uint8_t>(out[i] * 31 + c);
        out[i] ^= salt[i % saltLen];
    }
}

const uint8_t kSalt[admin_password::kSaltLen] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

// The admin password (#98) is set on the touchscreen, at least 8 characters, stored as
// salt + iterations + PBKDF2 hash; only the right password checks out.
void test_only_the_right_password_checks_out() {
    admin_password::Record r;
    TEST_ASSERT_TRUE(admin_password::make("correct horse", kSalt, 10000, fakeKdf, r));
    TEST_ASSERT_TRUE(admin_password::check(r, "correct horse", fakeKdf));
    TEST_ASSERT_FALSE(admin_password::check(r, "correct hors", fakeKdf));
    TEST_ASSERT_FALSE(admin_password::check(r, "", fakeKdf));
}

void test_short_passwords_and_low_iteration_counts_are_refused() {
    admin_password::Record r;
    TEST_ASSERT_FALSE(admin_password::make("1234567", kSalt, 10000, fakeKdf, r));
    TEST_ASSERT_FALSE(admin_password::make("long enough", kSalt, 9999, fakeKdf, r));
}

// The record goes to NVS as one blob; a blob of the wrong size or with too few iterations
// (damaged, or written by hand) doesn't load, so no login works until a reset.
void test_the_record_round_trips_as_a_blob_and_bad_blobs_dont_load() {
    admin_password::Record r, back;
    admin_password::make("correct horse", kSalt, 12345, fakeKdf, r);
    uint8_t blob[admin_password::kBlobLen];
    admin_password::toBlob(r, blob);
    TEST_ASSERT_TRUE(admin_password::fromBlob(blob, sizeof blob, back));
    TEST_ASSERT_TRUE(admin_password::check(back, "correct horse", fakeKdf));
    TEST_ASSERT_FALSE(admin_password::fromBlob(blob, sizeof blob - 1, back));
    std::memset(blob + admin_password::kSaltLen, 0, 4);  // iterations = 0
    TEST_ASSERT_FALSE(admin_password::fromBlob(blob, sizeof blob, back));
    std::memset(blob + admin_password::kSaltLen, 0xFF, 4);  // a corrupt count would hang check()
    TEST_ASSERT_FALSE(admin_password::fromBlob(blob, sizeof blob, back));
}

void test_iteration_counts_are_capped() {
    admin_password::Record r;
    TEST_ASSERT_TRUE(admin_password::make("long enough", kSalt, admin_password::kMaxIterations, fakeKdf, r));
    TEST_ASSERT_FALSE(admin_password::make("long enough", kSalt, admin_password::kMaxIterations + 1, fakeKdf, r));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_only_the_right_password_checks_out);
    RUN_TEST(test_short_passwords_and_low_iteration_counts_are_refused);
    RUN_TEST(test_the_record_round_trips_as_a_blob_and_bad_blobs_dont_load);
    RUN_TEST(test_iteration_counts_are_capped);
    return UNITY_END();
}
