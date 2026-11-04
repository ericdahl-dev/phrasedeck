// The one channel between the panel UI and the net task (#180). The UI submits a request (pair with
// a Helper, or a Send) and gets a ticket back, or Busy while one is in flight; it can Stop by ticket
// and read the result by ticket. The net task takes the request, runs it and finishes the ticket.
// The channel also publishes one Snapshot of the network and the Bluetooth keyboard, so the UI reads
// one moment. FreeRTOS sits behind the Rtos seam, so it's host-tested with a fake.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "nearby.h"
#include "send.h"

namespace request_channel {

// The RTOS seam: a lock (a FreeRTOS mutex on the panel) and a wakeup for the net task (a binary
// semaphore). wait() returns true when notify() came, false after ms.
class Rtos {
  public:
    virtual ~Rtos() = default;
    virtual void lock() = 0;
    virtual void unlock() = 0;
    virtual void notify() = 0;
    virtual bool wait(uint32_t ms) = 0;
};

using Ticket = uint32_t;
constexpr Ticket kBusy = 0;  // submit() refused it: another request is in flight

struct Request {
    enum Kind { Pair, Type } kind = Type;
    nearby::Helper helper;        // Pair
    std::string code, shortPin;   // Pair: the Pairing code and the short Pin
    send::Phrase phrase;          // Type
};

// How a request went, for its ticket: the words for the status line, and for a Pair whether it
// paired, for a Send its outcome.
struct Result {
    std::string message;
    bool paired = false;
    send::Outcome sent = send::Outcome::NotTyped;
};

// The net task's part of a Snapshot.
struct Network {
    std::string paired;   // the paired Computer's name, empty when not paired
    bool wifiConfigured = false;
    bool wifiUp = false;
    std::string ip;       // on the panel's Wi-Fi; empty when not joined
    // The panel's own network (#165), up while it can't join its Wi-Fi: only the web editor, at
    // https://192.168.4.1/. The password is for the screen; it's never logged.
    bool ownNetwork = false;
    std::string ownName, ownPassword;
    std::vector<nearby::Helper> nearby;
};

// The Bluetooth keyboard's part (#94), read from it under the channel's lock.
struct Bluetooth {
    bool on = false;        // the Mode: Sends go as keystrokes over Bluetooth, not through phrased
    bool ready = false;     // the paired Computer is connected
    bool pairing = false;   // the pairing window is open
    int32_t passkey = -1;   // to show while a Computer pairs, or -1
    bool operator==(const Bluetooth& o) const {
        return on == o.on && ready == o.ready && pairing == o.pairing && passkey == o.passkey;
    }
};

// What the UI reads: one moment of both parts.
struct Snapshot : Network {
    bool bluetooth = false;
    bool bleReady = false;
    bool blePairing = false;
    int32_t passkey = -1;
    unsigned version = 0;  // bumps on every change, a finished request included
};

// A request as the net task takes it.
struct Taken {
    Ticket ticket = kBusy;
    Request request;
};

class Channel {
  public:
    // Before begin(), every request is Busy.
    void begin(Rtos& rtos) { rtos_ = &rtos; }

    // UI side. A ticket, or kBusy while a request is in flight (or before begin()).
    Ticket submit(Request request);
    bool busy();
    // True once ticket t has finished, with its result. Only the last finished ticket has one.
    bool result(Ticket t, Result& out);
    // Stop: only while ticket t is in flight, taken or not. The request still finishes with a result.
    void cancel(Ticket t);
    // Cheap to poll before taking a snapshot.
    unsigned version() const { return version_.load(); }
    Snapshot snapshot();

    // The net task's network state, whole.
    void publish(const Network& n);
    // The Bluetooth keyboard changed (any task): read() runs under the lock, so every update reads
    // the newest state and the Snapshot can't mix two moments. The version bumps only on a change.
    void updateBluetooth(const std::function<Bluetooth()>& read);

    // Net task side. The Stop for the request in flight, for send::overBluetooth to read (and clear)
    // between keys. Cleared when a request is submitted.
    std::atomic<bool>& stop() { return stop_; }
    // Waits up to ms for a request; true with it. Each request is taken once.
    bool take(Taken& out, uint32_t ms);
    // Ends ticket t with its result; the next request can come.
    void finish(Ticket t, const Result& result);

  private:
    struct Guard {
        explicit Guard(Rtos& r) : r_(r) { r_.lock(); }
        ~Guard() { r_.unlock(); }
        Rtos& r_;
    };

    Rtos* rtos_ = nullptr;
    Ticket next_ = 0;
    Ticket inFlight_ = kBusy;  // submitted and not finished
    bool waiting_ = false;     // inFlight_ is submitted and not taken yet
    Request pending_;
    Ticket done_ = kBusy;      // the last finished ticket, and its result
    Result result_;
    std::atomic<bool> stop_{false};  // set under the lock, for inFlight_ only; read lock-free
    Snapshot snap_;
    std::atomic<unsigned> version_{0};  // snap_.version, written under the lock

    void bump() { version_.store(++snap_.version); }  // under the lock
};

}  // namespace request_channel
