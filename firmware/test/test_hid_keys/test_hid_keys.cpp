#include <unity.h>

#include <string>
#include <vector>

#include "hid_keys.h"

void setUp() {}
void tearDown() {}

using hid_keys::Key;
using hid_keys::kShift;

static void expectKeys(const std::vector<Key>& want, const hid_keys::Plan& got) {
    TEST_ASSERT_TRUE(got.ok);
    TEST_ASSERT_EQUAL(want.size(), got.keys.size());
    for (size_t i = 0; i < want.size(); i++) {
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(want[i].mod, got.keys[i].mod, "modifier");
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(want[i].code, got.keys[i].code, "usage");
    }
}

// Letters and digits are the HID usages a US keyboard sends; capitals hold Shift.
void test_letters_and_digits() {
    expectKeys({{0, 0x04}, {kShift, 0x1D}, {0, 0x1E}, {0, 0x27}, {0, 0x2C}}, hid_keys::plan("aZ10 ", false));
}

// Every printable ASCII character has a key, and the shifted symbols hold Shift on the key they
// share (US layout).
void test_punctuation() {
    expectKeys({{0, 0x2D}, {kShift, 0x2D}, {0, 0x2F}, {kShift, 0x30}, {kShift, 0x1E}, {kShift, 0x27},
                {0, 0x34}, {kShift, 0x34}, {0, 0x35}, {kShift, 0x35}, {0, 0x31}, {kShift, 0x31},
                {0, 0x38}, {kShift, 0x38}, {kShift, 0x33}, {kShift, 0x37}},
               hid_keys::plan("-_[}!)'\"`~\\|/?:>", false));
    for (char c = 0x20; c < 0x7F; c++) {
        TEST_ASSERT_TRUE_MESSAGE(hid_keys::plan(std::string(1, c), false).ok, std::string(1, c).c_str());
    }
}

// A newline inside a Phrase is Shift+Enter, as the Helper's type mode does, so a chat box doesn't
// send early; "\r\n" counts once. With enter, one plain Enter goes after the text.
void test_newlines_tab_and_enter() {
    const Key shiftEnter{kShift, 0x28}, enter{0, 0x28};
    expectKeys({{0, 0x04}, shiftEnter, {0, 0x05}, shiftEnter, {0, 0x2B}}, hid_keys::plan("a\nb\r\n\t", false));
    expectKeys({{0, 0x04}, enter}, hid_keys::plan("a", true));
}

// A US keyboard can't type "ï" or a control character, and typing a Phrase with them left
// out could run a different command, so the whole Send is refused and the panel names the first
// one: the full UTF-8 character, or U+00XX for a control.
void test_what_a_us_keyboard_cant_type_refuses_the_send() {
    hid_keys::Plan p = hid_keys::plan("na\xc3\xafve caf\xc3\xa9", false);
    TEST_ASSERT_FALSE(p.ok);
    TEST_ASSERT_TRUE(p.keys.empty());
    TEST_ASSERT_EQUAL_STRING("\xc3\xaf", p.bad.c_str());

    TEST_ASSERT_EQUAL_STRING("\xe2\x9c\x93", hid_keys::plan("ok \xe2\x9c\x93", false).bad.c_str());
    TEST_ASSERT_EQUAL_STRING("U+001B", hid_keys::plan("a\x1b[2J", false).bad.c_str());
    TEST_ASSERT_EQUAL_STRING("U+007F", hid_keys::plan("\x7f", false).bad.c_str());
}

// Reports go out no faster than one per interval, or a host can drop keys. The first is due at
// once, and millis() wrapping past 2^32 doesn't stall it.
void test_pacing() {
    hid_keys::Pacer pace(10);
    TEST_ASSERT_TRUE(pace.due(1000));
    TEST_ASSERT_FALSE(pace.due(1005));
    TEST_ASSERT_FALSE(pace.due(1009));
    TEST_ASSERT_TRUE(pace.due(1010));
    TEST_ASSERT_FALSE(pace.due(1010));

    hid_keys::Pacer wrap(10);
    TEST_ASSERT_TRUE(wrap.due(0xFFFFFFFA));
    TEST_ASSERT_FALSE(wrap.due(0xFFFFFFFF));
    TEST_ASSERT_TRUE(wrap.due(4));  // 10 ms later, across the wrap
}

