#include <unity.h>

#include <cstdint>
#include <string>
#include <vector>

#include "phone_link.h"

void setUp() {}
void tearDown() {}

using phone_link::Assembler;
using phone_link::Fed;
using phone_link::Token;

namespace {
const Token kToken = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                      0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};

Fed feedAll(Assembler& a, const std::vector<std::vector<uint8_t>>& frames) {
    Fed last = Fed::More;
    for (const auto& f : frames) last = a.feed(f.data(), f.size());
    return last;
}
}  // namespace

// A short Phrase fits one GATT write: the panel gets the text, the Send Enter flag and the Send's id.
void test_a_short_phrase_arrives_in_one_write() {
    const auto frames = phone_link::frames(7, kToken, "Ship it", true, 180);
    TEST_ASSERT_EQUAL(1, frames.size());
    Assembler a;
    TEST_ASSERT_EQUAL(Fed::Done, feedAll(a, frames));
    TEST_ASSERT_EQUAL_STRING("Ship it", a.text().c_str());
    TEST_ASSERT_TRUE(a.enter());
    TEST_ASSERT_EQUAL(7, a.id());
    TEST_ASSERT_TRUE(a.token() == kToken);
}

// A long Phrase at the smallest ATT MTU (23, so 20-byte writes): the Begin carries the Token and a
// little text, every write after it carries 16 more bytes, and the panel gets the whole text back.
void test_a_long_phrase_is_chunked_across_writes_and_put_back_together() {
    std::string text;
    for (int i = 0; text.size() < phone_link::kMaxText; ++i) text += static_cast<char>('a' + i % 26);
    text.replace(10, 4, "\xC3\xA9\xE2\x82");  // UTF-8 split across writes is fine: it's bytes
    const auto frames = phone_link::frames(1, kToken, text, false, 20);
    TEST_ASSERT_TRUE(frames.size() > 100);
    for (const auto& f : frames) TEST_ASSERT_TRUE(f.size() <= 20);
    Assembler a;
    for (size_t i = 0; i + 1 < frames.size(); ++i) TEST_ASSERT_EQUAL(Fed::More, a.feed(frames[i].data(), frames[i].size()));
    TEST_ASSERT_EQUAL(Fed::Done, a.feed(frames.back().data(), frames.back().size()));
    TEST_ASSERT_TRUE(a.text() == text);
    TEST_ASSERT_FALSE(a.enter());
}

// An empty Phrase is one Begin with no text: it's Done at once (and typing it does nothing).
void test_an_empty_phrase_is_one_begin() {
    const auto frames = phone_link::frames(2, kToken, "", false, 20);
    TEST_ASSERT_EQUAL(1, frames.size());
    TEST_ASSERT_EQUAL(phone_link::kBeginHeader, frames[0].size());
    Assembler a;
    TEST_ASSERT_EQUAL(Fed::Done, feedAll(a, frames));
}

// A lost, repeated or foreign write must never splice two Sends into one Phrase: it's Bad, and the
// rest of that Send is Bad too, until a new Begin.
void test_a_lost_or_repeated_write_drops_the_send() {
    const std::string text(100, 'x');
    const auto frames = phone_link::frames(3, kToken, text, false, 20);
    Assembler lost;
    lost.feed(frames[0].data(), frames[0].size());
    TEST_ASSERT_EQUAL(Fed::Bad, lost.feed(frames[2].data(), frames[2].size()));
    TEST_ASSERT_EQUAL(Fed::Bad, lost.feed(frames[3].data(), frames[3].size()));

    Assembler repeated;
    repeated.feed(frames[0].data(), frames[0].size());
    repeated.feed(frames[1].data(), frames[1].size());
    TEST_ASSERT_EQUAL(Fed::Bad, repeated.feed(frames[1].data(), frames[1].size()));

    // Another Send's More, with the right offset but another id.
    const auto other = phone_link::frames(4, kToken, text, false, 20);
    Assembler foreign;
    foreign.feed(frames[0].data(), frames[0].size());
    TEST_ASSERT_EQUAL(Fed::Bad, foreign.feed(other[1].data(), other[1].size()));

    // A More with no Begin.
    Assembler none;
    TEST_ASSERT_EQUAL(Fed::Bad, none.feed(frames[1].data(), frames[1].size()));
}

