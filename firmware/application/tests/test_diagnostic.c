#include "yandex_diagnostic.h"
#include "hal/nvm.h"
#include "hal/timer.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static uint32_t now;
static uint8_t pins[64];
static uint8_t output_values[64];
static gpio_callback_t callbacks[64];
static void *contexts[64];
static struct { hal_task_t *task; uint32_t due; } tasks[32];
static hal_zigbee_endpoint *eps;
static unsigned sends, notifications, writes, output_writes;
static uint8_t sent_ep[16], sent_cmd[16], nv[18];
static hal_attribute_change_callback_t attribute_change_callback;
static bool nv_valid, nv_fail;
static unsigned factory_resets, system_resets, leaves;
static unsigned endpoint_refreshes;
void ws_diag_platform_endpoints_changed(void) { endpoint_refreshes++; }
static hal_zigbee_network_status_t network;
uint32_t hal_millis(void) { return now; }
void hal_tasks_init(hal_task_t *task) { (void)task; }
void hal_tasks_unschedule(hal_task_t *task) {
    for(unsigned i=0;i<32;i++) if(tasks[i].task==task) tasks[i].task=0;
}
void hal_tasks_schedule(hal_task_t *task,uint32_t delay) {
    for(unsigned i=0;i<32;i++) if(!tasks[i].task) {
        tasks[i].task=task; tasks[i].due=now+delay; return;
    }
    assert(!"task capacity");
}
void hal_gpio_init(hal_gpio_pin_t pin,uint8_t input,hal_gpio_pull_t pull) {
    assert(pin<64);
    if (input) assert(pull==HAL_GPIO_PULL_UP);
    else assert(pull==HAL_GPIO_PULL_NONE);
}
uint8_t hal_gpio_read(hal_gpio_pin_t pin) { assert(pin<64); return pins[pin]; }
void hal_gpio_set(hal_gpio_pin_t pin) { assert(pin<64); output_values[pin]=1; output_writes++; }
void hal_gpio_clear(hal_gpio_pin_t pin) { assert(pin<64); output_values[pin]=0; output_writes++; }
void hal_gpio_callback(hal_gpio_pin_t pin,gpio_callback_t cb,void *arg) {
    callbacks[pin]=cb; contexts[pin]=arg;
}
void hal_zigbee_init(hal_zigbee_endpoint *endpoints,uint8_t count) {
    assert(count==6); eps=endpoints;
}
void hal_zigbee_register_on_attribute_change_callback(hal_attribute_change_callback_t callback) {
    attribute_change_callback = callback;
}
hal_zigbee_network_status_t hal_zigbee_get_network_status(void) { return network; }
hal_zigbee_status_t hal_zigbee_send_cmd_to_bindings(const hal_zigbee_cmd *cmd) {
    assert(sends<16 && cmd->profile_id==0x104 && cmd->cluster_id==6);
    assert(cmd->cluster_specific==1 && cmd->direction==HAL_ZIGBEE_DIR_CLIENT_TO_SERVER);
    assert(cmd->payload_len==0 && cmd->manufacturer_code==0);
    sent_ep[sends]=cmd->endpoint; sent_cmd[sends++]=cmd->command_id;
    return 0;
}
void hal_zigbee_notify_attribute_changed(uint8_t ep,uint16_t cluster,uint16_t attr) {
    assert(ep>=1 && ep<=2); assert((cluster==6 && attr==0)||(cluster==0xFC03 && (attr==1 || attr==3 || attr==5)));
    notifications++;
}
hal_nvm_status_t hal_nvm_read(uint8_t item,uint16_t size,uint8_t *data) {
    assert(item==0x40 && size==18);
    if(!nv_valid) return HAL_NVM_NOT_FOUND;
    memcpy(data,nv,18); return HAL_NVM_SUCCESS;
}
hal_nvm_status_t hal_nvm_write(uint8_t item,uint16_t size,uint8_t *data) {
    assert(item==0x40 && size==18); writes++;
    if(nv_fail) return HAL_NVM_ERROR;
    memcpy(nv,data,18); nv_valid=true; return HAL_NVM_SUCCESS;
}
hal_nvm_status_t hal_nvm_clear_all(void) { factory_resets++; nv_valid=false; return HAL_NVM_SUCCESS; }
void hal_factory_reset(void) { factory_resets++; }
/* In firmware this reboots; in tests it just records the request. */
void hal_system_reset(void) { system_resets++; }
void hal_zigbee_leave_network(void) { leaves++; }
static void boot(bool keep_nv) {
    endpoint_refreshes=0;
    now=0; memset(pins,1,sizeof(pins)); memset(output_values,0,sizeof(output_values));
    memset(callbacks,0,sizeof(callbacks));
    memset(tasks,0,sizeof(tasks)); sends=notifications=writes=output_writes=0;
    attribute_change_callback=0;
    factory_resets=system_resets=leaves=0;
    if(!keep_nv) nv_valid=false;
    nv_fail=false; network=HAL_ZIGBEE_NETWORK_JOINED; ws_diag_init();
}
static void advance(unsigned ms) {
    while(ms--) {
        now++;
        for(unsigned i=0;i<32;i++) if(tasks[i].task && (int32_t)(now-tasks[i].due)>=0) {
            hal_task_t *task=tasks[i].task; tasks[i].task=0; task->handler(task->arg);
        }
    }
}
static void edge(unsigned pin,bool down) {
    pins[pin]=!down; assert(callbacks[pin]); callbacks[pin](pin,contexts[pin]);
}
static void stable(unsigned pin,bool down) { edge(pin,down); advance(60); }
static void click(unsigned pin) { stable(pin,true); stable(pin,false); }
static hal_zigbee_cluster *cluster(unsigned ep,unsigned id) {
    assert(ep>=1 && ep<=6);
    for(unsigned i=0;i<eps[ep-1].cluster_count;i++)
        if(eps[ep-1].clusters[i].cluster_id==id) return &eps[ep-1].clusters[i];
    assert(!"missing cluster"); return 0;
}
static uint8_t value(unsigned ep,unsigned id) {
    hal_zigbee_cluster *c = cluster(ep,id);
    if (id == 0xFC03) {
        for (unsigned i=0; i<c->attribute_count; i++)
            if (c->attributes[i].attribute_id == 1)
                return *c->attributes[i].value;
    }
    return *c->attributes[0].value;
}
static uint8_t fc03_value(unsigned ep, unsigned attribute_id) {
    hal_zigbee_cluster *c = cluster(ep,0xFC03);
    for (unsigned i=0; i<c->attribute_count; i++)
        if (c->attributes[i].attribute_id == attribute_id)
            return *c->attributes[i].value;
    assert(!"missing FC03 attribute"); return 0;
}
static uint8_t *attribute_value(unsigned ep, unsigned cluster_id, unsigned attribute_id) {
    hal_zigbee_cluster *c = cluster(ep,cluster_id);
    for (unsigned i=0; i<c->attribute_count; i++)
        if (c->attributes[i].attribute_id == attribute_id)
            return c->attributes[i].value;
    assert(!"missing attribute"); return 0;
}
static hal_zigbee_cmd_result_t command(unsigned ep,unsigned cl,unsigned cmd,void *data,unsigned len) {
    return cluster(ep,cl)->cmd_callback(ep,cl,cmd,data,len);
}
static void mode(unsigned ep,uint8_t value) {
    assert(command(ep,0xFC03,1,&value,1)==HAL_ZIGBEE_CMD_PROCESSED);
}
static void descriptors(void) {
    boot(false);
    for(unsigned i=0;i<6;i++) {
        assert(eps[i].endpoint==i+1 && eps[i].profile_id==0x104);
        assert(eps[i].device_id==(i<2?0x100:0x103));
        assert(eps[i].cluster_count==(i==0?5:4));
    }
    hal_zigbee_attribute *basic=cluster(1,0)->attributes;
    assert(cluster(1,0)->attribute_count==15);
    assert(basic[0].value[0]==8);
    assert(basic[1].value[0]==24 && basic[2].value[0]==2 && basic[3].value[0]==0);
    assert(basic[4].value[0]==6 && !memcmp(basic[4].value+1,"Yandex",6));
    assert(basic[5].value[0]==10 && !memcmp(basic[5].value+1,"YNDX-00532",10));
    assert(basic[6].value[0]==12 && !memcmp(basic[6].value+1,"202506201923",12));
    assert(basic[7].value[0]==1);
    assert(basic[10].attribute_id==0x000A && basic[10].value[0]==11 && basic[10].value[1]==4 && !memcmp(basic[10].value+2,"YNDX-00532",10));
    assert(basic[11].attribute_id==0x000B && basic[11].value[0]==35 && !memcmp(basic[11].value+1,"alice.yandex.ru/smart-home/switches",35));
    assert(basic[12].attribute_id==0x000D && basic[12].value[0]>0 && basic[12].value[0]<=32);
    assert(basic[13].value[0]==11 && !memcmp(basic[13].value+1,"Mar 19 2025",11));
    assert(basic[14].attribute_id==0xFFFD && *(uint16_t *)basic[14].value==3 && basic[14].flag==ATTR_READONLY);
    assert(eps[0].clusters[4].cluster_id==25 && eps[0].clusters[4].is_server==0);
    assert(cluster(1,6)->attribute_count==6 &&
           cluster(1,6)->attributes[0].flag==ATTR_READONLY_REPORTABLE &&
           cluster(1,6)->attributes[4].attribute_id==0x4003 &&
           cluster(1,6)->attributes[4].value[0]==0 &&
           cluster(1,6)->attributes[4].flag==ATTR_WRITABLE);
    assert(ws_diag_endpoint_active(1) && ws_diag_endpoint_active(2));
    for(unsigned ep=3;ep<=6;ep++) assert(ws_diag_endpoint_active(ep));
    assert(!ws_diag_endpoint_active(0) && !ws_diag_endpoint_active(7));
    assert(cluster(3,6)->is_server==0 && cluster(1,0xFC03)->attributes[0].flag==ATTR_WRITABLE);
    assert(cluster(1,0xFC03)->attribute_count==5);
    assert(cluster(1,0xFC03)->attributes[0].attribute_id==0 && cluster(1,0xFC03)->attributes[0].flag==ATTR_WRITABLE);
    assert(cluster(1,0xFC03)->attributes[1].attribute_id==5 && cluster(1,0xFC03)->attributes[1].flag==ATTR_WRITABLE);
    assert(cluster(1,0xFC03)->attributes[2].attribute_id==1 && cluster(1,0xFC03)->attributes[2].flag==ATTR_WRITABLE);
    assert(cluster(1,0xFC03)->attributes[3].attribute_id==3 && cluster(1,0xFC03)->attributes[3].flag==ATTR_WRITABLE);
    assert(cluster(1,0xFC03)->attributes[4].attribute_id==0xFFFD && cluster(1,0xFC03)->attributes[4].flag==ATTR_READONLY);
    assert(cluster(2,0xFC03)->attribute_count==2 &&
           cluster(2,0xFC03)->attributes[0].attribute_id==1 &&
           cluster(2,0xFC03)->attributes[0].flag==ATTR_WRITABLE &&
           cluster(2,0xFC03)->attributes[1].attribute_id==0xFFFD);
}
static void detached_buttons(void) {
    boot(false); mode(1,2); mode(2,3);
    assert(ws_diag_attach_button(0,52,250,800)); assert(ws_diag_attach_button(1,20,250,800));
    click(52); advance(300); assert(sends==1 && sent_ep[0]==3 && sent_cmd[0]==2);
    click(20); click(20); advance(300); assert(sends==2 && sent_ep[1]==4 && sent_cmd[1]==1);
    stable(52,true); advance(800); stable(52,false); advance(300);
    assert(sends==3 && sent_cmd[2]==0 && sent_ep[2]==3);
    assert(value(1,6)==0 && value(2,6)==0 && output_writes==0);
}
static void bounce_and_boot_held(void) {
    boot(false); mode(1,2); pins[52]=0;
    assert(ws_diag_attach_button(0,52,250,800)); advance(1000); stable(52,false); advance(300);
    assert(sends==0);
    edge(52,true); advance(10); edge(52,false); advance(10); edge(52,true); advance(60);
    stable(52,false); advance(300); assert(sends==1 && sent_cmd[0]==2);
}
static void coupled_and_remote(void) {
    boot(false); mode(1,0); mode(2,0); assert(ws_diag_attach_button(0,52,250,800));
    stable(52,true); assert(value(1,6)==1); stable(52,false); advance(300); assert(sends==0);
    mode(1,1); click(52); assert(value(1,6)==0); /* only up detached */
    mode(1,2); click(52); advance(300); assert(value(1,6)==0 && sends==1);
    assert(command(1,6,1,0,0)==HAL_ZIGBEE_CMD_PROCESSED); assert(value(1,6)==1);
    assert(command(2,6,2,0,0)==HAL_ZIGBEE_CMD_PROCESSED); assert(value(2,6)==1);
    assert(output_writes==0);
}
static void leds_follow_virtual_relays(void) {
    boot(false); mode(1,0); mode(2,0);
    uint8_t medium=1;
    assert(command(1,0xFC03,3,&medium,1)==HAL_ZIGBEE_CMD_PROCESSED);
    ws_diag_led_init(10,11,1);
    assert(output_values[10]==0 && output_values[11]==0);
    uint8_t enabled=0; /* Original Yandex field is indicatorDisable. */
    assert(command(1,0xFC03,5,&enabled,1)==HAL_ZIGBEE_CMD_PROCESSED);
    assert(ws_diag_attach_button(0,52,250,800));
    stable(52,true); assert(value(1,6)==1 && output_values[10]==1);
    stable(52,false); advance(300);
    assert(command(1,6,0,0,0)==HAL_ZIGBEE_CMD_PROCESSED);
    assert(value(1,6)==0 && output_values[10]==0);
    enabled=1;
    assert(command(1,0xFC03,5,&enabled,1)==HAL_ZIGBEE_CMD_PROCESSED);
    assert(output_values[10]==0);
    assert(command(1,6,1,0,0)==HAL_ZIGBEE_CMD_PROCESSED);
    assert(value(1,6)==1 && output_values[10]==0);
}
static void standard_led_attribute_write_and_persistence(void) {
    boot(false); ws_diag_led_init(10,11,1);
    assert(attribute_change_callback);
    assert(fc03_value(1,3)==3);
    *cluster(1,0xFC03)->attributes[3].value=1;
    attribute_change_callback(1,0xFC03,3);
    assert(fc03_value(1,3)==1 && writes==1);
    *cluster(1,0xFC03)->attributes[3].value=3;
    attribute_change_callback(1,0xFC03,3);
    assert(fc03_value(1,3)==3 && writes==2);
    assert(fc03_value(1,5)==0 && output_values[10]==1);
    /* Simulate the Telink ZCL layer writing attr 0x0005, then invoking the
     * registered callback exactly as zcl_incoming_message_callback does. */
    *cluster(1,0xFC03)->attributes[1].value=1;
    attribute_change_callback(1,0xFC03,5);
    assert(fc03_value(1,5)==1 && writes==3);
    assert(output_values[10]==0 && output_values[11]==0);
    assert(command(1,6,1,0,0)==HAL_ZIGBEE_CMD_PROCESSED);
    assert(output_values[10]==0);
    assert(command(2,6,1,0,0)==HAL_ZIGBEE_CMD_PROCESSED);
    assert(output_values[11]==0);
    boot(true);
    assert(fc03_value(1,3)==3 && fc03_value(1,5)==1);
    ws_diag_led_init(10,11,1);
    assert(output_values[10]==0 && output_values[11]==0); /* relay state is not persisted */
    uint8_t enabled=0;
    assert(command(1,0xFC03,5,&enabled,1)==HAL_ZIGBEE_CMD_PROCESSED);
    assert(output_values[10]==1 && output_values[11]==1);
    uint8_t unsupported=1;
    assert(command(2,0xFC03,5,&unsupported,1)==HAL_ZIGBEE_CMD_SKIPPED);
}
static void standard_mode_attribute_write_and_persistence(void) {
    boot(false); uint8_t *mode1=attribute_value(1,0xFC03,1);
    endpoint_refreshes=0; writes=0;
    *mode1=3; attribute_change_callback(1,0xFC03,1);
    assert(fc03_value(1,1)==3 && writes==1 && endpoint_refreshes==1);
    boot(true); endpoint_refreshes=0; assert(fc03_value(1,1)==3 && ws_diag_endpoint_active(3));
    *mode1=4; attribute_change_callback(1,0xFC03,1);
    assert(fc03_value(1,1)==3 && writes==0);
    nv_fail=true; *mode1=0; attribute_change_callback(1,0xFC03,1);
    assert(fc03_value(1,1)==3 && endpoint_refreshes==0 && writes==1);
}
static void startup_onoff_and_timed_onoff(void) {
    boot(false); ws_diag_relay_init(30,31,32,33,100);
    writes=0;
    uint8_t *startup=attribute_value(1,6,0x4003);
    *startup=1; attribute_change_callback(1,6,0x4003);
    assert(writes==1);
    boot(true); ws_diag_led_init(10,11,1);
    ws_diag_relay_init(30,31,32,33,100);
    assert(value(1,6)==1 && output_values[30]==1);
    advance(100); assert(output_values[30]==0);

    boot(false); ws_diag_relay_init(30,31,32,33,100);
    uint8_t timed[5]={0,2,0,1,0};
    assert(command(1,6,0x42,timed,5)==HAL_ZIGBEE_CMD_PROCESSED);
    assert(value(1,6)==1);
    advance(199); assert(value(1,6)==1);
    advance(1); assert(value(1,6)==0 && output_values[31]==1);
    advance(100); assert(output_values[31]==0);
    assert(command(1,6,0x42,timed,5)==HAL_ZIGBEE_CMD_PROCESSED);
    assert(value(1,6)==1);
}
static void local_relay_fallback(void) {
    boot(false); network=HAL_ZIGBEE_NETWORK_NOT_JOINED;
    ws_diag_relay_init(30,31,32,33,100);
    assert(output_values[30]==0 && output_values[31]==0);
    assert(ws_diag_attach_button(0,52,250,800));
    stable(52,true); assert(value(1,6)==1 && output_values[30]==1);
    advance(100); assert(output_values[30]==0 && output_values[31]==0);
    stable(52,false); advance(300);
    assert(command(1,6,0,0,0)==HAL_ZIGBEE_CMD_PROCESSED);
    assert(output_values[31]==1); advance(100);
    assert(output_values[30]==0 && output_values[31]==0);
}
static void cancel_and_persistence(void) {
    boot(false); mode(1,2); assert(ws_diag_attach_button(0,52,250,800));
    click(52); mode(1,0); advance(300); assert(sends==0);
    stable(52,true); mode(1,3); advance(900); stable(52,false); advance(300); assert(sends==0);
    mode(2,2); boot(true); assert(value(1,0xFC03)==3 && value(2,0xFC03)==2);
    nv[3]^=1; boot(true); assert(value(1,0xFC03)==2 && value(2,0xFC03)==2);
}
static void malformed_and_storage_failure(void) {
    boot(false); uint8_t bad=4,good=0;
    assert(command(1,0xFC03,1,&bad,1)==HAL_ZIGBEE_INVALID_VALUE);
    assert(command(1,0xFC03,1,0,0)==HAL_ZIGBEE_MALFORMED_COMMAND);
    assert(command(1,0xFC03,1,&good,2)==HAL_ZIGBEE_MALFORMED_COMMAND);
    assert(command(1,0xFC03,2,&good,1)==HAL_ZIGBEE_CMD_SKIPPED);
    assert(command(1,6,1,&good,1)==HAL_ZIGBEE_MALFORMED_COMMAND);
    assert(writes==0 && notifications==0);
    nv_fail=true; assert(command(1,0xFC03,1,&good,1)==HAL_ZIGBEE_ACTION_DENIED);
    assert(value(1,0xFC03)==2 && notifications==0);
    nv_fail=false; mode(1,2); unsigned n=writes; mode(1,2); assert(writes==n);
    nv_fail=true; uint8_t enabled=1;
    assert(command(1,0xFC03,5,&enabled,1)==HAL_ZIGBEE_ACTION_DENIED);
    assert(fc03_value(1,5)==0 && notifications==0);
}
static void offline_and_bad_channel(void) {
    boot(false); mode(1,2); network=HAL_ZIGBEE_NETWORK_NOT_JOINED;
    ws_diag_gesture(0,WS_GESTURE_SINGLE); assert(sends==0);
    network=HAL_ZIGBEE_NETWORK_JOINED;
    ws_diag_gesture(2,WS_GESTURE_SINGLE); ws_diag_gesture(0,WS_GESTURE_NONE);
    ws_diag_gesture(0,(ws_gesture_event)99); assert(sends==0);
    assert(!ws_diag_attach_button(2,52,250,800)); assert(!ws_diag_attach_button(0,HAL_INVALID_PIN,250,800));
    assert(ws_diag_attach_button(0,52,250,800)); assert(!ws_diag_attach_button(1,52,250,800));
}
#ifdef WS_ORIGINAL_MODE_FIXTURE
#include "original_mode_cases.h"
#include "original_endpoint_cases.h"
static void compare_original_endpoint_fixture(void) {
    for(unsigned i=0;i<sizeof(original_endpoint_cases)/sizeof(original_endpoint_cases[0]);i++) {
        boot(false);
        unsigned ep=original_endpoint_cases[i].ep;
        mode(ep,original_endpoint_cases[i].old); mode(3-ep,original_endpoint_cases[i].other);
        endpoint_refreshes=0;
        uint8_t next=original_endpoint_cases[i].next;
        (void)command(ep,0xFC03,original_endpoint_cases[i].cmd,&next,1);
        unsigned mask=0;
        for(unsigned e=1;e<=6;e++) if(ws_diag_endpoint_active(e)) mask|=1u<<e;
        assert(mask==original_endpoint_cases[i].mask);
        assert(endpoint_refreshes==original_endpoint_cases[i].refresh);
        boot(true); // Registry selection must survive a diagnostic NVM reload.
        unsigned restored=0;
        for(unsigned e=1;e<=6;e++) if(ws_diag_endpoint_active(e)) restored|=1u<<e;
        assert(restored==mask);
    }
    boot(false); nv_fail=true;
    uint8_t next=0;
    assert(command(1,0xFC03,1,&next,1)==HAL_ZIGBEE_ACTION_DENIED);
    /* Boot performs the initial AF refresh; a failed mode write must not add
     * another refresh or expose any detached endpoint. */
    assert(endpoint_refreshes==1 && ws_diag_endpoint_active(3) && ws_diag_endpoint_active(5));
    puts("original reference: 193 endpoint sets, persistence and failed-write isolation matched");
}
#include "original_button_cases.h"
static void compare_original_button_fixture(void) {
    for(unsigned i=0;i<sizeof(original_button_cases)/sizeof(original_button_cases[0]);i++) {
        boot(false);
        unsigned ch=original_button_cases[i].ch;
        mode(ch+1,original_button_cases[i].mode); mode(2-ch,original_button_cases[i].other);
        assert(command(ch+1,6,original_button_cases[i].old,0,0)==HAL_ZIGBEE_CMD_PROCESSED);
        assert(command(2-ch,6,1-original_button_cases[i].old,0,0)==HAL_ZIGBEE_CMD_PROCESSED);
        sends=0;
        if(original_button_cases[i].gesture==0) {
            assert(ws_diag_attach_button(ch,52,250,800)); stable(52,true);
        } else ws_diag_gesture(ch,(ws_gesture_event)original_button_cases[i].gesture);
        assert(value(ch+1,6)==original_button_cases[i].after);
        assert(value(2-ch,6)==1-original_button_cases[i].old);
        assert(sends==original_button_cases[i].sends && output_writes==0);
        if(sends) assert(sent_ep[0]==original_button_cases[i].ep && sent_cmd[0]==original_button_cases[i].cmd);
    }
    puts("original reference: 256 projected button cases matched");
}

