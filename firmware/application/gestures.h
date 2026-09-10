#ifndef WALL_SWITCH_GESTURES_H
#define WALL_SWITCH_GESTURES_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    WS_GESTURE_NONE,
    WS_GESTURE_SINGLE,
    WS_GESTURE_DOUBLE,
    WS_GESTURE_LONG
} ws_gesture_event;

typedef struct {
    uint32_t double_window_ms;
    uint32_t hold_ms;
    uint32_t pressed_at;
    uint32_t released_at;
    bool pressed;
    bool suppressed;
    bool held;
    bool pending;
} ws_gestures;

/* Timings must be nonzero and less than 2^31 ms. No hardware defaults.
 * An initially pressed key is ignored until released. */
bool ws_gestures_init(ws_gestures *g, uint32_t double_window_ms,
                      uint32_t hold_ms, bool initially_pressed);

/* Input is DEBOUNCED. Call on each stable edge and periodically for deadlines,
 * in timestamp order. now_ms is a wrapping monotonic uint32_t clock; active
 * sequences/poll gaps must be shorter than 2^31 ms. Returns at most one event.
 * A second press strictly before the window deadline belongs to the first.
 * Holding that second press replaces the pending click with LONG.
 * This module neither changes relays nor transmits Zigbee commands. */
ws_gesture_event ws_gestures_update(ws_gestures *g, bool pressed,
                                    uint32_t now_ms);

/* Discard any partial gesture, e.g. when detach changes. A key currently held
 * remains suppressed until released, so changing mode cannot emit a click. */
void ws_gestures_cancel(ws_gestures *g, bool currently_pressed);

#endif
