#include "phone_ble.h"

#ifdef PANEL_PHONE_BLE

#include <Arduino.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>
#include <host/ble_hs_mbuf.h>
#include <host/ble_store.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>

#include "ble_keyboard.h"
#include "bt_pairing.h"
#include "net.h"
#include "panel_store.h"
#include "phone_link.h"

namespace phone_ble {
namespace {

constexpr int kNone = -1;

// The Phrase service and its characteristics: 5e1f00NN-8b5a-4c2e-9d3b-7a6f2c1e0d99, NN = 01 service,
// 02 Phrase (write), 03 Status (read, notify), 04 Token (read, once). NimBLE wants the bytes reversed.
#define PHONE_UUID(n) \
    BLE_UUID128_INIT(0x99, 0x0d, 0x1e, 0x2c, 0x6f, 0x7a, 0x3b, 0x9d, 0x2e, 0x4c, 0x5a, 0x8b, n, 0x00, 0x1f, 0x5e)
const ble_uuid128_t kServiceUuid = PHONE_UUID(0x01);
const ble_uuid128_t kPhraseUuid = PHONE_UUID(0x02);
const ble_uuid128_t kStatusUuid = PHONE_UUID(0x03);
const ble_uuid128_t kTokenUuid = PHONE_UUID(0x04);

uint16_t g_phraseHandle = 0;
uint16_t g_statusHandle = 0;
uint16_t g_tokenHandle = 0;

std::atomic<bool> g_started{false};
std::atomic<int> g_conn{kNone};      // the phone identity's one connection
std::atomic<int> g_trusted{kNone};   // g_conn, once it's the bonded phone on its own key
std::atomic<int> g_pairing{kNone};   // the connection showing a passkey now
std::atomic<int32_t> g_passkey{kNone};
std::atomic<uint32_t> g_connectedAt{0};
std::atomic<bool> g_hasPhone{false};

portMUX_TYPE g_windowLock = portMUX_INITIALIZER_UNLOCKED;
bt_pairing::Window g_window;  // under g_windowLock: openWindow() runs on the loop task

bool windowIsOpen() {
    portENTER_CRITICAL(&g_windowLock);
    const bool open = g_window.isOpen(millis());
    portEXIT_CRITICAL(&g_windowLock);
    return open;
}

void closeWindow() {
    portENTER_CRITICAL(&g_windowLock);
    g_window.close();
    portEXIT_CRITICAL(&g_windowLock);
}

// Host task only, after start() (bondedPhone() reads NVS itself, before).
ble_addr_t g_phone{};
uint8_t g_phoneLtk[16];
bool g_hasLtk = false;
phone_link::Token g_token{};
phone_link::TokenOffer g_offer;
phone_link::Assembler g_assembler;
uint8_t g_ourKeyDist = 0;  // ble_keyboard's key distribution, back after a phone link
bool g_forget = false;     // a Just Works bond made on g_conn: its keys go at disconnect
ble_addr_t g_forgetAddr{};

// Free internal RAM (what NimBLE, Wi-Fi and TLS use) and PSRAM at each step of a phone link, so the
// cost of the second connection can be read off the log (#199).
void heapLog(const char* what) {
    Serial.printf("phone: heap %s: internal free %u, largest %u, min %u; PSRAM free %u\n", what,
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

bool loadPhone(ble_addr_t& peer, phone_link::Token& token) {
    Preferences p;
    uint8_t raw[7];
    if (!p.begin("phone", true, panel_store::kPartition)) return false;
    const bool ok = p.getBytes("peer", raw, sizeof raw) == sizeof raw &&
                    p.getBytes("token", token.data(), token.size()) == token.size();
    p.end();
    if (!ok) return false;
    peer.type = raw[0];
    memcpy(peer.val, raw + 1, 6);
    return true;
}

// The Token last, so a save that fails partway leaves no phone (without a Token it doesn't load).
bool savePhone(const ble_addr_t& peer, const phone_link::Token& token) {
    uint8_t raw[7] = {peer.type};
    memcpy(raw + 1, peer.val, 6);
    Preferences p;
    if (!p.begin("phone", false, panel_store::kPartition)) return false;
    p.remove("peer");
    p.remove("token");
    const bool ok = p.putBytes("peer", raw, sizeof raw) == sizeof raw &&
                    p.putBytes("token", token.data(), token.size()) == token.size();
    p.end();
    return ok;
}

// The phone identity's random static address: made once, kept in the panel NVS so a bonded phone finds
// the panel again after a restart, and forgotten by a factory reset.
bool ourAddress(ble_addr_t& out) {
    Preferences p;
    if (!p.begin("phone", false, panel_store::kPartition)) return false;
    bool ok = p.getBytes("addr", out.val, 6) == 6;
    if (!ok) ok = ble_hs_id_gen_rnd(0, &out) == 0 && p.putBytes("addr", out.val, 6) == 6;
    p.end();
    out.type = BLE_ADDR_RANDOM;
    return ok;
}

bool storedLtk(const ble_addr_t& addr, uint8_t out[16]) {
    ble_store_key_sec key{};
    key.peer_addr = addr;
    ble_store_value_sec value{};
    if (ble_store_read_our_sec(&key, &value) != 0 || !value.ltk_present) return false;
    memcpy(out, value.ltk, 16);
    return true;
}

bool isPhone(const ble_addr_t& addr) {
    uint8_t ltk[16];
    return g_hasPhone.load() && g_hasLtk && ble_addr_cmp(&addr, &g_phone) == 0 && storedLtk(addr, ltk) &&
           memcmp(ltk, g_phoneLtk, sizeof ltk) == 0;
}

void notifyStatus(uint16_t conn, uint8_t id, send::Outcome outcome, const std::string& message) {
    const uint16_t mtu = ble_att_mtu(conn);
    const auto bytes = phone_link::status(id, outcome, message, mtu > 3 ? mtu - 3 : 20);
    os_mbuf* om = ble_hs_mbuf_from_flat(bytes.data(), bytes.size());
    if (om == nullptr || ble_gatts_notify_custom(conn, g_statusHandle, om) != 0) {  // takes om
        Serial.println("phone: status notify failed");
    }
}

phone_link::Link linkFor(uint16_t conn) {
    phone_link::Link l;
    ble_gap_conn_desc d;
    if (ble_gap_conn_find(conn, &d) != 0) return l;
    l.phoneIdentity = static_cast<int>(conn) == g_conn.load() && d.our_ota_addr.type == BLE_ADDR_RANDOM;
    l.encrypted = d.sec_state.encrypted;
    l.authenticated = d.sec_state.authenticated;
    l.bonded = d.sec_state.bonded;
    l.keySize = d.sec_state.key_size;
    l.storedPhone = static_cast<int>(conn) == g_trusted.load();
    return l;
}

// One Send waits here for the phone task: the host task can't block on the request channel.
portMUX_TYPE g_mailLock = portMUX_INITIALIZER_UNLOCKED;
bool g_full = false;
uint8_t g_mailId = 0;
bool g_mailEnter = false;
uint16_t g_mailConn = 0;
std::string g_mailText;
TaskHandle_t g_task = nullptr;
StaticTask_t g_taskBuf;
StackType_t g_stack[4096];  // bytes on ESP-IDF: net::type copies the Phrase, the log uses printf

// Host task. False if a Send from the phone is already in flight.
bool post(uint16_t conn, uint8_t id, const std::string& text, bool enter) {
    std::string copy = text;  // allocated outside the critical section
    portENTER_CRITICAL(&g_mailLock);
    const bool free = !g_full;
    if (free) {
        g_full = true;
        g_mailId = id;
        g_mailEnter = enter;
        g_mailConn = conn;
        g_mailText.swap(copy);
    }
    portEXIT_CRITICAL(&g_mailLock);
    if (free) xTaskNotifyGive(g_task);
    return free;
}

uint8_t g_flat[BLE_ATT_ATTR_MAX_LEN];  // host task only

int access(uint16_t conn, uint16_t, ble_gatt_access_ctxt* ctxt, void* arg) {
    const uintptr_t which = reinterpret_cast<uintptr_t>(arg);
    const phone_link::Link link = linkFor(conn);
    // NimBLE has already asked for an encrypted, passkey-paired link (the _ENC and _AUTHEN flags).
    // This is the rest: the phone identity, and the phone paired in the window.
    if (!phone_link::trusted(link)) return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
    switch (which) {
        case 4:  // Token: once, to the connection that just paired
            if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR || !g_offer.take(conn, millis())) {
                return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
            }
            Serial.println("phone: Token read by the phone that paired");
            return os_mbuf_append(ctxt->om, g_token.data(), g_token.size()) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        case 3:  // Status: the notify carries it; a read is empty
            return 0;
        case 2: {  // Phrase
            if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
            uint16_t len = 0;
            if (ble_hs_mbuf_to_flat(ctxt->om, g_flat, sizeof g_flat, &len) != 0) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            switch (g_assembler.feed(g_flat, len)) {
                case phone_link::Fed::More:
                    return 0;
                case phone_link::Fed::Bad:
                    Serial.println("phone: bad write, Send dropped");
                    return 0x80;  // application error: the app starts the Send again with a Begin
                case phone_link::Fed::Done:
                    break;
            }
            const uint8_t id = g_assembler.id();
            switch (phone_link::check(link, g_assembler.token(), g_token)) {
                case phone_link::Verdict::Send:
                    if (!post(conn, id, g_assembler.text(), g_assembler.enter())) {
                        notifyStatus(conn, id, send::Outcome::NotTyped, "Busy: a Send from the phone is in flight");
                    }
                    return 0;
                case phone_link::Verdict::WrongToken:
                    Serial.println("phone: wrong Token, link ended");
                    notifyStatus(conn, id, send::Outcome::NotTyped, "Wrong Token: pair the phone again");
                    ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
                    return 0x81;
                case phone_link::Verdict::NotTrusted:
                    return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
            }
            return BLE_ATT_ERR_UNLIKELY;
        }
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// Encrypted and passkey-authenticated (MITM) before NimBLE calls access(): an unpaired phone gets
// Insufficient Authentication, which makes iOS and Android pair.
const ble_gatt_chr_def kChrs[] = {
    {.uuid = &kPhraseUuid.u, .access_cb = access, .arg = reinterpret_cast<void*>(2),
     .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_AUTHEN,
     .val_handle = &g_phraseHandle},
    {.uuid = &kStatusUuid.u, .access_cb = access, .arg = reinterpret_cast<void*>(3),
     .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_READ_AUTHEN,
     .val_handle = &g_statusHandle},
    {.uuid = &kTokenUuid.u, .access_cb = access, .arg = reinterpret_cast<void*>(4),
     .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_READ_AUTHEN,
     .val_handle = &g_tokenHandle},
    {},
};
const ble_gatt_svc_def kSvcs[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &kServiceUuid.u, .characteristics = kChrs},
    {},
};

// Ends the connection. A Just Works bond it made goes at disconnect; any other keys stay, since they
// can be the Computer's own bond (the store is shared and keyed by the peer, not by our identity).
void reject(const ble_gap_conn_desc& d) {
    if (!d.sec_state.authenticated && d.sec_state.bonded) {
        g_forget = true;
        g_forgetAddr = d.peer_id_addr;
    }
    ble_gap_terminate(d.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
}

void onEncrypted(uint16_t handle) {
    int expected = handle;
    const bool pairedNow = g_pairing.compare_exchange_strong(expected, kNone);
    if (pairedNow) g_passkey.store(kNone);
    ble_gap_conn_desc d;
    if (ble_gap_conn_find(handle, &d) != 0) return;
    const auto& sec = d.sec_state;
    if (!sec.encrypted || !sec.authenticated || !sec.bonded || sec.key_size != 16) {
        Serial.println("phone: link not passkey-paired and bonded, ended");
        reject(d);
        return;
    }
    if (pairedNow) {
        // A passkey showed for it inside the window: this is the phone now, with a new Token.
        uint8_t ltk[16];
        phone_link::Token token{};
        do esp_fill_random(token.data(), token.size());
        while (std::all_of(token.begin(), token.end(), [](uint8_t b) { return b == 0; }));
        if (!storedLtk(d.peer_id_addr, ltk) || !savePhone(d.peer_id_addr, token)) {
            Serial.println("phone: couldn't save the phone's bond");
            reject(d);
            return;
        }
        if (g_hasPhone.load() && ble_addr_cmp(&g_phone, &d.peer_id_addr) != 0) ble_store_util_delete_peer(&g_phone);
        g_phone = d.peer_id_addr;
        memcpy(g_phoneLtk, ltk, sizeof ltk);
        g_hasLtk = true;
        g_token = token;
        g_hasPhone.store(true);
        g_offer.offer(handle, millis());
        closeWindow();
        Serial.println("phone: paired; read the Token characteristic within a minute");
    } else if (!isPhone(d.peer_id_addr)) {
        Serial.println("phone: bonded, but not the paired phone: ended");
        reject(d);
        return;
    }
    g_trusted.store(handle);
    heapLog("phone trusted, both links up");
}

int gapEvent(ble_gap_event* e, void*) {
    switch (e->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (e->connect.status == 0) {
                if (g_conn.load() != kNone) {  // one phone at a time
                    ble_gap_terminate(e->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                    return 0;
                }
                g_conn.store(e->connect.conn_handle);
                g_connectedAt.store(millis());
                // Keep the phone identity apart from the Computer's: don't hand the phone the panel's
                // identity address (the HID one) or its IRK. Read when the pairing request comes.
                ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC;
                heapLog("phone connected");
            }
            ble_keyboard::refreshAdvertising();
            return 0;
        case BLE_GAP_EVENT_DISCONNECT: {
            const int handle = e->disconnect.conn.conn_handle;
            int expected = handle;
            if (!g_conn.compare_exchange_strong(expected, kNone)) return 0;
            expected = handle;
            g_trusted.compare_exchange_strong(expected, kNone);
            expected = handle;
            if (g_pairing.compare_exchange_strong(expected, kNone)) g_passkey.store(kNone);
            g_offer.drop(handle);
            ble_hs_cfg.sm_our_key_dist = g_ourKeyDist;
            if (g_forget) {
                ble_store_util_delete_peer(&g_forgetAddr);
                g_forget = false;
            }
            Serial.printf("phone: disconnected, reason 0x%x\n", e->disconnect.reason);
            heapLog("phone gone");
            ble_keyboard::refreshAdvertising();
            return 0;
        }
        case BLE_GAP_EVENT_PASSKEY_ACTION: {
            // The panel only displays. A passkey shows only inside the window, for the phone's link.
            const uint16_t handle = e->passkey.conn_handle;
            const int pairing = g_pairing.load();
            if (e->passkey.params.action == BLE_SM_IOACT_DISP && static_cast<int>(handle) == g_conn.load() &&
                windowIsOpen() && (pairing == kNone || pairing == handle)) {
                ble_sm_io io{};
                io.action = BLE_SM_IOACT_DISP;
                io.passkey = esp_random() % 1000000;
                if (ble_sm_inject_io(handle, &io) == 0) {
                    g_pairing.store(handle);
                    g_passkey.store(static_cast<int32_t>(io.passkey));
                    return 0;
                }
            }
            Serial.println("phone: pairing outside the window, ended");
            ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
            return 0;
        }
        case BLE_GAP_EVENT_ENC_CHANGE:
            if (e->enc_change.status != 0) {
                Serial.printf("phone: encryption failed: %d\n", e->enc_change.status);
                int expected = e->enc_change.conn_handle;
                if (g_pairing.compare_exchange_strong(expected, kNone)) g_passkey.store(kNone);
                return 0;
            }
            onEncrypted(e->enc_change.conn_handle);
            return 0;
        case BLE_GAP_EVENT_REPEAT_PAIRING: {
            // The phone forgot the panel and pairs again: only inside the window.
            if (!windowIsOpen()) return BLE_GAP_REPEAT_PAIRING_IGNORE;
            ble_gap_conn_desc d;
            if (ble_gap_conn_find(e->repeat_pairing.conn_handle, &d) == 0) ble_store_util_delete_peer(&d.peer_id_addr);
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
        case BLE_GAP_EVENT_MTU:
            Serial.printf("phone: MTU %u\n", e->mtu.value);
            return 0;
        default:
            return 0;
    }
}

// The phone task: runs each Send through the request channel, as a tap does, so it waits its turn,
// and tells the phone how it went. Once a second it also ends a link that sat unencrypted (the
// Computer's 10 s grace) and closes a window that ran out.
void run(void*) {
    for (;;) {
        const bool woke = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)) != 0;
        portENTER_CRITICAL(&g_windowLock);
        const bool lapsed = g_window.lapsed(millis());
        if (lapsed) g_window.close();
        portEXIT_CRITICAL(&g_windowLock);
        if (lapsed) ble_keyboard::refreshAdvertising();
        const int conn = g_conn.load();
        ble_gap_conn_desc d;
        if (conn != kNone && ble_gap_conn_find(conn, &d) == 0 &&
            bt_pairing::dropIdle(d.sec_state.encrypted, g_pairing.load() == conn, windowIsOpen(),
                                 millis() - g_connectedAt.load() < bt_pairing::kGraceMs)) {
            ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
        }
        if (!woke) continue;

        std::string text;
        uint8_t id;
        bool enter;
        uint16_t from;
        portENTER_CRITICAL(&g_mailLock);
        const bool full = g_full;
        text.swap(g_mailText);
        id = g_mailId;
        enter = g_mailEnter;
        from = g_mailConn;
        portEXIT_CRITICAL(&g_mailLock);
        if (!full) continue;

        net::Result r;
        const net::Ticket t = net::type(text, "", 0, enter);
        text.clear();
        text.shrink_to_fit();
        if (t == net::kBusy) {
            r.sent = send::Outcome::NotTyped;
            r.message = "Busy: the panel is sending";
        } else {
            // Only the last finished ticket has a result. If a tap's Send finished after ours before we
            // looked, ours is lost: it may have typed, so it's Maybe typed, never Not typed.
            for (;;) {
                if (net::result(t, r)) break;
                if (!net::busy()) {
                    if (net::result(t, r)) break;
                    r.sent = send::Outcome::MaybeTyped;
                    r.message = "Sent, but its result was lost: check the Computer";
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
        Serial.printf("phone: type: %s (stack left %u)\n", r.message.c_str(),
                      static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
        heapLog("after a phone Send");
        if (g_conn.load() == from) notifyStatus(from, id, r.sent, r.message);
        portENTER_CRITICAL(&g_mailLock);
        g_full = false;
        portEXIT_CRITICAL(&g_mailLock);
    }
}

}  // namespace

bool bondedPhone(uint8_t out[7]) {
    ble_addr_t peer;
    phone_link::Token token;
    if (!loadPhone(peer, token)) return false;
    out[0] = peer.type;
    memcpy(out + 1, peer.val, 6);
    return true;
}

bool start() {
    heapLog("before the phone link");
    ble_addr_t ours;
    if (!ourAddress(ours) || ble_hs_id_set_rnd(ours.val) != 0) {
        Serial.println("phone: couldn't set the phone identity's address");
        return false;
    }
    if (ble_gatts_count_cfg(kSvcs) != 0 || ble_gatts_add_svcs(kSvcs) != 0) {
        Serial.println("phone: couldn't add the Phrase service");
        return false;
    }
    g_ourKeyDist = ble_hs_cfg.sm_our_key_dist;
    if (loadPhone(g_phone, g_token)) {
        g_hasLtk = storedLtk(g_phone, g_phoneLtk);
        g_hasPhone.store(g_hasLtk);  // a phone without its bond can't reconnect: pair it again
    }
    g_task = xTaskCreateStaticPinnedToCore(run, "phone", sizeof g_stack, nullptr, 1, g_stack, &g_taskBuf, 0);
    if (g_task == nullptr) return false;
    g_started.store(true);
    Serial.printf("phone: identity %02x:%02x:%02x:%02x:%02x:%02x (random static), phone %s\n", ours.val[5],
                  ours.val[4], ours.val[3], ours.val[2], ours.val[1], ours.val[0],
                  g_hasPhone.load() ? "bonded" : "not paired");
    heapLog("phone link ready");
    return true;
}

bool connected() { return g_conn.load() != kNone; }
bool bonded() { return g_hasPhone.load(); }
bool windowOpen() { return g_started.load() && windowIsOpen(); }

bool startAdvertising() {
    ble_hs_adv_fields adv{};
    adv.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    adv.uuids128 = &kServiceUuid;
    adv.num_uuids128 = 1;
    adv.uuids128_is_complete = 1;
    static const char kName[] = "Phrasedeck phone link";
    ble_hs_adv_fields rsp{};
    rsp.name = reinterpret_cast<const uint8_t*>(kName);
    rsp.name_len = sizeof kName - 1;
    rsp.name_is_complete = 1;
    ble_gap_adv_params params{};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    const int rc = ble_gap_adv_set_fields(&adv) || ble_gap_adv_rsp_set_fields(&rsp) ||
                   ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, nullptr, BLE_HS_FOREVER, &params, gapEvent, nullptr);
    if (rc != 0) Serial.println("phone: couldn't advertise the phone identity");
    return rc == 0;
}

void openWindow(uint32_t ms) {
    if (!g_started.load()) {
        Serial.println("phonepair: Bluetooth Mode is off");
        return;
    }
    portENTER_CRITICAL(&g_windowLock);
    g_window.open(millis(), ms);
    portEXIT_CRITICAL(&g_windowLock);
    Serial.printf("phonepair: a phone may pair for %u s\n", static_cast<unsigned>(ms / 1000));
    ble_keyboard::refreshAdvertising();
}

int32_t passkey() { return g_passkey.load(); }

}  // namespace phone_ble

#endif  // PANEL_PHONE_BLE
