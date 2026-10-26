#include "net.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>
#include <map>

#include "ble_keyboard.h"
#include "helper_client.h"
#include "hid_keys.h"
#include "pairing.h"
#include "panel_store.h"
#include "send.h"
#include "wifi_secrets.h"

namespace net {
namespace {

struct Request {
    enum Kind { Pair, Type } kind;
    Helper helper;
    std::string code, shortPin, text;
    std::string raw;  // Type: the Phrase itself, for Bluetooth
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
struct Pairing {
    std::string name, address, pin, token;
    int port = 0;
    bool ok() const { return !token.empty() && pin.size() == 64; }
};
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

// The Helper's own words for an error reply, if it sent any.
std::string errorText(const std::string& body) {
    JsonDocument doc;
    if (deserializeJson(doc, body)) return "";
    return doc["error"] | "";
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
        Pairing pr{h.name, h.address, r.certSha256, token, h.txt.port};
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

void handleType(const Request& req, Snapshot& s) {
    if (!g_pairing.ok()) {
        s.message = "Not typed: pair with a Computer first";
        return;
    }
    // A paired Computer is found again by its Pin in Bonjour, since its address can change.
    std::string address = g_pairing.address;
    int port = g_pairing.port;
    for (const Helper& h : s.nearby) {
        if (pairing::matchesPin(g_pairing.pin, h.txt.fp)) {
            address = h.address;
            port = h.txt.port;
        }
    }
    const std::string& json = req.text;  // the whole /type body, from send::typeBody
    const auto pinned = [](const std::string& cert) { return pairing::matchesPin(g_pairing.pin, cert); };
    helper_client::Reply r = helper_client::post(address, port, "/type", json, g_pairing.token, pinned);
    // A Bonjour spoofer can claim the paired Computer's fp; the Pin stops it, then try the
    // address from pairing once, so a spoofer can't block every Send (checklist, Spoofed Bonjour).
    if (r.pinMismatch && (address != g_pairing.address || port != g_pairing.port)) {
        r = helper_client::post(g_pairing.address, g_pairing.port, "/type", json, g_pairing.token, pinned);
    }
    switch (send::outcome(r.sent, r.status)) {
        case send::Outcome::Typed:
            s.message = "Typed";
            s.ok = true;
            break;
        case send::Outcome::MaybeTyped:
            s.message = "Maybe typed: check the Computer before tapping again";
            s.maybe = true;
            break;
        case send::Outcome::NotTyped: {
            std::string why = r.pinMismatch ? "Pin doesn't match" : errorText(r.body);
            if (why.empty()) why = r.status ? std::to_string(r.status) : "can't reach " + g_pairing.name;
            s.message = "Not typed: " + why;
            break;
        }
    }
}

// One report per interval: a press, then its release. 20 ms leaves the host a connection event for
// each, so 100 characters take about 4 s.
constexpr uint32_t kKeyIntervalMs = 20;

std::atomic<bool> g_cancelSend{false};  // set from the UI task, read by the send loop

void handleBluetoothType(const Request& req, Snapshot& s) {
    const hid_keys::Plan p = hid_keys::plan(req.raw, false);
    hid_keys::Said r = p.ok ? hid_keys::Said{"Not sent: no Computer connected over Bluetooth", false}
                            : hid_keys::refused(p.bad);
    if (p.ok && ble_keyboard::ready()) {
        hid_keys::Sender sender(p.keys, kKeyIntervalMs);
        const unsigned disconnects = ble_keyboard::disconnects();
        const auto notify = [](const hid_keys::Key& k) { return ble_keyboard::send(k.mod, k.code); };
        g_cancelSend.store(false);
        while (!sender.done()) {
            if (ble_keyboard::disconnects() != disconnects) sender.drop();
            if (g_cancelSend.exchange(false)) sender.cancel();
            sender.step(millis(), notify);
            vTaskDelay(1);  // Sender needs a yield between steps
        }
        // Gave up partway on a stalled link: a key may still be down on the Computer, which would
        // auto-repeat it once the link recovers. Keep offering a release until it goes or the link does.
        const uint32_t start = millis();
        while (sender.result() == hid_keys::Sender::Result::Stopped && ble_keyboard::disconnects() == disconnects &&
               millis() - start < 6000 && !ble_keyboard::send(0, 0)) {
            vTaskDelay(pdMS_TO_TICKS(kKeyIntervalMs));
        }
        r = hid_keys::said(sender.result());
    }
    s.message = r.message;
    s.ok = r.ok;
}

void handleRequests(Snapshot& s, TickType_t wait) {
    Request* req = nullptr;
    while (xQueueReceive(g_requests, &req, wait) == pdTRUE) {
        s.ok = s.maybe = false;
        if (req->kind == Request::Pair) handlePair(*req, s);
        else if (g_bluetooth) handleBluetoothType(*req, s);
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
    if (!s.wifiConfigured) {
        Serial.println("net: no Wi-Fi in this build");
        if (g_bluetooth) {
            for (;;) handleRequests(s, portMAX_DELAY);
        }
        vTaskDelete(nullptr);
        return;
    }
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    bool mdns = false;
    for (;;) {
        const bool up = WiFi.status() == WL_CONNECTED;
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
    // No Wi-Fi in this build and no Bluetooth: the net task is gone, so nothing would answer.
    if ((sizeof(WIFI_SSID) > 1 || g_bluetooth) && g_requests != nullptr && xQueueSend(g_requests, &req, 0) == pdTRUE) return true;
    delete req;
    return false;
}

}  // namespace

bool pair(const Helper& h, const std::string& code, const std::string& shortPin) {
    return enqueue(new Request{Request::Pair, h, code, shortPin, "", ""});
}

bool type(const std::string& text, const std::string& phrase, uint32_t edit) {
    return enqueue(new Request{Request::Type, Helper{}, "", "", send::typeBody(text, phrase, edit), text});
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

}  // namespace net
