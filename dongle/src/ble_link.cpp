#include "ble_link.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>
#include <Preferences.h>
#include <esp_random.h>
#include <host/ble_gap.h>
#include <host/ble_hs.h>
#include <host/ble_hs_mbuf.h>
#include <host/ble_store.h>
#include <nimble/nimble_port.h>

#include <atomic>
#include <map>
#include <set>

#include "bt_pairing.h"
#include "dongle_trust.h"

namespace ble_link {
namespace {

const char* const kService = "6b1d0000-5e2a-4c6e-9a51-2f1c8d7e4a10";
const char* const kSendChar = "6b1d0001-5e2a-4c6e-9a51-2f1c8d7e4a10";
const char* const kResultChar = "6b1d0002-5e2a-4c6e-9a51-2f1c8d7e4a10";
const char* const kTokenChar = "6b1d0003-5e2a-4c6e-9a51-2f1c8d7e4a10";
const char* const kStore = "dongle";  // NVS namespace: the phone's address and its Token

constexpr int kNone = -1;

OnWrite g_onWrite = nullptr;
BLEServer* g_server = nullptr;
BLECharacteristic* g_result = nullptr;

std::atomic<bool> g_started{false};
std::atomic<int> g_trusted{kNone};  // the phone's connection: the only one that may send
std::atomic<int> g_pairing{kNone};  // the connection showing a passkey now
std::atomic<bool> g_unmatched{false};

// Host task only after start(): the phone (its identity address and the key it bonded with) and
// its Token. The Token is read on the host task too (Send writes and Token reads both run there).
ble_addr_t g_phone{};
bool g_hasPhone = false;
uint8_t g_phoneLtk[16];
bool g_hasLtk = false;
phrase_link::Token g_token{};
std::atomic<bool> g_hasPhoneFlag{false};  // g_hasPhone for other tasks

portMUX_TYPE g_windowLock = portMUX_INITIALIZER_UNLOCKED;
bt_pairing::Window g_tokenWindow;  // the phone may read the Token while this is open

std::map<uint16_t, bt_pairing::Window> g_grace;  // each connection's time to encrypt
std::map<uint16_t, ble_addr_t> g_forget;         // rejected: their keys go at disconnect
std::set<uint16_t> g_doomed;                     // ended in onPassKeyNotify: never trusted

bool tokenWindowOpen() {
    portENTER_CRITICAL(&g_windowLock);
    const bool open = g_tokenWindow.isOpen(millis());
    portEXIT_CRITICAL(&g_windowLock);
    return open;
}

bool loadPhone() {
    Preferences p;
    uint8_t raw[7];
    if (!p.begin(kStore, true)) return false;
    const bool ok = p.getBytes("phone", raw, sizeof raw) == sizeof raw &&
                    p.getBytes("token", g_token.data(), g_token.size()) == g_token.size();
    p.end();
    if (!ok) return false;
    g_phone.type = raw[0];
    memcpy(g_phone.val, raw + 1, 6);
    return true;
}

// False if it couldn't be saved: the bond wouldn't survive a restart, so don't trust it.
bool savePhone(const ble_addr_t& addr, const phrase_link::Token& token) {
    uint8_t raw[7] = {addr.type};
    memcpy(raw + 1, addr.val, 6);
    Preferences p;
    if (!p.begin(kStore, false)) return false;
    const bool ok = p.putBytes("token", token.data(), token.size()) == token.size() &&
                    p.putBytes("phone", raw, sizeof raw) == sizeof raw;
    p.end();
    if (!ok) return false;
    g_phone = addr;
    g_token = token;
    g_hasPhone = true;
    g_hasPhoneFlag.store(true);
    return true;
}

bool storedLtk(const ble_addr_t& addr, uint8_t out[16]) {
    ble_store_key_sec key{};
    key.peer_addr = addr;
    ble_store_value_sec value{};
    if (ble_store_read_our_sec(&key, &value) != 0 || !value.ltk_present) return false;
    memcpy(out, value.ltk, 16);
    return true;
}

// The phone, reconnecting with the key it bonded with (a fresh pairing that claims its address
// brings a new one).
bool isPhone(const ble_addr_t& addr) {
    uint8_t ltk[16];
    return g_hasPhone && g_hasLtk && ble_addr_cmp(&addr, &g_phone) == 0 && storedLtk(addr, ltk) &&
           memcmp(ltk, g_phoneLtk, sizeof ltk) == 0;
}

void reject(const ble_gap_conn_desc& d) {
    g_forget[d.conn_handle] = d.peer_id_addr;
    ble_gap_terminate(d.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
}

void forgetAllBut(const ble_addr_t* keep) {
    ble_addr_t peers[MYNEWT_VAL(BLE_STORE_MAX_BONDS)];
    int n = 0;
    if (ble_store_util_bonded_peers(peers, &n, MYNEWT_VAL(BLE_STORE_MAX_BONDS)) != 0) n = 0;
    for (int i = 0; i < n; ++i) {
        if (keep != nullptr && ble_addr_cmp(&peers[i], keep) == 0) continue;
        ble_gap_conn_desc d;
        if (ble_gap_conn_find_by_addr(&peers[i], &d) == 0) ble_gap_terminate(d.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        ble_store_util_delete_peer(&peers[i]);
    }
}

void advertiseUnlessPhone() {
    if (g_trusted.load() == kNone) BLEDevice::startAdvertising();
}

class DeviceCallbacks : public BLEDeviceCallbacks {
    // Refuse a bond when the store is full, rather than unpairing the oldest (often the phone).
    int onStoreStatus(ble_store_status_event* e, void*) override {
        return e->event_code == BLE_STORE_EVENT_OVERFLOW ? BLE_HS_ESTORE_CAP : 0;
    }
};

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer*, ble_gap_conn_desc* d) override {
        g_grace[d->conn_handle].open(millis(), bt_pairing::kGraceMs);
        // NimBLE stops advertising on a connect. Keep it up until the phone is here, so a stranger
        // who connects and sits can't lock the phone out.
        advertiseUnlessPhone();
    }
    void onDisconnect(BLEServer*, ble_gap_conn_desc* d) override {
        int expected = d->conn_handle;
        if (g_trusted.compare_exchange_strong(expected, kNone)) Serial.println("ble: phone disconnected");
        expected = d->conn_handle;
        g_pairing.compare_exchange_strong(expected, kNone);
        g_grace.erase(d->conn_handle);
        g_doomed.erase(d->conn_handle);
        const auto forget = g_forget.find(d->conn_handle);
        if (forget != g_forget.end()) {
            ble_store_util_delete_peer(&forget->second);
            g_forget.erase(forget);
        }
        advertiseUnlessPhone();
    }
};

class SendCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* c, ble_gap_conn_desc* d) override {
        if (static_cast<int>(d->conn_handle) != g_trusted.load()) return;  // only the phone sends
        if (g_onWrite != nullptr) g_onWrite(c->getData(), c->getLength(), g_token);
    }
};

