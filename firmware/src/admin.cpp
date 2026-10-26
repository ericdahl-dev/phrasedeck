#include "admin.h"

#include <Arduino.h>
#include <Preferences.h>

#include <atomic>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <mbedtls/pkcs5.h>

#include "admin_password.h"
#include "lockout.h"
#include "panel_store.h"

namespace admin {
namespace {

// Measured on the S3 (#98): 572 ms per set or check at this count.
constexpr uint32_t kIterations = 10000;

// The touchscreen (LVGL loop) and the web login (httpd task) share the record and the lockout.
// Static, so it can't fail and exists before any caller, whatever the init order.
StaticSemaphore_t g_mutexBuf;
SemaphoreHandle_t g_mutex = xSemaphoreCreateMutexStatic(&g_mutexBuf);
struct Hold {
    Hold() { xSemaphoreTake(g_mutex, portMAX_DELAY); }
    ~Hold() { xSemaphoreGive(g_mutex); }
};

admin_password::Record g_record;
std::atomic<bool> g_set{false};  // read without the mutex, so the UI never waits on a web PBKDF2
lockout::State g_lockout[2];  // by From

bool pbkdf2(const std::string& password, const uint8_t* salt, size_t saltLen, uint32_t iterations, uint8_t* out) {
    return mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, reinterpret_cast<const unsigned char*>(password.data()),
                                         password.size(), salt, saltLen, iterations, admin_password::kHashLen,
                                         out) == 0;
}

const char* const kFailureKeys[2] = {"failures", "webfails"};

bool saveFailures(From from) {
    const int i = static_cast<int>(from);
    Preferences p;
    return p.begin("admin", false, panel_store::kPartition) && p.putUInt(kFailureKeys[i], g_lockout[i].failures) == 4;
}

}  // namespace

void begin() {
    Hold hold;
    Preferences p;
    if (!p.begin("admin", true, panel_store::kPartition)) return;
    uint8_t blob[admin_password::kBlobLen];
    g_set = p.getBytes("password", blob, sizeof blob) == sizeof blob &&
            admin_password::fromBlob(blob, sizeof blob, g_record);
    for (int i = 0; i < 2; ++i) g_lockout[i] = lockout::loaded(p.getUInt(kFailureKeys[i], 0), millis());
}

bool isSet() { return g_set; }

bool set(const std::string& password) {
    Hold hold;
    uint8_t salt[admin_password::kSaltLen];
    esp_fill_random(salt, sizeof salt);
    admin_password::Record r;
    const uint32_t start = millis();
    if (!admin_password::make(password, salt, kIterations, pbkdf2, r)) return false;
    Serial.printf("admin: PBKDF2 x%u took %u ms\n", static_cast<unsigned>(kIterations),
                  static_cast<unsigned>(millis() - start));
    uint8_t blob[admin_password::kBlobLen];
    admin_password::toBlob(r, blob);
    Preferences p;
    if (!p.begin("admin", false, panel_store::kPartition) || p.putBytes("password", blob, sizeof blob) != sizeof blob) {
        return false;
    }
    g_record = r;
    g_set = true;
    for (From from : {From::Panel, From::Web}) {  // tries made before a password existed don't count against it
        lockout::clear(g_lockout[static_cast<int>(from)]);
        saveFailures(from);
    }
    return true;
}

Check check(const std::string& password, uint32_t& waitMs, From from) {
    Hold hold;
    lockout::State& lock = g_lockout[static_cast<int>(from)];
    waitMs = lockout::waitMs(lock, millis());
    if (waitMs > 0) return Check::Wait;
    if (!g_set) return Check::Wrong;
    // Counted as wrong before the check and cleared after, so a power cut mid-check still counts,
    // and a count that can't be saved fails closed: a reboot must not hand out free tries.
    lockout::failed(lock, millis());
    if (!saveFailures(from)) {
        waitMs = 30000;
        return Check::Wait;
    }
    if (!admin_password::check(g_record, password, pbkdf2)) return Check::Wrong;
    lockout::clear(lock);
    saveFailures(from);
    return Check::Ok;
}

}  // namespace admin
