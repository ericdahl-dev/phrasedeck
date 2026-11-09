#include <unity.h>

#include <atomic>
#include <string>
#include <vector>

#include "usb_typing.h"

void setUp() {}
void tearDown() {}

using phrase_link::Code;

// The Computer's end of the USB cable: one report per 1 ms frame, as a full-speed interrupt
// endpoint with bInterval 1 polls. report() blocks until the host has taken it, like
// USBHID::SendReport, so the clock moves 1 ms per report.
struct FakeUsb : send::Keyboard {
    uint32_t clock = 0;
    bool mounted = true;
    unsigned unplugs = 0;
    int unplugAfter = -1;  // reports taken before the cable comes out
    int stopAfter = -1;    // reports taken before the phone writes Stop
    std::atomic<bool>* stop = nullptr;
    std::vector<uint32_t> at;  // when each report was taken
    std::vector<hid_keys::Key> reports;

    bool ready() override { return mounted; }
    bool report(uint8_t mod, uint8_t code) override {
        if (!mounted) return false;
        clock += 1;
        at.push_back(clock);
        reports.push_back({mod, code});
        if (int(reports.size()) == stopAfter) stop->store(true);
        if (int(reports.size()) == unplugAfter) {
            mounted = false;
            unplugs++;
        }
        return true;
    }
    unsigned disconnects() override { return unplugs; }
    uint32_t now() override { return clock; }
    // As on the dongle: a report already waited for its frame, so no tick; otherwise one 1 ms tick.
    size_t seen = 0;
    void yield() override {
        if (reports.size() == seen) clock += 1;
        seen = reports.size();
    }
    void sleep(uint32_t ms) override { clock += ms; }
};

static usb_typing::Outcome type(FakeUsb& usb, const std::string& text, bool enter, uint32_t intervalMs,
                                send::KeyStats* stats = nullptr) {
    std::atomic<bool> stop{false};
    usb.stop = &stop;
    return usb_typing::type(text, enter, usb, stop, intervalMs, stats);
}

// A Phrase types as US keyboard reports and ends Typed, with Send Enter's Enter last.
void test_a_phrase_types_and_is_typed() {
    FakeUsb usb;
    const auto out = type(usb, "Hi", true, 0);
    TEST_ASSERT_EQUAL(int(Code::Typed), int(out.code));
    // H (Shift), release (the Shift changes), i, Enter (it rolls over: its press releases i, #174),
    // release.
    TEST_ASSERT_EQUAL(5, usb.reports.size());
    TEST_ASSERT_EQUAL_HEX8(0x0B, usb.reports[0].code);
    TEST_ASSERT_EQUAL_HEX8(0x28, usb.reports[usb.reports.size() - 2].code);
    TEST_ASSERT_EQUAL_HEX8(0x00, usb.reports.back().code);
}

// "é" has no key on a US keyboard: the whole Send is Refused, naming it, before any key goes.
void test_a_character_a_us_keyboard_cant_type_is_refused_whole() {
    FakeUsb usb;
    const auto out = type(usb, "caf\xc3\xa9", false, 0);
    TEST_ASSERT_EQUAL(int(Code::Refused), int(out.code));
    TEST_ASSERT_EQUAL_STRING("\xc3\xa9", out.bad.c_str());
    TEST_ASSERT_TRUE(usb.reports.empty());
}

// No Computer on the cable (or it's asleep): nothing is typed.
void test_no_computer_on_usb_is_not_typed() {
    FakeUsb usb;
    usb.mounted = false;
    TEST_ASSERT_EQUAL(int(Code::NotTyped), int(type(usb, "abc", false, 0).code));
}

// A Stop partway ends with the held key released, Maybe typed: the start of the Phrase is on the
// Computer. Pulling the cable partway is Maybe typed too.
void test_stop_or_unplug_partway_is_maybe_typed() {
    FakeUsb stopped;
    stopped.stopAfter = 3;
    TEST_ASSERT_EQUAL(int(Code::MaybeTyped), int(type(stopped, "abcdef", false, 0).code));
    TEST_ASSERT_TRUE(stopped.reports.size() < 7);
    TEST_ASSERT_EQUAL_HEX8(0x00, stopped.reports.back().code);

    FakeUsb unplugged;
    unplugged.unplugAfter = 2;
    TEST_ASSERT_EQUAL(int(Code::MaybeTyped), int(type(unplugged, "abcdef", false, 0).code));
}

// The key interval spaces the reports: at 0 they go one per USB frame, back to back; at 3 ms no
// two are closer than 3 ms.
void test_the_key_interval_spaces_reports() {
    FakeUsb fast;
    send::KeyStats stats;
    type(fast, "abcdefgh", false, 0, &stats);
    TEST_ASSERT_EQUAL(9, fast.reports.size());  // rollover: one press each, one release at the end
    TEST_ASSERT_EQUAL_UINT32(9, stats.tookMs);
    TEST_ASSERT_EQUAL(9, stats.reports);
    TEST_ASSERT_EQUAL(8, stats.keys);

    FakeUsb paced;
    type(paced, "abcdefgh", false, 3);
    for (size_t i = 1; i < paced.at.size(); i++) TEST_ASSERT_TRUE(paced.at[i] - paced.at[i - 1] >= 3);
}

// The speed log line, for a 2000-character Phrase.
void test_the_speed_line_gives_characters_per_second() {
    send::KeyStats stats{2050, 2000, 2100};
    TEST_ASSERT_EQUAL_STRING(
        "speed: 2000 chars, 2050 reports in 2100 ms: 952 chars/s, 0.98 reports/ms, key interval 0 ms",
        usb_typing::speedLine(stats, 0).c_str());
    send::KeyStats none{0, 0, 0};
    TEST_ASSERT_EQUAL_STRING("speed: 0 chars, 0 reports in 0 ms: 0 chars/s, 0.00 reports/ms, key interval 2 ms",
                             usb_typing::speedLine(none, 2).c_str());
}

// The speed test's Phrase: exactly n characters, all typable, in numbered lines so a dropped or
// doubled key is easy to find (each line is 64 characters with its newline).
void test_the_speed_phrase_is_n_typable_characters_in_numbered_lines() {
    const std::string p = usb_typing::speedPhrase(2000);
    TEST_ASSERT_EQUAL(2000, p.size());
    TEST_ASSERT_TRUE(hid_keys::plan(p, false).ok);
    TEST_ASSERT_EQUAL_STRING("0001 ", p.substr(0, 5).c_str());
    TEST_ASSERT_EQUAL('\n', p[63]);
    TEST_ASSERT_EQUAL_STRING("0002 ", p.substr(64, 5).c_str());
    TEST_ASSERT_EQUAL_STRING(p.substr(5, 58).c_str(), p.substr(69, 58).c_str());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_phrase_types_and_is_typed);
    RUN_TEST(test_a_character_a_us_keyboard_cant_type_is_refused_whole);
    RUN_TEST(test_no_computer_on_usb_is_not_typed);
    RUN_TEST(test_stop_or_unplug_partway_is_maybe_typed);
    RUN_TEST(test_the_key_interval_spaces_reports);
    RUN_TEST(test_the_speed_line_gives_characters_per_second);
    RUN_TEST(test_the_speed_phrase_is_n_typable_characters_in_numbered_lines);
    return UNITY_END();
}