static void compare_original_mode_fixture(void) {
    for (unsigned i=0;i<sizeof(original_mode_cases)/sizeof(original_mode_cases[0]);i++) {
        boot(false);
        mode(original_mode_cases[i].ep, original_mode_cases[i].old);
        writes=0; endpoint_refreshes=0;
        uint8_t next=original_mode_cases[i].next;
        hal_zigbee_cmd_result_t result=command(original_mode_cases[i].ep,0xFC03,
                                               original_mode_cases[i].cmd,&next,1);
        unsigned status=result==HAL_ZIGBEE_CMD_PROCESSED?0:
                        result==HAL_ZIGBEE_INVALID_VALUE?0x85:
                        result==HAL_ZIGBEE_CMD_SKIPPED?0x81:0xff;
        assert(status==original_mode_cases[i].status);
        assert(value(original_mode_cases[i].ep,0xFC03)==original_mode_cases[i].result);
        assert(endpoint_refreshes==original_mode_cases[i].writes);
        assert(writes==original_mode_cases[i].writes && output_writes==0 && sends==0);
    }
    puts("original reference: 49 projected mode cases matched");
}
#endif
static void leave_then_reset(void) {
    /* Joined: Leave goes out first, wipe + reboot only after airtime. */
    boot(false); network=HAL_ZIGBEE_NETWORK_JOINED;
    ws_diag_request_leave_reset();
    assert(leaves==1 && factory_resets==0 && system_resets==0);
    ws_diag_leave_reset_poll();
    assert(factory_resets==0 && system_resets==0);
    network=HAL_ZIGBEE_NETWORK_NOT_JOINED; advance(1600);
    ws_diag_leave_reset_poll();
    assert(factory_resets==1 && system_resets==1);
    /* Offline: immediate local wipe, no Leave to send. */
    boot(false); network=HAL_ZIGBEE_NETWORK_NOT_JOINED;
    ws_diag_request_leave_reset();
    assert(leaves==0 && factory_resets==1 && system_resets==1);
    /* A stuck join still reboots on the backstop so the device retries. */
    boot(false); network=HAL_ZIGBEE_NETWORK_JOINED;
    ws_diag_request_leave_reset();
    assert(leaves==1 && factory_resets==0 && system_resets==0);
    advance(6000); ws_diag_leave_reset_poll();
    assert(factory_resets==1 && system_resets==1);
}
int main(void) {
    descriptors(); detached_buttons(); bounce_and_boot_held(); coupled_and_remote();
    cancel_and_persistence(); malformed_and_storage_failure(); offline_and_bad_channel();
    leds_follow_virtual_relays(); standard_led_attribute_write_and_persistence();
    standard_mode_attribute_write_and_persistence(); startup_onoff_and_timed_onoff();
    local_relay_fallback(); leave_then_reset();
    puts("diagnostic: 11 integration scenarios passed");
#ifdef WS_ORIGINAL_MODE_FIXTURE
    compare_original_mode_fixture();
    compare_original_endpoint_fixture();
    compare_original_button_fixture();
#endif
}
