#include <unity.h>

#include <atomic>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "send.h"

void setUp() {}
void tearDown() {}

using send::Outcome;

// What a tap's Send means for the user, from /type's status (docs: "What each status means").
void test_status_codes_map_to_typed_not_typed_or_maybe_typed() {
    TEST_ASSERT_EQUAL(Outcome::Typed, send::outcome(true, 200));
    TEST_ASSERT_EQUAL(Outcome::MaybeTyped, send::outcome(true, 500));
    for (int status : {400, 401, 403, 404, 409, 413, 415, 429, 503}) {
        TEST_ASSERT_EQUAL(Outcome::NotTyped, send::outcome(true, status));
    }
}

// Nothing went out (no Wi-Fi, no TLS, or the Pin didn't match): safe to tap again. The request went
// out but no reply came back: the Helper may have typed it.
void test_no_reply_is_not_typed_unless_the_request_was_sent() {
    TEST_ASSERT_EQUAL(Outcome::NotTyped, send::outcome(false, 0));
    TEST_ASSERT_EQUAL(Outcome::MaybeTyped, send::outcome(true, 0));
    // HTTP clients report "no reply" as negative codes (HTTPClient's -11 read timeout); after the
    // request went out that's still Maybe typed, or the user taps again and it types twice.
    TEST_ASSERT_EQUAL(Outcome::MaybeTyped, send::outcome(true, -11));
    TEST_ASSERT_EQUAL(Outcome::NotTyped, send::outcome(false, -1));
}

// /type's body: the text, plus which panel Phrase and edit it is, so the Helper can show its
// edit notice (#98). A Send without a Phrase (the dev serial `type`) carries the text alone.
void test_the_type_body_names_the_phrase_and_its_edit() {
    TEST_ASSERT_EQUAL_STRING("{\"text\":\"Ship it\\n\",\"phrase\":\"a1b2:7\",\"edit\":3}",
                             send::typeBody("Ship it\n", "a1b2:7", 3).c_str());
    TEST_ASSERT_EQUAL_STRING("{\"text\":\"hi\"}", send::typeBody("hi", "", 0).c_str());
}

// Send Enter (#171): the Helper presses Enter after the text only when the Button asks for it. A
// "\n" in the text wouldn't do: the Helper types a line break as Shift+Enter, which doesn't submit.
void test_the_type_body_asks_for_enter_only_when_the_button_does() {
    TEST_ASSERT_EQUAL_STRING("{\"text\":\"Ship it\",\"phrase\":\"a1b2:7\",\"edit\":3,\"enter\":true}",
                             send::typeBody("Ship it", "a1b2:7", 3, true).c_str());
    TEST_ASSERT_EQUAL_STRING("{\"text\":\"Ship it\",\"phrase\":\"a1b2:7\",\"edit\":3}",
                             send::typeBody("Ship it", "a1b2:7", 3, false).c_str());
}

// The Helper route, through a fake transport. Each post is recorded, and the fake shows allow() a
// certificate: the paired Computer's, or a spoofer's.
const std::string kPin(64, 'a');
const std::string kSpoofer(64, 'b');

struct Post {
    std::string address;
    int port;
    std::string json, token;
};

struct FakeHelper : send::HelperTransport {
    std::vector<Post> posts;
    std::vector<std::string> certs;    // the cert each post's handshake shows, in turn; else kPin
    std::vector<send::Reply> replies;  // what each post that passes the check gets; else 200
    send::Reply post(const std::string& address, int port, const std::string& json, const std::string& token,
                     const std::function<bool(const std::string&)>& allow) override {
        const size_t i = posts.size();
        posts.push_back({address, port, json, token});
        send::Reply r;
        if (!allow(i < certs.size() ? certs[i] : kPin)) {
            r.pinMismatch = true;
            return r;
        }
        if (i < replies.size()) return replies[i];
        r.sent = true;
        r.status = 200;
        return r;
    }
};

send::Paired paired() { return {"omarchy", "10.0.0.5", 47812, kPin, "tok"}; }

nearby::Helper seen(const std::string& address, int port, const std::string& fp) {
    nearby::Helper h;
    h.name = "phrased-x";
    h.address = address;
    h.txt.port = port;
    h.txt.fp = fp;
    return h;
}

