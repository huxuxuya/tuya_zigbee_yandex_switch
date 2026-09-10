#include "gestures.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

static ws_gestures fresh(void) {
    ws_gestures g;
    assert(ws_gestures_init(&g, 250, 800, false));
    return g;
}
#define STEP(g, down, time, expected) \
    assert(ws_gestures_update(&(g), (down), (uint32_t)(time)) == (expected))
#define NONE WS_GESTURE_NONE
#define SINGLE WS_GESTURE_SINGLE
#define DOUBLE WS_GESTURE_DOUBLE
#define LONG WS_GESTURE_LONG

static void single(void) {
    ws_gestures g = fresh();
    STEP(g, false, 0, NONE);
    STEP(g, true, 10, NONE);
    STEP(g, true, 20, NONE);
    STEP(g, false, 60, NONE);
    STEP(g, false, 309, NONE);
    STEP(g, false, 310, SINGLE);
    STEP(g, false, 2000, NONE);
}
static void double_click(void) {
    ws_gestures g = fresh();
    STEP(g, true, 0, NONE);
    STEP(g, false, 50, NONE);
    STEP(g, true, 299, NONE);
    /* The second key is down beyond the first window: no premature single. */
    STEP(g, true, 400, NONE);
    STEP(g, false, 500, DOUBLE);
    STEP(g, false, 1500, NONE);
}
static void exact_window_boundary(void) {
    ws_gestures g = fresh();
    STEP(g, true, 0, NONE);
    STEP(g, false, 50, NONE);
    STEP(g, true, 300, SINGLE);
    STEP(g, false, 350, NONE);
    STEP(g, false, 600, SINGLE);
}
static void long_hold(void) {
    ws_gestures g = fresh();
    STEP(g, true, 100, NONE);
    STEP(g, true, 899, NONE);
    STEP(g, true, 900, LONG);
    STEP(g, true, 1500, NONE);
    STEP(g, false, 1600, NONE);
    STEP(g, false, 2000, NONE);
}
static void release_without_timer_poll(void) {
    ws_gestures g = fresh();
    STEP(g, true, 100, NONE);
    STEP(g, false, 900, LONG);
    STEP(g, false, 2000, NONE);
    STEP(g, true, 2100, NONE);
    STEP(g, false, 2899, NONE);
    STEP(g, false, 3149, SINGLE);
}
static void second_hold_overrides_click(void) {
    ws_gestures g = fresh();
    STEP(g, true, 0, NONE);
    STEP(g, false, 50, NONE);
    STEP(g, true, 100, NONE);
    STEP(g, true, 900, LONG);
    STEP(g, false, 1000, NONE);
    STEP(g, false, 2000, NONE);
}
static void triple_click(void) {
    ws_gestures g = fresh();
    STEP(g, true, 0, NONE);
    STEP(g, false, 50, NONE);
    STEP(g, true, 100, NONE);
    STEP(g, false, 150, DOUBLE);
    STEP(g, true, 200, NONE);
    STEP(g, false, 250, NONE);
    STEP(g, false, 500, SINGLE);
}
static void initial_hold(void) {
    ws_gestures g;
    assert(ws_gestures_init(&g, 250, 800, true));
    STEP(g, true, 5000, NONE);
    STEP(g, false, 6000, NONE);
    STEP(g, false, 7000, NONE);
    STEP(g, true, 7100, NONE);
    STEP(g, false, 7150, NONE);
    STEP(g, false, 7400, SINGLE);
}
static void mode_change(void) {
    ws_gestures g = fresh();
    STEP(g, true, 0, NONE);
    STEP(g, false, 50, NONE);
    ws_gestures_cancel(&g, false);
    STEP(g, false, 300, NONE);
    STEP(g, true, 400, NONE);
    ws_gestures_cancel(&g, true);
    STEP(g, true, 2000, NONE);
    STEP(g, false, 2100, NONE);
    STEP(g, false, 2400, NONE);
}
static void wrapping_clock(void) {
    ws_gestures g = fresh();
    uint32_t start = UINT32_MAX - 100;
    STEP(g, true, start, NONE);
    STEP(g, false, start + 50, NONE);
    STEP(g, false, start + 299, NONE);
    STEP(g, false, start + 300, SINGLE);
    g = fresh();
    STEP(g, true, start, NONE);
    STEP(g, true, start + 800, LONG);
    STEP(g, false, start + 900, NONE);
    g = fresh();
    STEP(g, true, start, NONE);
    STEP(g, false, start + 50, NONE);
    STEP(g, true, start + 150, NONE);
    STEP(g, false, start + 200, DOUBLE);
}
static void independent_keys(void) {
    ws_gestures left = fresh(), right = fresh();
    STEP(left, true, 0, NONE);
    STEP(right, true, 0, NONE);
    STEP(left, false, 50, NONE);
    STEP(left, false, 300, SINGLE);
    STEP(right, true, 800, LONG);
    STEP(right, false, 850, NONE);
}
static void invalid_config(void) {
    ws_gestures g = fresh();
    assert(!ws_gestures_init(NULL, 250, 800, false));
    assert(!ws_gestures_init(&g, 0, 800, false));
    assert(!ws_gestures_init(&g, 250, 0, false));
    assert(!ws_gestures_init(&g, UINT32_MAX, 800, false));
    assert(!ws_gestures_init(&g, 250, UINT32_C(0x80000000), false));
    assert(g.double_window_ms == 250 && g.hold_ms == 800);
}
int main(void) {
    single(); double_click(); exact_window_boundary(); long_hold();
    release_without_timer_poll(); second_hold_overrides_click(); triple_click();
    initial_hold(); mode_change(); wrapping_clock(); independent_keys();
    invalid_config();
    puts("gestures: 12 scenarios passed");
    return 0;
}
