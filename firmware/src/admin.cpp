#include "admin.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_random.h>
#include <mbedtls/pkcs5.h>

#include "admin_password.h"
#include "lockout.h"
#include "panel_store.h"

namespace admin {
namespace {

// Measured on the S3 (#98): 572 ms per set or check at this count.
constexpr uint32_t kIterations = 10000;

admin_password::Record g_record;
bool g_set = false;
lockout::State g_lockout;

void pbkdf2(const std::string& password, const uint8_t* salt, size_t saltLen, uint32_t iterations, uint8_t* out) {
    mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, reinterpret_cast<const unsigned char*>(password.data()),
                                  password.size(), salt, saltLen, iterations, admin_password::kHashLen, out);
}

void saveFailures() {
    Preferences p;
    if (p.begin("admin", false, panel_store::kPartition)) p.putUInt("failures", g_lockout.failures);
}

}  // namespace

void begin() {
    Preferences p;
    if (!p.begin("admin", true, panel_store::kPartition)) return;
    uint8_t blob[admin_password::kBlobLen];
    g_set = p.getBytes("password", blob, sizeof blob) == sizeof blob &&
            admin_password::fromBlob(blob, sizeof blob, g_record);
    g_lockout = lockout::loaded(p.getUInt("failures", 0), millis());
}

bool isSet() { return g_set; }

bool set(const std::string& password) {
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
    return true;
}

Check check(const std::string& password, uint32_t& waitMs) {
    waitMs = lockout::waitMs(g_lockout, millis());
    if (waitMs > 0) return Check::Wait;
    if (g_set && admin_password::check(g_record, password, pbkdf2)) {
        if (g_lockout.failures > 0) {
            lockout::clear(g_lockout);
            saveFailures();
        }
        return Check::Ok;
    }
    lockout::failed(g_lockout, millis());
    saveFailures();
    return Check::Wrong;
}

}  // namespace admin
