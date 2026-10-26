// The admin password lockout (#98): after kFreeTries wrong passwords, each further try waits,
// doubling from 30 s to 15 min. Only the count is saved in NVS, so a reboot can't reset it.
#pragma once

#include <cstdint>

namespace lockout {

constexpr uint32_t kFreeTries = 5;

struct State {
    uint32_t failures = 0;
    uint32_t lastFailMs = 0;  // millis() of the last failure, or of the boot that loaded the count
};

// The state after a reboot: the saved count, timed from now.
inline State loaded(uint32_t failures, uint32_t nowMs) { return {failures, nowMs}; }

inline void failed(State& s, uint32_t nowMs) {
    if (s.failures < UINT32_MAX) ++s.failures;
    s.lastFailMs = nowMs;
}

inline void clear(State& s) { s = State{}; }

// How long before the next try is allowed; 0 means now.
inline uint32_t waitMs(const State& s, uint32_t nowMs) {
    if (s.failures < kFreeTries) return 0;
    const uint32_t extra = s.failures - kFreeTries;
    const uint32_t wait = extra >= 5 ? 15u * 60 * 1000 : 30000u << extra;  // 30 s .. 8 min, then 15 min
    const uint32_t since = nowMs - s.lastFailMs;  // wrap-safe
    return since >= wait ? 0 : wait - since;
}

}  // namespace lockout