// A new Begin replaces a Send that never finished: the app gave up on it and starts again.
void test_a_new_begin_replaces_an_unfinished_send() {
    const auto first = phone_link::frames(5, kToken, std::string(100, 'x'), false, 20);
    const auto second = phone_link::frames(6, kToken, "ok", false, 20);
    Assembler a;
    a.feed(first[0].data(), first[0].size());
    TEST_ASSERT_EQUAL(Fed::Done, feedAll(a, second));
    TEST_ASSERT_EQUAL_STRING("ok", a.text().c_str());
    TEST_ASSERT_EQUAL(6, a.id());
}

// Writes the panel can't take: too short for a header, an unknown op, a length past
// the Phrase limit, and more text than the Begin promised.
void test_malformed_writes_are_bad() {
    Assembler a;
    const uint8_t empty[1] = {0};
    TEST_ASSERT_EQUAL(Fed::Bad, a.feed(empty, 0));
    TEST_ASSERT_EQUAL(Fed::Bad, a.feed(nullptr, 5));
    const uint8_t unknown[] = {0x7f, 1, 0, 0};
    TEST_ASSERT_EQUAL(Fed::Bad, a.feed(unknown, sizeof unknown));

    auto shortBegin = phone_link::frames(1, kToken, "hi", false, 64)[0];
    shortBegin.resize(phone_link::kBeginHeader - 1);
    TEST_ASSERT_EQUAL(Fed::Bad, a.feed(shortBegin.data(), shortBegin.size()));

    auto tooLong = phone_link::frames(1, kToken, "hi", false, 64)[0];
    const size_t over = phone_link::kMaxText + 1;
    tooLong[2] = static_cast<uint8_t>(over);
    tooLong[3] = static_cast<uint8_t>(over >> 8);
    TEST_ASSERT_EQUAL(Fed::Bad, a.feed(tooLong.data(), tooLong.size()));

    auto extra = phone_link::frames(1, kToken, "hi", false, 64)[0];
    extra.push_back('!');
    TEST_ASSERT_EQUAL(Fed::Bad, a.feed(extra.data(), extra.size()));

    TEST_ASSERT_TRUE(phone_link::frames(1, kToken, std::string(phone_link::kMaxText + 1, 'x'), false, 64).empty());
    TEST_ASSERT_TRUE(phone_link::frames(1, kToken, "hi", false, phone_link::kBeginHeader - 1).empty());
}

using phone_link::Link;
using phone_link::Verdict;

namespace {
// The bonded phone, reconnected: encrypted with its stored key, MITM-protected, 128-bit key.
Link bondedPhone() {
    Link l;
    l.phoneIdentity = true;
    l.encrypted = l.authenticated = l.bonded = true;
    l.keySize = 16;
    l.storedPhone = true;
    return l;
}
}  // namespace

// Only the bonded phone with the right Token may Send. Each missing piece is Not trusted, and nothing
// about the Token is looked at: a stranger learns nothing by guessing.
void test_only_the_bonded_phone_with_the_token_may_send() {
    TEST_ASSERT_EQUAL(Verdict::Send, phone_link::check(bondedPhone(), kToken, kToken));
    Link l = bondedPhone();
    l.phoneIdentity = false;  // came in on the Computer's identity (the HID one)
    TEST_ASSERT_EQUAL(Verdict::NotTrusted, phone_link::check(l, kToken, kToken));
    l = bondedPhone();
    l.encrypted = false;
    TEST_ASSERT_EQUAL(Verdict::NotTrusted, phone_link::check(l, kToken, kToken));
    l = bondedPhone();
    l.authenticated = false;  // Just Works: no passkey, so no MITM protection
    TEST_ASSERT_EQUAL(Verdict::NotTrusted, phone_link::check(l, kToken, kToken));
    l = bondedPhone();
    l.bonded = false;
    TEST_ASSERT_EQUAL(Verdict::NotTrusted, phone_link::check(l, kToken, kToken));
    l = bondedPhone();
    l.keySize = 7;
    TEST_ASSERT_EQUAL(Verdict::NotTrusted, phone_link::check(l, kToken, kToken));
    l = bondedPhone();
    l.storedPhone = false;  // bonded, but not the phone paired in the window (or its key changed)
    TEST_ASSERT_EQUAL(Verdict::NotTrusted, phone_link::check(l, kToken, kToken));
}

// The bond alone isn't enough: the Token from pairing must come with every Send. Any byte wrong is
// Wrong token (the panel ends the link), and so is no Token issued yet (all zeros never matches).
void test_a_wrong_token_is_refused() {
    for (size_t i = 0; i < phone_link::kTokenLen; ++i) {
        Token wrong = kToken;
        wrong[i] ^= 0x01;
        TEST_ASSERT_EQUAL(Verdict::WrongToken, phone_link::check(bondedPhone(), wrong, kToken));
    }
    const Token none{};
    TEST_ASSERT_EQUAL(Verdict::WrongToken, phone_link::check(bondedPhone(), none, none));
}

