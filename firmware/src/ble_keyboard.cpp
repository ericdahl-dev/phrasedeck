#include "ble_keyboard.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>
#include <Preferences.h>
#include <host/ble_gap.h>
#include <host/ble_hs.h>
#include <host/ble_hs_mbuf.h>
#include <host/ble_store.h>
#include <nimble/nimble_port.h>

#include <atomic>
#include <map>
#include <set>
#include <vector>

#include "bt_pairing.h"
#include "panel_store.h"

namespace ble_keyboard {
namespace {

// A boot-style keyboard, report ID 1: modifier byte, reserved byte, six key codes.
const uint8_t kReportMap[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x06,  // Usage (Keyboard)
    0xA1, 0x01,  // Collection (Application)
    0x85, 0x01,  //   Report ID (1)
    0x05, 0x07,  //   Usage Page (Keyboard)
    0x19, 0xE0,  //   Usage Minimum (Left Control)
    0x29, 0xE7,  //   Usage Maximum (Right GUI)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x01,  //   Logical Maximum (1)
    0x75, 0x01,  //   Report Size (1)
    0x95, 0x08,  //   Report Count (8)
    0x81, 0x02,  //   Input (Data, Variable, Absolute): modifiers
    0x95, 0x01,  //   Report Count (1)
    0x75, 0x08,  //   Report Size (8)
    0x81, 0x01,  //   Input (Constant): reserved
    0x95, 0x06,  //   Report Count (6)
    0x75, 0x08,  //   Report Size (8)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x65,  //   Logical Maximum (101)
    0x05, 0x07,  //   Usage Page (Keyboard)
    0x19, 0x00,  //   Usage Minimum (0)
    0x29, 0x65,  //   Usage Maximum (101)
    0x81, 0x00,  //   Input (Data, Array): keys
    0xC0,        // End Collection
};

constexpr int kNone = -1;

std::atomic<bool> g_started{false};  // NimBLE is up: the API is safe to call
BLEServer* g_server = nullptr;
BLECharacteristic* g_input = nullptr;

// Written on the NimBLE host task, read anywhere.
std::atomic<int> g_trusted{kNone};      // the paired Computer's connection, the only one keys go to
std::atomic<int> g_pairing{kNone};      // the connection showing a passkey now
std::atomic<bool> g_unmatched{false};   // a passkey went to a connection we couldn't tell apart
std::atomic<int> g_subscribed{kNone};   // the connection that turned on input report notifies
std::atomic<int32_t> g_passkey{kNone};
std::atomic<unsigned> g_disconnects{0};
std::atomic<unsigned> g_version{0};

portMUX_TYPE g_windowLock = portMUX_INITIALIZER_UNLOCKED;
bt_pairing::Window g_window;  // under g_windowLock: openPairing() runs on another task

bool windowOpen() {
    portENTER_CRITICAL(&g_windowLock);
    const bool open = g_window.isOpen(millis());
    portEXIT_CRITICAL(&g_windowLock);
    return open;
}

void bump() { g_version.fetch_add(1); }

// The paired Computer's identity address, in the panel NVS so a factory reset forgets it. Only a
// bond with this address reconnects (#141). Set in start(), then on the host task only.
ble_addr_t g_computer{};
bool g_hasComputer = false;

bool loadComputer() {
    Preferences p;
    uint8_t raw[7];
    if (!p.begin("ble", true, panel_store::kPartition)) return false;
    if (p.getBytes("computer", raw, sizeof(raw)) != sizeof(raw)) return false;
    g_computer.type = raw[0];
    memcpy(g_computer.val, raw + 1, 6);
    return true;
}

// False if it couldn't be saved: the bond wouldn't survive a restart, so don't trust it.
bool saveComputer(const ble_addr_t& addr) {
    uint8_t raw[7] = {addr.type};
    memcpy(raw + 1, addr.val, 6);
    Preferences p;
    if (!p.begin("ble", false, panel_store::kPartition) || p.putBytes("computer", raw, sizeof(raw)) != sizeof(raw)) {
        log_e("couldn't save the paired Computer");
        return false;
    }
    g_computer = addr;
    g_hasComputer = true;
    return true;
}

// Each connection's time to encrypt, from its connect (host task only).
std::map<uint16_t, bt_pairing::Window> g_grace;

// Rejected connections whose keys go at disconnect: NimBLE writes the peer's CCCDs after the
// ENC_CHANGE callback, so deleting there would leave orphans that can evict the Computer's bond.
// Host task only (connect, security and disconnect callbacks all run there).
std::map<uint16_t, ble_addr_t> g_forget;

// Every connection with input notifies on, trusted or not (host task only). A host can turn them on
// before pairing finishes, and NimBLE doesn't say so again once it's bonded: trust promotes it.
std::set<uint16_t> g_notifying;

// Ends the connection and forgets its keys, so it has to pair again (inside a window) to return.
void reject(const ble_gap_conn_desc& d) {
    g_forget[d.conn_handle] = d.peer_id_addr;
    ble_gap_terminate(d.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
}

// Forgets every bonded Computer but keep (all of them for nullptr), and drops any still connected.
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

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer*, ble_gap_conn_desc* d) override {
        g_grace[d->conn_handle].open(millis(), bt_pairing::kGraceMs);
        // NimBLE stops advertising on a connect. Keep it up while no paired Computer is here, so a
        // stranger who connects and sits can't lock the paired one out, and while a window is open.
        if (g_trusted.load() == kNone || windowOpen()) BLEDevice::startAdvertising();
    }
    void onDisconnect(BLEServer*, ble_gap_conn_desc* d) override {
        int handle = d->conn_handle;
        int expected = handle;
        // Only the Computer leaving stops a Send: a stranger's disconnect mustn't.
        if (g_trusted.compare_exchange_strong(expected, kNone)) g_disconnects.fetch_add(1);
        expected = handle;
        if (g_pairing.compare_exchange_strong(expected, kNone)) g_passkey.store(kNone);
        expected = handle;
        g_subscribed.compare_exchange_strong(expected, kNone);
        g_notifying.erase(d->conn_handle);
        g_grace.erase(d->conn_handle);
        const auto forget = g_forget.find(d->conn_handle);
        if (forget != g_forget.end()) {
            ble_store_util_delete_peer(&forget->second);
            g_forget.erase(forget);
        }
        // advertiseOnDisconnect is off by default: without this the Computer can't come back.
        if (g_trusted.load() == kNone || windowOpen()) BLEDevice::startAdvertising();
        bump();
    }
};

class InputCallbacks : public BLECharacteristicCallbacks {
    // Also called when NimBLE restores a bonded Computer's subscription on reconnect.
    void onSubscribe(BLECharacteristic*, ble_gap_conn_desc* d, uint16_t subValue) override {
        if (subValue & 1) {
            g_notifying.insert(d->conn_handle);
            // A stranger's subscription mustn't take the slot. NimBLE restores the Computer's after
            // encryption, so it still lands here.
            const auto& sec = d->sec_state;
            if (!sec.encrypted || !sec.authenticated || !sec.bonded) return;
            g_subscribed.store(d->conn_handle);
        } else {
            g_notifying.erase(d->conn_handle);
            int expected = d->conn_handle;
            g_subscribed.compare_exchange_strong(expected, kNone);
        }
        bump();
    }
};

class SecurityCallbacks : public BLESecurityCallbacks {
    uint32_t onPassKeyRequest() override { return 0; }  // never asked: the panel only displays
    bool onConfirmPIN(uint32_t) override { return false; }  // no numeric comparison
    bool onSecurityRequest() override { return true; }

