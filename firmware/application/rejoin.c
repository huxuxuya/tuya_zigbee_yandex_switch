#include "rejoin.h"
void ws_rejoin_init(ws_rejoin_state *s) {
    *s=(ws_rejoin_state){WS_REJOIN_IDLE,0,0,0,0xff,false};
}
void ws_rejoin_changed(ws_rejoin_state *s,uint32_t now) {
    s->phase=WS_REJOIN_WAITING; s->due=now+1000; s->attempts=0; s->last_status=0xff;
}
void ws_rejoin_poll(ws_rejoin_state *s,uint32_t now,bool ready,uint8_t (*request)(void)) {
    if(s->steering_guard && (uint32_t)(now-s->accepted_at)>=30000)s->steering_guard=false;
    if(s->phase!=WS_REJOIN_WAITING || (int32_t)(now-s->due)<0)return;
    if(!ready) {s->due=now+1000;return;}
    s->last_status=request(); s->attempts++;
    if(s->last_status==0) {s->phase=WS_REJOIN_REQUEST_ACCEPTED;s->accepted_at=now;s->steering_guard=true;return;}
    if(s->attempts>=3) {s->phase=WS_REJOIN_FAILED;return;}
    /* Bounded retries for immediate service/registry errors; not radio retries. */
    s->due=now+5000;
}
bool ws_rejoin_allows_steering(const ws_rejoin_state *s,uint32_t now) {
    /* SDK accepted is not joined. Avoid a competing steering request during rejoin. */
    return !s->steering_guard || (uint32_t)(now-s->accepted_at)>=30000;
}
