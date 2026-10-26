#include <unity.h>

#include "bt_pairing.h"

void setUp() {}
void tearDown() {}

using bt_pairing::Bond;
using bt_pairing::Window;

// The stored Computer reconnects any time; a new pairing only while the window is open, and then it
// replaces the old one, so the panel types to one Computer. A reconnect from any other bond is
// rejected, such as one whose keys outlived a reset between its rejection and its disconnect (#141).
void test_who_may_pair() {
    const bool known = true, window = true, newPairing = true;
    TEST_ASSERT_EQUAL(Bond::Keep, bt_pairing::onPaired(!newPairing, known, !window));
    TEST_ASSERT_EQUAL(Bond::Keep, bt_pairing::onPaired(!newPairing, known, window));
    TEST_ASSERT_EQUAL(Bond::Reject, bt_pairing::onPaired(!newPairing, !known, !window));
    TEST_ASSERT_EQUAL(Bond::Reject, bt_pairing::onPaired(!newPairing, !known, window));
    TEST_ASSERT_EQUAL(Bond::Reject, bt_pairing::onPaired(newPairing, !known, !window));
    TEST_ASSERT_EQUAL(Bond::Reject, bt_pairing::onPaired(newPairing, known, !window));
    TEST_ASSERT_EQUAL(Bond::ReplaceOthers, bt_pairing::onPaired(newPairing, !known, window));
    TEST_ASSERT_EQUAL(Bond::ReplaceOthers, bt_pairing::onPaired(newPairing, known, window));
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

// A window that runs out without a pairing or Cancel has lapsed, once, so the panel can stop
// advertising (#141). Closing it ends that.
void test_a_window_lapses_when_it_runs_out() {
    Window w;
    TEST_ASSERT_FALSE(w.lapsed(0));
    w.open(1000, 120000);
    TEST_ASSERT_FALSE(w.lapsed(120999));
    TEST_ASSERT_TRUE(w.lapsed(121000));
    w.close();
    TEST_ASSERT_FALSE(w.lapsed(121001));
}

// A connection that hasn't encrypted within its grace ends, so idle strangers can't fill NimBLE's
// three slots (#141). Not while a window is open, and never one showing a passkey: entering it can
// outlast both the grace and the window.
void test_an_idle_unencrypted_connection_is_dropped() {
    const bool encrypted = true, passkey = true, window = true, grace = true;
    TEST_ASSERT_TRUE(bt_pairing::dropIdle(!encrypted, !passkey, !window, !grace));
    TEST_ASSERT_FALSE(bt_pairing::dropIdle(!encrypted, !passkey, !window, grace));
    TEST_ASSERT_FALSE(bt_pairing::dropIdle(!encrypted, !passkey, window, !grace));
    TEST_ASSERT_FALSE(bt_pairing::dropIdle(!encrypted, passkey, !window, !grace));
    TEST_ASSERT_FALSE(bt_pairing::dropIdle(encrypted, !passkey, !window, !grace));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_who_may_pair);
    RUN_TEST(test_the_pairing_window);
    RUN_TEST(test_a_window_lapses_when_it_runs_out);
    RUN_TEST(test_an_idle_unencrypted_connection_is_dropped);
    return UNITY_END();
}