    void onPassKeyNotify(uint32_t key) override {
        // A new pairing has started. Every authenticated pairing comes through here (the panel
        // can only display), so this is where the window is enforced. The callback has no handle:
        // find the one unencrypted connection. Outside a window, or if there isn't exactly one,
        // end them all before a passkey is shown.
        std::vector<uint16_t> pending;
        for (const auto& [handle, status] : g_server->getPeerDevices(false)) {
            (void)status;
            ble_gap_conn_desc d;
            if (ble_gap_conn_find(handle, &d) == 0 && !d.sec_state.encrypted) pending.push_back(handle);
        }
        if (pending.size() == 1 && windowOpen()) {
            g_pairing.store(pending[0]);
            g_passkey.store(static_cast<int32_t>(key));
            bump();
            return;
        }
        if (pending.empty()) g_unmatched.store(true);  // onAuthenticationComplete rejects it
        for (uint16_t handle : pending) ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
    }

    void onAuthenticationComplete(ble_gap_conn_desc* d) override {
        int expected = d->conn_handle;
        const bool pairedNow = g_pairing.compare_exchange_strong(expected, kNone);
        if (pairedNow) g_passkey.store(kNone);
        const bool unmatched = g_unmatched.exchange(false);
        bump();
        if (!d->sec_state.encrypted) return;  // failed: the link stays unencrypted and never trusted
        // Just Works or no bond: never trusted, whatever the window says.
        if (!d->sec_state.authenticated || !d->sec_state.bonded || d->sec_state.key_size != 16 ||
            (unmatched && !pairedNow)) {
            reject(*d);
            return;
        }
        const bool known = g_hasComputer && ble_addr_cmp(&d->peer_id_addr, &g_computer) == 0;
        switch (bt_pairing::onPaired(pairedNow, known, windowOpen())) {
            case bt_pairing::Bond::Reject:
                reject(*d);
                return;
            case bt_pairing::Bond::ReplaceOthers:
                // Saved first: after a power cut in between, the next start forgets the old bonds.
                if (!saveComputer(d->peer_id_addr)) {
                    reject(*d);
                    return;
                }
                forgetAllBut(&d->peer_id_addr);
                closePairing();
                break;
            case bt_pairing::Bond::Keep:
                break;
        }
        g_trusted.store(d->conn_handle);
        if (g_notifying.count(d->conn_handle)) g_subscribed.store(d->conn_handle);
        if (!windowOpen()) BLEDevice::stopAdvertising();  // one Computer: nobody else needs to find it
        bump();
    }
};

void pairingClosed() {
    if (g_started.load() && g_trusted.load() != kNone) BLEDevice::stopAdvertising();
    bump();
}

// Once a second on the host task, beside the callbacks that own g_grace: stops advertising when a
// window runs out, and ends connections that sat unencrypted past their grace (#141).
ble_npl_callout g_tick;

void tick(ble_npl_event*) {
    // Read, checked and closed under one lock, so an openPairing() on the net task can't be closed.
    portENTER_CRITICAL(&g_windowLock);
    const bool lapsed = g_window.lapsed(millis());
    if (lapsed) g_window.close();
    portEXIT_CRITICAL(&g_windowLock);
    if (lapsed) pairingClosed();
    const uint32_t now = millis();
    const bool window = windowOpen();
    for (const auto& [handle, grace] : g_grace) {
        ble_gap_conn_desc d;
        if (ble_gap_conn_find(handle, &d) != 0) continue;
        if (bt_pairing::dropIdle(d.sec_state.encrypted, g_pairing.load() == handle, window, grace.isOpen(now))) {
            ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
        }
    }
    ble_npl_callout_reset(&g_tick, ble_npl_time_ms_to_ticks32(1000));
}

}  // namespace

