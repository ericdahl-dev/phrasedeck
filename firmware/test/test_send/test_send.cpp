#include <unity.h>

#include <initializer_list>
#include <string>

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
    // HTTP clients report "no reply" as negative codes (HTTPClient's -11 read timeout); after the
    // request went out that's still Maybe typed, or the user taps again and it types twice.
    TEST_ASSERT_EQUAL(Outcome::MaybeTyped, send::outcome(true, -11));
    TEST_ASSERT_EQUAL(Outcome::NotTyped, send::outcome(false, -1));
}

// /type's body: the text, plus which panel Phrase and edit it is, so the Helper can show its
// edit notice (#98). A Send without a Phrase (the dev serial `type`) carries the text alone.
void test_the_type_body_names_the_phrase_and_its_edit() {
    TEST_ASSERT_EQUAL_STRING("{\"text\":\"Ship it\\n\",\"phrase\":\"a1b2:7\",\"edit\":3}",
                             send::typeBody("Ship it\n", "a1b2:7", 3).c_str());
    TEST_ASSERT_EQUAL_STRING("{\"text\":\"hi\"}", send::typeBody("hi", "", 0).c_str());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_status_codes_map_to_typed_not_typed_or_maybe_typed);
    RUN_TEST(test_no_reply_is_not_typed_unless_the_request_was_sent);
    RUN_TEST(test_the_type_body_names_the_phrase_and_its_edit);
    return UNITY_END();
}
