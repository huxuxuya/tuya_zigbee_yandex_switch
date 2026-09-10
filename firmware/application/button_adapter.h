#ifndef WS_BUTTON_ADAPTER_H
#define WS_BUTTON_ADAPTER_H
#include "gestures.h"
#include "base_components/button.h"
typedef void (*ws_button_event_cb)(void *, ws_gesture_event);
typedef void (*ws_button_press_cb)(void *);
typedef struct {
    button_t button;
    ws_gestures gestures;
    hal_task_t deadline;
    ws_button_event_cb event;
    ws_button_press_cb press;
    void *context;
} ws_button_adapter;
bool ws_button_init(ws_button_adapter *a, hal_gpio_pin_t pin,
                    uint32_t window_ms, uint32_t hold_ms,
                    ws_button_event_cb event, ws_button_press_cb press, void *context);
void ws_button_cancel(ws_button_adapter *a);
#endif
