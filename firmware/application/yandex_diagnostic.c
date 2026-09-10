#ifdef HAL_TELINK
#include "telink_size_t_hack.h"
#endif
#include "yandex_diagnostic.h"
#include "hal/nvm.h"
#include "hal/system.h"
#include "hal/timer.h"
#include "hal/tasks.h"
ws_rejoin_state ws_diag_rejoin;
volatile ws_diag_interview_counters ws_diag_interview;
#include <string.h>

#ifdef HAL_TELINK
#pragma pack(push, 1)
#include "tl_common.h"
#include "zb_common.h"
#pragma pack(pop)
#include "flash.h"
#endif

#define FC03 0xFC03
#define NV_DIAGNOSTIC 0x40
#define NV_DIAGNOSTIC_VERSION 5
#define NV_DIAGNOSTIC_RECORD_SIZE 18
#define WS_POWER_TYPE_HIGH   0
#define WS_POWER_TYPE_MEDIUM 1
#define WS_POWER_TYPE_LOW    2
#define WS_POWER_TYPE_FULL   3
#define WS_LED_FEEDBACK_MS   300
static hal_zigbee_endpoint endpoints[6];
static hal_zigbee_cluster clusters[6][5];
static hal_zigbee_attribute basic[15], identify[6][2], onoff[2][6], yandex_attr[2][5];
/* Match the values exposed by a real YNDX-00532 v24 during interview. */
static uint8_t zcl_version = 8, app_version = 24, stack_version = 2,
              hw_version = 0, power_source = 1;
static uint8_t manufacturer[] = {6, 'Y','a','n','d','e','x'};
static uint8_t model[] = {10, 'Y','N','D','X','-','0','0','5','3','2'};
static uint8_t date_code[] = {12, '2','0','2','5','0','6','2','0','1','9','2','3'};
static uint8_t build[] = {11, 'M','a','r',' ','1','9',' ','2','0','2','5'};
/* Seen next to the YNDX-00531/00532 model strings in the v24 OTA payload
 * (0x26419...); the family shares one smart-home page. */
static uint8_t product_url[] = {35,'a','l','i','c','e','.','y','a','n','d','e','x','.','r','u','/','s','m','a','r','t','-','h','o','m','e','/','s','w','i','t','c','h','e','s'};
static uint8_t generic_device_class;
static uint8_t generic_device_type = 0xE1;
static uint8_t product_code[] = {11, 4, 'Y','N','D','X','-','0','0','5','3','2'};
static uint8_t serial_number[34];
static uint16_t basic_revision = 3;
static uint16_t identify_time[6], identify_revision = 2;
static uint8_t relay[2], startup_onoff[2], global_scene_control[2] = {1,1};
static uint16_t on_time[2], off_wait_time[2], onoff_revision = 2;
static uint8_t mode[2], indicator_mode[2] = {3,3}, power_type[2] = {2,2}, led_indicator[2];
static uint8_t persisted_mode[2], persisted_startup_onoff[2];
static uint8_t persisted_led_indicator[2];
static uint8_t persisted_power_type;
static uint16_t persisted_on_time[2], persisted_off_wait_time[2];
static uint8_t startup_pulse_pending[2];
static uint16_t yandex_revision = 1;
static uint8_t indices[2] = {0,1};
static ws_button_adapter buttons[2];
static bool attached[2];
static hal_gpio_pin_t led_pins[2] = {HAL_INVALID_PIN, HAL_INVALID_PIN};
static uint8_t led_active_level;
static bool leds_attached;
static hal_gpio_pin_t relay_on_pins[2] = {HAL_INVALID_PIN, HAL_INVALID_PIN};
static hal_gpio_pin_t relay_off_pins[2] = {HAL_INVALID_PIN, HAL_INVALID_PIN};
static hal_task_t relay_pulse_tasks[2];
static hal_task_t led_feedback_tasks[2];
static hal_task_t onoff_timed_tasks[2];
static bool led_feedback_active[2];
static uint8_t onoff_timed_stage[2];
static uint32_t onoff_timed_remaining_ms[2];
static uint16_t relay_pulse_ms;
static bool relays_attached;

static bool save_diag_state(void);
static void onoff_timed_done(void *context);

void ws_diag_zdo_request_seen(uint16_t cluster_id, uint8_t endpoint) {
    ws_diag_interview.last_zdo_cluster = cluster_id;
    ws_diag_interview.last_zdo_endpoint = endpoint;
    switch (cluster_id) {
    case 0x0002: ws_diag_interview.zdo_node_desc_requests++; break;
    case 0x0003: ws_diag_interview.zdo_power_desc_requests++; break;
    case 0x0004: ws_diag_interview.zdo_simple_desc_requests++; break;
    case 0x0005: ws_diag_interview.zdo_active_ep_requests++; break;
    default: break;
    }
}

