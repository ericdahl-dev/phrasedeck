// Frame timing for the panel's "perf:" log line (#170, -DPANEL_PERF_LOG). LVGL brackets each
// refresh with REFR_START and REFR_READY; a refresh that flushed pixels is a frame. A window opens
// at its first frame, and the first frame ending 5 s or more later closes it with a Report: frames
// per second, frame times, and the time spent copying into the framebuffer.
//
// LVGL 9.1 pauses its refresh timer while nothing is invalid, so an idle screen sends no events at
// all. A frame 5 s or more after the last one therefore drops the partial window and opens a new
// one: frames/s counts time the screen was changing, not time it sat still.
//
// Times are micros() and millis(), so unsigned subtraction survives their wrap.
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
    uint32_t windowMs       = 0;  // millis() at the start of this window's first frame
    uint32_t lastFrameMs    = 0;  // millis() at the end of its latest frame
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

// Ends the refresh. True, with `out` filled, when this frame closed a window.
inline bool refreshReady(Stats& s, uint32_t nowUs, uint32_t nowMs, Report& out) {
    if (s.refreshPx == 0) return false;  // not a frame
    const uint32_t took    = nowUs - s.refreshUs;
    const uint32_t startMs = nowMs - took / 1000;
    if (s.frames > 0 && startMs - s.lastFrameMs >= kWindowMs) {
        const Stats keep = s;  // the refresh in progress
        s                = Stats{};
        s.refreshFlushUs = keep.refreshFlushUs;
        s.refreshPx      = keep.refreshPx;
    }
    if (s.frames == 0) s.windowMs = startMs;
    ++s.frames;
    s.frameUs += took;
    if (took > s.frameMaxUs) s.frameMaxUs = took;
    s.flushUs += s.refreshFlushUs;
    s.pixels += s.refreshPx;
    s.lastFrameMs = nowMs;

    const uint32_t span = nowMs - s.windowMs;
    if (span < kWindowMs) return false;
    out.frames          = s.frames;
    out.framesPerSecond = s.frames * 1000.0f / span;
    out.frameAvgMs      = s.frameUs / 1000.0f / s.frames;
    out.frameMaxMs      = s.frameMaxUs / 1000.0f;
    out.flushMsPerFrame = s.flushUs / 1000.0f / s.frames;
    out.kpxPerFrame     = static_cast<uint32_t>(s.pixels / s.frames / 1000);
    s                   = Stats{};
    return true;
}

}  // namespace frame_stats
