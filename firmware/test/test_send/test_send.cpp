#include <unity.h>

#include <initializer_list>

#include "send.h"

void setUp() {}
void tearDown() {}

using send::Outcome;

// What a tap's Send means for the user, from /type's status (docs: "What each status means").
void test_status_codes_map_to_typed_not_typed_or_maybe_typed() {
    TEST_ASSERT_EQUAL(Outcome::Typed, send::outcome(true, 200));
    TEST_ASSERT_EQUAL(Outcome::MaybeTyped, send::outcome(true, 500));
    for (int status : {400, 401, 403, 404, 409, 413, 415, 429, 503}) {
        TEST_ASSERT_EQUAL(Outcome::NotTyped, send::outcome(true, status));
    }
}

// Nothing went out (no Wi-Fi, no TLS, or the Pin didn't match): safe to tap again. The request went
// out but no reply came back: the Helper may have typed it.
void test_no_reply_is_not_typed_unless_the_request_was_sent() {
    TEST_ASSERT_EQUAL(Outcome::NotTyped, send::outcome(false, 0));
    TEST_ASSERT_EQUAL(Outcome::MaybeTyped, send::outcome(true, 0));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_status_codes_map_to_typed_not_typed_or_maybe_typed);
    RUN_TEST(test_no_reply_is_not_typed_unless_the_request_was_sent);
    return UNITY_END();
}