send::Reply reply(bool sent, int status, const std::string& body = "") {
    send::Reply r;
    r.sent = sent;
    r.status = status;
    r.body = body;
    return r;
}

const send::Phrase kShip{"Ship it", "a1b2:7", 3, true};

// Nothing goes out before the panel is paired, or while it isn't on its Wi-Fi (its own network).
void test_a_helper_send_needs_a_pairing_and_the_wifi() {
    FakeHelper fake;
    send::Result r = send::toHelper(kShip, send::Paired{}, true, {}, fake);
    TEST_ASSERT_EQUAL(Outcome::NotTyped, r.outcome);
    TEST_ASSERT_EQUAL_STRING("Not typed: pair with a Computer first", r.message.c_str());
    r = send::toHelper(kShip, paired(), false, {}, fake);
    TEST_ASSERT_EQUAL(Outcome::NotTyped, r.outcome);
    TEST_ASSERT_EQUAL_STRING("Not typed: the panel isn't on its Wi-Fi", r.message.c_str());
    TEST_ASSERT_EQUAL(0, fake.posts.size());
}

// The paired Computer is found again by its Pin among the Nearby Helpers, since its address can
// change; with no match, the address from pairing. The body is /type's, with the Token as bearer.
void test_the_route_goes_to_the_nearby_helper_with_the_pin() {
    FakeHelper fake;
    send::Result r = send::toHelper(kShip, paired(), true,
                                    {seen("10.0.0.9", 1, kSpoofer), seen("10.0.0.7", 47900, kPin)}, fake);
    TEST_ASSERT_EQUAL(Outcome::Typed, r.outcome);
    TEST_ASSERT_EQUAL_STRING("Typed", r.message.c_str());
    TEST_ASSERT_EQUAL(1, fake.posts.size());
    TEST_ASSERT_EQUAL_STRING("10.0.0.7", fake.posts[0].address.c_str());
    TEST_ASSERT_EQUAL(47900, fake.posts[0].port);
    TEST_ASSERT_EQUAL_STRING(send::typeBody("Ship it", "a1b2:7", 3, true).c_str(), fake.posts[0].json.c_str());
    TEST_ASSERT_EQUAL_STRING("tok", fake.posts[0].token.c_str());

    FakeHelper none;
    send::toHelper(kShip, paired(), true, {seen("10.0.0.9", 1, kSpoofer)}, none);
    TEST_ASSERT_EQUAL(1, none.posts.size());
    TEST_ASSERT_EQUAL_STRING("10.0.0.5", none.posts[0].address.c_str());
    TEST_ASSERT_EQUAL(47812, none.posts[0].port);
}

// Only the whole Pin passes: a cert that merely starts like it (the short Pin) is a mismatch.
void test_only_the_whole_pin_lets_a_send_out() {
    FakeHelper fake;
    fake.certs = {kPin.substr(0, 16) + std::string(48, 'c')};
    send::Result r = send::toHelper(kShip, paired(), true, {}, fake);
    TEST_ASSERT_EQUAL(Outcome::NotTyped, r.outcome);
    TEST_ASSERT_EQUAL_STRING("Not typed: Pin doesn't match", r.message.c_str());
    TEST_ASSERT_EQUAL(1, fake.posts.size());  // the paired address already: no second try
}

