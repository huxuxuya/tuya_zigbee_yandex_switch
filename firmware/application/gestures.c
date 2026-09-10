#include "gestures.h"
#include <stddef.h>

void ws_gestures_cancel(ws_gestures *g, bool currently_pressed) {
    g->pressed = currently_pressed;
    g->suppressed = currently_pressed;
    g->held = false;
    g->pending = false;
    g->pressed_at = 0;
    g->released_at = 0;
}

bool ws_gestures_init(ws_gestures *g, uint32_t double_window_ms,
                      uint32_t hold_ms, bool initially_pressed) {
    if (g == NULL || double_window_ms == 0 || hold_ms == 0 ||
        double_window_ms >= UINT32_C(0x80000000) ||
        hold_ms >= UINT32_C(0x80000000)) {
        return false;
    }
    g->double_window_ms = double_window_ms;
    g->hold_ms = hold_ms;
    ws_gestures_cancel(g, initially_pressed);
    return true;
}

ws_gesture_event ws_gestures_update(ws_gestures *g, bool pressed,
                                    uint32_t now_ms) {
    ws_gesture_event event = WS_GESTURE_NONE;
    if (g->suppressed) {
        g->pressed = pressed;
        if (!pressed) {
            g->suppressed = false;
        }
        return event;
    }

    /* Evaluate the previous stable state before processing an edge. A release
     * at/after the hold threshold is LONG even if a timer callback was late. */
    if (g->pressed && !g->held && now_ms - g->pressed_at >= g->hold_ms) {
        g->held = true;
        g->pending = false;
        event = WS_GESTURE_LONG;
    } else if (!g->pressed && g->pending &&
               now_ms - g->released_at >= g->double_window_ms) {
        g->pending = false;
        event = WS_GESTURE_SINGLE;
    }

    if (pressed && !g->pressed) {
        g->pressed_at = now_ms;
        g->held = false;
    } else if (!pressed && g->pressed) {
        if (!g->held) {
            if (g->pending) {
                g->pending = false;
                event = WS_GESTURE_DOUBLE;
            } else {
                g->pending = true;
                g->released_at = now_ms;
            }
        }
        g->held = false;
    }
    g->pressed = pressed;
    return event;
}
