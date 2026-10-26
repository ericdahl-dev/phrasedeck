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

#include "helper_client.h"
#include "pairing.h"
#include "panel_store.h"
#include "send.h"
#include "wifi_secrets.h"

namespace net {
namespace {

struct Request {
    enum Kind { Pair, Type } kind;
    size_t index = 0;
    std::string code, shortPin, text;
};

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
        if (!nearby::parse(txt, h.txt)) continue;
        h.name = MDNS.hostname(i).c_str();
        h.address = MDNS.address(i).toString().c_str();
        out.push_back(h);
    }
    return out;
}

void handlePair(const Request& req, Snapshot& s) {
    if (req.index >= s.nearby.size()) {
        s.message = "Pairing: that Computer isn't Nearby any more";
        return;
    }
    const Helper& h = s.nearby[req.index];
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
        if (savePairing(pr)) {
            g_pairing = pr;
            s.paired = pr.name;
            s.message = "Paired with " + pr.name;
        } else {
            s.message = "Paired, but couldn't save it";
        }
    } else if (r.status == 401) {
        s.message = "Wrong Pairing code";
    } else if (r.status == 404) {
        s.message = "No pairing window: run phrased pair";
    } else {
        s.message = "Pairing failed (" + std::to_string(r.status) + ")";
    }
    Serial.printf("net: pair %s: %s\n", h.name.c_str(), s.message.c_str());
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
    JsonDocument doc;
    doc["text"] = req.text;
    std::string json;
    serializeJson(doc, json);
    const helper_client::Reply r = helper_client::post(
        address, port, "/type", json, g_pairing.token,
        [](const std::string& cert) { return pairing::matchesPin(g_pairing.pin, cert); });
    switch (send::outcome(r.sent, r.status)) {
        case send::Outcome::Typed:
            s.message = "Typed";
            break;
        case send::Outcome::MaybeTyped:
            s.message = "Maybe typed: check the Computer before tapping again";
            break;
        case send::Outcome::NotTyped: {
            std::string why = r.pinMismatch ? "Pin doesn't match" : errorText(r.body);
            if (why.empty()) why = r.status ? std::to_string(r.status) : "can't reach " + g_pairing.name;
            s.message = "Not typed: " + why;
            break;
        }
    }
    Serial.printf("net: type: %s\n", s.message.c_str());
}

void handleRequests(Snapshot& s, TickType_t wait) {
    Request* req = nullptr;
    while (xQueueReceive(g_requests, &req, wait) == pdTRUE) {
        if (req->kind == Request::Pair) handlePair(*req, s);
        else handleType(*req, s);
        delete req;
        publish(s);
        wait = 0;
    }
}

void run(void*) {
    Snapshot s;
    loadPairing();
    s.paired = g_pairing.ok() ? g_pairing.name : "";
    s.wifiConfigured = sizeof(WIFI_SSID) > 1;
    publish(s);
    if (!s.wifiConfigured) {
        Serial.println("net: no Wi-Fi in this build");
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
    // 16 KB: the TLS client runs on this task too (mbedTLS handshakes are stack hungry).
    return xTaskCreatePinnedToCore(run, "net", 16384, nullptr, 1, nullptr, 0) == pdPASS;
}

unsigned version() { return g_version.load(); }

Snapshot snapshot() {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    Snapshot s = g_snap;
    xSemaphoreGive(g_lock);
    return s;
}

namespace {

void enqueue(Request* req) {
    if (g_requests == nullptr || xQueueSend(g_requests, &req, 0) != pdTRUE) delete req;  // full: drop
}

}  // namespace

void pair(size_t index, const std::string& code, const std::string& shortPin) {
    enqueue(new Request{Request::Pair, index, code, shortPin, ""});
}

void type(const std::string& text) { enqueue(new Request{Request::Type, 0, "", "", text}); }

}  // namespace net