void ws_diag_zcl_message_seen(uint8_t endpoint, uint16_t cluster_id,
                              uint8_t command_id) {
    ws_diag_interview.zcl_messages++;
    ws_diag_interview.last_zcl_endpoint = endpoint;
    ws_diag_interview.last_zcl_cluster = cluster_id;
    ws_diag_interview.last_zcl_command = command_id;
    switch (command_id) {
    case 0x00: ws_diag_interview.zcl_read_requests++; break;
    case 0x02:
    case 0x05: ws_diag_interview.zcl_write_requests++; break;
    case 0x06: ws_diag_interview.zcl_configure_reporting_requests++; break;
    case 0x0C:
    case 0x15: ws_diag_interview.zcl_discover_requests++; break;
    default: break;
    }
}
static hal_zigbee_attribute attr_flags(uint16_t id, uint8_t type, uint8_t size,
                                       void *value, hal_attr_flags_t flags) {
    hal_zigbee_attribute a = {id,type,flags,size,value}; return a;
}
static hal_zigbee_attribute attr(uint16_t id, uint8_t type, uint8_t size, void *value) {
    return attr_flags(id, type, size, value, ATTR_READONLY);
}
static hal_zigbee_attribute rattr(uint16_t id, uint8_t type, uint8_t size, void *value) {
    return attr_flags(id, type, size, value, ATTR_READONLY_REPORTABLE);
}
static hal_zigbee_attribute wattr(uint16_t id, uint8_t type, uint8_t size, void *value) {
    return attr_flags(id, type, size, value, ATTR_WRITABLE);
}

static void serial_hex_fallback(void) {
    static const char hex[] = "0123456789ABCDEF";
#ifdef HAL_TELINK
    const uint8_t *ieee = (const uint8_t *)ZB_PIB_EXTENDED_ADDRESS();
#else
    static const uint8_t ieee[8] = {0x54,0x45,0x53,0x54,0x53,0x45,0x52,0x1};
#endif
    const char prefix[] = "IEEE-";
    memcpy(serial_number + 1, prefix, sizeof(prefix) - 1);
    for (uint8_t i = 0; i < 8; i++) {
        serial_number[6 + i * 2] = hex[ieee[i] >> 4];
        serial_number[7 + i * 2] = hex[ieee[i] & 0x0F];
    }
    serial_number[0] = 21;
}

static void serial_init(void) {
#ifdef HAL_TELINK
    uint8_t raw[32];
    flash_read_page(0x0F10D0, sizeof(raw), raw);
    uint8_t length = 0;
    while (length < sizeof(raw) && raw[length] != 0 && raw[length] != 0xFF)
        length++;
    bool printable = length != 0;
    for (uint8_t i = 0; i < length; i++)
        if (raw[i] < 0x20 || raw[i] > 0x7E) printable = false;
    if (printable) {
        serial_number[0] = length;
        memcpy(serial_number + 1, raw, length);
        if (length + 1 < sizeof(serial_number)) serial_number[length + 1] = 0;
    } else {
        serial_hex_fallback();
    }
#else
    serial_hex_fallback();
#endif
}
static bool detached(uint8_t ch) { return mode[ch] == 2 || mode[ch] == 3; }
static void ws_diag_led_write_raw(uint8_t ch, uint8_t on) {
    if (ch > 1 || !leds_attached) return;
    hal_gpio_write(led_pins[ch], on ? led_active_level : (uint8_t)!led_active_level);
}
/* Network/commissioning indications intentionally bypass ledIndicator.  The
 * factory device uses the same pins for both relay feedback and pairing
 * status, so disabling the normal feedback must not hide a pairing search. */
