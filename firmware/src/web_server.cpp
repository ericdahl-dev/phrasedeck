#include "web_server.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_https_server.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <mbedtls/ecp.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <mbedtls/x509_crt.h>

#include "admin.h"
#include "panel_store.h"
#include "web.h"

namespace web_server {
namespace {

SemaphoreHandle_t g_lock = nullptr;
std::string g_fingerprint;  // under g_lock
std::string g_key, g_cert;  // PEM, kept for the server's lifetime
std::string g_ip;           // the address the server answers to
web::Session g_session;     // only touched on the httpd task

int random(void*, unsigned char* out, size_t len) {
    esp_fill_random(out, len);  // hardware RNG; Wi-Fi is up, so it has RF entropy
    return 0;
}

// A new P-256 key and a self-signed cert for it, as PEM. False on any mbedTLS error.
bool makeCert(std::string& keyPem, std::string& certPem) {
    mbedtls_pk_context key;
    mbedtls_x509write_cert crt;
    mbedtls_pk_init(&key);
    mbedtls_x509write_crt_init(&crt);
    static unsigned char buf[2048];
    unsigned char serial[16];
    esp_fill_random(serial, sizeof serial);
    serial[0] &= 0x7F;  // positive
    const bool ok =
        mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) == 0 &&
        mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key), random, nullptr) == 0 &&
        mbedtls_pk_write_key_pem(&key, buf, sizeof buf) == 0 && (keyPem = reinterpret_cast<char*>(buf), true) &&
        (mbedtls_x509write_crt_set_subject_key(&crt, &key), mbedtls_x509write_crt_set_issuer_key(&crt, &key), true) &&
        mbedtls_x509write_crt_set_subject_name(&crt, "CN=Phrasedeck panel") == 0 &&
        mbedtls_x509write_crt_set_issuer_name(&crt, "CN=Phrasedeck panel") == 0 &&
        mbedtls_x509write_crt_set_serial_raw(&crt, serial, sizeof serial) == 0 &&
        mbedtls_x509write_crt_set_validity(&crt, "20250101000000", "20491231235959") == 0 &&
        mbedtls_x509write_crt_set_basic_constraints(&crt, 0, -1) == 0 &&
        (mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256), true) &&
        mbedtls_x509write_crt_pem(&crt, buf, sizeof buf, random, nullptr) == 0 &&
        (certPem = reinterpret_cast<char*>(buf), true);
    mbedtls_platform_zeroize(buf, sizeof buf);  // held the private key
    mbedtls_x509write_crt_free(&crt);
    mbedtls_pk_free(&key);
    return ok;
}

// Loads the key and cert, or makes and saves them the first time. Factory reset must wipe "web"
// too, so a reset panel gets a new fingerprint (#98).
bool loadOrMakeCert() {
    Preferences p;
    if (!p.begin("web", false, panel_store::kPartition)) return false;
    g_key = p.getString("key", "").c_str();
    g_cert = p.getString("cert", "").c_str();
    if (!g_key.empty() && !g_cert.empty()) return true;
    const uint32_t start = millis();
    if (!makeCert(g_key, g_cert)) return false;
    Serial.printf("web: made a key and cert in %u ms\n", static_cast<unsigned>(millis() - start));
    return p.putString("key", g_key.c_str()) == g_key.size() && p.putString("cert", g_cert.c_str()) == g_cert.size();
}

std::string certFingerprint(const std::string& pem) {
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    std::string out;
    if (mbedtls_x509_crt_parse(&crt, reinterpret_cast<const unsigned char*>(pem.c_str()), pem.size() + 1) == 0) {
        unsigned char sum[32];
        mbedtls_sha256(crt.raw.p, crt.raw.len, sum, 0);
        char hex[4];
        for (int i = 0; i < 32; ++i) {
            snprintf(hex, sizeof hex, i ? ":%02X" : "%02X", sum[i]);
            out += hex;
        }
    }
    mbedtls_x509_crt_free(&crt);
    return out;
}