class TokenCallbacks : public BLECharacteristicCallbacks {
    void onRead(BLECharacteristic* c, ble_gap_conn_desc* d) override {
        const bool phone = static_cast<int>(d->conn_handle) == g_trusted.load();
        c->setValue(String(dongle_trust::tokenText(g_token, phone, tokenWindowOpen()).c_str()));
    }
};

class SecurityCallbacks : public BLESecurityCallbacks {
    uint32_t onPassKeyRequest() override { return 0; }      // never asked: the dongle only displays
    bool onConfirmPIN(uint32_t) override { return false; }  // no numeric comparison
    bool onSecurityRequest() override { return true; }

    // Every authenticated pairing starts here, so this is where "one phone" is enforced. The
    // callback has no handle: find the one unencrypted connection.
    void onPassKeyNotify(uint32_t key) override {
        std::vector<uint16_t> pending;
        for (const auto& [handle, status] : g_server->getPeerDevices(false)) {
            (void)status;
            ble_gap_conn_desc d;
            if (ble_gap_conn_find(handle, &d) == 0 && !d.sec_state.encrypted) pending.push_back(handle);
        }
        if (pending.size() == 1 && dongle_trust::acceptsNewPhone(g_hasPhone)) {
            g_pairing.store(pending[0]);
            // The spike's "display": USB serial on the Computer the dongle is plugged into.
            Serial.printf("\n*** Bluetooth passkey: %s (enter it on the phone) ***\n\n",
                          bt_pairing::passkeyText(static_cast<int32_t>(key)).c_str());
            return;
        }
        Serial.println(g_hasPhone ? "ble: a new pairing was refused: the dongle has a phone (hold BOOT 5 s to reset)"
                                  : "ble: a pairing was refused: more than one connection is pairing");
        if (pending.empty()) g_unmatched.store(true);
        for (uint16_t handle : pending) {
            g_doomed.insert(handle);
            ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
        }
    }

    void onAuthenticationComplete(ble_gap_conn_desc* d) override {
        int expected = d->conn_handle;
        const bool pairedNow = g_pairing.compare_exchange_strong(expected, kNone);
        const bool unmatched = g_unmatched.exchange(false);
        if (!d->sec_state.encrypted) return;
        // Just Works or no bond: never trusted.
        if (!d->sec_state.authenticated || !d->sec_state.bonded || d->sec_state.key_size != 16) {
            reject(*d);
            return;
        }
        const bool known = !pairedNow && isPhone(d->peer_id_addr);
        if (!known && ((unmatched && !pairedNow) || g_doomed.count(d->conn_handle))) {
            reject(*d);
            return;
        }
        switch (bt_pairing::onPaired(pairedNow, known)) {
            case bt_pairing::Bond::Reject:
                reject(*d);
                return;
            case bt_pairing::Bond::ReplaceOthers: {
                uint8_t ltk[16];
                phrase_link::Token token;
                esp_fill_random(token.data(), token.size());
                if (!storedLtk(d->peer_id_addr, ltk) || !savePhone(d->peer_id_addr, token)) {
                    Serial.println("ble: couldn't save the phone's bond");
                    reject(*d);
                    return;
                }
                memcpy(g_phoneLtk, ltk, sizeof ltk);
                g_hasLtk = true;
                forgetAllBut(&d->peer_id_addr);
                portENTER_CRITICAL(&g_windowLock);
                g_tokenWindow.open(millis(), dongle_trust::kTokenWindowMs);
                portEXIT_CRITICAL(&g_windowLock);
                Serial.println("ble: bonded with a new phone; it can read the Token for 2 minutes");
                break;
            }
            case bt_pairing::Bond::Keep:
                Serial.println("ble: phone reconnected");
                break;
        }
        g_trusted.store(d->conn_handle);
        BLEDevice::stopAdvertising();  // one phone: nobody else needs to find the dongle now
    }
};