// The whole US table, from the keyboard's point of view: each usage from 0x04 with the character
// it types unshifted, then shifted (0x28-0x2C are Enter, Escape, Backspace, Tab and Space).
void test_every_printable_character_types_its_us_key() {
    static const char* const keys[] = {
        "aA", "bB", "cC", "dD", "eE", "fF", "gG", "hH", "iI", "jJ", "kK", "lL", "mM", "nN", "oO",
        "pP", "qQ", "rR", "sS", "tT", "uU", "vV", "wW", "xX", "yY", "zZ", "1!", "2@", "3#", "4$",
        "5%", "6^", "7&", "8*", "9(", "0)", "", "", "", "", "  ", "-_", "=+", "[{", "]}", "\\|",
        "", ";:", "'\"", "`~", ",<", ".>", "/?"};
    size_t seen = 0;
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        const uint8_t code = uint8_t(0x04 + i);
        const std::string k = keys[i];
        for (size_t shift = 0; shift < k.size(); shift++) {
            if (code == 0x2C && shift == 1) break;  // Space has no shifted character
            hid_keys::Plan p = hid_keys::plan(k.substr(shift, 1), false);
            TEST_ASSERT_TRUE_MESSAGE(p.ok, k.c_str());
            TEST_ASSERT_EQUAL_HEX8_MESSAGE(code, p.keys[0].code, k.substr(shift, 1).c_str());
            TEST_ASSERT_EQUAL_HEX8_MESSAGE(shift ? kShift : 0, p.keys[0].mod, k.substr(shift, 1).c_str());
            seen++;
        }
    }
    TEST_ASSERT_EQUAL(95, seen);  // every character from 0x20 to 0x7E
}

// A Phrase with broken UTF-8 still names something the panel can draw: the byte, as 0xNN.
void test_broken_utf8_is_named_by_its_byte() {
    TEST_ASSERT_EQUAL_STRING("0xC3", hid_keys::plan("ab\xc3", false).bad.c_str());
    TEST_ASSERT_EQUAL_STRING("0xE2", hid_keys::plan("\xe2\x9c", false).bad.c_str());
    TEST_ASSERT_EQUAL_STRING("0x80", hid_keys::plan("\x80", false).bad.c_str());
    TEST_ASSERT_EQUAL_STRING("\xc3\xa9", hid_keys::plan("\xc3\xa9\xa9", false).bad.c_str());
}

// Records each report the Sender hands to the BLE stack; fail makes the next n of them fail.
struct Stack {
    std::vector<Key> sent;
    int fail = 0;
    bool notify(const Key& k) {
        if (fail > 0) {
            fail--;
            return false;
        }
        sent.push_back(k);
        return true;
    }
};

static const Key kRelease{0, 0};

// Steps the Sender every ms from 0 to `until`, as the BLE task's loop would.
static void run(hid_keys::Sender& s, Stack& stack, uint32_t until, uint32_t from = 0) {
    for (uint32_t t = from; t <= until; t++) s.step(t, [&](const Key& k) { return stack.notify(k); });
}

void test_each_key_is_a_press_then_a_release_one_report_per_interval() {
    hid_keys::Sender s(hid_keys::plan("aa", false).keys, 20);
    Stack stack;

    run(s, stack, 59);
    TEST_ASSERT_EQUAL(3, stack.sent.size());  // at 0, 20 and 40 ms
    TEST_ASSERT_FALSE(s.done());

    run(s, stack, 60, 60);
    const std::vector<Key> want{{0, 0x04}, kRelease, {0, 0x04}, kRelease};
    TEST_ASSERT_EQUAL(want.size(), stack.sent.size());
    for (size_t i = 0; i < want.size(); i++) TEST_ASSERT_TRUE(want[i] == stack.sent[i]);
    TEST_ASSERT_TRUE(s.done());
    TEST_ASSERT_EQUAL(hid_keys::Sender::Result::Sent, s.result());
}

