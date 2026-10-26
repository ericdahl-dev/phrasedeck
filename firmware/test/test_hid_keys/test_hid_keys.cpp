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

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_letters_and_digits);
    RUN_TEST(test_punctuation);
    RUN_TEST(test_newlines_tab_and_enter);
    RUN_TEST(test_what_a_us_keyboard_cant_type_refuses_the_send);
    RUN_TEST(test_pacing);
    return UNITY_END();
}
