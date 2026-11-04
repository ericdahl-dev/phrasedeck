#include <unity.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "own_network.h"
#include "web.h"

void setUp() {}
void tearDown() {}

// The panel's own network (#165) is named after the last 4 hex digits of its MAC, so two panels
// in one room don't clash.
void test_the_network_is_named_after_the_mac() {
    const uint8_t mac[6] = {0x24, 0x6f, 0x28, 0xab, 0x1a, 0x2b};
    TEST_ASSERT_EQUAL_STRING("Phrasedeck-1A2B", own_network::name(mac).c_str());
    const uint8_t zero[6] = {0, 0, 0, 0, 0x00, 0x0f};
    TEST_ASSERT_EQUAL_STRING("Phrasedeck-000F", own_network::name(zero).c_str());
}

// A stand-in for esp_fill_random: counts up from start, so every byte value turns up.
own_network::Fill counting(uint8_t start) {
    return [start](uint8_t* out, size_t len) mutable {
        for (size_t i = 0; i < len; ++i) out[i] = start++;
    };
}

// The password is read off the panel's screen and typed on a phone: long enough to hold off an
// offline guess at a captured handshake, and no characters that look alike.
void test_the_password_is_long_and_has_no_look_alike_characters() {
    for (int start = 0; start < 256; start += 7) {
        const std::string pw = own_network::makePassword(counting(static_cast<uint8_t>(start)));
        TEST_ASSERT_GREATER_OR_EQUAL(12, pw.size());
        for (char c : pw) {
            TEST_ASSERT_NOT_NULL_MESSAGE(strchr(own_network::kAlphabet, c), pw.c_str());
            TEST_ASSERT_NULL_MESSAGE(strchr("0O1lI", c), pw.c_str());
        }
    }
    TEST_ASSERT_EQUAL(32, strlen(own_network::kAlphabet));  // 5 bits a character, so 12 hold 60
}

void test_the_password_comes_from_the_random_bytes() {
    TEST_ASSERT_TRUE(own_network::makePassword(counting(0)) != own_network::makePassword(counting(100)));
    TEST_ASSERT_EQUAL_STRING(own_network::makePassword(counting(9)).c_str(), own_network::makePassword(counting(9)).c_str());
}

// The panel NVS, as a map: what the panel keeps between restarts.
struct FakeNvs {
    std::string saved;
    bool writable = true;
    int puts = 0;
    own_network::Store store() {
        return {[this] { return saved; },
                [this](const std::string& pw) {
                    ++puts;
                    if (writable) saved = pw;
                    return writable;
                }};
    }
};

// Made the first time and kept, like the web cert: the phone remembers the network.
void test_the_password_is_made_once_and_kept() {
    FakeNvs nvs;
    const std::string first = own_network::loadOrMakePassword(nvs.store(), counting(1));
    TEST_ASSERT_EQUAL(12, first.size());
    TEST_ASSERT_EQUAL_STRING(first.c_str(), nvs.saved.c_str());
    const std::string again = own_network::loadOrMakePassword(nvs.store(), counting(77));
    TEST_ASSERT_EQUAL_STRING(first.c_str(), again.c_str());
    TEST_ASSERT_EQUAL(1, nvs.puts);
}

// A factory reset erases the panel NVS, so the next start makes a new password.
void test_after_a_factory_reset_there_is_a_new_password() {
    FakeNvs nvs;
    const std::string before = own_network::loadOrMakePassword(nvs.store(), counting(1));
    nvs.saved.clear();  // panel_store::erase()
    const std::string after = own_network::loadOrMakePassword(nvs.store(), counting(150));
    TEST_ASSERT_TRUE(before != after);
    TEST_ASSERT_EQUAL_STRING(after.c_str(), nvs.saved.c_str());
}

// Anything stored that isn't a password this firmware makes is replaced, not used.
void test_a_bad_stored_password_is_replaced() {
    for (const char* bad : {"short", "abcdefghijk0", "abcdefghijkmn"}) {
        FakeNvs nvs;
        nvs.saved = bad;
        const std::string pw = own_network::loadOrMakePassword(nvs.store(), counting(3));
        TEST_ASSERT_TRUE(pw != bad);
        TEST_ASSERT_EQUAL(12, pw.size());
    }
}

// Not saved: no network, rather than one whose password changes at every restart.
void test_no_password_if_it_cant_be_saved() {
    FakeNvs nvs;
    nvs.writable = false;
    TEST_ASSERT_EQUAL_STRING("", own_network::loadOrMakePassword(nvs.store(), counting(1)).c_str());
}

// Whether the network is up, from boot (0 ms) on: no Wi-Fi in the build starts it at once.
void test_a_build_without_wifi_starts_it_at_once() {
    own_network::Decision d;
    TEST_ASSERT_TRUE(d.wanted(false, false, 0));
    TEST_ASSERT_TRUE(d.wanted(false, false, 60000));
}

// The panel's Wi-Fi gets 30 s from boot to join before the network starts.
void test_not_joined_within_30_s_starts_it() {
    own_network::Decision d;
    TEST_ASSERT_FALSE(d.wanted(true, false, 0));
    TEST_ASSERT_FALSE(d.wanted(true, false, 29999));
    TEST_ASSERT_TRUE(d.wanted(true, false, 30000));
    TEST_ASSERT_TRUE(d.wanted(true, false, 600000));
}

