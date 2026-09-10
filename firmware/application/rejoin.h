#ifndef WS_REJOIN_H
#define WS_REJOIN_H
#include <stdint.h>
#include <stdbool.h>
typedef enum { WS_REJOIN_IDLE, WS_REJOIN_WAITING, WS_REJOIN_REQUEST_ACCEPTED, WS_REJOIN_FAILED } ws_rejoin_phase;
typedef struct {
    ws_rejoin_phase phase;
    uint32_t due, accepted_at;
    uint8_t attempts, last_status;
    bool steering_guard;
} ws_rejoin_state;
void ws_rejoin_init(ws_rejoin_state *s);
void ws_rejoin_changed(ws_rejoin_state *s,uint32_t now);
void ws_rejoin_poll(ws_rejoin_state *s,uint32_t now,bool ready,uint8_t (*request)(void));
bool ws_rejoin_allows_steering(const ws_rejoin_state *s,uint32_t now);
#endif
