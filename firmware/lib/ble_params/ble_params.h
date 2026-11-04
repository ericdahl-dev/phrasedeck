// Whether the panel asks the Computer for new Bluetooth connection parameters (#174, #187).
#pragma once

#include <cstdint>

namespace ble_params {

// 2 s, in 10 ms units: Apple's minimum. The timeout is how long a stalled link can hold a key down
// before the host drops it and its keys; longer would let a stall run into the host's auto-repeat
// (BlueZ's own default is 420 ms, which this replaces, so keep it at the shortest every host takes).
constexpr uint16_t kSupervisionTimeout = 200;

// interval is the host's, in 1.25 ms units; timeout its supervision timeout, in 10 ms units;
// maxInterval the longest interval the panel asks for. Leave the host alone only when it's already
// at least as fast and as quick to drop a stalled link: a host's own timeout can be up to 32 s,
// time for a held key to auto-repeat (#187). The cost is a Mac at 11.25 ms moving to 15 ms, which
// doesn't matter, since the key interval sets the typing pace.
inline bool shouldRequest(uint16_t interval, uint16_t timeout, uint16_t maxInterval) {
    return interval > maxInterval || timeout > kSupervisionTimeout;
}

}  // namespace ble_params