std::string header(httpd_req_t* req, const char* name) {
    const size_t len = httpd_req_get_hdr_value_len(req, name);
    if (len == 0 || len > 512) return "";
    std::string v(len + 1, '\0');
    if (httpd_req_get_hdr_value_str(req, name, &v[0], v.size()) != ESP_OK) return "";
    v.resize(len);
    return v;
}

// On every reply: no framing, no sniffing, no caching, no referrer, and only our own CSS.
void securityHeaders(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Content-Security-Policy",
                       "default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; frame-ancestors 'none'; base-uri 'none'");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
}

esp_err_t reply(httpd_req_t* req, const char* status, const std::string& body) {
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    securityHeaders(req);
    Serial.printf("web: %s %s %s\n", req->method == HTTP_GET ? "GET" : "POST", req->uri, status);
    return httpd_resp_send(req, body.data(), body.size());
}

std::string page(const std::string& title, const std::string& body) {
    return "<!doctype html><html lang=en><meta charset=utf-8><meta name=viewport content=\"width=device-width\">"
           "<title>Phrasedeck panel</title><style>body{font:18px system-ui;margin:2em auto;max-width:28em;padding:0 1em;"
           "background:#111418;color:#e6e9ed}input,button{font:inherit;padding:.5em}p.note{color:#f0a020}</style>"
           "<h1>" + title + "</h1>" + body;
}

const char* kLoginForm =
    "<form method=post action=/login><p><label>Admin password<br><input type=password name=password "
    "autocomplete=current-password required></label></p><p><button>Log in</button></p></form>";

bool hostOk(httpd_req_t* req) { return web::hostAllowed(header(req, "Host"), g_ip); }

bool loggedIn(httpd_req_t* req) {
    const std::string token = web::cookieToken(header(req, "Cookie"));
    if (!web::sessionValid(g_session, token, millis())) return false;
    web::touch(g_session, millis());
    return true;
}

esp_err_t getRoot(httpd_req_t* req) {
    if (!hostOk(req)) return reply(req, "421 Misdirected Request", page("Wrong address", ""));
    if (!loggedIn(req)) return reply(req, "200 OK", page("Phrasedeck panel", kLoginForm));
    return reply(req, "200 OK", page("Logged in",
                                      "<p>The Button editor comes in the next firmware update.</p>"
                                      "<form method=post action=/logout><button>Log out</button></form>"));
}

// Every POST: our own Host and Origin, a form body, and not too big.
bool postOk(httpd_req_t* req, std::string& body, esp_err_t& sent) {
    if (!hostOk(req)) {
        sent = reply(req, "421 Misdirected Request", page("Wrong address", ""));
        return false;
    }
    if (!web::originAllowed(header(req, "Origin"), g_ip)) {
        sent = reply(req, "403 Forbidden", page("Not from this page", ""));
        return false;
    }
    if (header(req, "Content-Type") != "application/x-www-form-urlencoded" || req->content_len > 256) {
        sent = reply(req, "400 Bad Request", page("Bad request", ""));
        return false;
    }
    body.assign(req->content_len, '\0');
    for (size_t got = 0; got < body.size();) {
        const int n = httpd_req_recv(req, &body[got], body.size() - got);
        if (n <= 0) {
            sent = ESP_FAIL;
            return false;
        }
        got += n;
    }
    return true;
}