// The Token goes out once: to the connection that just paired with the passkey, within a minute,
// on its first read. Any other connection, a second read, or a late one gets nothing.
void test_the_token_is_offered_once_to_the_connection_that_paired() {
    phone_link::TokenOffer offer;
    TEST_ASSERT_FALSE(offer.take(1, 0));  // nothing offered
    offer.offer(1, 1000);
    TEST_ASSERT_FALSE(offer.take(2, 1001));  // another connection
    TEST_ASSERT_TRUE(offer.take(1, 1002));
    TEST_ASSERT_FALSE(offer.take(1, 1003));  // only once

    offer.offer(3, 0xFFFFFF00u);  // across millis() wrapping
    TEST_ASSERT_TRUE(offer.take(3, 0x100u));

    offer.offer(4, 5000);
    TEST_ASSERT_FALSE(offer.take(4, 5000 + phone_link::kTokenOfferMs));  // too late

    offer.offer(5, 0);
    offer.drop(5);  // it disconnected: its handle can be reused by a stranger
    TEST_ASSERT_FALSE(offer.take(5, 1));
}

// The Status characteristic notifies the Send's id and outcome, then the words for the app, cut to
// fit one notify. Every refusal before typing is Not typed, so it's safe to tap again.
void test_the_status_notify_carries_the_id_outcome_and_words() {
    const auto s = phone_link::status(9, send::Outcome::MaybeTyped, "Stopped partway", 64);
    TEST_ASSERT_EQUAL(9, s[0]);
    TEST_ASSERT_EQUAL(phone_link::kMaybeTyped, s[1]);
    TEST_ASSERT_EQUAL_STRING("Stopped partway", std::string(s.begin() + 2, s.end()).c_str());
    TEST_ASSERT_EQUAL(phone_link::kTyped, phone_link::status(1, send::Outcome::Typed, "", 20)[1]);
    TEST_ASSERT_EQUAL(phone_link::kNotTyped, phone_link::status(1, send::Outcome::NotTyped, "", 20)[1]);
    TEST_ASSERT_EQUAL(20, phone_link::status(1, send::Outcome::NotTyped, std::string(100, 'x'), 20).size());
    // Cut on a character boundary: "é" is two bytes and doesn't fit after 17 x's.
    const auto cut = phone_link::status(1, send::Outcome::Typed, std::string(17, 'x') + "\xC3\xA9", 20);
    TEST_ASSERT_EQUAL(19, cut.size());
}

using phone_link::Advertise;

// One advertising set (legacy advertising in this build): the Computer's HID identity has it whenever
// the keyboard wants it (no Computer connected, or its pairing window open). Otherwise the phone
// identity advertises, only while no phone is connected and there's a phone to come back or a phone
// window open. The phone link is no use without the Computer, which types its Sends.
void test_the_keyboard_comes_first_then_the_phone_identity_advertises() {
    TEST_ASSERT_EQUAL(Advertise::Keyboard, phone_link::advertise(true, false, true, true));
    TEST_ASSERT_EQUAL(Advertise::Phone, phone_link::advertise(false, false, true, false));
    TEST_ASSERT_EQUAL(Advertise::Phone, phone_link::advertise(false, false, false, true));
    TEST_ASSERT_EQUAL(Advertise::None, phone_link::advertise(false, true, true, true));   // phone here
    TEST_ASSERT_EQUAL(Advertise::None, phone_link::advertise(false, false, false, false)); // no phone yet
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_the_token_is_offered_once_to_the_connection_that_paired);
    RUN_TEST(test_the_status_notify_carries_the_id_outcome_and_words);
    RUN_TEST(test_the_keyboard_comes_first_then_the_phone_identity_advertises);
    RUN_TEST(test_only_the_bonded_phone_with_the_token_may_send);
    RUN_TEST(test_a_wrong_token_is_refused);
    RUN_TEST(test_a_short_phrase_arrives_in_one_write);
    RUN_TEST(test_a_long_phrase_is_chunked_across_writes_and_put_back_together);
    RUN_TEST(test_an_empty_phrase_is_one_begin);
    RUN_TEST(test_a_lost_or_repeated_write_drops_the_send);
    RUN_TEST(test_a_new_begin_replaces_an_unfinished_send);
    RUN_TEST(test_malformed_writes_are_bad);
    return UNITY_END();
}
