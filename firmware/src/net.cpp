#include "net.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <mbedtls/platform_util.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>
#include <map>

#include "ble_keyboard.h"
#include "helper_client.h"
#include "own_network.h"
#include "pairing.h"
#include "panel_store.h"
#include "send.h"
#include "wifi_secrets.h"

namespace net {
namespace {

struct Request {
    enum Kind { Pair, Type } kind;
    Helper helper;                // Pair
    std::string code, shortPin;   // Pair
    send::Phrase phrase;          // Type
};

// Read once at start(), before the task: a Mode change waits for a restart.
bool g_bluetooth = false;

bool loadBluetooth() {
    Preferences p;
    if (!p.begin("mode", true, panel_store::kPartition)) return false;
    const bool on = p.getBool("bluetooth", false);
    p.end();
    return on;
}

QueueHandle_t g_requests = nullptr;  // Request*, owned by the receiver

// The pairing, in the panel NVS. The Token stays on this task: it's never in a Snapshot.
using Pairing = send::Paired;
Pairing g_pairing;

void loadPairing() {
    Preferences p;
    if (!p.begin("pairing", true, panel_store::kPartition)) return;
    g_pairing.name = p.getString("name", "").c_str();
    g_pairing.address = p.getString("address", "").c_str();
    g_pairing.port = p.getInt("port", 0);
    g_pairing.pin = p.getString("pin", "").c_str();
    g_pairing.token = p.getString("token", "").c_str();
    p.end();
}

bool savePairing(const Pairing& pr) {
    Preferences p;
    if (!p.begin("pairing", false, panel_store::kPartition)) return false;
    // Clear first and write the Token last, so a save that fails partway can't pair one
    // Computer's Pin with another's Token: without a Token the pairing doesn't load.
    p.clear();
    const bool ok = p.putString("name", pr.name.c_str()) && p.putString("address", pr.address.c_str()) &&
                    p.putInt("port", pr.port) && p.putString("pin", pr.pin.c_str()) &&
                    p.putString("token", pr.token.c_str());
    p.end();
    return ok;
}

SemaphoreHandle_t g_lock = nullptr;
Snapshot g_snap;
std::atomic<unsigned> g_version{0};

void publish(const Snapshot& s) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    const unsigned v = g_snap.version + 1;
    g_snap = s;
    g_snap.version = v;
    xSemaphoreGive(g_lock);
    g_version.store(v);
}

std::vector<Helper> browse() {
    std::vector<Helper> out;
    const int n = MDNS.queryService("phrasedeck", "tcp");
    for (int i = 0; i < n; ++i) {
        std::map<std::string, std::string> txt;
        for (const char* key : {"v", "port", "fp", "ts"}) {
            if (MDNS.hasTxt(i, key)) txt[key] = MDNS.txt(i, key).c_str();
        }
        Helper h;
        // An answer without an A record yet comes back as 0.0.0.0: not Nearby until it resolves.
        if (!nearby::parse(txt, h.txt) || MDNS.address(i) == IPAddress()) continue;
        h.name = MDNS.hostname(i).c_str();
        h.address = MDNS.address(i).toString().c_str();
        out.push_back(h);
    }
    return out;
}

void handlePair(const Request& req, Snapshot& s) {
    // A Helper from a Pair screen opened before the Wi-Fi went: on the own network its address
    // could be one of that network's clients, so nothing is sent.
    s.message = own_network::offWifi(s.wifiUp);
    if (!s.message.empty()) return;
    const Helper& h = req.helper;
    JsonDocument doc;
    doc["code"] = req.code;
    doc["device"] = "Phrasedeck panel";
    std::string json;
    serializeJson(doc, json);
    const helper_client::Reply r = helper_client::post(
        h.address, h.txt.port, "/pair", json, "",
        [&](const std::string& cert) { return pairing::matchesShortPin(req.shortPin, cert); });
    std::string token;
    if (r.pinMismatch) {
        s.message = "Pin doesn't match " + h.name + ": nothing sent";
    } else if (!r.sent) {
        s.message = "Can't reach " + h.name;
    } else if (pairing::readPairReply(r.status, r.body, r.certSha256, token)) {
        Pairing pr{h.name, h.address, h.txt.port, r.certSha256, token};
        // The Helper has used up its pairing window either way, so keep the pairing in RAM.
        g_pairing = pr;
        s.paired = pr.name;
        s.ok = true;
        s.message = savePairing(pr) ? "Paired with " + pr.name
                                    : "Paired with " + pr.name + ", but not saved: pair again after a restart";
    } else if (r.status == 401) {
        s.message = "Wrong Pairing code";
    } else if (r.status == 404) {
        s.message = "No pairing window: run phrased pair";
    } else {
        s.message = "Pairing failed (" + std::to_string(r.status) + ")";
    }
}

// The Send module's two seams on the panel: TLS to the Helper, and NimBLE HID reports.
class TlsHelper : public send::HelperTransport {
  public:
    send::Reply post(const std::string& address, int port, const std::string& json, const std::string& token,
                     const std::function<bool(const std::string&)>& allow) override {
        const helper_client::Reply r = helper_client::post(address, port, "/type", json, token, allow);
        send::Reply out;
        out.sent = r.sent;
        out.pinMismatch = r.pinMismatch;
        out.status = r.status;
        out.body = r.body;
        return out;
    }
};

class BleKeyboard : public send::Keyboard {
  public:
    bool ready() override { return ble_keyboard::ready(); }
    bool report(uint8_t mod, uint8_t code) override { return ble_keyboard::send(mod, code); }
    unsigned disconnects() override { return ble_keyboard::disconnects(); }
    uint32_t now() override { return millis(); }
    void yield() override { vTaskDelay(1); }
    void sleep(uint32_t ms) override { vTaskDelay(pdMS_TO_TICKS(ms)); }
};

// One report per interval. Most characters are one report (the next key's press releases the last,
// #174), and the panel asks for a 15 ms connection interval; the controller sends more than one
// report in a connection event, in order. 10 ms makes about 80 characters a second of plain text.
// Override to A/B on the device: -DPANEL_KEY_INTERVAL_MS=20 was the speed before #174.
#ifndef PANEL_KEY_INTERVAL_MS
#define PANEL_KEY_INTERVAL_MS 10
#endif
constexpr uint32_t kKeyIntervalMs = PANEL_KEY_INTERVAL_MS;
static_assert(kKeyIntervalMs >= 1, "PANEL_KEY_INTERVAL_MS must be at least 1");

std::atomic<bool> g_cancelSend{false};  // set from the UI task, read by the send loop

send::Result sendOverBluetooth(const send::Phrase& phrase) {
    BleKeyboard keyboard;
#ifdef PANEL_BLE_DEBUG
    const uint16_t itvl = ble_keyboard::connInterval();
    Serial.printf("ble: send, interval %u.%02u ms, key interval %u ms\n", itvl * 125 / 100, itvl * 125 % 100,
                  static_cast<unsigned>(kKeyIntervalMs));
    send::KeyStats st;
    const send::Result r = send::overBluetooth(phrase, keyboard, g_cancelSend, kKeyIntervalMs, &st);
    // Chars per second of the whole Send, as typed; the host's own timing should match.
    Serial.printf("ble: sent %u reports for %u chars in %u ms: %u.%u chars/s\n", st.reports,
                  static_cast<unsigned>(st.keys), static_cast<unsigned>(st.tookMs),
                  st.tookMs ? static_cast<unsigned>(st.keys * 1000 / st.tookMs) : 0,
                  st.tookMs ? static_cast<unsigned>(st.keys * 10000 / st.tookMs % 10) : 0);
    return r;
#else
    return send::overBluetooth(phrase, keyboard, g_cancelSend, kKeyIntervalMs);
#endif
}

// The Mode picks where a Send goes; the Send module does the rest.
void handleType(const Request& req, Snapshot& s) {
    TlsHelper helper;
    const send::Result r = g_bluetooth ? sendOverBluetooth(req.phrase)
                                       : send::toHelper(req.phrase, g_pairing, s.wifiUp, s.nearby, helper);
    s.sent = r.outcome;
    s.message = r.message;
}

// The panel's own network (#165). Only the web editor answers on it: phrased isn't reachable,
// mDNS runs only on the panel's Wi-Fi (it starts after this network stops), and there's no DNS
// server or captive portal. The password stays in the panel NVS, so a factory reset makes a new one.
std::string ownPassword() {
    const own_network::Store store{
        [] {
            Preferences p;
            if (!p.begin("ownnet", true, panel_store::kPartition)) return std::string();
            const std::string pw = p.getString("password", "").c_str();
            p.end();
            return pw;
        },
        [](const std::string& pw) {
            Preferences p;
            if (!p.begin("ownnet", false, panel_store::kPartition)) return false;
            const bool ok = p.putString("password", pw.c_str()) == pw.size();
            p.end();
            return ok;
        }};
    // The radio is on (STA started in run), so the hardware RNG has RF entropy.
    return own_network::loadOrMakePassword(store, [](uint8_t* out, size_t len) { esp_fill_random(out, len); });
}

// Writes the own network's WPA2 settings into the Wi-Fi driver's NVS while the radio is off, so
// the AP's first beacon is already WPA2: Arduino's next esp_wifi_init loads them (it keeps
// WIFI_STORAGE_FLASH). Without this, softAP() enables the AP with the driver's stored config,
// which after a factory reset is an open ESP_XXXXXX network, and only then sets ours.
bool seedApConfig(const std::string& name, const std::string& pw) {
    WiFi.mode(WIFI_OFF);  // Arduino: esp_wifi_stop, then esp_wifi_deinit
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) return false;
    wifi_config_t conf = {};
    memcpy(conf.ap.ssid, name.data(), name.size());
    conf.ap.ssid_len = name.size();
    memcpy(conf.ap.password, pw.data(), pw.size());
    conf.ap.channel = 1;
    conf.ap.authmode = WIFI_AUTH_WPA2_PSK;
    conf.ap.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP;
    conf.ap.max_connection = 2;
    conf.ap.beacon_interval = 100;
    const bool ok = esp_wifi_set_storage(WIFI_STORAGE_FLASH) == ESP_OK &&
                    esp_wifi_set_mode(WIFI_MODE_AP) == ESP_OK &&  // AP enabled, not started
                    esp_wifi_set_config(WIFI_IF_AP, &conf) == ESP_OK;
    mbedtls_platform_zeroize(&conf, sizeof conf);
    return esp_wifi_deinit() == ESP_OK && ok;
}

