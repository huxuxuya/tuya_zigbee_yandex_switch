/* Exact SDK adapter with observable stand-ins; real SDK is checked by TC32 build. */
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
static bool factory,joined,idle,done,busy,repair_fails;
static struct { uint8_t phyChannelCur; } g_zbMacPib;
static struct { uint8_t config_nwk_scan_duration; } zdo_cfg_attributes;
static volatile unsigned ws_diag_endpoint_registration_failures;
static unsigned repairs,security_calls,requests;
static uint8_t response;
#define REJOIN_SECURITY 1
static bool zb_isDeviceFactoryNew(void){return factory;}
static bool zb_isDeviceJoinedNwk(void){return joined;}
static bool bdb_isIdle(void){return idle;}
static bool zb_isTaskDone(void){return done;}
static bool tl_stackBusy(void){return busy;}
static void ws_diag_platform_endpoints_changed(void){repairs++;ws_diag_endpoint_registration_failures=repair_fails?1:0;}
static void zb_rejoinSecModeSet(uint8_t mode){assert(mode==1);security_calls++;}
static uint8_t zb_rejoinReq(uint32_t mask,uint8_t scan){assert(mask==(1u<<g_zbMacPib.phyChannelCur));assert(scan==5);requests++;return response;}
#include "../telink/network_rejoin.inc"
int main(void){
    for(unsigned bits=0;bits<32;bits++){
        factory=bits&1;joined=bits&2;idle=bits&4;done=bits&8;busy=bits&16;
        assert(ws_diag_network_ready()==(bits==14));
    }
    zdo_cfg_attributes.config_nwk_scan_duration=5;
    for(unsigned ch=0;ch<32;ch++){
        g_zbMacPib.phyChannelCur=ch;unsigned before=requests;
        uint8_t result=ws_diag_request_rejoin();
        assert(result==(ch>=11 && ch<=26?0:0xfd));
        assert(requests==before+(ch>=11 && ch<=26));
    }
    g_zbMacPib.phyChannelCur=15;ws_diag_endpoint_registration_failures=1;repair_fails=true;
    unsigned before=requests;assert(ws_diag_request_rejoin()==0xfe && requests==before && repairs==1);
    repair_fails=false;assert(ws_diag_request_rejoin()==0 && requests==before+1 && repairs==2);
    response=0x80;assert(ws_diag_request_rejoin()==0x80);
    assert(security_calls==requests);
    puts("Rejoin SDK boundary: 32 readiness states, all channels, AF repair, security/mask/scan and status passed");
}
