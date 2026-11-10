#include <unity.h>

#include <string>

#include "dongle_trust.h"

void setUp() {}
void tearDown() {}

using dongle_trust::Hold;

// Holding BOOT for 5 s forgets the phone: it fires once, at 5 s, however long the button stays down.
void test_holding_boot_five_seconds_fires_once() {
    Hold h(5000);
    TEST_ASSERT_FALSE(h.update(true, 1000));
    TEST_ASSERT_FALSE(h.update(true, 5999));
    TEST_ASSERT_TRUE(h.update(true, 6000));
    TEST_ASSERT_FALSE(h.update(true, 9000));
    TEST_ASSERT_FALSE(h.update(false, 9100));
}

// Letting go before 5 s starts the count again, so taps never add up to a reset.
void test_a_shorter_press_does_nothing_and_starts_over() {
    Hold h(5000);
    h.update(true, 0);
    TEST_ASSERT_FALSE(h.update(true, 4900));
    TEST_ASSERT_FALSE(h.update(false, 4950));
    TEST_ASSERT_FALSE(h.update(true, 5000));
    TEST_ASSERT_FALSE(h.update(true, 9999));
    TEST_ASSERT_TRUE(h.update(true, 10000));
}

// millis() wraps after 49 days; the hold still times right across it.
void test_the_hold_times_right_across_the_millis_wrap() {
    Hold h(5000);
    h.update(true, 0xFFFFF000u);
    TEST_ASSERT_FALSE(h.update(true, 0x00000100u));
    TEST_ASSERT_TRUE(h.update(true, 0x00000388u));
}

static const phrase_link::Token kToken = {0x00, 0x01, 0x0a, 0xff, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 0xab};

// The Token reads as 32 lowercase hex digits: nRF Connect shows it as text, and the same digits
// pasted as hex go into a Begin.
void test_the_token_reads_as_hex_only_by_the_phone_in_its_window() {
    TEST_ASSERT_EQUAL_STRING("00010aff0405060708090a0b0c0d0eab",
                             dongle_trust::tokenText(kToken, true, true).c_str());
    TEST_ASSERT_EQUAL_STRING("", dongle_trust::tokenText(kToken, false, true).c_str());  // a stranger
    TEST_ASSERT_EQUAL_STRING("", dongle_trust::tokenText(kToken, true, false).c_str());  // too late
}

// A dongle takes a new phone only while it has none: new, or after the BOOT reset. Then the
// passkey shows on USB serial. With a phone, every new pairing is ended before its passkey.
void test_only_a_dongle_with_no_phone_takes_a_new_one() {
    TEST_ASSERT_TRUE(dongle_trust::acceptsNewPhone(false));
    TEST_ASSERT_FALSE(dongle_trust::acceptsNewPhone(true));
}

// The dongle's end of a forget, for the tests: what was erased, BOOT, the clock and the restart.
struct FakeDongle : dongle_trust::Device {
    std::string log;           // "bonds phone restart " in the order they happened
    uint32_t clock = 0;
    uint32_t bootUntil = 0;    // BOOT stays down until this time
    uint32_t restartedAt = 0;
    bool bondsOk = true;
    bool phoneOk = true;

    bool eraseBonds() override {
        log += "bonds ";
        return bondsOk;
    }
    bool erasePhone() override {
        log += "phone ";
        return phoneOk;
    }
    bool bootHeld() override { return clock < bootUntil; }
    uint32_t now() override { return clock; }
    void sleep(uint32_t ms) override { clock += ms; }
    void restart() override {
        log += "restart ";
        restartedAt = clock;
    }
};

// A forget, from BOOT or from the serial `forget`, erases the bonds and the phone with its Token
// now, before the restart: the next boot doesn't have to finish the job.
void test_forgetting_erases_the_bonds_and_the_phone_then_restarts() {
    FakeDongle d;
    dongle_trust::forget(d);
    TEST_ASSERT_EQUAL_STRING("bonds phone restart ", d.log.c_str());
}

// BOOT is GPIO0, a strapping pin: the restart waits until it's let go, so the dongle boots its
// firmware and not the ROM's download mode. The bonds are gone already while it waits.
void test_the_restart_waits_for_boot_to_be_let_go() {
    FakeDongle d;
    d.bootUntil = 3000;
    dongle_trust::forget(d);
    TEST_ASSERT_EQUAL_STRING("bonds phone restart ", d.log.c_str());
    TEST_ASSERT_TRUE(d.restartedAt >= 3000);
    TEST_ASSERT_TRUE(d.restartedAt < 3100);
}

// A button that never comes up (stuck, or a board that reads GPIO0 low) doesn't hang the dongle:
// it restarts 30 s after the forget anyway.
void test_a_stuck_boot_button_still_restarts_after_30_s() {
    FakeDongle d;
    d.clock = 1000;
    d.bootUntil = 0xFFFFFFFFu;
    dongle_trust::forget(d);
    TEST_ASSERT_EQUAL_STRING("bonds phone restart ", d.log.c_str());
    TEST_ASSERT_TRUE(d.restartedAt >= 31000);
    TEST_ASSERT_TRUE(d.restartedAt < 31100);
}

// If the phone can't be erased, the dongle doesn't restart as if it had been: forget() says so,
// and the log can tell the user to try again. A bond that can't be erased is the same.
void test_a_forget_that_couldnt_erase_doesnt_restart() {
    FakeDongle phone;
    phone.phoneOk = false;
    TEST_ASSERT_FALSE(dongle_trust::forget(phone));
    TEST_ASSERT_EQUAL_STRING("bonds phone ", phone.log.c_str());

    FakeDongle bonds;
    bonds.bondsOk = false;
    TEST_ASSERT_FALSE(dongle_trust::forget(bonds));
    TEST_ASSERT_EQUAL_STRING("bonds phone ", bonds.log.c_str());  // still erases what it can
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_forgetting_erases_the_bonds_and_the_phone_then_restarts);
    RUN_TEST(test_a_forget_that_couldnt_erase_doesnt_restart);
    RUN_TEST(test_the_restart_waits_for_boot_to_be_let_go);
    RUN_TEST(test_a_stuck_boot_button_still_restarts_after_30_s);
    RUN_TEST(test_holding_boot_five_seconds_fires_once);
    RUN_TEST(test_a_shorter_press_does_nothing_and_starts_over);
    RUN_TEST(test_the_hold_times_right_across_the_millis_wrap);
    RUN_TEST(test_the_token_reads_as_hex_only_by_the_phone_in_its_window);
    RUN_TEST(test_only_a_dongle_with_no_phone_takes_a_new_one);
    return UNITY_END();
}