void ws_diag_led_set(uint8_t ch, uint8_t on) {
    ws_diag_led_write_raw(ch, on);
}
static void ws_diag_led_apply_channel(uint8_t ch) {
    if (ch > 1) return;
    /* YNDX-00532 exposes one ledIndicator setting on EP1.  The original
     * firmware stores this as indicatorDisable: 0 permits normal indication,
     * 1 disables it.  It is a global configuration switch for the pair of
     * channel/status indicators.  The Yandex power type then selects the
     * steady/pulse/off policy. */
    bool on = false;
    if (!led_indicator[0] &&
        (power_type[0] == WS_POWER_TYPE_HIGH || power_type[0] == WS_POWER_TYPE_FULL))
        on = !relay[ch];
    ws_diag_led_write_raw(ch, on);
}
void ws_diag_led_sync(void) {
    ws_diag_led_apply_channel(0);
    ws_diag_led_apply_channel(1);
}
void ws_diag_led_init(hal_gpio_pin_t led0, hal_gpio_pin_t led1,
                      uint8_t active_level) {
    if (led0 == HAL_INVALID_PIN || led1 == HAL_INVALID_PIN || led0 == led1)
        return;
    led_pins[0] = led0; led_pins[1] = led1;
    led_active_level = active_level ? 1 : 0;
    hal_gpio_init(led0, 0, HAL_GPIO_PULL_NONE);
    hal_gpio_init(led1, 0, HAL_GPIO_PULL_NONE);
    leds_attached = true;
    ws_diag_led_sync();
}
static void led_feedback_done(void *context) {
    uint8_t ch = *(uint8_t *)context;
    if (ch > 1) return;
    led_feedback_active[ch] = false;
    ws_diag_led_apply_channel(ch);
}
static void ws_diag_led_feedback(uint8_t ch) {
    if (ch > 1 || !leds_attached || led_indicator[0] ||
        power_type[0] != WS_POWER_TYPE_MEDIUM)
        return;
    hal_tasks_unschedule(&led_feedback_tasks[ch]);
    led_feedback_active[ch] = true;
    ws_diag_led_write_raw(ch, 1);
    hal_tasks_schedule(&led_feedback_tasks[ch], WS_LED_FEEDBACK_MS);
}
static void relay_pulse_done(void *context) {
    uint8_t ch = *(uint8_t *)context;
    if (ch > 1 || !relays_attached) return;
    hal_gpio_clear(relay_on_pins[ch]);
    hal_gpio_clear(relay_off_pins[ch]);
}
static void relay_pulse(uint8_t ch, uint8_t on) {
    if (ch > 1 || !relays_attached) return;
    hal_tasks_unschedule(&relay_pulse_tasks[ch]);
    /* A latching driver must never see both coils asserted. */
    hal_gpio_clear(relay_on_pins[ch]);
    hal_gpio_clear(relay_off_pins[ch]);
    hal_gpio_set(on ? relay_on_pins[ch] : relay_off_pins[ch]);
    hal_tasks_schedule(&relay_pulse_tasks[ch], relay_pulse_ms);
}
void ws_diag_relay_init(hal_gpio_pin_t on0, hal_gpio_pin_t off0,
                        hal_gpio_pin_t on1, hal_gpio_pin_t off1,
                        uint16_t pulse_ms) {
    hal_gpio_pin_t pins[] = {on0, off0, on1, off1};
    if (!pulse_ms || on0 == HAL_INVALID_PIN || off0 == HAL_INVALID_PIN ||
        on1 == HAL_INVALID_PIN || off1 == HAL_INVALID_PIN)
        return;
    for (unsigned i=0;i<4;i++) for (unsigned j=0;j<i;j++)
        if (pins[i] == pins[j]) return;
    if (relays_attached) {
        for (uint8_t ch=0;ch<2;ch++) hal_tasks_unschedule(&relay_pulse_tasks[ch]);
    }
    relay_on_pins[0] = on0; relay_off_pins[0] = off0;
    relay_on_pins[1] = on1; relay_off_pins[1] = off1;
    relay_pulse_ms = pulse_ms;
    for (uint8_t ch=0;ch<2;ch++) {
        hal_gpio_init(relay_on_pins[ch], 0, HAL_GPIO_PULL_NONE);
        hal_gpio_init(relay_off_pins[ch], 0, HAL_GPIO_PULL_NONE);
        hal_gpio_clear(relay_on_pins[ch]);
        hal_gpio_clear(relay_off_pins[ch]);
        relay_pulse_tasks[ch].handler = relay_pulse_done;
        relay_pulse_tasks[ch].arg = &indices[ch];
        hal_tasks_init(&relay_pulse_tasks[ch]);
    }
    relays_attached = true;
    for (uint8_t ch = 0; ch < 2; ch++) {
        if (startup_pulse_pending[ch]) {
            relay_pulse(ch, relay[ch]);
            startup_pulse_pending[ch] = 0;
        }
    }
    (void)save_diag_state();
}
bool ws_diag_endpoint_active(uint8_t ep) {
    if (ep==1 || ep==2) return true;
    if (ep==3 || ep==4) return mode[ep-3]==2 || mode[ep-3]==3;
    if (ep==5 || ep==6) return mode[ep-5]==1 || mode[ep-5]==2;
    return false;
}
static bool set_relay(uint8_t ch, uint8_t state) {
    if (ch > 1) return false;
    if (relay[ch] == state) return true;
    uint8_t old = relay[ch];
    relay[ch] = state;
    if (!save_diag_state()) {
        relay[ch] = old;
        return false;
    }
    relay_pulse(ch, state);
    ws_diag_led_apply_channel(ch);
    hal_zigbee_notify_attribute_changed(ch + 1, 6, 0);
    return true;
}
static bool save_diag_state(void) {
    uint8_t record[NV_DIAGNOSTIC_RECORD_SIZE] = {
        NV_DIAGNOSTIC_VERSION, mode[0], mode[1],
        power_type[0], led_indicator[0], startup_onoff[0], startup_onoff[1],
        relay[0], relay[1],
        (uint8_t)on_time[0], (uint8_t)(on_time[0] >> 8),
        (uint8_t)off_wait_time[0], (uint8_t)(off_wait_time[0] >> 8),
        (uint8_t)on_time[1], (uint8_t)(on_time[1] >> 8),
        (uint8_t)off_wait_time[1], (uint8_t)(off_wait_time[1] >> 8),
        0
    };
    uint8_t checksum = 0xA5;
    for (uint8_t i = 0; i < NV_DIAGNOSTIC_RECORD_SIZE - 1; i++)
        checksum ^= record[i];
    record[NV_DIAGNOSTIC_RECORD_SIZE - 1] = checksum;
    return hal_nvm_write(NV_DIAGNOSTIC, sizeof(record), record) == HAL_NVM_SUCCESS;
}
static void onoff_timed_done(void *context) {
    uint8_t ch = *(uint8_t *)context;
    if (ch > 1) return;
    if (onoff_timed_stage[ch] == 1) {
        on_time[ch] = 0;
        off_wait_time[ch] = 0;
        (void)set_relay(ch, 0);
        onoff_timed_stage[ch] = 0;
        onoff_timed_remaining_ms[ch] = 0;
    } else {
        off_wait_time[ch] = 0;
        onoff_timed_stage[ch] = 0;
        onoff_timed_remaining_ms[ch] = 0;
        (void)save_diag_state();
    }
}
static bool set_led_indicator(uint8_t next) {
    if (next > 1) return false;
    if (led_indicator[0] == next && led_indicator[1] == next) {
        ws_diag_led_sync();
        return true;
    }
    uint8_t old0 = led_indicator[0], old1 = led_indicator[1];
    led_indicator[0] = led_indicator[1] = next;
    if (!save_diag_state()) {
        led_indicator[0] = old0; led_indicator[1] = old1;
        ws_diag_led_sync();
        return false;
    }
    persisted_led_indicator[0] = persisted_led_indicator[1] = next;
    ws_diag_led_sync();
    hal_zigbee_notify_attribute_changed(1, FC03, 5);
    return true;
}
static bool set_power_type(uint8_t next) {
    if (next > WS_POWER_TYPE_FULL) return false;
    if (power_type[0] == next && power_type[1] == next) {
        ws_diag_led_sync();
        return true;
    }
    uint8_t old0 = power_type[0], old1 = power_type[1];
    power_type[0] = power_type[1] = next;
    if (!save_diag_state()) {
        power_type[0] = old0; power_type[1] = old1;
        ws_diag_led_sync();
        return false;
    }
    persisted_power_type = next;
    ws_diag_led_sync();
    hal_zigbee_notify_attribute_changed(1, FC03, 3);
    return true;
}
static void ws_diag_attribute_changed(uint8_t ep, uint16_t cluster,
                                      uint16_t attribute) {
    if (ep < 1 || ep > 2) return;
    if (cluster == 6 && (attribute == 0x4001 || attribute == 0x4002 ||
                         attribute == 0x4003)) {
        uint8_t ch = ep - 1;
        if (attribute == 0x4003 && startup_onoff[ch] > 3) {
            startup_onoff[ch] = persisted_startup_onoff[ch];
            return;
        }
        if (!save_diag_state()) {
            startup_onoff[ch] = persisted_startup_onoff[ch];
            on_time[ch] = persisted_on_time[ch];
            off_wait_time[ch] = persisted_off_wait_time[ch];
            return;
        }
        persisted_startup_onoff[ch] = startup_onoff[ch];
        persisted_on_time[ch] = on_time[ch];
        persisted_off_wait_time[ch] = off_wait_time[ch];
        return;
    }
    if (cluster != FC03 || attribute < 1 || attribute > 5 ||
        (attribute != 1 && attribute != 3 && attribute != 5)) return;
    if (attribute == 1) {
        uint8_t ch = ep - 1;
        uint8_t next = mode[ch];
        if (next > 3) {
            mode[ch] = persisted_mode[ch];
            return;
        }
        if (next == persisted_mode[ch]) return;
        uint8_t old = persisted_mode[ch];
        if (!save_diag_state()) {
            mode[ch] = old;
            return;
        }
        persisted_mode[ch] = next;
        if (attached[ch]) ws_button_cancel(&buttons[ch]);
        ws_diag_platform_endpoints_changed();
        ws_rejoin_changed(&ws_diag_rejoin,hal_millis());
        hal_zigbee_notify_attribute_changed(ep, FC03, 1);
        return;
    }
    /* The Telink ZCL layer has already written the value through the pointer
     * in yandex_attr[].  Validate it and persist the change made by a normal
     * ZCL Write Attributes request, not only by the custom 0x05 command. */
    if (attribute == 3) {
        uint8_t next = power_type[0];
        if (next > WS_POWER_TYPE_FULL) {
            power_type[0] = power_type[1] = persisted_power_type;
            ws_diag_led_sync();
            return;
        }
        if (next == persisted_power_type && power_type[1] == persisted_power_type) {
            ws_diag_led_sync();
            return;
        }
        uint8_t old = persisted_power_type;
        power_type[1] = next;
        if (!save_diag_state()) {
            power_type[0] = power_type[1] = old;
            ws_diag_led_sync();
            return;
        }
        power_type[0] = power_type[1] = next;
        persisted_power_type = next;
        ws_diag_led_sync();
        return;
    }
    uint8_t next = led_indicator[0];
    if (next > 1) {
        led_indicator[0] = persisted_led_indicator[0];
        led_indicator[1] = persisted_led_indicator[1];
        ws_diag_led_sync();
        return;
    }
    if (next == persisted_led_indicator[0] &&
        led_indicator[1] == persisted_led_indicator[1]) {
        ws_diag_led_sync();
        return;
    }
    uint8_t old0 = persisted_led_indicator[0], old1 = persisted_led_indicator[1];
    led_indicator[1] = next;
    if (!save_diag_state()) {
        led_indicator[0] = old0; led_indicator[1] = old1;
        ws_diag_led_sync();
        return;
    }
    persisted_led_indicator[0] = persisted_led_indicator[1] = next;
    ws_diag_led_sync();
}
static hal_zigbee_cmd_result_t command(uint8_t ep, uint16_t cluster, uint8_t cmd,
                                       void *payload, uint16_t length) {
    if (ep < 1 || ep > 2) return HAL_ZIGBEE_CMD_SKIPPED;
    uint8_t ch = ep - 1;
    if (cluster == 6) {
        if (cmd <= 2) {
            if (length) return HAL_ZIGBEE_MALFORMED_COMMAND;
            if (onoff_timed_stage[ch] == 2 && cmd != 0)
                return HAL_ZIGBEE_CMD_PROCESSED;
            if (onoff_timed_stage[ch] == 1 && cmd == 0) {
                on_time[ch] = 0;
                hal_tasks_unschedule(&onoff_timed_tasks[ch]);
                onoff_timed_stage[ch] = off_wait_time[ch] ? 2 : 0;
                onoff_timed_remaining_ms[ch] = 0;
                if (!set_relay(ch, 0)) return HAL_ZIGBEE_ACTION_DENIED;
                if (onoff_timed_stage[ch] == 2)
                    hal_tasks_schedule(&onoff_timed_tasks[ch],
                                       (uint32_t)off_wait_time[ch] * 100u);
                return HAL_ZIGBEE_CMD_PROCESSED;
            }
            hal_tasks_unschedule(&onoff_timed_tasks[ch]);
            onoff_timed_stage[ch] = 0;
            onoff_timed_remaining_ms[ch] = 0;
            on_time[ch] = 0;
            off_wait_time[ch] = 0;
            return set_relay(ch, cmd == 2 ? !relay[ch] : cmd)
                ? HAL_ZIGBEE_CMD_PROCESSED : HAL_ZIGBEE_ACTION_DENIED;
        }
        if (cmd == 0x42) {
            if (length != 5 || !payload) return HAL_ZIGBEE_MALFORMED_COMMAND;
            const uint8_t *p = (const uint8_t *)payload;
            if (p[0] & 0xFE) return HAL_ZIGBEE_INVALID_VALUE;
            uint16_t on_ticks = (uint16_t)p[1] | ((uint16_t)p[2] << 8);
            uint16_t off_ticks = (uint16_t)p[3] | ((uint16_t)p[4] << 8);
            if (on_ticks == 0xFFFF || off_ticks == 0xFFFF)
                return HAL_ZIGBEE_INVALID_VALUE;
            if ((p[0] & 1) && !relay[ch]) return HAL_ZIGBEE_CMD_PROCESSED;
            if (onoff_timed_stage[ch] == 2 && !relay[ch]) {
                on_time[ch] = 0;
                off_wait_time[ch] = off_wait_time[ch] < off_ticks
                    ? off_wait_time[ch] : off_ticks;
                if (!save_diag_state()) return HAL_ZIGBEE_ACTION_DENIED;
                if (!off_wait_time[ch]) {
                    onoff_timed_stage[ch] = 0;
                    hal_tasks_unschedule(&onoff_timed_tasks[ch]);
                } else {
                    hal_tasks_unschedule(&onoff_timed_tasks[ch]);
                    hal_tasks_schedule(&onoff_timed_tasks[ch],
                                       (uint32_t)off_wait_time[ch] * 100u);
                }
                return HAL_ZIGBEE_CMD_PROCESSED;
            }
            on_time[ch] = on_time[ch] > on_ticks ? on_time[ch] : on_ticks;
            off_wait_time[ch] = off_ticks;
            if (!on_time[ch]) off_wait_time[ch] = 0;
            if (!save_diag_state()) return HAL_ZIGBEE_ACTION_DENIED;
            persisted_on_time[ch] = on_time[ch];
            persisted_off_wait_time[ch] = off_wait_time[ch];
            hal_tasks_unschedule(&onoff_timed_tasks[ch]);
            onoff_timed_stage[ch] = 0;
            onoff_timed_remaining_ms[ch] = 0;
            if (!set_relay(ch, 1)) return HAL_ZIGBEE_ACTION_DENIED;
            if (on_time[ch]) {
                onoff_timed_stage[ch] = 1;
                onoff_timed_remaining_ms[ch] = (uint32_t)on_time[ch] * 100u;
                hal_tasks_schedule(&onoff_timed_tasks[ch], onoff_timed_remaining_ms[ch]);
            }
            return HAL_ZIGBEE_CMD_PROCESSED;
        }
        return HAL_ZIGBEE_CMD_SKIPPED;
    }
    if (cluster != FC03 || (cmd != 1 && cmd != 3 && cmd != 5)) return HAL_ZIGBEE_CMD_SKIPPED;
    if (length != 1 || !payload) return HAL_ZIGBEE_MALFORMED_COMMAND;
    uint8_t next = *(uint8_t *)payload;
    if (cmd == 3) {
        if (ep != 1) return HAL_ZIGBEE_CMD_SKIPPED;
        if (next > 3) return HAL_ZIGBEE_INVALID_VALUE;
        if (!set_power_type(next)) return HAL_ZIGBEE_ACTION_DENIED;
        return HAL_ZIGBEE_CMD_PROCESSED;
    }
    if (cmd == 5) {
        if (ep != 1) return HAL_ZIGBEE_CMD_SKIPPED;
        if (next > 1) return HAL_ZIGBEE_INVALID_VALUE;
        if (!set_led_indicator(next)) return HAL_ZIGBEE_ACTION_DENIED;
        return HAL_ZIGBEE_CMD_PROCESSED;
    }
    if (next > 3) return HAL_ZIGBEE_INVALID_VALUE;
    if (mode[ch] == next) return HAL_ZIGBEE_CMD_PROCESSED;
    uint8_t old = mode[ch];
    mode[ch] = next;
    if (!save_diag_state()) {
        mode[ch] = old;
        return HAL_ZIGBEE_ACTION_DENIED;
    }
    persisted_mode[ch] = next;
    if (attached[ch]) ws_button_cancel(&buttons[ch]);
    ws_diag_platform_endpoints_changed();
    ws_rejoin_changed(&ws_diag_rejoin,hal_millis());
    hal_zigbee_notify_attribute_changed(ep, FC03, 1);
    return HAL_ZIGBEE_CMD_PROCESSED;
}
void ws_diag_gesture(uint8_t ch, ws_gesture_event event) {
    if (ch > 1 || !detached(ch) || event < WS_GESTURE_SINGLE || event > WS_GESTURE_LONG) return;
    static const uint8_t commands[] = {0,2,1,0};
    hal_zigbee_cmd cmd = {0};
    cmd.endpoint = ch + 3; cmd.profile_id = 0x0104; cmd.cluster_id = 6;
    cmd.command_id = commands[event]; cmd.cluster_specific = 1;
    cmd.direction = HAL_ZIGBEE_DIR_CLIENT_TO_SERVER; cmd.disable_default_rsp = 1;
    if (hal_zigbee_get_network_status() == HAL_ZIGBEE_NETWORK_JOINED)
        (void)hal_zigbee_send_cmd_to_bindings(&cmd);
}
static void gesture_cb(void *context, ws_gesture_event event) {
    ws_diag_gesture(*(uint8_t *)context, event);
}
static void press_cb(void *context) {
    uint8_t ch = *(uint8_t *)context;
    /* Preserve Yandex detach once joined, but retain a useful local fallback
     * while the device is offline or still commissioning. */
    if (!detached(ch) || hal_zigbee_get_network_status() != HAL_ZIGBEE_NETWORK_JOINED)
        (void)set_relay(ch, !relay[ch]);
    ws_diag_led_feedback(ch);
}
static void reset_cb(void *context) {
    (void)context;
    ws_diag_request_leave_reset();
}
/* Call through a plain function pointer so the host HAL stub can return after
 * recording a reset request; the production HAL still never returns. */
