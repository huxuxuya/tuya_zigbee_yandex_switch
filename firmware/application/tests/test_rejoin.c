#include "rejoin.h"
#include <assert.h>
#include <stdio.h>
static unsigned calls;
static uint8_t status;
static uint8_t request(void) { calls++; return status; }
static ws_rejoin_state s;
static void reset(void) {ws_rejoin_init(&s);calls=0;status=0;}
int main(void) {
    reset();ws_rejoin_poll(&s,100000,true,request);assert(calls==0);
    ws_rejoin_changed(&s,0);ws_rejoin_poll(&s,999,true,request);assert(calls==0);
    ws_rejoin_changed(&s,900);ws_rejoin_changed(&s,950);
    ws_rejoin_poll(&s,1949,true,request);assert(calls==0);
    ws_rejoin_poll(&s,1950,true,request);assert(calls==1 && s.phase==WS_REJOIN_REQUEST_ACCEPTED);
    ws_rejoin_poll(&s,2000,true,request);assert(calls==1);
    assert(!ws_rejoin_allows_steering(&s,1950));
    ws_rejoin_changed(&s,2000);assert(!ws_rejoin_allows_steering(&s,2000));
    ws_rejoin_poll(&s,3000,false,request);assert(calls==1 && s.attempts==0);
    ws_rejoin_poll(&s,3999,true,request);assert(calls==1);
    ws_rejoin_poll(&s,4000,true,request);assert(calls==2);
    assert(!ws_rejoin_allows_steering(&s,33999));assert(ws_rejoin_allows_steering(&s,34000));
    reset();ws_rejoin_changed(&s,0);
    for(unsigned t=1000;t<=10000;t+=1000)ws_rejoin_poll(&s,t,false,request);
    assert(calls==0 && s.attempts==0 && s.phase==WS_REJOIN_WAITING);
    ws_rejoin_poll(&s,11000,true,request);assert(calls==1);
    reset();status=0x80;ws_rejoin_changed(&s,0);
    ws_rejoin_poll(&s,1000,true,request);assert(calls==1 && s.last_status==0x80);
    ws_rejoin_poll(&s,5999,true,request);assert(calls==1);
    ws_rejoin_poll(&s,6000,true,request);ws_rejoin_poll(&s,11000,true,request);
    assert(calls==3 && s.phase==WS_REJOIN_FAILED && s.attempts==3);
    ws_rejoin_poll(&s,100000,true,request);assert(calls==3);
    ws_rejoin_changed(&s,100000);status=0;ws_rejoin_poll(&s,101000,true,request);
    assert(calls==4 && s.last_status==0 && s.attempts==1);
    reset();ws_rejoin_changed(&s,0xfffffe00u);
    ws_rejoin_poll(&s,487,true,request);assert(calls==0);
    ws_rejoin_poll(&s,488,true,request);assert(calls==1);
    reset();ws_rejoin_changed(&s,0xfffff000u);ws_rejoin_poll(&s,0xfffff3e8u,true,request);
    assert(!ws_rejoin_allows_steering(&s,100));assert(ws_rejoin_allows_steering(&s,30000));
    puts("Rejoin controller: coalescing, busy/offline wait, bounded errors, cooldown across changes, reset and clock wrap passed");
}
