#include "button_adapter.h"
#include "hal/timer.h"
#include <string.h>

static void update(ws_button_adapter *a) {
    uint32_t now = hal_millis();
    ws_gesture_event event = ws_gestures_update(&a->gestures, a->button.pressed, now);
    hal_tasks_unschedule(&a->deadline);
    if (a->event && event != WS_GESTURE_NONE) a->event(a->context, event);
    ws_gestures *g = &a->gestures;
    if (!g->suppressed && ((g->pressed && !g->held) || (!g->pressed && g->pending))) {
        uint32_t elapsed = now - (g->pressed ? g->pressed_at : g->released_at);
        uint32_t duration = g->pressed ? g->hold_ms : g->double_window_ms;
        hal_tasks_schedule(&a->deadline, elapsed < duration ? duration - elapsed : 1);
    }
}
static void deadline(void *arg) { update(arg); }
static void pressed(void *arg) {
    ws_button_adapter *a = arg;
    update(a);
    if (a->press) a->press(a->context);
}
static void released(void *arg) { update(arg); }
bool ws_button_init(ws_button_adapter *a, hal_gpio_pin_t pin,
                    uint32_t window_ms, uint32_t hold_ms,
                    ws_button_event_cb event, ws_button_press_cb press, void *context) {
    if (!a || pin == HAL_INVALID_PIN || !window_ms || !hold_ms ||
        window_ms >= UINT32_C(0x80000000) || hold_ms >= UINT32_C(0x80000000)) return false;
    memset(a, 0, sizeof(*a));
    a->event = event; a->press = press; a->context = context;
    hal_gpio_init(pin, 1, HAL_GPIO_PULL_UP);
    a->button.pin = pin;
    a->button.debounce_delay_ms = DEBOUNCE_DELAY_MS;
    a->button.long_press_duration_ms = UINT16_MAX;
    a->button.on_press = pressed;
    a->button.on_release = released;
    a->button.callback_param = a;
    a->deadline.handler = deadline; a->deadline.arg = a;
    hal_tasks_init(&a->deadline);
    ws_gestures_init(&a->gestures, window_ms, hold_ms, hal_gpio_read(pin) == 0);
    btn_init(&a->button);
    return true;
}
void ws_button_cancel(ws_button_adapter *a) {
    hal_tasks_unschedule(&a->deadline);
    ws_gestures_cancel(&a->gestures, a->button.pressed);
}