// A Bonjour spoofer can claim the paired Computer's fp. Its cert fails the Pin, so nothing is
// written to it, and the Send tries the address from pairing once (checklist, Spoofed Bonjour).
void test_a_spoofer_is_skipped_for_the_address_from_pairing() {
    FakeHelper fake;
    fake.certs = {kSpoofer, kPin};
    send::Result r = send::toHelper(kShip, paired(), true, {seen("10.0.0.66", 47812, kPin)}, fake);
    TEST_ASSERT_EQUAL(Outcome::Typed, r.outcome);
    TEST_ASSERT_EQUAL(2, fake.posts.size());
    TEST_ASSERT_EQUAL_STRING("10.0.0.66", fake.posts[0].address.c_str());
    TEST_ASSERT_EQUAL_STRING("10.0.0.5", fake.posts[1].address.c_str());
    TEST_ASSERT_EQUAL(47812, fake.posts[1].port);

    // Both fail the Pin: Not typed, and only the one retry.
    FakeHelper both;
    both.certs = {kSpoofer, kSpoofer};
    r = send::toHelper(kShip, paired(), true, {seen("10.0.0.66", 47812, kPin)}, both);
    TEST_ASSERT_EQUAL(Outcome::NotTyped, r.outcome);
    TEST_ASSERT_EQUAL_STRING("Not typed: Pin doesn't match", r.message.c_str());
    TEST_ASSERT_EQUAL(2, both.posts.size());

    // Same address, another port: still somewhere else, so the pairing's is tried.
    FakeHelper port;
    port.certs = {kSpoofer, kPin};
    send::toHelper(kShip, paired(), true, {seen("10.0.0.5", 47999, kPin)}, port);
    TEST_ASSERT_EQUAL(2, port.posts.size());
}

send::Result helperSays(const send::Reply& rep) {
    FakeHelper fake;
    fake.replies = {rep};
    return send::toHelper(kShip, paired(), true, {}, fake);
}

// The words for each reply (docs: "What each status means"). Not typed says why: the Helper's own
// error, else the status, else that it can't be reached.
void test_what_the_status_line_says_after_a_helper_send() {
    send::Result r = helperSays(reply(true, 500));
    TEST_ASSERT_EQUAL(Outcome::MaybeTyped, r.outcome);
    TEST_ASSERT_EQUAL_STRING("Maybe typed: check the Computer before tapping again", r.message.c_str());
    r = helperSays(reply(true, -11));
    TEST_ASSERT_EQUAL(Outcome::MaybeTyped, r.outcome);
    r = helperSays(reply(false, 0));
    TEST_ASSERT_EQUAL(Outcome::NotTyped, r.outcome);
    TEST_ASSERT_EQUAL_STRING("Not typed: can't reach omarchy", r.message.c_str());
    r = helperSays(reply(true, 409, "{\"error\":\"not ready: no Accessibility\"}"));
    TEST_ASSERT_EQUAL(Outcome::NotTyped, r.outcome);
    TEST_ASSERT_EQUAL_STRING("Not typed: not ready: no Accessibility", r.message.c_str());
    r = helperSays(reply(true, 403, "<html>"));
    TEST_ASSERT_EQUAL_STRING("Not typed: 403", r.message.c_str());
}

// The result's color, as on the iPhone: green Typed, orange Maybe typed, red Not typed.
void test_each_outcome_has_its_color() {
    TEST_ASSERT_EQUAL(send::Color::Green, send::color(Outcome::Typed));
    TEST_ASSERT_EQUAL(send::Color::Orange, send::color(Outcome::MaybeTyped));
    TEST_ASSERT_EQUAL(send::Color::Red, send::color(Outcome::NotTyped));
}

// The Bluetooth Mode, through a fake keyboard with its own clock: yield() is a millisecond, as
// vTaskDelay(1) is on the panel. It records each report the "stack" takes, and can refuse them for
// a while (a stalled link), disconnect after some, or have the user tap Stop.
struct Report {
    uint8_t mod, code;
    uint32_t at;
};

struct FakeKeyboard : send::Keyboard {
    bool connected = true;
    uint32_t clock = 1000;
    std::vector<Report> taken;
    std::vector<uint32_t> offers;  // when each report was offered, taken or not
    uint32_t refuseFrom = UINT32_MAX, refuseUntil = UINT32_MAX;  // refused while clock is in [from, until)
    size_t disconnectAfter = SIZE_MAX;  // the link drops once this many are taken
    size_t stopAfter = SIZE_MAX;        // Stop is tapped once this many are taken
    std::atomic<bool>* stop = nullptr;
    unsigned drops = 0;

    bool ready() override { return connected; }
    bool report(uint8_t mod, uint8_t code) override {
        offers.push_back(clock);
        if (clock >= refuseFrom && clock < refuseUntil) return false;
        taken.push_back({mod, code, clock});
        if (taken.size() == disconnectAfter) ++drops;
        if (taken.size() == stopAfter && stop != nullptr) stop->store(true);
        return true;
    }
    unsigned disconnects() override { return drops; }
    uint32_t now() override { return clock; }
    void yield() override { clock += 1; }
    void sleep(uint32_t ms) override { clock += ms; }
};

