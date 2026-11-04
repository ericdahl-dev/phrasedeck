#include <unity.h>

#include "frame_stats.h"

void setUp() {}
void tearDown() {}

using frame_stats::Report;
using frame_stats::Stats;

// One LVGL refresh at `at_us` that flushes `pixels` in `flush_us`, taking `took_us` in all.
static bool frame(Stats& s, uint32_t at_us, uint32_t took_us, uint32_t pixels, uint32_t flush_us,
                  Report& out) {
    frame_stats::refreshStart(s, at_us);
    if (pixels > 0) frame_stats::flushed(s, pixels, flush_us);
    return frame_stats::refreshReady(s, at_us + took_us, (at_us + took_us) / 1000, out);
}

// The "perf:" line (#170): after 5 s, frames per second, frame and flush times, pixels per frame.
void test_reports_the_frames_drawn_in_a_5_s_window() {
    Stats s;
    Report r;
    TEST_ASSERT_FALSE(frame(s, 1000000, 10000, 8000, 2000, r));  // 10 ms, 8 kpx, 2 ms of copying
    TEST_ASSERT_FALSE(frame(s, 2000000, 30000, 16000, 4000, r));  // 30 ms, 16 kpx, 4 ms
    // The refresh that ends the window counts too.
    TEST_ASSERT_TRUE(frame(s, 5000000, 20000, 24000, 6000, r));
    TEST_ASSERT_EQUAL_UINT32(3, r.frames);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3 / 5.02f, r.framesPerSecond);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, r.frameAvgMs);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.0f, r.frameMaxMs);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.0f, r.flushMsPerFrame);
    TEST_ASSERT_EQUAL_UINT32(16, r.kpxPerFrame);
}

// Most refreshes find nothing to redraw. They aren't frames: an idle screen isn't 0 ms frames.
void test_a_refresh_that_flushes_nothing_is_not_a_frame() {
    Stats s;
    Report r;
    TEST_ASSERT_FALSE(frame(s, 1000000, 40000, 8000, 1000, r));
    for (uint32_t t = 1100000; t < 4900000; t += 33000) TEST_ASSERT_FALSE(frame(s, t, 50, 0, 0, r));
    TEST_ASSERT_TRUE(frame(s, 5000000, 40, 0, 0, r));
    TEST_ASSERT_EQUAL_UINT32(1, r.frames);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 40.0f, r.frameAvgMs);
}

// A window with no frames at all prints nothing, and each window starts from zero.
void test_an_idle_window_reports_nothing_and_windows_start_fresh() {
    Stats s;
    Report r;
    TEST_ASSERT_FALSE(frame(s, 5000000, 50, 0, 0, r));  // idle, window over: nothing to say
    TEST_ASSERT_FALSE(frame(s, 6000000, 10000, 8000, 1000, r));
    TEST_ASSERT_TRUE(frame(s, 10000000, 10000, 8000, 1000, r));
    TEST_ASSERT_EQUAL_UINT32(2, r.frames);
    TEST_ASSERT_FALSE(frame(s, 11000000, 90000, 8000, 1000, r));
    TEST_ASSERT_TRUE(frame(s, 15100000, 10000, 8000, 1000, r));
    TEST_ASSERT_EQUAL_UINT32(2, r.frames);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 90.0f, r.frameMaxMs);  // not the earlier window's
}

// micros() wraps every 71 minutes; a frame across the wrap still measures its real length.
void test_a_frame_across_the_microsecond_wrap_keeps_its_length() {
    Stats s;
    Report r;
    frame_stats::refreshStart(s, UINT32_MAX - 4999);
    frame_stats::flushed(s, 8000, 1000);
    TEST_ASSERT_TRUE(frame_stats::refreshReady(s, 5000, 6000, r));  // 10 ms later, 6 s into the window
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, r.frameAvgMs);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_reports_the_frames_drawn_in_a_5_s_window);
    RUN_TEST(test_a_refresh_that_flushes_nothing_is_not_a_frame);
    RUN_TEST(test_an_idle_window_reports_nothing_and_windows_start_fresh);
    RUN_TEST(test_a_frame_across_the_microsecond_wrap_keeps_its_length);
    return UNITY_END();
}
