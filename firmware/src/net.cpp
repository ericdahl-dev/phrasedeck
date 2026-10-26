#include "net.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <map>

#include "wifi_secrets.h"

namespace net {
namespace {

SemaphoreHandle_t g_lock = nullptr;
Snapshot g_snap;

void publish(const Snapshot& s) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    const unsigned v = g_snap.version + 1;
    g_snap = s;
    g_snap.version = v;
    xSemaphoreGive(g_lock);
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

void run(void*) {
    Snapshot s;
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
            if (!up) s.nearby.clear();
            Serial.printf("net: Wi-Fi %s %s\n", up ? "connected" : "down", s.ip.c_str());
            publish(s);
        }
        if (up && !mdns) mdns = MDNS.begin("phrasedeck-panel");
        if (up && mdns) {
            s.nearby = browse();  // blocks about 3 s; fine on this task
            Serial.printf("net: %u Nearby Helper(s)\n", static_cast<unsigned>(s.nearby.size()));
            publish(s);
            vTaskDelay(pdMS_TO_TICKS(10000));
        } else {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

}  // namespace

void start() {
    g_lock = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(run, "net", 8192, nullptr, 1, nullptr, 0);
}

Snapshot snapshot() {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    Snapshot s = g_snap;
    xSemaphoreGive(g_lock);
    return s;
}

}  // namespace net
