// The panel as a Bluetooth keyboard (#94): a Phrase as USB HID key presses on a US layout.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace hid_keys {

constexpr uint8_t kShift = 0x02;  // left Shift in the report's modifier byte

// One key press. The sender sends a report with this key down, then an all-zero release report,
// each paced, so a repeated key ("aa") reads as two presses. If a send fails, retry that report.
struct Key {
    uint8_t mod;   // modifier byte
    uint8_t code;  // HID usage (keyboard page)
    bool operator==(const Key& o) const { return mod == o.mod && code == o.code; }
};

struct Plan {
    bool ok = false;
    std::vector<Key> keys;  // empty unless ok
    std::string bad;        // when not ok: the first character a US keyboard can't type
};

Plan plan(const std::string& text, bool enter);

// Spaces HID reports: due(now) is true when the next one may go, and then counts it as sent.
// now is millis(); unsigned subtraction keeps it right across the wrap.
class Pacer {
  public:
    explicit Pacer(uint32_t intervalMs) : interval_(intervalMs) {}
    bool due(uint32_t now) {
        if (started_ && now - last_ < interval_) return false;
        sentAt(now);
        return true;
    }
    // Counts a report sent outside due(), so the next one still waits a whole interval.
    void sentAt(uint32_t now) {
        started_ = true;
        last_ = now;
    }

  private:
    uint32_t interval_;
    uint32_t last_ = 0;
    bool started_ = false;
};

// Sends a Plan's keys as HID reports: each key's press, then an all-zero release, one report per
// Pacer interval. step(), done() and result() belong to the BLE task; call step() often. notify
// sends one report and returns false when the stack didn't take it: that report goes again next
// interval, or at once for a release, so a held key can't reach the host's auto-repeat. After a
// second of failures in a row it gives up. The BLE task must yield between steps (vTaskDelay or an
// event wait), so a release retry doesn't spin. cancel() and drop() may come from any task and take
// effect at the next step(): cancel() still sends the release of a key that's down (a user cancel);
// drop() stops at once (a disconnect, where the host drops held keys itself) and wins over a cancel.
class Sender {
  public:
    // Stopped: ended partway, so some keys were typed.
    enum class Result { Running, Sent, Stopped, NotSent };

    Sender(std::vector<Key> keys, uint32_t intervalMs)
        : keys_(std::move(keys)), pacer_(intervalMs), end_(keys_.size() * 2) {}

    void cancel() { request_.fetch_or(kCancel); }
    void drop() { request_.fetch_or(kDrop); }

    template <typename Notify>
    void step(uint32_t now, Notify notify) {
        const uint8_t request = request_.exchange(0);
        if (request & kDrop) {
            end_ = next_;
        } else if (request & kCancel) {
            end_ = std::min(end_, next_ + next_ % 2);
        }
        if (done()) return;
        const bool releaseRetry = next_ % 2 == 1 && failures_ > 0;
        if (!releaseRetry && !pacer_.due(now)) return;
        const Key report = next_ % 2 == 0 ? keys_[next_ / 2] : Key{0, 0};
        if (notify(report)) {
            next_++;
            failures_ = 0;
            pacer_.sentAt(now);
        } else if (failures_++ == 0) {
            firstFailure_ = now;
        } else if (now - firstFailure_ >= kGiveUpMs) {
            end_ = next_;
        }
    }

    bool done() const { return next_ >= end_; }
    Result result() const {
        if (!done()) return Result::Running;
        if (next_ == 0) return Result::NotSent;
        return next_ == keys_.size() * 2 ? Result::Sent : Result::Stopped;
    }

  private:
    enum : uint8_t { kCancel = 1, kDrop = 2 };  // bits, so a drop isn't lost to a later cancel
    static constexpr uint32_t kGiveUpMs = 1000;
    std::vector<Key> keys_;
    Pacer pacer_;
    size_t end_;       // reports to send in all; cancel(), drop() and failures cut it short
    size_t next_ = 0;  // even: the next key's press; odd: its release
    uint32_t failures_ = 0;  // notify failures in a row
    uint32_t firstFailure_ = 0;
    std::atomic<uint8_t> request_{0};
};

// The status line after a Bluetooth send, and whether it's green. The panel can't see what landed,
// so it never says Typed, and never Maybe typed either: Stopped says what it knows.
struct Said {
    std::string message;
    bool ok;
};

inline Said said(Sender::Result r) {
    switch (r) {
        case Sender::Result::Sent: return {"Sent as keystrokes", true};
        case Sender::Result::Stopped: return {"Stopped partway: some keystrokes were sent", false};
        case Sender::Result::NotSent: return {"Not sent", false};
        case Sender::Result::Running: return {"Sending", false};
    }
    return {"Not sent", false};
}

// When plan() refused the text; bad is Plan::bad.
inline Said refused(const std::string& bad) { return {"Not sent: a US keyboard can't type " + bad, false}; }

}  // namespace hid_keys
