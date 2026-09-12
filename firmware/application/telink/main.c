#pragma pack(push, 1)
#include "tl_common.h"
#include "zb_common.h"
#include "zcl_include.h"
#pragma pack(pop)
#include "telink_size_t_hack.h"
#include "hal/zigbee.h"
#include "hal/timer.h"
#include "hal/telink_zigbee_hal.h"
#include "application/yandex_diagnostic.h"
#include "application/telink/network_rejoin.inc"

#define WS_BUTTON_WINDOW_MS       800
#define WS_BUTTON_HOLD_MS         2000
#define WS_BUTTON_RESET_HOLD_MS   10000
/* Tuya ZT3L pin numbering is not the Telink GPIO enum.  The stock config
 * maps led1_pin=7 -> PB7 and led2_pin=16 -> PD7 (see the upstream extractor).
 * PC2/PB1 are unrelated test/debug lines and must not be driven as LEDs. */
#define WS_LED1_PIN               GPIO_PB7
#define WS_LED2_PIN               GPIO_PD7
#define WS_RELAY1_ON_PIN          GPIO_PC2
#define WS_RELAY1_OFF_PIN         GPIO_PD4
#define WS_RELAY2_ON_PIN          GPIO_PB5
#define WS_RELAY2_OFF_PIN         GPIO_PC4
#define WS_RELAY_PULSE_MS         100

/* Diagnostic entry point. The two stock indicator outputs are low-current
 * LEDs driven from the virtual relay state; the four stock relay outputs
 * receive bounded latching-coil pulses.
 * No upstream OTA-layout migration, config parser, reset migration, or OTA init.
 * SDK commissioning/NVM still writes flash: NOT a read-only image. */
volatile uint8_t ws_diag_inject_channel;
volatile uint8_t ws_diag_inject_event; /* debugger writes event last; 1/2/3 */

int main(void) {
    (void)drv_platform_init();
    os_init(0);
    irq_enable();
    ws_diag_init();
    /* Stock Tuya config: led1_pin=7 (PB7), led2_pin=16 (PD7), led*_lv=1.
     * Force GPIO function explicitly before handing the pins to the HAL. */
    gpio_set_func(WS_LED1_PIN, AS_GPIO);
    gpio_set_func(WS_LED2_PIN, AS_GPIO);
    gpio_set_func(WS_RELAY1_ON_PIN, AS_GPIO);
    gpio_set_func(WS_RELAY1_OFF_PIN, AS_GPIO);
    gpio_set_func(WS_RELAY2_ON_PIN, AS_GPIO);
    gpio_set_func(WS_RELAY2_OFF_PIN, AS_GPIO);
    ws_diag_led_init(WS_LED1_PIN, WS_LED2_PIN, 1);
    ws_diag_relay_init(WS_RELAY1_ON_PIN, WS_RELAY1_OFF_PIN,
                       WS_RELAY2_ON_PIN, WS_RELAY2_OFF_PIN,
                       WS_RELAY_PULSE_MS);
    /* Tuya pin numbering maps bt1_pin=11 -> PC3 and bt2_pin=13 -> PD2.
     * The stock reset_t=10 is a 10 s hold; the normal
     * 2 s hold remains the Yandex long gesture. */
    (void)ws_diag_attach_button_with_reset(0, GPIO_PC3,
                                           WS_BUTTON_WINDOW_MS,
                                           WS_BUTTON_HOLD_MS,
                                           WS_BUTTON_RESET_HOLD_MS);
    (void)ws_diag_attach_button_with_reset(1, GPIO_PD2,
                                           WS_BUTTON_WINDOW_MS,
                                           WS_BUTTON_HOLD_MS,
                                           WS_BUTTON_RESET_HOLD_MS);
    drv_wd_setInterval(1000);
    drv_wd_start();
    bool announced = false;
    uint8_t announce_count = 0;
    uint32_t announce_at = 0;
    uint32_t retry_at = 0;
    bool led_was_blinking = false;
    while (1) {
        drv_wd_clear(); ev_main();
        drv_wd_clear(); tl_zbTaskProcedure();
        drv_wd_clear(); report_handler();
        ws_rejoin_poll(&ws_diag_rejoin,hal_millis(),ws_diag_network_ready(),ws_diag_request_rejoin);
        ws_diag_leave_reset_poll();
        uint8_t injection = ws_diag_inject_event;
        if (injection) {
            ws_diag_inject_event = 0;
            ws_diag_gesture(ws_diag_inject_channel, (ws_gesture_event)injection);
        }
        hal_zigbee_network_status_t state = hal_zigbee_get_network_status();
        uint32_t now = hal_millis();
        /* Original Tuya behavior: blink 500 ms while steering, solid
         * relay state once joined.  No diagnostic blink masks this state. */
        if (state != HAL_ZIGBEE_NETWORK_JOINED) {
            uint8_t blink = (uint8_t)((now / 500) & 1);
            ws_diag_led_set(0, blink);
            ws_diag_led_set(1, blink);
            led_was_blinking = true;
        } else if (led_was_blinking) {
            ws_diag_led_sync();
            led_was_blinking = false;
        }
        if (state == HAL_ZIGBEE_NETWORK_JOINED) {
            if (!announced) {
                (void)hal_zigbee_send_announce();
                announced = true; announce_count = 1; announce_at = now + 5000;
            } else if ((int32_t)(now - announce_at) >= 0) {
                /* MiDi's 45 s search window can start after the device has
                 * joined.  Keep the original burst, then send a low-rate
                 * announce so the coordinator can re-interview the same IEEE
                 * address after a late search or a deleted card. */
                (void)hal_zigbee_send_announce();
                if (announce_count < 3) {
                    announce_count++;
                    announce_at = now + (announce_count == 2 ? 10000 : 30000);
                } else {
                    announce_at = now + 60000;
                }
            }
        } else {
            announced = false; announce_count = 0;
        }
        if (state == HAL_ZIGBEE_NETWORK_NOT_JOINED && bdb_isIdle() &&
            ws_rejoin_allows_steering(&ws_diag_rejoin,now) && (int32_t)(now - retry_at) >= 0) {
            retry_at = now + 30000;
            hal_zigbee_start_network_steering();
        }
        if (!tl_stackBusy() && zb_isTaskDone()) {
            /* Arm every registered button GPIO before suspend.  Timer-only
             * wakeup loses a short press that starts and ends while asleep.
             * Re-arm on every pass because press and release use opposite
             * wake levels. */
            telink_gpio_hal_setup_wake_ups();
            ev_timer_event_t *timer = ev_timer_nearestGet();
            /* Keep 500 ms network blink smooth while steering. */
            uint32_t max_sleep = state != HAL_ZIGBEE_NETWORK_JOINED ? 250 : 1000;
            uint32_t duration = timer && timer->timeout < max_sleep ? timer->timeout : max_sleep;
            drv_pm_sleep(PM_SLEEP_MODE_SUSPEND,
                         PM_WAKEUP_SRC_PAD | PM_WAKEUP_SRC_TIMER, duration);
        }
    }
}
