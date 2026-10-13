#ifndef ROBOT_BRIDGE_H
#define ROBOT_BRIDGE_H
#include "robot_wire.h"
#define BRIDGE_ACK_MS 400
#define BRIDGE_ONLINE_MS 600
#define BRIDGE_RETRY_MS 100
#define BRIDGE_PING_MS 200
enum { BRIDGE_OK=0, BRIDGE_OFFLINE=6, BRIDGE_REJECTED=7, BRIDGE_TX_ERROR=8, BRIDGE_ACK_TIMEOUT=9 };
typedef struct {
    bool enabled, online, waiting, queued, sent, confirmed;
    uint32_t session, next_token, revision, sent_at, last_tx, last_rx;
    uint32_t completed_token, completed_phone, ack;
    uint8_t desired_motion, desired_speed, desired_mode, peer_mode, peer_phase, peer_motion, peer_speed, peer_flags, peer_error, error;
    uint32_t desired_phone, forward_ms, reverse_ms;
    uint16_t battery_mv;
    wire_frame_t inflight;
} robot_bridge_t;
void bridge_parameters(robot_bridge_t *b,uint8_t mode,uint32_t forward_ms,uint32_t reverse_ms);
void bridge_init(robot_bridge_t *b,bool enabled,uint32_t session);
void bridge_request(robot_bridge_t *b,uint32_t revision,uint8_t motion,uint8_t speed,uint32_t phone_seq,uint32_t now);
bool bridge_tick(robot_bridge_t *b,uint32_t now,wire_frame_t *out);
bool bridge_receive(robot_bridge_t *b,const wire_frame_t *frame,uint32_t now);
void bridge_tx_failed(robot_bridge_t *b);
#endif