// Once a second on the host task: ends connections that sat unencrypted past their grace, unless
// the dongle is waiting for its first phone.
ble_npl_callout g_tick;

void tick(ble_npl_event*) {
    const uint32_t now = millis();
    const bool accepting = dongle_trust::acceptsNewPhone(g_hasPhone);
    for (const auto& [handle, grace] : g_grace) {
        ble_gap_conn_desc d;
        if (ble_gap_conn_find(handle, &d) != 0) continue;
        if (bt_pairing::dropIdle(d.sec_state.encrypted, g_pairing.load() == handle, accepting, grace.isOpen(now))) {
            ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
        }
    }
    ble_npl_callout_reset(&g_tick, ble_npl_time_ms_to_ticks32(1000));
}

}  // namespace

bool start(OnWrite onWrite) {
    g_onWrite = onWrite;
    BLEDevice::setDeviceCallbacks(new DeviceCallbacks());
    if (!BLEDevice::init("Phrasedeck dongle")) return false;
    BLEDevice::setMTU(247);  // a 2000-character Phrase in about 9 writes, not 12 (iOS still caps at 185)
    BLESecurity* sec = new BLESecurity();
    sec->setCapability(ESP_IO_CAP_OUT);            // the dongle "displays" a passkey (on USB serial)
    sec->setAuthenticationMode(true, true, true);  // bond, MITM, Secure Connections
    sec->setPassKey(false);                        // random
    sec->regenPassKeyOnConnect(true);              // a new one for every pairing
    ble_hs_cfg.sm_sc_only = 1;                     // no legacy pairing: its passkey is crackable
    BLEDevice::setSecurityCallbacks(new SecurityCallbacks());

    // No phone saved (new, or reset with BOOT): no bond is trusted, so forget them all. Safe from
    // this task: nothing is connected or advertising yet.
    g_hasPhone = loadPhone();
    g_hasLtk = g_hasPhone && storedLtk(g_phone, g_phoneLtk);
    g_hasPhoneFlag.store(g_hasPhone);
    forgetAllBut(g_hasPhone ? &g_phone : nullptr);

    g_server = BLEDevice::createServer();
    if (g_server == nullptr) return false;
    g_server->setCallbacks(new ServerCallbacks());
    BLEService* service = g_server->createService(kService);
    BLECharacteristic* send = service->createCharacteristic(
        kSendChar, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_ENC |
                       BLECharacteristic::PROPERTY_WRITE_AUTHEN);
    send->setCallbacks(new SendCallbacks());
    g_result = service->createCharacteristic(kResultChar, BLECharacteristic::PROPERTY_NOTIFY);
    BLECharacteristic* token = service->createCharacteristic(
        kTokenChar, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_READ_ENC |
                        BLECharacteristic::PROPERTY_READ_AUTHEN);
    token->setCallbacks(new TokenCallbacks());
    service->start();

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(kService);
    adv->setScanResponse(true);  // the name goes in the scan response: the 128-bit UUID fills the ad
    if (ble_npl_callout_init(&g_tick, nimble_port_get_dflt_eventq(), tick, nullptr) != 0 ||
        ble_npl_callout_reset(&g_tick, ble_npl_time_ms_to_ticks32(1000)) != 0) {
        return false;
    }
    BLEDevice::startAdvertising();
    g_started.store(true);
    Serial.println(g_hasPhone ? "ble: advertising for the bonded phone"
                              : "ble: no phone yet: advertising, the first phone to bond with the passkey is kept");
    return true;
}

bool notify(const std::vector<uint8_t>& result) {
    const int handle = g_trusted.load();
    if (!g_started.load() || handle == kNone) return false;
    // To the phone's connection only: a plain notify() would reach a stranger who subscribed.
    os_mbuf* om = ble_hs_mbuf_from_flat(result.data(), result.size());
    if (om == nullptr) return false;
    return ble_gatts_notify_custom(static_cast<uint16_t>(handle), g_result->getHandle(), om) == 0;  // takes om
}

bool hasPhone() { return g_hasPhoneFlag.load(); }
bool phoneConnected() { return g_trusted.load() != kNone; }

void forgetPhoneAndRestart() {
    Preferences p;
    if (p.begin(kStore, false)) {
        p.clear();
        p.end();
    }
    // With no phone saved, start() forgets every bond on the next boot.
    Serial.println("ble: phone and Token forgotten; restarting");
    Serial.flush();
    delay(100);
    ESP.restart();
    while (true) {
    }
}

}  // namespace ble_link
