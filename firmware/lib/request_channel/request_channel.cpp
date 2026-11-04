#include "request_channel.h"

#include <utility>

namespace request_channel {

Ticket Channel::submit(Request request) {
    if (rtos_ == nullptr) return kBusy;
    {
        Guard g(*rtos_);
        if (inFlight_ != kBusy) return kBusy;
        if (++next_ == kBusy) ++next_;  // skip 0 on wrap
        inFlight_ = next_;
        waiting_ = true;
        stop_.store(false);
        pending_ = std::move(request);
    }
    rtos_->notify();
    return next_;
}

bool Channel::busy() {
    if (rtos_ == nullptr) return true;
    Guard g(*rtos_);
    return inFlight_ != kBusy;
}

bool Channel::result(Ticket t, Result& out) {
    if (rtos_ == nullptr || t == kBusy) return false;
    Guard g(*rtos_);
    if (t != done_) return false;
    out = result_;
    return true;
}

void Channel::cancel(Ticket t) {
    if (rtos_ == nullptr || t == kBusy) return;
    Guard g(*rtos_);
    // Under the lock, so a late Stop can't land after the next submit cleared it.
    if (t == inFlight_) stop_.store(true);
}

bool Channel::take(Taken& out, uint32_t ms) {
    if (rtos_ == nullptr) return false;
    for (bool waited = false;; waited = true) {
        {
            Guard g(*rtos_);
            if (waiting_) {
                waiting_ = false;
                out.ticket = inFlight_;
                out.request = std::move(pending_);
                pending_ = Request();
                return true;
            }
        }
        if (waited || !rtos_->wait(ms)) return false;
    }
}

void Channel::finish(Ticket t, const Result& result) {
    if (rtos_ == nullptr) return;
    Guard g(*rtos_);
    if (t == kBusy || t != inFlight_) return;
    inFlight_ = kBusy;
    done_ = t;
    result_ = result;
    bump();
}

Snapshot Channel::snapshot() {
    if (rtos_ == nullptr) return Snapshot();
    Guard g(*rtos_);
    return snap_;
}

void Channel::publish(const Network& n) {
    if (rtos_ == nullptr) return;
    Guard g(*rtos_);
    static_cast<Network&>(snap_) = n;
    bump();
}

void Channel::updateBluetooth(const std::function<Bluetooth()>& read) {
    if (rtos_ == nullptr) return;
    Guard g(*rtos_);
    const Bluetooth b = read();
    const Bluetooth was{snap_.bluetooth, snap_.bleReady, snap_.blePairing, snap_.passkey};
    if (b == was) return;
    snap_.bluetooth = b.on;
    snap_.bleReady = b.ready;
    snap_.blePairing = b.pairing;
    snap_.passkey = b.passkey;
    bump();
}

}  // namespace request_channel
