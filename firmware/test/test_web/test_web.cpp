#include <unity.h>

#include <string>

#include "web.h"

void setUp() {}
void tearDown() {}

// DNS rebinding (#98): a page from another name that resolves to the panel must not reach it, so
// only the panel's own address is an allowed Host.
void test_only_the_panels_own_address_is_an_allowed_host() {
    TEST_ASSERT_TRUE(web::hostAllowed("192.168.1.58", "192.168.1.58"));
    TEST_ASSERT_TRUE(web::hostAllowed("192.168.1.58:443", "192.168.1.58"));
    TEST_ASSERT_FALSE(web::hostAllowed("evil.example", "192.168.1.58"));
    TEST_ASSERT_FALSE(web::hostAllowed("192.168.1.58.evil.example", "192.168.1.58"));
    TEST_ASSERT_FALSE(web::hostAllowed("192.168.1.5", "192.168.1.58"));
    TEST_ASSERT_FALSE(web::hostAllowed("192.168.1.58:8443", "192.168.1.58"));
    TEST_ASSERT_FALSE(web::hostAllowed("", ""));
}

// Every POST, the login too, must come from the panel's own page.
void test_a_post_needs_the_panels_own_origin() {
    TEST_ASSERT_TRUE(web::originAllowed("https://192.168.1.58", "192.168.1.58"));
    TEST_ASSERT_FALSE(web::originAllowed("", "192.168.1.58"));
    TEST_ASSERT_FALSE(web::originAllowed("null", "192.168.1.58"));
    TEST_ASSERT_FALSE(web::originAllowed("http://192.168.1.58", "192.168.1.58"));
    TEST_ASSERT_FALSE(web::originAllowed("https://192.168.1.58.evil.example", "192.168.1.58"));
    TEST_ASSERT_FALSE(web::originAllowed("https://evil.example", "192.168.1.58"));
}

// The login form posts application/x-www-form-urlencoded.
void test_a_form_field_is_decoded() {
    TEST_ASSERT_EQUAL_STRING("correct horse", web::formValue("password=correct+horse", "password").c_str());
    TEST_ASSERT_EQUAL_STRING("a&b=c%", web::formValue("x=1&password=a%26b%3Dc%25", "password").c_str());
    TEST_ASSERT_EQUAL_STRING("", web::formValue("passwordx=1", "password").c_str());
    TEST_ASSERT_EQUAL_STRING("", web::formValue("password=%zz", "password").c_str());  // bad escape
    TEST_ASSERT_EQUAL_STRING("", web::formValue("password=a%4&x=1", "password").c_str());  // cut-off escape
    TEST_ASSERT_EQUAL_STRING("", web::formValue("password=", "password").c_str());
}

// The session cookie: only "__Host-s" (the browser takes it only from the panel's own https
// origin, Path=/, no Domain), only 32 lowercase hex.
void test_the_session_token_comes_from_its_cookie() {
    const std::string t = "0123456789abcdef0123456789abcdef";
    TEST_ASSERT_EQUAL_STRING(t.c_str(), web::cookieToken("__Host-s=" + t).c_str());
    TEST_ASSERT_EQUAL_STRING(t.c_str(), web::cookieToken("a=1; __Host-s=" + t + "; b=2").c_str());
    TEST_ASSERT_EQUAL_STRING("", web::cookieToken("s=" + t).c_str());  // no prefix: any subdomain could set it
    TEST_ASSERT_EQUAL_STRING("", web::cookieToken("x__Host-s=" + t).c_str());
    TEST_ASSERT_EQUAL_STRING("", web::cookieToken("__Host-s=" + t.substr(1)).c_str());
    TEST_ASSERT_EQUAL_STRING("", web::cookieToken("__Host-s=" + t + "0").c_str());
    TEST_ASSERT_EQUAL_STRING("", web::cookieToken("").c_str());
}

// One session at a time; it matches only its own token and ends after kSessionMs idle.
void test_a_session_matches_its_token_until_it_expires() {
    web::Session s;
    const std::string t = "0123456789abcdef0123456789abcdef";
    TEST_ASSERT_FALSE(web::sessionValid(s, t, 1000));  // none yet
    web::startSession(s, t, 1000);
    TEST_ASSERT_TRUE(web::sessionValid(s, t, 2000));
    TEST_ASSERT_FALSE(web::sessionValid(s, "0123456789abcdef0123456789abcdee", 2000));
    TEST_ASSERT_FALSE(web::sessionValid(s, "", 2000));
    web::touch(s, 2000);
    TEST_ASSERT_TRUE(web::sessionValid(s, t, 2000 + web::kSessionMs - 1));
    TEST_ASSERT_FALSE(web::sessionValid(s, t, 2000 + web::kSessionMs));
    web::startSession(s, t, 0xFFFFFF00u);  // across where a 32-bit clock would wrap
    TEST_ASSERT_TRUE(web::sessionValid(s, t, 0x100000100ull));
    web::endSession(s);
    TEST_ASSERT_FALSE(web::sessionValid(s, t, 0x100000100ull));
}

// Requests keep a session alive, but never past kSessionMaxMs from login: a stolen cookie in
// a tab left open doesn't work forever.
void test_activity_does_not_keep_a_session_past_its_cap() {
    web::Session s;
    const std::string t = "0123456789abcdef0123456789abcdef";
    const uint64_t start = 0xFFFFF000u;  // past where a 32-bit millis() would wrap
    web::startSession(s, t, start);
    for (uint64_t now = start; now - start < web::kSessionMaxMs - 1000; now += web::kSessionMs / 2) {
        web::touch(s, now);
        TEST_ASSERT_TRUE(web::sessionValid(s, t, now));
    }
    web::touch(s, start + web::kSessionMaxMs - 1);
    TEST_ASSERT_TRUE(web::sessionValid(s, t, start + web::kSessionMaxMs - 1));
    TEST_ASSERT_FALSE(web::sessionValid(s, t, start + web::kSessionMaxMs));
}

// A 32-bit clock comes back around after 49.7 days: a cookie left unused that long must not look
// fresh again.
void test_a_session_left_unused_for_49_days_stays_ended() {
    web::Session s;
    const std::string t = "0123456789abcdef0123456789abcdef";
    const uint64_t hour = 60ull * 60 * 1000;
    web::startSession(s, t, 1000);
    web::touch(s, 1000 + hour);
    TEST_ASSERT_FALSE(web::sessionValid(s, t, 1000 + hour + (1ull << 32) + 60000));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_only_the_panels_own_address_is_an_allowed_host);
    RUN_TEST(test_a_post_needs_the_panels_own_origin);
    RUN_TEST(test_a_form_field_is_decoded);
    RUN_TEST(test_the_session_token_comes_from_its_cookie);
    RUN_TEST(test_a_session_matches_its_token_until_it_expires);
    RUN_TEST(test_activity_does_not_keep_a_session_past_its_cap);
    RUN_TEST(test_a_session_left_unused_for_49_days_stays_ended);
    return UNITY_END();
}
