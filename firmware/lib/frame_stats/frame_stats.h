// Frame timing for the panel's "perf:" log line (#170, -DPANEL_PERF_LOG). LVGL brackets each
// refresh with REFR_START and REFR_READY; a refresh that flushed pixels is a frame. Every 5 s the
// window closes with a Report: frames per second, frame times, and the time spent copying into the
// framebuffer. Times are micros() and millis(), so unsigned subtraction survives their wrap.
#pragma once

#include <cstdint>

namespace frame_stats {

constexpr uint32_t kWindowMs = 5000;

struct Report {
    uint32_t frames          = 0;
    float    framesPerSecond = 0;
    float    frameAvgMs      = 0;
    float    frameMaxMs      = 0;
    float    flushMsPerFrame = 0;
    uint32_t kpxPerFrame     = 0;
};

struct Stats {
    uint32_t windowMs       = 0;  // millis() at the start of this window
    uint32_t refreshUs      = 0;  // micros() at the start of the refresh in progress
    uint32_t refreshPx      = 0;  // pixels flushed in the refresh in progress
    uint32_t refreshFlushUs = 0;
    uint32_t frames         = 0;
    uint32_t frameMaxUs     = 0;
    uint64_t frameUs        = 0;  // summed over the window
    uint64_t flushUs        = 0;
    uint64_t pixels         = 0;
};

inline void refreshStart(Stats& s, uint32_t nowUs) {
    s.refreshUs      = nowUs;
    s.refreshPx      = 0;
    s.refreshFlushUs = 0;
}

inline void flushed(Stats& s, uint32_t pixels, uint32_t tookUs) {
    s.refreshPx += pixels;
    s.refreshFlushUs += tookUs;
}

// Ends the refresh. True, with `out` filled, when it also closed a window that had frames in it.
inline bool refreshReady(Stats& s, uint32_t nowUs, uint32_t nowMs, Report& out) {
    if (s.refreshPx > 0) {
        const uint32_t took = nowUs - s.refreshUs;
        ++s.frames;
        s.frameUs += took;
        if (took > s.frameMaxUs) s.frameMaxUs = took;
        s.flushUs += s.refreshFlushUs;
        s.pixels += s.refreshPx;
    }
    const uint32_t span = nowMs - s.windowMs;
    if (span < kWindowMs) return false;
    const bool any = s.frames > 0;
    if (any) {
        out.frames          = s.frames;
        out.framesPerSecond = s.frames * 1000.0f / span;
        out.frameAvgMs      = s.frameUs / 1000.0f / s.frames;
        out.frameMaxMs      = s.frameMaxUs / 1000.0f;
        out.flushMsPerFrame = s.flushUs / 1000.0f / s.frames;
        out.kpxPerFrame     = static_cast<uint32_t>(s.pixels / s.frames / 1000);
    }
    s          = Stats{};
    s.windowMs = nowMs;
    return any;
}

}  // namespace frame_stats
