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
#include "phrases.h"
#include "web.h"
#include "web_editor.h"

namespace web_server {
namespace {

// Static, so publish() from setup() works before start(), and creating it can't fail.
StaticSemaphore_t g_lockBuf;
SemaphoreHandle_t g_lock = xSemaphoreCreateMutexStatic(&g_lockBuf);
std::string g_fingerprint;  // under g_lock
std::string g_listJson;     // under g_lock: the Buttons as the loop last saved them
// One change at a time from the page to the loop, under g_lock (httpd is one task).
web::Edit g_edit;
bool g_editWaiting = false;
uint32_t g_editSeq = 0, g_doneSeq = 0;
bool g_doneOk = false;

struct Locked {
    Locked() { xSemaphoreTake(g_lock, portMAX_DELAY); }
    ~Locked() { xSemaphoreGive(g_lock); }
};
std::string g_key, g_cert;  // PEM, kept for the server's lifetime
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
    serial[0] = (serial[0] & 0x7F) | 0x01;  // positive, and no leading zero byte (non-minimal DER)
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
    Serial.printf("web: %s %s %s, httpd stack left %u\n", req->method == HTTP_GET ? "GET" : "POST", req->uri, status,
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    return httpd_resp_send(req, body.data(), body.size());
}

std::string page(const std::string& title, const std::string& body) {
    return "<!doctype html><html lang=en><meta charset=utf-8><meta name=viewport content=\"width=device-width\">"
           "<title>Phrasedeck panel</title><style>body{font:18px system-ui;margin:2em auto;max-width:28em;padding:0 1em;"
           "background:#111418;color:#e6e9ed}input,button,textarea{font:inherit;padding:.5em}input,textarea{width:100%;"
           "box-sizing:border-box}pre{white-space:pre-wrap;background:#1c2026;padding:.5em}summary{font-size:1.2em;"
           "padding:.4em 0;cursor:pointer}p.note{color:#f0a020}</style>"
           "<h1>" + title + "</h1>" + body;
}

const char* kLoginForm =
    "<form method=post action=/login><p><label>Admin password<br><input type=password name=password "
    "autocomplete=current-password required></label></p><p><button>Log in</button></p></form>";

// Read per request, so a new DHCP lease doesn't lock the browser out until a reboot.
std::string ip() { return WiFi.localIP().toString().c_str(); }

bool hostOk(httpd_req_t* req) { return web::hostAllowed(header(req, "Host"), ip()); }

bool loggedIn(httpd_req_t* req) {
    const std::string token = web::cookieToken(header(req, "Cookie"));
    if (!web::sessionValid(g_session, token, millis())) return false;
    web::touch(g_session, millis());
    return true;
}

// Sent a Button at a time: a full list can escape to ~1 MB, too much to build whole (#137 review).
esp_err_t editor(httpd_req_t* req, const char* status, const std::string& note) {
    std::string json;
    {
        Locked l;
        json = g_listJson;
    }
    phrases::List list;
    if (!list.fromJson(json)) return reply(req, "503 Service Unavailable", page("Buttons", "<p>Still starting: reload in a moment.</p>"));
    json.clear();
    json.shrink_to_fit();
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    securityHeaders(req);
    Serial.printf("web: %s %s %s (editor)\n", req->method == HTTP_GET ? "GET" : "POST", req->uri, status);
    const auto chunk = [req](const std::string& s) { return httpd_resp_send_chunk(req, s.data(), s.size()) == ESP_OK; };
    bool ok = chunk(page("Buttons", web::editorHead(list, note)));
    for (size_t i = 0; ok && i < list.size(); ++i) ok = chunk(web::editorButton(list.at(i)));
    ok = ok && chunk(web::editorTail());
    return ok ? httpd_resp_send_chunk(req, nullptr, 0) : ESP_FAIL;
}

esp_err_t getRoot(httpd_req_t* req) {
    if (req->content_len > 0) return ESP_FAIL;  // a GET has no body; don't drain one
    if (!hostOk(req)) return reply(req, "421 Misdirected Request", page("Wrong address", ""));
    if (!loggedIn(req)) return reply(req, "200 OK", page("Phrasedeck panel", kLoginForm));
    return editor(req, "200 OK", "");
}

// Every POST: our own Host and Origin, a form body, and not too big.
bool postOk(httpd_req_t* req, std::string& body, esp_err_t& sent, size_t maxBody = 256) {
    // First, before any reply: a reply makes httpd drain the body, which a trickle can drag out.
    if (req->content_len > maxBody) {
        sent = ESP_FAIL;  // close without draining, so a trickled huge body can't hold the one httpd task
        return false;
    }
    if (!hostOk(req)) {
        sent = reply(req, "421 Misdirected Request", page("Wrong address", ""));
        return false;
    }
    if (!web::originAllowed(header(req, "Origin"), ip())) {
        sent = reply(req, "403 Forbidden", page("Not from this page", ""));
        return false;
    }
    if (header(req, "Content-Type") != "application/x-www-form-urlencoded") {
        sent = reply(req, "400 Bad Request", page("Bad request", ""));
        return false;
    }
    body.assign(req->content_len, '\0');
    const uint32_t start = millis();
    for (size_t got = 0; got < body.size();) {
        const int n = millis() - start > 10000 ? -1 : httpd_req_recv(req, &body[got], body.size() - got);
        if (n <= 0) {  // closed, failed, or trickled past 10 s: a slow body can't hold the one httpd task
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
    const admin::Check c = admin::check(password, wait, admin::From::Web);
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
    const std::string cookie = std::string("__Host-s=") + token + "; HttpOnly; Secure; SameSite=Strict; Path=/";
    httpd_resp_set_hdr(req, "Set-Cookie", cookie.c_str());
    httpd_resp_set_hdr(req, "Location", "/");
    return reply(req, "303 See Other", "");
}

// A Phrase of 2,000 bytes, each byte a %0D%0A Enter at worst, plus a %-escaped label and the fields.
constexpr size_t kMaxEditBody = 2000 * 6 + 40 * 3 + 256;

esp_err_t postEdit(httpd_req_t* req) {
    // The session first, before the body is read: without one, close at once (a reply would drain it).
    if (!loggedIn(req)) {
        if (req->content_len > 256) return ESP_FAIL;
        return reply(req, "401 Unauthorized", page("Phrasedeck panel", "<p class=note>Log in again.</p>" + std::string(kLoginForm)));
    }
    std::string body;
    esp_err_t sent = ESP_OK;
    if (!postOk(req, body, sent, kMaxEditBody)) return sent;
    web::Edit e;
    if (!web::editFrom(body, e)) return reply(req, "400 Bad Request", page("Bad request", ""));
    uint32_t seq;
    {
        Locked l;
        g_edit = e;
        g_editWaiting = true;
        seq = ++g_editSeq;
    }
    // The loop takes it within a few ms unless a Send or pair is in flight; wait for the result.
    // Decided under the lock, replied after it: editor() takes the lock too, and it isn't recursive.
    enum { Waiting, Done, Refused, Withdrawn } outcome = Waiting;
    for (uint32_t waited = 0; outcome == Waiting; waited += 20) {
        {
            Locked l;
            if (g_doneSeq == seq) {
                outcome = g_doneOk ? Done : Refused;
            } else if (waited >= 5000 && g_editWaiting) {
                g_editWaiting = false;  // withdrawn, so it can't apply after this reply
                outcome = Withdrawn;
            }
        }
        if (outcome == Waiting) vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (outcome == Done) {
        httpd_resp_set_hdr(req, "Location", "/");
        return reply(req, "303 See Other", "");
    }
    if (outcome == Withdrawn) {
        return editor(req, "503 Service Unavailable", "The panel is busy sending or pairing: nothing changed, try again.");
    }
    return editor(req, "409 Conflict",
                  "Nothing changed: that Button was changed or removed meanwhile, or it's over the limits "
                  "(48 Buttons, a 40-byte label, a 2,000-byte Phrase, neither empty).");
}

esp_err_t postLogout(httpd_req_t* req) {
    std::string body;
    esp_err_t sent = ESP_OK;
    if (!postOk(req, body, sent)) return sent;
    web::endSession(g_session);
    httpd_resp_set_hdr(req, "Set-Cookie", "__Host-s=; HttpOnly; Secure; SameSite=Strict; Path=/; Max-Age=0");
    httpd_resp_set_hdr(req, "Location", "/");
    return reply(req, "303 See Other", "");
}

// Returns instead of deleting itself mid-way, so locals are destroyed (run deletes the task).
void serve() {
    while (WiFi.status() != WL_CONNECTED) vTaskDelay(pdMS_TO_TICKS(500));
    if (!loadOrMakeCert()) {
        Serial.println("web: no key or cert, not serving");
        return;
    }
    httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();
    conf.servercert = reinterpret_cast<const uint8_t*>(g_cert.c_str());
    conf.servercert_len = g_cert.size() + 1;
    conf.prvtkey_pem = reinterpret_cast<const uint8_t*>(g_key.c_str());
    conf.prvtkey_len = g_key.size() + 1;
    conf.httpd.max_open_sockets = 2;  // each TLS session holds ~32 KB of buffers
    conf.httpd.lru_purge_enable = true;
    conf.httpd.stack_size = 12 * 1024;  // TLS handshake and PBKDF2 run on it
    conf.httpd.task_priority = 1;       // like the net task: TLS and PBKDF2 never preempt the UI
    conf.httpd.core_id = 0;             // the LVGL loop runs on core 1
    conf.httpd.max_uri_handlers = 4;
    httpd_handle_t server = nullptr;
    const size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (httpd_ssl_start(&server, &conf) != ESP_OK) {
        Serial.println("web: server didn't start");
        return;
    }
    const httpd_uri_t routes[] = {
        {"/", HTTP_GET, getRoot, nullptr},
        {"/login", HTTP_POST, postLogin, nullptr},
        {"/logout", HTTP_POST, postLogout, nullptr},
        {"/edit", HTTP_POST, postEdit, nullptr},
    };
    for (const httpd_uri_t& r : routes) {
        if (httpd_register_uri_handler(server, &r) != ESP_OK) {
            Serial.printf("web: couldn't register %s\n", r.uri);
            httpd_ssl_stop(server);
            return;
        }
    }
    const std::string fp = certFingerprint(g_cert);  // published only once the server is up
    Locked l;
    g_fingerprint = fp;
    Serial.printf("web: serving https://%s/, internal heap %u -> %u, stack left %u\n", ip().c_str(),
                  static_cast<unsigned>(before), static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

void run(void*) {
    serve();
    vTaskDelete(nullptr);
}

}  // namespace

bool start() {
    return xTaskCreate(run, "web", 8192, nullptr, 1, nullptr) == pdPASS;
}

std::string fingerprint() {
    Locked l;
    return g_fingerprint;
}

void publish(const std::string& json) {
    Locked l;
    g_listJson = json;
}

bool takeEdit(web::Edit& e, uint32_t& seq) {
    Locked l;
    if (!g_editWaiting) return false;
    g_editWaiting = false;
    e = g_edit;
    seq = g_editSeq;
    return true;
}

void editDone(uint32_t seq, bool ok) {
    Locked l;
    g_doneSeq = seq;
    g_doneOk = ok;
}

}  // namespace web_server