esp_err_t postLogin(httpd_req_t* req) {
    std::string body;
    esp_err_t sent = ESP_OK;
    if (!postOk(req, body, sent)) return sent;
    std::string password = web::formValue(body, "password");
    mbedtls_platform_zeroize(&body[0], body.size());
    if (!admin::isSet()) {
        return reply(req, "403 Forbidden", page("No admin password yet", "<p>Set one on the panel first.</p>"));
    }
    uint32_t wait = 0;
    const admin::Check c = admin::check(password, wait);
    mbedtls_platform_zeroize(&password[0], password.size());
    if (c == admin::Check::Wait) {
        return reply(req, "429 Too Many Requests",
                     page("Phrasedeck panel", "<p class=note>Too many wrong tries: wait " +
                                                  std::to_string((wait + 999) / 1000) + " s.</p>" + kLoginForm));
    }
    if (c == admin::Check::Wrong) {
        return reply(req, "401 Unauthorized", page("Phrasedeck panel", std::string("<p class=note>Wrong password.</p>") + kLoginForm));
    }
    unsigned char raw[web::kTokenLen / 2];
    esp_fill_random(raw, sizeof raw);
    char token[web::kTokenLen + 1];
    for (size_t i = 0; i < sizeof raw; ++i) snprintf(token + 2 * i, 3, "%02x", raw[i]);
    web::startSession(g_session, token, millis());
    const std::string cookie = std::string("s=") + token + "; HttpOnly; Secure; SameSite=Strict; Path=/";
    httpd_resp_set_hdr(req, "Set-Cookie", cookie.c_str());
    httpd_resp_set_hdr(req, "Location", "/");
    return reply(req, "303 See Other", "");
}

esp_err_t postLogout(httpd_req_t* req) {
    std::string body;
    esp_err_t sent = ESP_OK;
    if (!postOk(req, body, sent)) return sent;
    web::endSession(g_session);
    httpd_resp_set_hdr(req, "Set-Cookie", "s=; HttpOnly; Secure; SameSite=Strict; Path=/; Max-Age=0");
    httpd_resp_set_hdr(req, "Location", "/");
    return reply(req, "303 See Other", "");
}

void run(void*) {
    while (WiFi.status() != WL_CONNECTED) vTaskDelay(pdMS_TO_TICKS(500));
    g_ip = WiFi.localIP().toString().c_str();
    if (!loadOrMakeCert()) {
        Serial.println("web: no key or cert, not serving");
        vTaskDelete(nullptr);
    }
    const std::string fp = certFingerprint(g_cert);
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_fingerprint = fp;
    xSemaphoreGive(g_lock);

    httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();
    conf.servercert = reinterpret_cast<const uint8_t*>(g_cert.c_str());
    conf.servercert_len = g_cert.size() + 1;
    conf.prvtkey_pem = reinterpret_cast<const uint8_t*>(g_key.c_str());
    conf.prvtkey_len = g_key.size() + 1;
    conf.httpd.max_open_sockets = 2;  // each TLS session holds ~32 KB of buffers
    conf.httpd.lru_purge_enable = true;
    conf.httpd.stack_size = 12 * 1024;  // TLS handshake and PBKDF2 run on it
    conf.httpd.max_uri_handlers = 4;
    httpd_handle_t server = nullptr;
    const size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (httpd_ssl_start(&server, &conf) != ESP_OK) {
        Serial.println("web: server didn't start");
        vTaskDelete(nullptr);
    }
    const httpd_uri_t routes[] = {
        {"/", HTTP_GET, getRoot, nullptr},
        {"/login", HTTP_POST, postLogin, nullptr},
        {"/logout", HTTP_POST, postLogout, nullptr},
    };
    for (const httpd_uri_t& r : routes) httpd_register_uri_handler(server, &r);
    Serial.printf("web: serving https://%s/, internal heap %u -> %u\n", g_ip.c_str(), static_cast<unsigned>(before),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
    vTaskDelete(nullptr);
}

}  // namespace

bool start() {
    g_lock = xSemaphoreCreateMutex();
    return g_lock != nullptr && xTaskCreate(run, "web", 8192, nullptr, 1, nullptr) == pdPASS;
}

std::string fingerprint() {
    if (g_lock == nullptr) return "";
    xSemaphoreTake(g_lock, portMAX_DELAY);
    std::string fp = g_fingerprint;
    xSemaphoreGive(g_lock);
    return fp;
}

}  // namespace web_server
