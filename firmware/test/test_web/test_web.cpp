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

// The session cookie: only "s", only 32 lowercase hex.
void test_the_session_token_comes_from_its_cookie() {
    const std::string t = "0123456789abcdef0123456789abcdef";
    TEST_ASSERT_EQUAL_STRING(t.c_str(), web::cookieToken("s=" + t).c_str());
    TEST_ASSERT_EQUAL_STRING(t.c_str(), web::cookieToken("a=1; s=" + t + "; b=2").c_str());
    TEST_ASSERT_EQUAL_STRING("", web::cookieToken("xs=" + t).c_str());
    TEST_ASSERT_EQUAL_STRING("", web::cookieToken("s=" + t.substr(1)).c_str());
    TEST_ASSERT_EQUAL_STRING("", web::cookieToken("s=" + t + "0").c_str());
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
    web::startSession(s, t, 0xFFFFFF00u);  // millis() wraps
    TEST_ASSERT_TRUE(web::sessionValid(s, t, 0x100u));
    web::endSession(s);
    TEST_ASSERT_FALSE(web::sessionValid(s, t, 0x100u));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_only_the_panels_own_address_is_an_allowed_host);
    RUN_TEST(test_a_post_needs_the_panels_own_origin);
    RUN_TEST(test_a_form_field_is_decoded);
    RUN_TEST(test_the_session_token_comes_from_its_cookie);
    RUN_TEST(test_a_session_matches_its_token_until_it_expires);
    return UNITY_END();
}
