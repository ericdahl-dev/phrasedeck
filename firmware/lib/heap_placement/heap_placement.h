// Where a plain malloc() or new lands on the panel (#182): internal RAM or PSRAM. Internal RAM is
// what Wi-Fi, NimBLE and the web editor's TLS need, so a full list of long Phrases must not fill it.
// Kept free of Arduino so it's host-tested.
#pragma once

#include <cstddef>
#include <cstdio>
#include <string>

namespace heap_placement {

// The board's sdkconfig (CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL) puts a malloc() of up to 4096 bytes
// in internal RAM first, and a Phrase is up to 2000 bytes. The firmware passes this lower limit to
// heap_caps_malloc_extmem_enable() before the Buttons load.
constexpr size_t kInternalUpTo = 256;

// mbedTLS allocates through its own calloc (main.cpp tlsCalloc), which keeps the split it had before
// #182: its 16 KB records to PSRAM, everything else in internal RAM, as fast as before.
constexpr size_t kTlsInternalUpTo = 4096;

// True if a malloc() of this many bytes goes to internal RAM first (ESP-IDF's heap_caps_malloc_default:
// up to the limit, internal first; above it, PSRAM first). Either falls back to the other when full.
inline bool internalFirst(size_t bytes, size_t limit = kInternalUpTo) { return bytes <= limit; }

// One heap: free bytes now, the largest block free now, and the least free since boot.
struct Stats {
    size_t free = 0;
    size_t largest = 0;
    size_t minimum = 0;
};

// The `heap` serial command's line, and the boot log's: internal RAM, then PSRAM.
inline std::string report(const Stats& internal, const Stats& psram) {
    char line[160];
    snprintf(line, sizeof line, "heap: internal free %zu, largest %zu, min %zu; PSRAM free %zu, largest %zu, min %zu",
             internal.free, internal.largest, internal.minimum, psram.free, psram.largest, psram.minimum);
    return line;
}

}  // namespace heap_placement
