// Factory reset (#98): a finger on the top-left corner from power-on, held 10 s, erases the panel's
// admin password, lockouts, web key and cert, pairing and Buttons. Needs hands on the panel, like USB.
#pragma once

#include <cstdint>

namespace factory_reset {

constexpr uint32_t kHoldMs = 10000;
constexpr int kCorner = 120;  // px from each edge

enum class Hold { Holding, Reset, Boot };

inline Hold check(bool pressed, int x, int y, uint32_t heldMs) {
    if (!pressed || x >= kCorner || y >= kCorner) return Hold::Boot;
    return heldMs >= kHoldMs ? Hold::Reset : Hold::Holding;
}

}  // namespace factory_reset