constexpr uint32_t kInterval = 10;
constexpr uint8_t kA = 0x04, kB = 0x05;

send::Result overBluetooth(FakeKeyboard& kb, const std::string& text, bool enter = false) {
    std::atomic<bool> stop{false};
    kb.stop = &stop;
    return send::overBluetooth({text, "", 0, enter}, kb, stop, kInterval);
}

void expectReports(const std::vector<Report>& want, const FakeKeyboard& kb) {
    TEST_ASSERT_EQUAL(want.size(), kb.taken.size());
    for (size_t i = 0; i < want.size(); i++) {
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(want[i].mod, kb.taken[i].mod, "modifier");
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(want[i].code, kb.taken[i].code, "usage");
    }
}

// What a US keyboard can't type refuses the whole Send, and naming it comes before saying no
// Computer is connected. Neither sends a key.
void test_a_bluetooth_send_needs_a_typable_phrase_and_a_computer() {
    FakeKeyboard kb;
    kb.connected = false;
    send::Result r = overBluetooth(kb, "caf\xc3\xa9");
    TEST_ASSERT_EQUAL(Outcome::NotTyped, r.outcome);
    TEST_ASSERT_EQUAL_STRING("Not sent: a US keyboard can't type \xc3\xa9", r.message.c_str());
    r = overBluetooth(kb, "cafe");
    TEST_ASSERT_EQUAL(Outcome::NotTyped, r.outcome);
    TEST_ASSERT_EQUAL_STRING("Not sent: no Computer connected over Bluetooth", r.message.c_str());
    TEST_ASSERT_EQUAL(0, kb.offers.size());
}

// The whole Phrase as keystrokes, one report per key interval (#174), then a release. The panel
// can't see what landed, so it says Sent as keystrokes, never Typed (#98 UI review); green.
void test_a_bluetooth_send_types_every_key_at_the_key_interval() {
    FakeKeyboard kb;
    send::Result r = overBluetooth(kb, "ab", true);
    TEST_ASSERT_EQUAL(Outcome::Typed, r.outcome);
    TEST_ASSERT_EQUAL_STRING("Sent as keystrokes", r.message.c_str());
    expectReports({{0, kA, 0}, {0, kB, 0}, {0, 0x28, 0}, {0, 0, 0}}, kb);  // a, b, Enter rolling over
    for (size_t i = 1; i < kb.taken.size(); i++) {
        TEST_ASSERT_EQUAL_UINT32(kInterval, kb.taken[i].at - kb.taken[i - 1].at);
    }
}

// Stop partway: the key that's down is released, and nothing after it is typed. The start of the
// Phrase went, so it's Maybe typed, orange, not "safe to tap again" (#177).
void test_stop_partway_releases_the_key_and_is_maybe_typed() {
    FakeKeyboard kb;
    kb.stopAfter = 1;  // a is down
    send::Result r = overBluetooth(kb, "abc");
    TEST_ASSERT_EQUAL(Outcome::MaybeTyped, r.outcome);
    TEST_ASSERT_EQUAL_STRING("Maybe typed: check the Computer before tapping again", r.message.c_str());
    expectReports({{0, kA, 0}, {0, 0, 0}}, kb);
}

// A Stop tapped while the Send still waited in the queue stops it before any key: Not sent.
void test_stop_before_the_first_key_is_not_sent() {
    FakeKeyboard kb;
    std::atomic<bool> stop{true};
    send::Result r = send::overBluetooth({"abc", "", 0, false}, kb, stop, kInterval);
    TEST_ASSERT_EQUAL(Outcome::NotTyped, r.outcome);
    TEST_ASSERT_EQUAL_STRING("Not sent", r.message.c_str());
    TEST_ASSERT_EQUAL(0, kb.taken.size());
    TEST_ASSERT_FALSE(stop.load());  // used up, so it can't stop the next Send
}

