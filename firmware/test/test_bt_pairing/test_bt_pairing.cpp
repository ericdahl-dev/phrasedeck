#include <unity.h>

#include "bt_pairing.h"

void setUp() {}
void tearDown() {}

using bt_pairing::Bond;
using bt_pairing::Window;

// The stored Computer reconnects any time. A new pairing replaces it: a passkey is only ever shown
// inside a window, so one entered after the window ran out still counts (#141). A reconnect from any
// other bond is rejected, such as one whose keys outlived a reset between its rejection and its
// disconnect.
void test_who_may_pair() {
    const bool known = true, newPairing = true;
    TEST_ASSERT_EQUAL(Bond::Keep, bt_pairing::onPaired(!newPairing, known));
    TEST_ASSERT_EQUAL(Bond::Reject, bt_pairing::onPaired(!newPairing, !known));
    TEST_ASSERT_EQUAL(Bond::ReplaceOthers, bt_pairing::onPaired(newPairing, !known));
    TEST_ASSERT_EQUAL(Bond::ReplaceOthers, bt_pairing::onPaired(newPairing, known));
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

// With a Computer stored and no window open, only the Computer may connect, so a nearby device can't
// start the pairing that wipes its bond (#144). A window lets a new Computer in.
void test_only_the_computer_may_connect_outside_a_window() {
    const bool computer = true, window = true;
    TEST_ASSERT_TRUE(bt_pairing::allowListOnly(computer, !window));
    TEST_ASSERT_FALSE(bt_pairing::allowListOnly(computer, window));
    TEST_ASSERT_FALSE(bt_pairing::allowListOnly(!computer, !window));
    TEST_ASSERT_FALSE(bt_pairing::allowListOnly(!computer, window));
}

// Keys are saved only for a passkey pairing from a window: a Just Works pairing claiming the
// Computer's identity address can't overwrite its keys (#144). Other records, such as
// subscriptions, are saved as usual.
void test_only_a_window_pairing_saves_keys() {
    const bool keys = true, windowPairing = true;
    TEST_ASSERT_TRUE(bt_pairing::saveToStore(keys, windowPairing));
    TEST_ASSERT_FALSE(bt_pairing::saveToStore(keys, !windowPairing));
    TEST_ASSERT_TRUE(bt_pairing::saveToStore(!keys, !windowPairing));
}

// A Computer that sent a real IRK is matched by the private addresses made from it only, not by its
// identity address sent in the clear, which anyone can copy (#144). One with no IRK, or an all-zero
// one, can only be matched by its address.
void test_a_computer_with_an_irk_is_matched_by_it_only() {
    const uint8_t irk[16] = {0x5a, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    const uint8_t zero[16] = {};
    TEST_ASSERT_TRUE(bt_pairing::networkPrivacy(true, irk));
    TEST_ASSERT_FALSE(bt_pairing::networkPrivacy(true, zero));
    TEST_ASSERT_FALSE(bt_pairing::networkPrivacy(false, irk));
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
    RUN_TEST(test_a_window_lapses_when_it_runs_out);
    RUN_TEST(test_an_idle_unencrypted_connection_is_dropped);
    RUN_TEST(test_what_the_panel_shows);
    RUN_TEST(test_only_the_computer_may_connect_outside_a_window);
    RUN_TEST(test_only_a_window_pairing_saves_keys);
    RUN_TEST(test_a_computer_with_an_irk_is_matched_by_it_only);
    return UNITY_END();
}
