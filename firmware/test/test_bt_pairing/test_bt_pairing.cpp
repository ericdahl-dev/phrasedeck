#include <unity.h>

#include "bt_pairing.h"

void setUp() {}
void tearDown() {}

using bt_pairing::Bond;
using bt_pairing::Window;

// A Computer that already paired reconnects any time; a new one only while the window is open, and
// then it replaces the old one, so the panel types to one Computer.
void test_who_may_pair() {
    TEST_ASSERT_EQUAL(Bond::Keep, bt_pairing::onPaired(false, false));
    TEST_ASSERT_EQUAL(Bond::Keep, bt_pairing::onPaired(false, true));
    TEST_ASSERT_EQUAL(Bond::Reject, bt_pairing::onPaired(true, false));
    TEST_ASSERT_EQUAL(Bond::ReplaceOthers, bt_pairing::onPaired(true, true));
}

// Opened from Settings for two minutes, closed early by a pairing or Cancel; right across the
// millis() wrap.
void test_the_pairing_window() {
    Window w;
    TEST_ASSERT_FALSE(w.isOpen(0));
    w.open(1000, 120000);
    TEST_ASSERT_TRUE(w.isOpen(1000));
    TEST_ASSERT_TRUE(w.isOpen(120999));
    TEST_ASSERT_FALSE(w.isOpen(121000));

    w.open(0xFFFFF000u, 120000);
    TEST_ASSERT_TRUE(w.isOpen(50000));
    w.close();
    TEST_ASSERT_FALSE(w.isOpen(50001));
}

// What the panel shows: the passkey as typed on the Computer (6 digits, leading zeros kept), and
// the link in one word for the status line.
void test_what_the_panel_shows() {
    TEST_ASSERT_EQUAL_STRING("", bt_pairing::passkeyText(-1).c_str());
    TEST_ASSERT_EQUAL_STRING("004217", bt_pairing::passkeyText(4217).c_str());
    TEST_ASSERT_EQUAL_STRING("999999", bt_pairing::passkeyText(999999).c_str());
    TEST_ASSERT_EQUAL_STRING("Connected", bt_pairing::status(true, false));
    TEST_ASSERT_EQUAL_STRING("Connected", bt_pairing::status(true, true));
    TEST_ASSERT_EQUAL_STRING("Pairing", bt_pairing::status(false, true));
    TEST_ASSERT_EQUAL_STRING("Not connected", bt_pairing::status(false, false));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_who_may_pair);
    RUN_TEST(test_the_pairing_window);
    RUN_TEST(test_what_the_panel_shows);
    return UNITY_END();
}