bool startOwnNetwork(Snapshot& s) {
    const std::string pw = ownPassword();
    if (pw.empty()) {
        Serial.println("net: own network: no password saved (panel NVS), not started");
        return false;
    }
    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) != ESP_OK) return false;
    const std::string name = own_network::name(mac);
    // Retried on our own schedule (retryDue): a reconnect loop would scan every channel nonstop.
    WiFi.setAutoReconnect(false);
    WiFi.disconnect();
    // One radio restart per start; STA isn't joined at this point anyway. The password was made
    // above with the radio still on (RF entropy).
    if (!seedApConfig(name, pw) || !WiFi.mode(WIFI_AP_STA)) {  // the AP starts with the seeded WPA2 config
        Serial.println("net: own network didn't start (Wi-Fi driver)");
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        if (s.wifiConfigured) WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        return false;
    }
    IPAddress ip;
    ip.fromString(own_network::kAddress);
    // WPA2, at most 2 clients (the web server's 2 sockets); no DNS offered over DHCP (dns 0). The
    // same settings as seeded, so softAP() finds them equal and doesn't restart the AP.
    if (!WiFi.softAP(name.c_str(), pw.c_str(), 1, 0, 2, false, WIFI_AUTH_WPA2_PSK, WIFI_CIPHER_TYPE_CCMP) ||
        !WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0), IPAddress(), IPAddress())) {
        Serial.println("net: own network didn't start");
        WiFi.enableAP(false);
        WiFi.setAutoReconnect(true);
        if (s.wifiConfigured) WiFi.begin(WIFI_SSID, WIFI_PASSWORD);  // disconnect() above stopped the tries
        return false;
    }
    s.ownNetwork = true;
    s.ownName = name;
    s.ownPassword = pw;
    Serial.printf("net: own network %s up, editor at https://%s/\n", name.c_str(), own_network::kAddress);  // never the password
    return true;
}

