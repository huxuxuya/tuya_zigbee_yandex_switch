#ifndef WS_YANDEX_DIAGNOSTIC_H
#define WS_YANDEX_DIAGNOSTIC_H
#include "button_adapter.h"
#include "rejoin.h"
extern ws_rejoin_state ws_diag_rejoin;
#include "hal/zigbee.h"

/* Host builds use the historical two-state HAL header; keep the exact ZCL
 * permission names available to source-linked diagnostic tests. */
#ifndef ATTR_READONLY_REPORTABLE
#define ATTR_READONLY_REPORTABLE ((hal_attr_flags_t)5)
#define ATTR_WRITABLE_REPORTABLE ((hal_attr_flags_t)7)
#endif

typedef struct {
    uint32_t zdo_node_desc_requests;
    uint32_t zdo_power_desc_requests;
    uint32_t zdo_active_ep_requests;
    uint32_t zdo_simple_desc_requests;
    uint32_t zcl_messages;
    uint32_t zcl_read_requests;
    uint32_t zcl_write_requests;
    uint32_t zcl_configure_reporting_requests;
    uint32_t zcl_discover_requests;
    uint16_t last_zdo_cluster;
    uint8_t last_zdo_endpoint;
    uint8_t last_zcl_endpoint;
    uint16_t last_zcl_cluster;
    uint8_t last_zcl_command;
} ws_diag_interview_counters;

extern volatile ws_diag_interview_counters ws_diag_interview;
void ws_diag_zdo_request_seen(uint16_t cluster_id, uint8_t endpoint);
void ws_diag_zcl_message_seen(uint8_t endpoint, uint16_t cluster_id,
                              uint8_t command_id);
/* Singleton HAL application. Relay GPIOs are optional; LED GPIOs are optional. */
void ws_diag_init(void);
/* Stock Tuya ZT3L config identifies LED1=PB7 and LED2=PD7, active-high. */
void ws_diag_led_init(hal_gpio_pin_t led0, hal_gpio_pin_t led1,
                      uint8_t active_level);
void ws_diag_led_set(uint8_t channel, uint8_t on);
/* Re-apply virtual relay state to both LEDs (used after blink/boot-test). */
void ws_diag_led_sync(void);
/* Attach the four active-high outputs of the two latching relays. Each
 * transition emits one bounded pulse, then returns both pins low. */
void ws_diag_relay_init(hal_gpio_pin_t on0, hal_gpio_pin_t off0,
                        hal_gpio_pin_t on1, hal_gpio_pin_t off1,
                        uint16_t pulse_ms);
/* All six descriptors exist; only selected endpoints are registered with AF. */
bool ws_diag_endpoint_active(uint8_t endpoint);
/* Platform hook: refresh AF registrations after a successfully persisted mode change.
 * Does not restart/rejoin the network. Called synchronously in the event loop. */
void ws_diag_platform_endpoints_changed(void);
bool ws_diag_attach_button(uint8_t channel, hal_gpio_pin_t pin,
                           uint32_t window_ms, uint32_t hold_ms);
/* Attach a physical active-low key and arm a separate very-long reset hold. */
bool ws_diag_attach_button_with_reset(uint8_t channel, hal_gpio_pin_t pin,
                                      uint32_t window_ms, uint32_t hold_ms,
                                      uint16_t reset_hold_ms);
/* Leave-then-reset: notify the coordinator before wiping local network state,
 * so a stale entry does not block the next interview for the same IEEE. */
void ws_diag_request_leave_reset(void);
void ws_diag_leave_reset_poll(void);
/* Software injection for simulator/debugger; channel is 0 or 1. */
void ws_diag_gesture(uint8_t channel, ws_gesture_event event);
#endif
