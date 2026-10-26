#include "ble_keyboard.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>
#include <host/ble_gap.h>
#include <host/ble_hs_mbuf.h>
#include <host/ble_store.h>

#include <atomic>

#include "bt_pairing.h"

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

BLEServer* g_server = nullptr;
BLECharacteristic* g_input = nullptr;

// Written on the NimBLE host task, read anywhere.
std::atomic<int> g_trusted{kNone};      // the paired Computer's connection, the only one keys go to
std::atomic<int> g_pairing{kNone};      // the connection showing a passkey now
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

// Ends the connection and forgets its keys, so it has to pair again (inside a window) to return.
void reject(const ble_gap_conn_desc& d) {
    ble_store_util_delete_peer(&d.peer_id_addr);
    ble_gap_terminate(d.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
}

// Forgets every bonded Computer but this one, and drops any of them still connected.
void forgetOthers(const ble_gap_conn_desc& keep) {
    ble_addr_t peers[MYNEWT_VAL(BLE_STORE_MAX_BONDS)];
    int n = 0;
    if (ble_store_util_bonded_peers(peers, &n, MYNEWT_VAL(BLE_STORE_MAX_BONDS)) != 0) n = 0;
    for (int i = 0; i < n; ++i) {
        if (ble_addr_cmp(&peers[i], &keep.peer_id_addr) == 0) continue;
        ble_gap_conn_desc d;
        if (ble_gap_conn_find_by_addr(&peers[i], &d) == 0) ble_gap_terminate(d.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        ble_store_util_delete_peer(&peers[i]);
    }
}

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer*, ble_gap_conn_desc*) override {
        // NimBLE stops advertising on a connect. Keep it up while no paired Computer is here, so a
        // stranger who connects and sits can't lock the paired one out, and while a window is open.
        if (g_trusted.load() == kNone || windowOpen()) BLEDevice::startAdvertising();
    }
    void onDisconnect(BLEServer*, ble_gap_conn_desc* d) override {
        int handle = d->conn_handle;
        int expected = handle;
        g_trusted.compare_exchange_strong(expected, kNone);
        expected = handle;
        if (g_pairing.compare_exchange_strong(expected, kNone)) g_passkey.store(kNone);
        g_disconnects.fetch_add(1);
        bump();
    }
};

class SecurityCallbacks : public BLESecurityCallbacks {
    uint32_t onPassKeyRequest() override { return 0; }  // never asked: the panel only displays
    bool onConfirmPIN(uint32_t) override { return false; }  // no numeric comparison
    bool onSecurityRequest() override { return true; }

    void onPassKeyNotify(uint32_t key) override {
        // Pairing has started on the connection being set up. Outside a window, end it before a
        // passkey is ever shown. The callback has no handle, so find the unencrypted connection.
        for (const auto& [handle, status] : g_server->getPeerDevices(false)) {
            (void)status;
            ble_gap_conn_desc d;
            if (ble_gap_conn_find(handle, &d) != 0 || d.sec_state.encrypted) continue;
            if (!windowOpen()) {
                ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
                continue;
            }
            g_pairing.store(handle);
            g_passkey.store(static_cast<int32_t>(key));
            bump();
        }
    }

    void onAuthenticationComplete(ble_gap_conn_desc* d) override {
        int expected = d->conn_handle;
        const bool pairedNow = g_pairing.compare_exchange_strong(expected, kNone);
        if (pairedNow) g_passkey.store(kNone);
        bump();
        if (!d->sec_state.encrypted) return;  // failed: the link stays unencrypted and never trusted
        // Just Works or no bond: never trusted, whatever the window says.
        if (!d->sec_state.authenticated || !d->sec_state.bonded) {
            reject(*d);
            return;
        }
        switch (bt_pairing::onPaired(pairedNow, windowOpen())) {
            case bt_pairing::Bond::Reject:
                reject(*d);
                return;
            case bt_pairing::Bond::ReplaceOthers:
                forgetOthers(*d);
                closePairing();
                break;
            case bt_pairing::Bond::Keep:
                break;
        }
        g_trusted.store(d->conn_handle);
        bump();
    }
};

}  // namespace

bool start() {
    if (!BLEDevice::init("Phrasedeck panel")) return false;
    BLESecurity* sec = new BLESecurity();
    sec->setCapability(ESP_IO_CAP_OUT);               // the panel displays a passkey
    sec->setAuthenticationMode(true, true, true);     // bond, MITM, Secure Connections
    sec->setPassKey(false);                           // random
    sec->regenPassKeyOnConnect(true);                 // a new one for every pairing
    BLEDevice::setSecurityCallbacks(new SecurityCallbacks());

    g_server = BLEDevice::createServer();
    if (g_server == nullptr) return false;
    g_server->setCallbacks(new ServerCallbacks());
    BLEHIDDevice* hid = new BLEHIDDevice(g_server);
    hid->manufacturer("Phrasedeck");
    hid->pnp(0x02, 0x303A, 0x1001, 0x0100);  // USB vendor: Espressif
    hid->hidInfo(0x00, 0x01);
    hid->reportMap(const_cast<uint8_t*>(kReportMap), sizeof(kReportMap));
    g_input = hid->inputReport(1);
    hid->startServices();

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->setAppearance(0x03C1);  // HID keyboard
    adv->addServiceUUID(hid->hidService()->getUUID());
    adv->setScanResponse(true);
    BLEDevice::startAdvertising();
    return true;
}

bool ready() { return g_trusted.load() != kNone; }

bool send(uint8_t mod, uint8_t code) {
    const int handle = g_trusted.load();
    if (handle == kNone || g_input == nullptr) return false;
    // Notify only the paired Computer's connection. NimBLE ignores the input report's encryption
    // permission, so a plain notify() would also reach a stranger who connected and subscribed.
    const uint8_t report[8] = {mod, 0, code, 0, 0, 0, 0, 0};
    os_mbuf* om = ble_hs_mbuf_from_flat(report, sizeof(report));
    if (om == nullptr) return false;
    return ble_gatts_notify_custom(static_cast<uint16_t>(handle), g_input->getHandle(), om) == 0;  // takes om
}

unsigned disconnects() { return g_disconnects.load(); }

void openPairing(uint32_t ms) {
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
    bump();
}

bool pairingOpen() { return windowOpen(); }

int32_t passkey() { return g_passkey.load(); }

unsigned version() { return g_version.load(); }

}  // namespace ble_keyboard