void stopOwnNetwork(Snapshot& s) {
    WiFi.enableAP(false);
    WiFi.setAutoReconnect(true);
    s.ownNetwork = false;
    s.ownName.clear();
    s.ownPassword.clear();
    Serial.println("net: own network stopped");
}

uint64_t nowMs() { return esp_timer_get_time() / 1000; }

void handleRequests(Snapshot& s, TickType_t wait) {
    Request* req = nullptr;
    while (xQueueReceive(g_requests, &req, wait) == pdTRUE) {
        s.ok = false;
        s.sent = send::Outcome::NotTyped;
        if (req->kind == Request::Pair) handlePair(*req, s);
        else handleType(*req, s);
        Serial.printf("net: %s: %s\n", req->kind == Request::Pair ? "pair" : "type", s.message.c_str());
        delete req;
        ++s.messageSeq;
        publish(s);
        wait = 0;
    }
}

void run(void*) {
    Snapshot s;
    loadPairing();
    s.paired = g_pairing.ok() ? g_pairing.name : "";
    s.wifiConfigured = sizeof(WIFI_SSID) > 1;
    if (g_bluetooth && !ble_keyboard::start()) {
        s.message = "Bluetooth didn't start";
        Serial.println("net: Bluetooth didn't start");
    }
    publish(s);
    // STA even without Wi-Fi in the build: the radio on gives the RNG entropy for the own network.
    WiFi.mode(WIFI_STA);
    if (s.wifiConfigured) {
        WiFi.setAutoReconnect(true);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    } else {
        Serial.println("net: no Wi-Fi in this build");
    }
    bool mdns = false;
    own_network::Decision own;
    uint64_t triedMs = 0, failedMs = 0;
    bool failed = false;
    for (;;) {
        const bool up = s.wifiConfigured && WiFi.status() == WL_CONNECTED;
        const uint64_t now = nowMs();
        // Before mDNS below: the own network stops before mDNS starts, so they never share a radio.
        const bool wanted = own.wanted(s.wifiConfigured, up, now);
        if (wanted && !s.ownNetwork && (!failed || now - failedMs >= 10000)) {
            failed = !startOwnNetwork(s);
            failedMs = triedMs = now;
            if (!failed) publish(s);
        } else if (!wanted && s.ownNetwork) {
            stopOwnNetwork(s);
            publish(s);
        }
        if (s.ownNetwork && s.wifiConfigured && own_network::retryDue(now, triedMs, WiFi.softAPgetStationNum())) {
            triedMs = now;
            Serial.println("net: trying the Wi-Fi again");
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        }
        if (up != s.wifiUp) {
            s.wifiUp = up;
            s.ip = up ? WiFi.localIP().toString().c_str() : "";
            if (!up) {
                s.nearby.clear();
                if (mdns) MDNS.end();
                mdns = false;  // started again once Wi-Fi is back
            }
            Serial.printf("net: Wi-Fi %s %s\n", up ? "connected" : "down", s.ip.c_str());
            publish(s);
        }
        if (up && !mdns) mdns = MDNS.begin("phrasedeck-panel");
        if (up && mdns) {
            s.nearby = browse();  // blocks about 3 s; fine on this task
            Serial.printf("net: %u Nearby Helper(s), stack left %u\n", static_cast<unsigned>(s.nearby.size()),
                          static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
            publish(s);
            // Sends and pairing wait at most this long; between browses they're answered at once.
            const TickType_t until = xTaskGetTickCount() + pdMS_TO_TICKS(10000);
            while (xTaskGetTickCount() < until) handleRequests(s, until - xTaskGetTickCount());
        } else {
            handleRequests(s, pdMS_TO_TICKS(500));
        }
    }
}

}  // namespace