// The Computer disconnects partway: it drops held keys itself, so nothing more is offered, not
// even a release. Some keys went: Maybe typed.
void test_a_disconnect_partway_is_maybe_typed_without_a_release() {
    FakeKeyboard kb;
    kb.disconnectAfter = 2;  // a, then b down
    send::Result r = overBluetooth(kb, "abc");
    TEST_ASSERT_EQUAL(Outcome::MaybeTyped, r.outcome);
    expectReports({{0, kA, 0}, {0, kB, 0}}, kb);
    TEST_ASSERT_EQUAL(2, kb.offers.size());
}

// The link stalls with a key down: after a second the Send gives up, but a key left down would
// auto-repeat once the link recovers, so a release is offered every key interval until it goes.
void test_a_stall_with_a_key_down_keeps_offering_the_release() {
    FakeKeyboard kb;
    kb.refuseFrom = 1001;  // after a's press at 1000
    kb.refuseUntil = 4000;
    send::Result r = overBluetooth(kb, "ab");
    TEST_ASSERT_EQUAL(Outcome::MaybeTyped, r.outcome);
    expectReports({{0, kA, 0}, {0, 0, 0}}, kb);
    TEST_ASSERT_UINT32_WITHIN(kInterval, 4000, kb.taken[1].at);
}

// ... but not forever: 6 s of refusals and it stops offering, so the net task moves on.
void test_the_release_is_offered_for_six_seconds_at_most() {
    FakeKeyboard kb;
    kb.refuseFrom = 1001;
    send::Result r = overBluetooth(kb, "ab");
    TEST_ASSERT_EQUAL(Outcome::MaybeTyped, r.outcome);
    TEST_ASSERT_EQUAL(1, kb.taken.size());
    const uint32_t gaveUp = kb.offers.back();
    TEST_ASSERT_TRUE(gaveUp > 1000 + 6000);  // a second of key tries, then up to 6 s of releases
    TEST_ASSERT_TRUE(gaveUp < 1000 + 1000 + 6000 + 2 * kInterval);
    TEST_ASSERT_TRUE(kb.clock < 1000 + 1000 + 6000 + 3 * kInterval);
}

// A disconnect during the stall ends the release offers at once: the host has let go already.
void test_a_disconnect_ends_the_release_offers() {
    FakeKeyboard kb;
    kb.refuseFrom = 1001;
    kb.disconnectAfter = 1;  // the link goes with a's press
    // The drop is seen before the next step, so the Send ends without a refused report.
    send::Result r = overBluetooth(kb, "ab");
    TEST_ASSERT_EQUAL(Outcome::MaybeTyped, r.outcome);
    TEST_ASSERT_EQUAL(1, kb.offers.size());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_status_codes_map_to_typed_not_typed_or_maybe_typed);
    RUN_TEST(test_no_reply_is_not_typed_unless_the_request_was_sent);
    RUN_TEST(test_the_type_body_names_the_phrase_and_its_edit);
    RUN_TEST(test_the_type_body_asks_for_enter_only_when_the_button_does);
    RUN_TEST(test_a_helper_send_needs_a_pairing_and_the_wifi);
    RUN_TEST(test_the_route_goes_to_the_nearby_helper_with_the_pin);
    RUN_TEST(test_only_the_whole_pin_lets_a_send_out);
    RUN_TEST(test_a_spoofer_is_skipped_for_the_address_from_pairing);
    RUN_TEST(test_what_the_status_line_says_after_a_helper_send);
    RUN_TEST(test_each_outcome_has_its_color);
    RUN_TEST(test_a_bluetooth_send_needs_a_typable_phrase_and_a_computer);
    RUN_TEST(test_a_bluetooth_send_types_every_key_at_the_key_interval);
    RUN_TEST(test_stop_partway_releases_the_key_and_is_maybe_typed);
    RUN_TEST(test_stop_before_the_first_key_is_not_sent);
    RUN_TEST(test_a_disconnect_partway_is_maybe_typed_without_a_release);
    RUN_TEST(test_a_stall_with_a_key_down_keeps_offering_the_release);
    RUN_TEST(test_the_release_is_offered_for_six_seconds_at_most);
    RUN_TEST(test_a_disconnect_ends_the_release_offers);
    return UNITY_END();
}
