// The panel as a Bluetooth keyboard (#94): a Phrase as USB HID key presses on a US layout.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hid_keys {

constexpr uint8_t kShift = 0x02;  // left Shift in the report's modifier byte

// One key press: a report with this key down. The Sender releases it with an all-zero report, or
// with the next key's press when that rolls over (see Sender), so a repeated key ("aa") still reads
// as two presses.
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

// Sends a Plan's keys as HID reports, one per Pacer interval: each key's press, and an all-zero
// release unless the next key rolls over (a different usage with the same modifiers: its press
// releases this one, #174), then a release at the end. step(), done(), keyDown() and result() belong
// to the BLE task; call step() often. notify sends one report and returns false when the stack
// didn't take it: that report goes again next interval, or at once while a key is down, so a held
// key can't reach the host's auto-repeat. After a second of failures in a row it gives up, and
// keyDown() says whether a release is still owed. The BLE task must yield between steps
// (vTaskDelay or an event wait), so a retry doesn't spin. cancel() and drop() may come from any task
// and take effect at the next step(): cancel() still sends a release for a key that's down (a user
// cancel), never the next key; drop() stops at once (a disconnect, where the host drops held keys
// itself) and wins over a cancel.
class Sender {
  public:
    // Stopped: ended partway, so some keys were typed.
    enum class Result { Running, Sent, Stopped, NotSent };

    Sender(const std::vector<Key>& keys, uint32_t intervalMs) : reports_(reports(keys)), pacer_(intervalMs), end_(reports_.size()) {}

    void cancel() { request_.fetch_or(kCancel); }
    void drop() { request_.fetch_or(kDrop); }

    template <typename Notify>
    void step(uint32_t now, Notify notify) {
        const uint8_t request = request_.exchange(0);
        if (request & kDrop) {
            end_ = next_;
            releasing_ = false;
        } else if ((request & kCancel) && !done()) {
            const Key& planned = reports_[next_];
            if (planned.code == 0 && planned.mod == 0) {
                // The next planned report is a release: it goes, and nothing after it.
                end_ = next_ + 1;
            } else {
                // A key that's down still gets its own release, even when the next report was a
                // press that would have released it: a cancel mustn't type one more key.
                end_ = next_;
                releasing_ = down_;
            }
        }
        if (done()) return;
        // A failed report that would let go of a held key goes again at once, so the key can't
        // reach the host's auto-repeat delay. Anything else waits its interval.
        const bool heldRetry = down_ && failures_ > 0;
        if (!heldRetry && !pacer_.due(now)) return;
        const Key report = releasing_ ? Key{0, 0} : reports_[next_];
        if (notify(report)) {
            down_ = report.code != 0 || report.mod != 0;
            if (releasing_) releasing_ = false;
            else next_++;
            failures_ = 0;
            pacer_.sentAt(now);
        } else if (failures_++ == 0) {
            firstFailure_ = now;
        } else if (now - firstFailure_ >= kGiveUpMs) {
            end_ = next_;
            releasing_ = false;
        }
    }

    bool done() const { return next_ >= end_ && !releasing_; }
    // A key is down on the Computer, as far as the panel knows: the last report it took was a press.
    bool keyDown() const { return down_; }
    Result result() const {
        if (!done()) return Result::Running;
        if (next_ == 0) return Result::NotSent;
        return next_ == reports_.size() ? Result::Sent : Result::Stopped;
    }

    // The reports for keys, in order: each key's press, and an all-zero release after it unless the
    // next key can roll over (see rollsOver), then a release at the end.
    static std::vector<Key> reports(const std::vector<Key>& keys) {
        std::vector<Key> out;
        out.reserve(keys.size() * 2);
        for (size_t i = 0; i < keys.size(); i++) {
            out.push_back(keys[i]);
            if (i + 1 == keys.size() || !rollsOver(keys[i], keys[i + 1])) out.push_back(Key{0, 0});
        }
        return out;
    }

    // The next key's press can stand in for this key's release: a different usage, so the host sees
    // this one go up and the next go down, and the same modifiers, so no host can apply a Shift
    // change to the wrong key. The same key twice ("ll") needs its release, or it reads as one press.
    static bool rollsOver(const Key& from, const Key& to) { return from.code != to.code && from.mod == to.mod; }

  private:
    enum : uint8_t { kCancel = 1, kDrop = 2 };  // bits, so a drop isn't lost to a later cancel
    static constexpr uint32_t kGiveUpMs = 1000;
    std::vector<Key> reports_;
    Pacer pacer_;
    size_t end_;       // reports to send in all; cancel(), drop() and failures cut it short
    size_t next_ = 0;  // the next report in reports_
    bool down_ = false;       // the last report taken held a key
    bool releasing_ = false;  // a cancel's release, outside reports_, is still to go
    uint32_t failures_ = 0;   // notify failures in a row
    uint32_t firstFailure_ = 0;
    std::atomic<uint8_t> request_{0};
};

// The status line after a Bluetooth send, and its color: green when ok, orange when maybe, red
// otherwise. The panel can't see what landed, so it never says Typed. Stopped typed the start of
// the Phrase, so it's Maybe typed (#177): tapping again would type that start a second time.
struct Said {
    std::string message;
    bool ok;
    bool maybe = false;
};

inline Said said(Sender::Result r) {
    switch (r) {
        case Sender::Result::Sent: return {"Sent as keystrokes", true};
        case Sender::Result::Stopped: return {"Maybe typed: check the Computer before tapping again", false, true};
        case Sender::Result::NotSent: return {"Not sent", false};
        case Sender::Result::Running: return {"Sending", false};
    }
    return {"Not sent", false};
}

// When plan() refused the text; bad is Plan::bad.
inline Said refused(const std::string& bad) { return {"Not sent: a US keyboard can't type " + bad, false}; }

}  // namespace hid_keys