bool start() {
    g_lock = xSemaphoreCreateMutex();
    g_requests = xQueueCreate(8, sizeof(Request*));
    if (g_lock == nullptr || g_requests == nullptr) return false;
    g_bluetooth = loadBluetooth();
    // 16 KB: the TLS client runs on this task too (mbedTLS handshakes are stack hungry).
    return xTaskCreatePinnedToCore(run, "net", 16384, nullptr, 1, nullptr, 0) == pdPASS;
}

unsigned version() { return g_version.load() + ble_keyboard::version(); }

Snapshot snapshot() {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    Snapshot s = g_snap;
    xSemaphoreGive(g_lock);
    s.version += ble_keyboard::version();  // the same number version() returns
    s.bluetooth = g_bluetooth;
    if (g_bluetooth) {
        s.bleReady = ble_keyboard::ready();
        s.blePairing = ble_keyboard::pairingOpen();
        s.passkey = ble_keyboard::passkey();
    }
    return s;
}

namespace {

bool enqueue(Request* req) {
    // The net task runs in every build now (it runs the own network), so something always answers.
    if (g_requests != nullptr && xQueueSend(g_requests, &req, 0) == pdTRUE) return true;
    delete req;
    return false;
}

}  // namespace

bool pair(const Helper& h, const std::string& code, const std::string& shortPin) {
    return enqueue(new Request{Request::Pair, h, code, shortPin, {}});
}

bool type(const std::string& text, const std::string& phrase, uint32_t edit, bool enter) {
    // Cleared here, not when the Send starts: a Stop tapped while this one waits in the queue (a
    // browse holds the net task about 3 s) must still stop it. One Send at a time, so none is stale.
    g_cancelSend.store(false);
    return enqueue(new Request{Request::Type, Helper{}, "", "", send::Phrase{text, phrase, edit, enter}});
}

bool setBluetooth(bool on) {
    Preferences p;
    if (!p.begin("mode", false, panel_store::kPartition)) return false;
    const bool ok = p.putBool("bluetooth", on) > 0;
    p.end();
    return ok;
}

void cancelBluetoothSend() { g_cancelSend.store(true); }

void openBluetoothPairing(uint32_t ms) {
    if (g_bluetooth) ble_keyboard::openPairing(ms);
}

void closeBluetoothPairing() {
    if (g_bluetooth) ble_keyboard::closePairing();
}

}  // namespace net
