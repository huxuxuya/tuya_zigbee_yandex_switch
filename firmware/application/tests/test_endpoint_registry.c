/* Tests the exact include compiled into the Telink HAL, with observable AF calls. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
#include "original_endpoint_cases.h"
static struct { uint8_t endpoint; } endpoint_descriptors[6];
static unsigned desired, active, ops[16], count, reject;
static void af_rx_callback(void *arg) {(void)arg;}
static bool ws_diag_endpoint_active(uint8_t ep) {return (desired & (1u<<ep))!=0;}
static bool af_endpointUnregister(uint8_t ep) {
    assert(ep>=3 && ep<=6 && count<16); ops[count++]=ep;
    bool existed=(active & (1u<<ep))!=0; active&=~(1u<<ep); return existed;
}
static bool af_endpointRegister(uint8_t ep,void *desc,void (*cb)(void *),void *cnf) {
    assert(ep>=3 && ep<=6 && count<16 && desc==&endpoint_descriptors[ep-1]);
    assert(cb==af_rx_callback && cnf==NULL && !(active & (1u<<ep)));
    ops[count++]=100+ep;
    if(ep==reject)return false;
    active|=1u<<ep; return true;
}
#include "../telink/endpoint_registry.inc"
int main(void) {
    for(unsigned i=0;i<sizeof(original_endpoint_cases)/sizeof(original_endpoint_cases[0]);i++) {
        desired=original_endpoint_cases[i].mask; active=0x7e; count=0; reject=0;
        ws_diag_platform_endpoints_changed();
        assert(active==desired && ws_diag_endpoint_registration_failures==0);
        for(unsigned j=0;j<4;j++)assert(ops[j]==j+3);
        unsigned n=4;
        const unsigned *order;
        const unsigned pairing_order[]={3,4,5,6};
        const unsigned runtime_order[]={3,5,4,6};
        order = desired == 0x7e ? pairing_order : runtime_order;
        for(unsigned j=0;j<4;j++)if(desired & (1u<<order[j]))assert(ops[n++]==100+order[j]);
        assert(count==n);
    }
    desired=0x7e;active=6;count=0;reject=5;
    ws_diag_platform_endpoints_changed();
    assert(ws_diag_endpoint_registration_failures==1 && active==(desired & ~(1u<<5)));
    count=0;reject=0;ws_diag_platform_endpoints_changed();
    assert(ws_diag_endpoint_registration_failures==0 && active==desired);
    puts("Telink AF registry: 193 sets, ordering, stable descriptors, failure visibility and retry passed");
}
