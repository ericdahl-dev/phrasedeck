#include <unity.h>

#include <string>
#include <vector>

#include "phrase_link.h"

void setUp() {}
void tearDown() {}

using phrase_link::Event;
using phrase_link::Receiver;
using phrase_link::Token;

static const Token kToken = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

static Event feed(Receiver& r, const std::vector<uint8_t>& frame, const Token& token = kToken) {
    return r.feed(frame.data(), frame.size(), token);
}

// The smallest Send, the one to type by hand in nRF Connect: one Begin write with the Token and the
// whole Phrase inline.
void test_one_begin_write_with_the_phrase_inline_is_ready() {
    Receiver r;
    std::vector<uint8_t> begin = {0x01, 7, 0x00, 2, 0};
    begin.insert(begin.end(), kToken.begin(), kToken.end());
    begin.push_back('h');
    begin.push_back('i');
    const Event e = feed(r, begin);
    TEST_ASSERT_EQUAL(int(Event::Kind::Ready), int(e.kind));
    TEST_ASSERT_EQUAL_UINT8(7, e.id);
    TEST_ASSERT_FALSE(e.enter);
    TEST_ASSERT_EQUAL_STRING("hi", r.text().c_str());
}

// A long Phrase goes as a Begin and then Data writes, each no bigger than the link's MTU allows
// (ATT_MTU - 3; iOS gives 182 bytes). It's Ready only once the last byte is in.
void test_a_long_phrase_is_chunked_to_the_mtu_and_ready_after_the_last_write() {
    std::string phrase;
    for (int i = 0; phrase.size() < 2000; i++) phrase += "line " + std::to_string(i) + " of a long Phrase\n";
    phrase.resize(2000);
    const auto frames = phrase_link::frames(42, phrase, true, kToken, 182);
    TEST_ASSERT_TRUE(frames.size() > 10);
    Receiver r;
    for (size_t i = 0; i < frames.size(); i++) {
        TEST_ASSERT_TRUE(frames[i].size() <= 182);
        const Event e = feed(r, frames[i]);
        if (i + 1 < frames.size()) {
            TEST_ASSERT_EQUAL(int(Event::Kind::None), int(e.kind));
            continue;
        }
        TEST_ASSERT_EQUAL(int(Event::Kind::Ready), int(e.kind));
        TEST_ASSERT_EQUAL_UINT8(42, e.id);
        TEST_ASSERT_TRUE(e.enter);
    }
    TEST_ASSERT_TRUE(phrase == r.text());
}

static void expectDone(const Event& e, uint8_t id, phrase_link::Code code) {
    TEST_ASSERT_EQUAL(int(Event::Kind::Done), int(e.kind));
    TEST_ASSERT_EQUAL_UINT8(id, e.id);
    TEST_ASSERT_EQUAL(int(code), int(e.code));
}

// The Token is checked on the Begin, before anything is kept: a phone that bonded but doesn't hold
// the Token gets Bad token, and its Data writes go nowhere.
void test_a_wrong_token_ends_the_send_at_its_begin() {
    Receiver r;
    Token wrong = kToken;
    wrong[15] ^= 1;
    auto f = phrase_link::frames(3, std::string(300, 'x'), false, wrong, 182);
    expectDone(feed(r, f[0]), 3, phrase_link::Code::BadToken);
    expectDone(feed(r, f[1]), 3, phrase_link::Code::BadFrame);
    TEST_ASSERT_TRUE(r.text().empty());
}

// A write the dongle can't place ends the Send it names, Not typed: an unknown op, a short Begin,
// Data with no Begin or for another Send, and more bytes than the Begin promised.
void test_a_write_that_doesnt_fit_ends_the_send() {
    Receiver r;
    expectDone(feed(r, {0x7F, 9}), 9, phrase_link::Code::BadFrame);
    expectDone(feed(r, {0x01, 9, 0, 1}), 9, phrase_link::Code::BadFrame);
    expectDone(feed(r, {0x02, 9, 'a'}), 9, phrase_link::Code::BadFrame);
    TEST_ASSERT_EQUAL(int(Event::Kind::None), int(r.feed(nullptr, 0, kToken).kind));  // nothing to name

    auto f = phrase_link::frames(4, std::string(300, 'x'), false, kToken, 182);
    TEST_ASSERT_EQUAL(int(Event::Kind::None), int(feed(r, f[0]).kind));
    expectDone(feed(r, {0x02, 5, 'a'}), 5, phrase_link::Code::BadFrame);  // another Send's Data
    f[1].push_back('!');
    expectDone(feed(r, f[1]), 4, phrase_link::Code::BadFrame);  // one byte too many
}

// A Phrase longer than the dongle keeps is refused at its Begin, before it's sent.
void test_a_phrase_over_the_limit_is_refused_at_its_begin() {
    Receiver r;
    const auto f = phrase_link::frames(6, std::string(phrase_link::kMaxPhrase + 1, 'x'), false, kToken, 182);
    expectDone(feed(r, f[0]), 6, phrase_link::Code::TooLong);
    const auto ok = phrase_link::frames(7, std::string(phrase_link::kMaxPhrase, 'x'), false, kToken, 512);
    Event e;
    for (const auto& frame : ok) e = feed(r, frame);
    TEST_ASSERT_EQUAL(int(Event::Kind::Ready), int(e.kind));
}

// Stop names a Send. One still coming in ends there, Not typed, with nothing kept. Any other id
// is passed on as a Stop: the dongle cancels it if it's the Send typing now.
void test_stop_ends_a_send_still_coming_in_or_is_passed_on() {
    Receiver r;
    auto f = phrase_link::frames(8, std::string(300, 'x'), false, kToken, 182);
    feed(r, f[0]);
    expectDone(feed(r, {0x03, 8}), 8, phrase_link::Code::NotTyped);
    TEST_ASSERT_TRUE(r.text().empty());
    expectDone(feed(r, f[1]), 8, phrase_link::Code::BadFrame);  // its Data has nowhere to go

    const Event e = feed(r, {0x03, 9});
    TEST_ASSERT_EQUAL(int(Event::Kind::Stop), int(e.kind));
    TEST_ASSERT_EQUAL_UINT8(9, e.id);
}

// The Result notify: the Send's id, its code, and for Refused the character a US keyboard can't
// type, in UTF-8.
void test_the_result_names_the_send_its_code_and_the_refused_character() {
    const auto typed = phrase_link::result(5, phrase_link::Code::Typed, "");
    TEST_ASSERT_EQUAL(2, typed.size());
    TEST_ASSERT_EQUAL_HEX8(5, typed[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, typed[1]);
    const auto refused = phrase_link::result(6, phrase_link::Code::Refused, "\xc3\xa9");
    const std::vector<uint8_t> want = {6, 0x03, 0xC3, 0xA9};
    TEST_ASSERT_TRUE(want == refused);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_one_begin_write_with_the_phrase_inline_is_ready);
    RUN_TEST(test_a_long_phrase_is_chunked_to_the_mtu_and_ready_after_the_last_write);
    RUN_TEST(test_a_wrong_token_ends_the_send_at_its_begin);
    RUN_TEST(test_a_write_that_doesnt_fit_ends_the_send);
    RUN_TEST(test_a_phrase_over_the_limit_is_refused_at_its_begin);
    RUN_TEST(test_stop_ends_a_send_still_coming_in_or_is_passed_on);
    RUN_TEST(test_the_result_names_the_send_its_code_and_the_refused_character);
    return UNITY_END();
}
