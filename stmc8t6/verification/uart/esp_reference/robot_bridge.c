#include "robot_bridge.h"
#include <string.h>
static void make_command(robot_bridge_t *b,uint32_t now) {
    memset(&b->inflight,0,sizeof(b->inflight));b->inflight.type=WIRE_COMMAND;
    b->inflight.session=b->session;b->inflight.token=++b->next_token;
    b->inflight.payload[0]=b->desired_motion;b->inflight.payload[1]=b->desired_speed;
    b->inflight.payload[2]=0xf4;b->inflight.payload[3]=1; // STM32 lease = 500 ms
    wire_put32(b->inflight.payload+4,b->desired_phone);
    b->inflight.payload[8]=b->desired_mode;wire_put32(b->inflight.payload+10,b->forward_ms);wire_put32(b->inflight.payload+14,b->reverse_ms);
    b->waiting=true;b->sent=false;b->sent_at=now;b->queued=false;
}
void bridge_parameters(robot_bridge_t *b,uint8_t mode,uint32_t forward_ms,uint32_t reverse_ms) {
    b->desired_mode=mode;b->forward_ms=forward_ms;b->reverse_ms=reverse_ms;
}
void bridge_init(robot_bridge_t *b,bool enabled,uint32_t session) {
    memset(b,0,sizeof(*b));b->enabled=enabled;b->session=session?session:1;b->battery_mv=0xffff;
    if(enabled){b->error=BRIDGE_OFFLINE;make_command(b,0);} // STOP handshake on startup
}
void bridge_request(robot_bridge_t *b,uint32_t rev,uint8_t op,uint8_t speed,uint32_t phone,uint32_t now) {
    if(!b->enabled||rev==b->revision)return;
    b->revision=rev;b->desired_motion=op;b->desired_speed=speed;b->desired_phone=phone;
    if(op==0||!b->waiting)make_command(b,now);else b->queued=true;
}
bool bridge_tick(robot_bridge_t *b,uint32_t now,wire_frame_t *out) {
    if(!b->enabled)return false;
    if(b->online&&(uint32_t)(now-b->last_rx)>=BRIDGE_ONLINE_MS){b->online=false;b->peer_flags=0;b->battery_mv=0xffff;b->error=BRIDGE_OFFLINE;}
    if(b->waiting) {
        if(b->sent&&(uint32_t)(now-b->sent_at)>=BRIDGE_ACK_MS) {
            b->online=false;b->peer_flags=0;b->battery_mv=0xffff;b->error=BRIDGE_ACK_TIMEOUT;
            // Drop queued movement and supersede failed movement with STOP.
            b->desired_motion=0;b->desired_speed=0;b->queued=false;make_command(b,now);
        }
        if(!b->sent||(uint32_t)(now-b->last_tx)>=BRIDGE_RETRY_MS) {
            *out=b->inflight;if(!b->sent)b->sent_at=now;b->sent=true;b->last_tx=now;return true;
        }
        return false;
    }
    if(!b->online) {b->desired_motion=0;b->desired_speed=0;make_command(b,now);return bridge_tick(b,now,out);}
    if(b->queued) {make_command(b,now);return bridge_tick(b,now,out);}
    if((uint32_t)(now-b->last_tx)>=BRIDGE_PING_MS) {
        memset(out,0,sizeof(*out));out->type=WIRE_PING;out->session=b->session;out->token=b->completed_token;
        b->last_tx=now;return true;
    }
    return false;
}
bool bridge_receive(robot_bridge_t *b,const wire_frame_t *f,uint32_t now) {
    if(!b->enabled||f->type!=WIRE_STATUS||f->session!=b->session||f->payload[0]>2||f->payload[1]>100||
        (f->payload[2]&~31)||f->payload[3]>4||f->payload[10]>1||f->payload[11]>2)return false;
    uint32_t phone=wire_u32(f->payload+6);
    bool current=b->waiting&&f->token==b->inflight.token&&phone==wire_u32(b->inflight.payload+4);
    bool prior=b->confirmed&&f->token==b->completed_token&&phone==b->completed_phone;
    if(!current&&!prior)return false;
    if(current&&f->payload[3]==0&&
       ((b->inflight.payload[0]==0?f->payload[0]!=0:
          b->inflight.payload[8]==0?f->payload[0]!=b->inflight.payload[0]:f->payload[0]==0)||
        f->payload[1]!=b->inflight.payload[1]||f->payload[10]!=b->inflight.payload[8]))return false;
    // Ignore an older heartbeat while a newer command is awaiting confirmation.
    if(b->waiting&&!current)return false;
    if((f->payload[2]&PEER_DEMO)&&(f->payload[2]&PEER_APPLIED))return false;
    b->last_rx=now;b->online=true;b->peer_motion=f->payload[0];b->peer_speed=f->payload[1];
    b->peer_mode=f->payload[10];b->peer_phase=f->payload[11];
    b->peer_flags=f->payload[2];b->peer_error=f->payload[3];
    b->battery_mv=(uint16_t)f->payload[4]|(uint16_t)f->payload[5]<<8;
    if(b->battery_mv==0xffff)b->peer_flags&=~PEER_BATTERY;
    b->error=b->peer_error?BRIDGE_REJECTED:BRIDGE_OK;
    if(current) {
        b->waiting=false;b->confirmed=true;b->completed_token=f->token;b->completed_phone=phone;b->ack=phone;
        if(b->peer_error)b->queued=false;
    }
    return true;
}
void bridge_tx_failed(robot_bridge_t *b) {b->online=false;b->peer_flags=0;b->error=BRIDGE_TX_ERROR;b->queued=false;}