void test_joined_in_time_never_starts_it() {
    own_network::Decision d;
    TEST_ASSERT_FALSE(d.wanted(true, false, 5000));
    TEST_ASSERT_FALSE(d.wanted(true, true, 8000));
    TEST_ASSERT_FALSE(d.wanted(true, true, 60000));
}

// Joined later (the panel kept trying): the network stops. If the Wi-Fi goes again, it gets
// another 30 s before the network comes back.
void test_joined_later_stops_it() {
    own_network::Decision d;
    TEST_ASSERT_TRUE(d.wanted(true, false, 45000));
    TEST_ASSERT_FALSE(d.wanted(true, true, 300000));
    TEST_ASSERT_FALSE(d.wanted(true, false, 310000));
    TEST_ASSERT_FALSE(d.wanted(true, false, 329999));
    TEST_ASSERT_TRUE(d.wanted(true, false, 330000));
}

// While the network is up the panel still tries its Wi-Fi, but each try scans every channel and
// stalls the network: once a minute, or every 5 minutes while someone is on it.
void test_the_wifi_is_retried_less_often_while_someone_is_on_the_network() {
    TEST_ASSERT_FALSE(own_network::retryDue(59999, 0, 0));
    TEST_ASSERT_TRUE(own_network::retryDue(60000, 0, 0));
    TEST_ASSERT_FALSE(own_network::retryDue(60000, 0, 1));
    TEST_ASSERT_FALSE(own_network::retryDue(299999, 0, 2));
    TEST_ASSERT_TRUE(own_network::retryDue(300000, 0, 1));
    TEST_ASSERT_TRUE(own_network::retryDue(1000000, 900000, 0));
}

// The web editor's Host and Origin checks (#98) take the panel's address: its Wi-Fi address when
// joined, 192.168.4.1 only while its own network is up, and none otherwise.
bool hostOk(const std::string& host, const std::string& joinedIp, bool networkUp) {
    return web::hostAllowed(host, own_network::editorAddress(joinedIp, networkUp));
}
bool originOk(const std::string& origin, const std::string& joinedIp, bool networkUp) {
    return web::originAllowed(origin, own_network::editorAddress(joinedIp, networkUp));
}

void test_192_168_4_1_is_allowed_only_while_the_network_is_up() {
    TEST_ASSERT_TRUE(hostOk("192.168.4.1", "", true));
    TEST_ASSERT_TRUE(hostOk("192.168.4.1:443", "", true));
    TEST_ASSERT_TRUE(originOk("https://192.168.4.1", "", true));
    TEST_ASSERT_FALSE(hostOk("192.168.4.1", "", false));
    TEST_ASSERT_FALSE(originOk("https://192.168.4.1", "", false));
    TEST_ASSERT_FALSE(hostOk("", "", false));
    TEST_ASSERT_FALSE(originOk("", "", false));
    // Rebinding and CSRF stay shut on the network too.
    TEST_ASSERT_FALSE(hostOk("evil.example", "", true));
    TEST_ASSERT_FALSE(hostOk("phrasedeck.local", "", true));
    TEST_ASSERT_FALSE(originOk("http://192.168.4.1", "", true));
    TEST_ASSERT_FALSE(originOk("null", "", true));
}

// Joined to its Wi-Fi, the panel answers only to its Wi-Fi address, as before; 192.168.4.1 isn't
// the panel there, whatever the network's state.
void test_joined_only_the_wifi_address_is_allowed() {
    TEST_ASSERT_TRUE(hostOk("192.168.1.58", "192.168.1.58", false));
    TEST_ASSERT_TRUE(originOk("https://192.168.1.58", "192.168.1.58", false));
    TEST_ASSERT_FALSE(hostOk("192.168.4.1", "192.168.1.58", false));
    TEST_ASSERT_FALSE(originOk("https://192.168.4.1", "192.168.1.58", false));
    TEST_ASSERT_FALSE(hostOk("192.168.4.1", "192.168.1.58", true));
    TEST_ASSERT_EQUAL_STRING("192.168.1.58", own_network::editorAddress("192.168.1.58", true).c_str());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_the_network_is_named_after_the_mac);
    RUN_TEST(test_the_password_is_long_and_has_no_look_alike_characters);
    RUN_TEST(test_the_password_comes_from_the_random_bytes);
    RUN_TEST(test_the_password_is_made_once_and_kept);
    RUN_TEST(test_after_a_factory_reset_there_is_a_new_password);
    RUN_TEST(test_a_bad_stored_password_is_replaced);
    RUN_TEST(test_no_password_if_it_cant_be_saved);
    RUN_TEST(test_a_build_without_wifi_starts_it_at_once);
    RUN_TEST(test_not_joined_within_30_s_starts_it);
    RUN_TEST(test_joined_in_time_never_starts_it);
    RUN_TEST(test_joined_later_stops_it);
    RUN_TEST(test_the_wifi_is_retried_less_often_while_someone_is_on_the_network);
    RUN_TEST(test_192_168_4_1_is_allowed_only_while_the_network_is_up);
    RUN_TEST(test_joined_only_the_wifi_address_is_allowed);
    return UNITY_END();
}
