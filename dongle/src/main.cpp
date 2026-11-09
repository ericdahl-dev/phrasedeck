// Phrasedeck USB dongle spike (#198). Tasks:
//   NimBLE host  Send writes -> phrase_link::Receiver -> a job for the typing task; Stop
//   typing       one Send at a time on the USB keyboard, then its Result notify and the speed log
//   loop         BOOT long press, serial commands (heap, speed, interval, status, forget)
#include <Arduino.h>
#include <Preferences.h>
#include <esp_heap_caps.h>

#include <atomic>
#include <string>

#include "ble_link.h"
#include "dongle_trust.h"
#include "phrase_link.h"
#include "usb_keyboard.h"
#include "usb_typing.h"

namespace {

constexpr int kBootPin = 0;  // BOOT, active low
constexpr uint32_t kResetHoldMs = 5000;
constexpr uint32_t kTypingStack = 8192;
#ifndef DONGLE_KEY_INTERVAL_MS
#define DONGLE_KEY_INTERVAL_MS 1  // see README: try 0 (one report per USB frame) on each Computer
#endif

// The one Send being typed. The task that wins g_typing (false -> true) fills it and wakes the
// typing task, which clears g_typing when done; nobody else touches it in between.
struct Job {
    std::string text;
    uint8_t id = 0;
    bool enter = false;
    bool fromPhone = false;  // false: the serial speed test, with no Result to notify
};
Job g_job;
std::atomic<bool> g_typing{false};
std::atomic<bool> g_stop{false};
std::atomic<uint32_t> g_interval{DONGLE_KEY_INTERVAL_MS};
TaskHandle_t g_typingTask = nullptr;

phrase_link::Receiver g_receiver;  // host task only

void logHeap(const char* when) {
    Serial.printf("heap (%s): internal free %u, largest %u, least since boot %u; psram free %u; typing stack left %u\n",
                  when, heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                  g_typingTask == nullptr ? 0u : unsigned(uxTaskGetStackHighWaterMark(g_typingTask) * sizeof(StackType_t)));
}

// Claims the typing task for one Send. False if one is typing already.
bool startJob(const std::string& text, uint8_t id, bool enter, bool fromPhone) {
    bool idle = false;
    if (!g_typing.compare_exchange_strong(idle, true)) return false;
    g_job.text.assign(text);  // reserved at boot: no allocation for a Phrase up to the limit
    g_job.id = id;
    g_job.enter = enter;
    g_job.fromPhone = fromPhone;
    g_stop.store(false);  // here, not in the typing task, so a Stop right after can't be lost
    xTaskNotifyGive(g_typingTask);
    return true;
}

// NimBLE host task: one write from the phone.
void onSendWrite(const uint8_t* data, size_t len, const phrase_link::Token& token) {
    const phrase_link::Event e = g_receiver.feed(data, len, token);
    switch (e.kind) {
        case phrase_link::Event::Kind::None: return;
        case phrase_link::Event::Kind::Ready:
            if (!startJob(g_receiver.text(), e.id, e.enter, true)) {
                ble_link::notify(phrase_link::result(e.id, phrase_link::Code::Busy, ""));
            }
            return;
        case phrase_link::Event::Kind::Done:
            Serial.printf("send %u: not typed, code %u\n", e.id, unsigned(e.code));
            ble_link::notify(phrase_link::result(e.id, e.code, ""));
            return;
        case phrase_link::Event::Kind::Stop:
            if (g_typing.load() && g_job.fromPhone && g_job.id == e.id) g_stop.store(true);
            return;
    }
}

void typingTask(void*) {
    usb_keyboard::Port usb;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        const uint32_t interval = g_interval.load();
        send::KeyStats stats;
        const usb_typing::Outcome out = usb_typing::type(g_job.text, g_job.enter, usb, g_stop, interval, &stats);
        if (g_job.fromPhone) ble_link::notify(phrase_link::result(g_job.id, out.code, out.bad));
        Serial.printf("send %u: code %u%s%s\n", g_job.id, unsigned(out.code), out.bad.empty() ? "" : ", can't type ",
                      out.bad.c_str());
        if (stats.keys > 0) Serial.println(usb_typing::speedLine(stats, interval).c_str());
        logHeap("after a Send");
        g_typing.store(false);
    }
}

void loadInterval() {
    Preferences p;
    if (!p.begin("dongle-dev", true)) return;
    g_interval.store(p.getUInt("interval", DONGLE_KEY_INTERVAL_MS));
    p.end();
}

void saveInterval(uint32_t ms) {
    g_interval.store(ms);
    Preferences p;
    if (!p.begin("dongle-dev", false)) return;
    p.putUInt("interval", ms);
    p.end();
}

void status() {
    usb_keyboard::Port usb;
    Serial.printf("status: usb %s, phone %s, key interval %u ms, %s\n", usb.ready() ? "ready" : "not ready",
                  ble_link::phoneConnected() ? "connected" : ble_link::hasPhone() ? "bonded, not connected" : "none yet",
                  unsigned(g_interval.load()), g_typing.load() ? "typing" : "idle");
}

// Serial commands, one per line, over the USB CDC port.
void command(const String& line) {
    if (line == "heap") {
        logHeap("now");
    } else if (line == "status") {
        status();
    } else if (line == "stop") {
        g_stop.store(true);
    } else if (line == "forget") {
        ble_link::forgetPhoneAndRestart();
    } else if (line.startsWith("interval ")) {
        const long ms = line.substring(9).toInt();
        if (ms < 0 || ms > 100) {
            Serial.println("interval: 0 to 100 ms");
            return;
        }
        saveInterval(uint32_t(ms));
        Serial.printf("key interval %ld ms (saved)\n", ms);
    } else if (line == "speed" || line.startsWith("speed ")) {
        long n = line.length() > 6 ? line.substring(6).toInt() : 2000;
        if (n < 1 || n > long(phrase_link::kMaxPhrase)) n = 2000;
        // Time to click into an empty text editor on the Computer first.
        Serial.printf("speed: typing %ld characters in 5 s: click into an empty text editor\n", n);
        delay(5000);
        if (!startJob(usb_typing::speedPhrase(size_t(n)), 0, false, false)) Serial.println("speed: busy");
    } else if (line.length() > 0) {
        Serial.println("commands: heap, status, speed [n], interval <ms>, stop, forget");
    }
}

String g_line;
dongle_trust::Hold g_hold(kResetHoldMs);

}  // namespace

void setup() {
    pinMode(kBootPin, INPUT_PULLUP);
    g_job.text.reserve(phrase_link::kMaxPhrase);
    usb_keyboard::start();  // USB itself is up already: the core starts it for CDC on boot
    loadInterval();
    if (xTaskCreate(typingTask, "typing", kTypingStack, nullptr, 5, &g_typingTask) != pdPASS) {
        Serial.println("couldn't start the typing task");
        return;  // no typing: don't take Sends
    }
    if (!ble_link::start(onSendWrite)) Serial.println("ble: couldn't start");
    status();
    logHeap("boot");
}

void loop() {
    if (g_hold.update(digitalRead(kBootPin) == LOW, millis())) {
        Serial.println("BOOT held 5 s: forgetting the phone");
        ble_link::forgetPhoneAndRestart();
    }
    while (Serial.available() > 0) {
        const char c = char(Serial.read());
        if (c == '\n' || c == '\r') {
            g_line.trim();
            command(g_line);
            g_line = "";
        } else if (g_line.length() < 64) {
            g_line += c;
        }
    }
    delay(20);
}
