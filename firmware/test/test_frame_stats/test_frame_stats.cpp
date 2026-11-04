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

// The "perf:" line (#170): a window runs 5 s from its first frame, then reports frames per
// second, frame and flush times, and pixels per frame.
void test_reports_the_frames_drawn_in_a_5_s_window() {
    Stats s;
    Report r;
    TEST_ASSERT_FALSE(frame(s, 1000000, 10000, 8000, 2000, r));  // 10 ms, 8 kpx, 2 ms of copying
    TEST_ASSERT_FALSE(frame(s, 3000000, 30000, 16000, 4000, r));  // 30 ms, 16 kpx, 4 ms
    TEST_ASSERT_FALSE(frame(s, 5000000, 20000, 24000, 6000, r));  // ends 4.02 s into the window
    // The frame that ends the window counts too.
    TEST_ASSERT_TRUE(frame(s, 6000000, 20000, 16000, 4000, r));
    TEST_ASSERT_EQUAL_UINT32(4, r.frames);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4 / 5.02f, r.framesPerSecond);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, r.frameAvgMs);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.0f, r.frameMaxMs);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.0f, r.flushMsPerFrame);
    TEST_ASSERT_EQUAL_UINT32(16, r.kpxPerFrame);
}

// LVGL 9.1 pauses its refresh timer while nothing is invalid, so after a long idle the first key
// press is the first refresh in a minute. It starts a window; it doesn't close one across the idle.
void test_a_key_press_after_a_long_idle_starts_a_window() {
    Stats s;
    Report r;
    TEST_ASSERT_FALSE(frame(s, 1000000, 10000, 8000, 1000, r));   // a frame, then 60 s of nothing
    TEST_ASSERT_FALSE(frame(s, 61000000, 10000, 8000, 1000, r));  // the press
    TEST_ASSERT_FALSE(frame(s, 63000000, 10000, 8000, 1000, r));
    TEST_ASSERT_TRUE(frame(s, 66000000, 10000, 8000, 1000, r));
    TEST_ASSERT_EQUAL_UINT32(3, r.frames);  // not the one before the idle
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3 / 5.01f, r.framesPerSecond);
}

// A refresh that flushes nothing isn't a frame, and doesn't close a window.
void test_a_refresh_that_flushes_nothing_is_not_a_frame() {
    Stats s;
    Report r;
    TEST_ASSERT_FALSE(frame(s, 1000000, 40000, 8000, 1000, r));
    TEST_ASSERT_FALSE(frame(s, 3000000, 20000, 8000, 1000, r));
    TEST_ASSERT_FALSE(frame(s, 6500000, 50, 0, 0, r));  // 5.5 s into the window, but not a frame
    TEST_ASSERT_TRUE(frame(s, 6600000, 30000, 8000, 1000, r));
    TEST_ASSERT_EQUAL_UINT32(3, r.frames);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.0f, r.frameAvgMs);
}

// Each window starts from zero.
void test_windows_start_fresh() {
    Stats s;
    Report r;
    TEST_ASSERT_FALSE(frame(s, 6000000, 10000, 8000, 1000, r));
    TEST_ASSERT_TRUE(frame(s, 11000000, 10000, 8000, 1000, r));
    TEST_ASSERT_EQUAL_UINT32(2, r.frames);
    TEST_ASSERT_FALSE(frame(s, 11500000, 90000, 8000, 1000, r));
    TEST_ASSERT_FALSE(frame(s, 14000000, 10000, 8000, 1000, r));
    TEST_ASSERT_TRUE(frame(s, 16600000, 10000, 8000, 1000, r));
    TEST_ASSERT_EQUAL_UINT32(3, r.frames);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 90.0f, r.frameMaxMs);  // not the earlier window's
}

// micros() wraps every 71 minutes; a frame across the wrap still measures its real length.
void test_a_frame_across_the_microsecond_wrap_keeps_its_length() {
    Stats s;
    Report r;
    frame_stats::refreshStart(s, 100);
    frame_stats::flushed(s, 8000, 1000);
    TEST_ASSERT_FALSE(frame_stats::refreshReady(s, 10100, 1010, r));  // 10 ms, at 1 s
    frame_stats::refreshStart(s, UINT32_MAX - 4999);
    frame_stats::flushed(s, 8000, 1000);
    TEST_ASSERT_TRUE(frame_stats::refreshReady(s, 5000, 6010, r));  // 10 ms across the wrap, at 6 s
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, r.frameAvgMs);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_reports_the_frames_drawn_in_a_5_s_window);
    RUN_TEST(test_a_key_press_after_a_long_idle_starts_a_window);
    RUN_TEST(test_a_refresh_that_flushes_nothing_is_not_a_frame);
    RUN_TEST(test_windows_start_fresh);
    RUN_TEST(test_a_frame_across_the_microsecond_wrap_keeps_its_length);
    return UNITY_END();
}