bool start() {
    if (!BLEDevice::init("Phrasedeck panel")) return false;
    BLESecurity* sec = new BLESecurity();
    sec->setCapability(ESP_IO_CAP_OUT);               // the panel displays a passkey
    sec->setAuthenticationMode(true, true, true);     // bond, MITM, Secure Connections
    sec->setPassKey(false);                           // random
    sec->regenPassKeyOnConnect(true);                 // a new one for every pairing
    // Secure Connections only: a sniffed legacy passkey pairing gives up its key in seconds.
    ble_hs_cfg.sm_sc_only = 1;
    BLEDevice::setSecurityCallbacks(new SecurityCallbacks());
    // No stored Computer (first start, a factory reset, or bonds from #140): no bond is trusted, so
    // forget them all. Otherwise forget any but the Computer's. The store takes no lock; this is safe
    // only because nothing is connected or advertising yet.
    g_hasComputer = loadComputer();
    forgetAllBut(g_hasComputer ? &g_computer : nullptr);

    g_server = BLEDevice::createServer();
    if (g_server == nullptr) return false;
    g_server->setCallbacks(new ServerCallbacks());
    BLEHIDDevice* hid = new BLEHIDDevice(g_server);
    hid->manufacturer("Phrasedeck");
    hid->pnp(0x02, 0x303A, 0x1001, 0x0100);  // USB vendor: Espressif
    hid->hidInfo(0x00, 0x01);
    hid->reportMap(const_cast<uint8_t*>(kReportMap), sizeof(kReportMap));
    g_input = hid->inputReport(1);
    g_input->setCallbacks(new InputCallbacks());
    hid->startServices();

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->setAppearance(0x03C1);  // HID keyboard
    adv->addServiceUUID(hid->hidService()->getUUID());
    adv->setScanResponse(true);
    if (ble_npl_callout_init(&g_tick, nimble_port_get_dflt_eventq(), tick, nullptr) != 0 ||
        ble_npl_callout_reset(&g_tick, ble_npl_time_ms_to_ticks32(1000)) != 0) {
        return false;
    }
    BLEDevice::startAdvertising();
    g_started.store(true);
    return true;
}

// Subscribed too: until the Computer turns on notifies it drops reports, and a send would claim
// keystrokes that never landed.
bool ready() {
    const int handle = g_trusted.load();
    return handle != kNone && g_subscribed.load() == handle;
}

bool send(uint8_t mod, uint8_t code) {
    const int handle = g_trusted.load();
    if (!g_started.load() || handle == kNone || g_subscribed.load() != handle) return false;
    // Notify only the paired Computer's connection. NimBLE ignores the input report's encryption
    // permission, so a plain notify() would also reach a stranger who connected and subscribed.
    const uint8_t report[8] = {mod, 0, code, 0, 0, 0, 0, 0};
    os_mbuf* om = ble_hs_mbuf_from_flat(report, sizeof(report));
    if (om == nullptr) return false;
    return ble_gatts_notify_custom(static_cast<uint16_t>(handle), g_input->getHandle(), om) == 0;  // takes om
}

unsigned disconnects() { return g_disconnects.load(); }

void openPairing(uint32_t ms) {
    if (!g_started.load()) return;
    portENTER_CRITICAL(&g_windowLock);
    g_window.open(millis(), ms);
    portEXIT_CRITICAL(&g_windowLock);
    BLEDevice::startAdvertising();
    bump();
}

void closePairing() {
    portENTER_CRITICAL(&g_windowLock);
    g_window.close();
    portEXIT_CRITICAL(&g_windowLock);
    pairingClosed();
}

bool pairingOpen() { return windowOpen(); }

int32_t passkey() { return g_passkey.load(); }

unsigned version() { return g_version.load(); }

}  // namespace ble_keyboard