static void request_system_reset(void) {
    void (*reset_fn)(void) = hal_system_reset;
    reset_fn();
}
/* A bare Leave frame needs airtime before the local wipe + reboot, so the
 * very-long hold only arms the sequence; the main loop finishes it. */
static bool leave_reset_pending;
static uint32_t leave_reset_at;
void ws_diag_request_leave_reset(void) {
    if (leave_reset_pending) return;
    leave_reset_pending = true;
    leave_reset_at = hal_millis();
    if (hal_zigbee_get_network_status() != HAL_ZIGBEE_NETWORK_JOINED) {
        hal_factory_reset();
        request_system_reset();
        return;
    }
    hal_zigbee_leave_network();
}
void ws_diag_leave_reset_poll(void) {
    if (!leave_reset_pending) return;
    uint32_t now = hal_millis();
    bool gone = hal_zigbee_get_network_status() != HAL_ZIGBEE_NETWORK_JOINED;
    if ((gone && (int32_t)(now - leave_reset_at) >= 1500) ||
        (int32_t)(now - leave_reset_at) >= 5000) {
        hal_factory_reset();
        request_system_reset();
    }
}
bool ws_diag_attach_button_with_reset(uint8_t ch, hal_gpio_pin_t pin,
                                      uint32_t window_ms, uint32_t hold_ms,
                                      uint16_t reset_hold_ms) {
    if (ch > 1 || attached[ch]) return false;
    if (attached[1-ch] && buttons[1-ch].button.pin == pin) return false;
    attached[ch] = ws_button_init(&buttons[ch], pin, window_ms, hold_ms,
                                  gesture_cb, press_cb, &indices[ch]);
    if (attached[ch] && reset_hold_ms != 0) {
        buttons[ch].button.long_press_duration_ms = reset_hold_ms;
        buttons[ch].button.on_long_press = reset_cb;
    }
    return attached[ch];
}
bool ws_diag_attach_button(uint8_t ch, hal_gpio_pin_t pin,
                           uint32_t window_ms, uint32_t hold_ms) {
    return ws_diag_attach_button_with_reset(ch, pin, window_ms, hold_ms, 0);
}
void ws_diag_init(void) {
    /* Once per boot; do not reinitialize while HAL callbacks are active. */
    memset((void *)&ws_diag_interview, 0, sizeof(ws_diag_interview));
    ws_rejoin_init(&ws_diag_rejoin);
    memset(relay,0,sizeof(relay));
    if (relays_attached) {
        for (uint8_t ch=0;ch<2;ch++) hal_tasks_unschedule(&relay_pulse_tasks[ch]);
    }
    relays_attached = false;
    relay_on_pins[0] = relay_on_pins[1] = HAL_INVALID_PIN;
    relay_off_pins[0] = relay_off_pins[1] = HAL_INVALID_PIN;
    for (uint8_t ch=0; ch<2; ch++) {
        hal_tasks_unschedule(&led_feedback_tasks[ch]);
        led_feedback_tasks[ch].handler = led_feedback_done;
        led_feedback_tasks[ch].arg = &indices[ch];
        hal_tasks_init(&led_feedback_tasks[ch]);
        led_feedback_active[ch] = false;
        hal_tasks_unschedule(&onoff_timed_tasks[ch]);
        onoff_timed_tasks[ch].handler = onoff_timed_done;
        onoff_timed_tasks[ch].arg = &indices[ch];
        hal_tasks_init(&onoff_timed_tasks[ch]);
        onoff_timed_stage[ch] = 0;
    }
    /* The first YNDX-00532 interview exposes both up/down button zones.
     * Use the full detach profile until the coordinator writes an explicit
     * operation mode.  This avoids presenting only EP1/EP2 during pairing,
     * which some hubs reject before they have created the device card. */
    mode[0] = mode[1] = 2;
    memset(attached,0,sizeof(attached)); memset(identify_time,0,sizeof(identify_time));
    leds_attached = false;
    leave_reset_pending = false;
    led_pins[0] = led_pins[1] = HAL_INVALID_PIN;
    power_type[0] = power_type[1] = WS_POWER_TYPE_FULL;
    persisted_power_type = WS_POWER_TYPE_FULL;
    led_indicator[0] = led_indicator[1] = 0;
    persisted_led_indicator[0] = persisted_led_indicator[1] = 0;
    memset(startup_onoff, 0, sizeof(startup_onoff));
    memset(on_time, 0, sizeof(on_time));
    memset(off_wait_time, 0, sizeof(off_wait_time));
    memset(persisted_startup_onoff, 0, sizeof(persisted_startup_onoff));
    memset(persisted_on_time, 0, sizeof(persisted_on_time));
    memset(persisted_off_wait_time, 0, sizeof(persisted_off_wait_time));
    uint8_t record[NV_DIAGNOSTIC_RECORD_SIZE];
    if (hal_nvm_read(NV_DIAGNOSTIC, sizeof(record), record) == HAL_NVM_SUCCESS &&
        record[0] == NV_DIAGNOSTIC_VERSION && record[1] <= 3 && record[2] <= 3 &&
        record[3] <= WS_POWER_TYPE_FULL && record[4] <= 1 &&
        record[5] <= 3 && record[6] <= 3 && record[7] <= 1 && record[8] <= 1 &&
        ((uint16_t)record[9] | ((uint16_t)record[10] << 8)) <= 0xFFFF &&
        ((uint16_t)record[11] | ((uint16_t)record[12] << 8)) <= 0xFFFF &&
        ((uint16_t)record[13] | ((uint16_t)record[14] << 8)) <= 0xFFFF &&
        ((uint16_t)record[15] | ((uint16_t)record[16] << 8)) <= 0xFFFF) {
        uint8_t checksum = 0xA5;
        for (uint8_t i = 0; i < NV_DIAGNOSTIC_RECORD_SIZE - 1; i++) checksum ^= record[i];
        if (record[NV_DIAGNOSTIC_RECORD_SIZE - 1] != checksum) goto defaults;
        mode[0] = record[1]; mode[1] = record[2];
        persisted_mode[0] = mode[0]; persisted_mode[1] = mode[1];
        power_type[0] = power_type[1] = persisted_power_type = record[3];
        led_indicator[0] = persisted_led_indicator[0] = record[4];
        led_indicator[1] = persisted_led_indicator[1] = record[4];
        startup_onoff[0] = persisted_startup_onoff[0] = record[5];
        startup_onoff[1] = persisted_startup_onoff[1] = record[6];
        relay[0] = record[7]; relay[1] = record[8];
        on_time[0] = persisted_on_time[0] = (uint16_t)record[9] | ((uint16_t)record[10] << 8);
        off_wait_time[0] = persisted_off_wait_time[0] = (uint16_t)record[11] | ((uint16_t)record[12] << 8);
        on_time[1] = persisted_on_time[1] = (uint16_t)record[13] | ((uint16_t)record[14] << 8);
        off_wait_time[1] = persisted_off_wait_time[1] = (uint16_t)record[15] | ((uint16_t)record[16] << 8);
    }
defaults:
    persisted_mode[0] = mode[0]; persisted_mode[1] = mode[1];
    for (uint8_t ch = 0; ch < 2; ch++) {
        uint8_t previous = relay[ch];
        if (startup_onoff[ch] == 1) relay[ch] = 1;
        else if (startup_onoff[ch] == 2) relay[ch] = (uint8_t)!previous;
        else if (startup_onoff[ch] == 3) relay[ch] = previous;
        else relay[ch] = 0;
        startup_pulse_pending[ch] = (uint8_t)(relay[ch] != previous);
    }
    serial_init();
    basic[0] = attr(0,0x20,1,&zcl_version);
    basic[1] = attr(1,0x20,1,&app_version);
    basic[2] = attr(2,0x20,1,&stack_version);
    basic[3] = attr(3,0x20,1,&hw_version);
    basic[4] = attr(4,0x42,sizeof(manufacturer),manufacturer);
    basic[5] = attr(5,0x42,sizeof(model),model);
    basic[6] = attr(6,0x42,sizeof(date_code),date_code);
    basic[7] = attr(7,0x30,1,&power_source);
    basic[8] = attr(8,0x30,1,&generic_device_class);
    basic[9] = attr(9,0x30,1,&generic_device_type);
    basic[10] = attr(0x000A,0x41,sizeof(product_code),product_code);
    basic[11] = attr(0x000B,0x42,sizeof(product_url),product_url);
    basic[12] = attr(0x000D,0x42,sizeof(serial_number),serial_number);
    basic[13] = attr(0x4000,0x42,sizeof(build),build);
    basic[14] = attr(0xFFFD,0x21,2,&basic_revision);
    for (uint8_t i=0;i<6;i++) {
        identify[i][0] = wattr(0,0x21,2,&identify_time[i]);
        identify[i][1] = attr(0xFFFD,0x21,2,&identify_revision);
        clusters[i][0] = (hal_zigbee_cluster){0,1,15,basic,0};
        clusters[i][1] = (hal_zigbee_cluster){3,1,2,identify[i],0};
        if (i<2) {
            onoff[i][0] = rattr(0,0x10,1,&relay[i]);
            onoff[i][1] = attr(0x4000,0x10,1,&global_scene_control[i]);
            onoff[i][2] = wattr(0x4001,0x21,2,&on_time[i]);
            onoff[i][3] = wattr(0x4002,0x21,2,&off_wait_time[i]);
            onoff[i][4] = wattr(0x4003,0x30,1,&startup_onoff[i]);
            onoff[i][5] = attr(0xFFFD,0x21,2,&onoff_revision);
            if (i == 0) {
                yandex_attr[i][0] = wattr(0,0x30,1,&indicator_mode[i]);
                yandex_attr[i][1] = wattr(5,0x10,1,&led_indicator[i]);
                yandex_attr[i][2] = wattr(1,0x30,1,&mode[i]);
                yandex_attr[i][3] = wattr(3,0x30,1,&power_type[i]);
                yandex_attr[i][4] = attr(0xFFFD,0x21,2,&yandex_revision);
            } else {
                yandex_attr[i][0] = wattr(1,0x30,1,&mode[i]);
                yandex_attr[i][1] = attr(0xFFFD,0x21,2,&yandex_revision);
            }
            clusters[i][2] = (hal_zigbee_cluster){6,1,6,onoff[i],command};
            /* The reference YNDX-00532 exposes switchMode on EP2, while
             * powerType and ledIndicator are present only on EP1. */
            clusters[i][3] = (hal_zigbee_cluster){FC03,1,(uint8_t)(i == 0 ? 5 : 2),yandex_attr[i],command};
            /* The real 00532 advertises OTA as an output cluster on EP1.
             * Diagnostic OTA handling remains disabled; this is descriptor
             * compatibility only and does not register an OTA server. */
            clusters[i][4] = (hal_zigbee_cluster){25,0,0,basic,0};
            endpoints[i] = (hal_zigbee_endpoint){i+1,0x0104,0x0100,1,
                                                 (uint8_t)(i == 0 ? 5 : 4),clusters[i]};
        } else {
            /* Empty attribute arrays use a valid base pointer for HAL loops. */
            clusters[i][2] = (hal_zigbee_cluster){3,0,0,basic,0};
            clusters[i][3] = (hal_zigbee_cluster){6,0,0,basic,0};
            endpoints[i] = (hal_zigbee_endpoint){i+1,0x0104,0x0103,1,4,clusters[i]};
        }
    }
    hal_zigbee_init(endpoints,6);
    hal_zigbee_register_on_attribute_change_callback(ws_diag_attribute_changed);
    /* hal_zigbee_init registers the descriptor table in one pass.  The
     * diagnostic profile keeps all six descriptors in RAM, but the active AF
     * set must match the persisted switchMode before the first interview.
     * Without this initial refresh, default coupled mode accidentally exposed
     * button endpoints 3..6 until the first mode change. */
    ws_diag_platform_endpoints_changed();
}
