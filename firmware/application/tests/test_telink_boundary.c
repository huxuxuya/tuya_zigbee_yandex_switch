#include "zcl_include.h"
#include "hal/timer.h"
#include <assert.h>
#include <stdio.h>
status_t zcl_yandex_register(u8,u16,u8,const zclAttrInfo_t *,cluster_forAppCb_t);
status_t zcl_diagnostic_onoff_register(u8,u16,u8,const zclAttrInfo_t *,cluster_forAppCb_t);
static cluster_cmdHdlr_t handler;
static unsigned calls;
static uint32_t ticks;
uint32_t clock_time(void) { return ticks; }
status_t zcl_registerCluster(u8 ep,u16 cluster,u16 manufacturer,u8 count,const zclAttrInfo_t *attrs,
                             cluster_cmdHdlr_t fn,cluster_forAppCb_t cb) {
    (void)attrs; (void)count; (void)cb;
    assert(ep==1 && ((cluster==6 && manufacturer==0)||(cluster==0xFC03 && manufacturer==0x140A)));
    handler=fn; return 0;
}
static status_t callback(zclIncomingAddrInfo_t *addr,u8 cmd,void *payload) {
    (void)cmd; (void)payload; assert(addr->dstEp==1); calls++; return 0x93;
}
int main(void) {
    zclIncoming_t msg={0}; msg.addrInfo.dstEp=1; msg.clusterAppCb=callback;
    zcl_yandex_register(1,0x140A,0,0,callback);
    assert(handler(&msg)==ZCL_STA_UNSUP_MANU_CLUSTER_COMMAND && calls==0);
    msg.hdr.manufCode=0x132F;
    assert(handler(&msg)==0x93 && calls==1);
    msg.hdr.manufCode=0x140A;
    assert(handler(&msg)==0x93 && calls==2);
    msg.hdr.frmCtrl.bf.dir=1;
    assert(handler(&msg)==ZCL_STA_UNSUP_MANU_CLUSTER_COMMAND && calls==2);
    zcl_diagnostic_onoff_register(1,0,0,0,callback);
    msg.hdr.frmCtrl.bf.dir=0; msg.hdr.manufCode=0; msg.hdr.cmd=1;
    assert(handler(&msg)==0x93 && calls==3);
    msg.dataLen=1;
    assert(handler(&msg)==ZCL_STA_MALFORMED_COMMAND && calls==3);
    msg.dataLen=0; msg.hdr.cmd=0x42;
    assert(handler(&msg)==ZCL_STA_UNSUP_CLUSTER_COMMAND && calls==3);
    msg.hdr.cmd=0; msg.hdr.manufCode=0x140A;
    assert(handler(&msg)==ZCL_STA_UNSUP_CLUSTER_COMMAND && calls==3);
    /* Clock remainder, hardware wrap, and repeated wraps: use a 64-bit oracle. */
    uint64_t total=0;
    const uint32_t steps[]={1,15998,1,UINT32_MAX-16000,1000,UINT32_MAX-123,50000};
    for(unsigned i=0;i<sizeof(steps)/sizeof(steps[0]);i++) {
        total+=steps[i]; ticks+=steps[i];
        assert(hal_millis()==(uint32_t)(total/16000));
    }
    puts("Telink boundary: manufacturer/direction/length/status and clock-wrap checks passed");
}