// ENOMEM from the stack: the same report goes again next interval, so no key is lost or left down.
void test_a_report_the_stack_refuses_is_sent_again() {
    hid_keys::Sender s(hid_keys::plan("b", false).keys, 20);
    Stack stack;
    stack.fail = 2;

    run(s, stack, 100);
    const std::vector<Key> want{{0, 0x05}, kRelease};
    TEST_ASSERT_EQUAL(want.size(), stack.sent.size());
    for (size_t i = 0; i < want.size(); i++) TEST_ASSERT_TRUE(want[i] == stack.sent[i]);
    TEST_ASSERT_TRUE(s.done());
}

// Cancel with a key down: its release still goes, so the Computer isn't left with a held key.
void test_cancel_releases_a_held_key_then_stops() {
    hid_keys::Sender s(hid_keys::plan("abc", false).keys, 20);
    Stack stack;
    run(s, stack, 40);  // press a, release, press b (at 0, 20, 40 ms): b is down
    TEST_ASSERT_EQUAL(3, stack.sent.size());

    s.cancel();
    run(s, stack, 200, 41);
    TEST_ASSERT_EQUAL(4, stack.sent.size());
    TEST_ASSERT_TRUE(kRelease == stack.sent.back());
    TEST_ASSERT_TRUE(s.done());
    TEST_ASSERT_EQUAL(hid_keys::Sender::Result::Stopped, s.result());
}

// Cancel before anything went: nothing was typed.
void test_cancel_before_the_first_key_sends_nothing() {
    hid_keys::Sender s(hid_keys::plan("abc", false).keys, 20);
    Stack stack;
    s.cancel();
    run(s, stack, 200);
    TEST_ASSERT_EQUAL(0, stack.sent.size());
    TEST_ASSERT_TRUE(s.done());
    TEST_ASSERT_EQUAL(hid_keys::Sender::Result::NotSent, s.result());
}

// The panel can't see what landed over Bluetooth, so it never says Typed (#98 UI review).
void test_what_the_status_line_says_after_a_bluetooth_send() {
    using R = hid_keys::Sender::Result;
    hid_keys::Said said = hid_keys::said(R::Sent);
    TEST_ASSERT_EQUAL_STRING("Sent as keystrokes", said.message.c_str());
    TEST_ASSERT_TRUE(said.ok);

    said = hid_keys::said(R::Stopped);
    TEST_ASSERT_EQUAL_STRING("Stopped partway: some keystrokes were sent", said.message.c_str());
    TEST_ASSERT_FALSE(said.ok);

    said = hid_keys::said(R::NotSent);
    TEST_ASSERT_EQUAL_STRING("Not sent", said.message.c_str());
    TEST_ASSERT_FALSE(said.ok);

    said = hid_keys::refused("\xc3\xa9");
    TEST_ASSERT_EQUAL_STRING("Not sent: a US keyboard can't type \xc3\xa9", said.message.c_str());
    TEST_ASSERT_FALSE(said.ok);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_letters_and_digits);
    RUN_TEST(test_punctuation);
    RUN_TEST(test_newlines_tab_and_enter);
    RUN_TEST(test_what_a_us_keyboard_cant_type_refuses_the_send);
    RUN_TEST(test_pacing);
    RUN_TEST(test_every_printable_character_types_its_us_key);
    RUN_TEST(test_broken_utf8_is_named_by_its_byte);
    RUN_TEST(test_each_key_is_a_press_then_a_release_one_report_per_interval);
    RUN_TEST(test_a_report_the_stack_refuses_is_sent_again);
    RUN_TEST(test_cancel_releases_a_held_key_then_stops);
    RUN_TEST(test_cancel_before_the_first_key_sends_nothing);
    RUN_TEST(test_what_the_status_line_says_after_a_bluetooth_send);
    return UNITY_END();
}
