#include "robot_control.h"
#include <string.h>
static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void write32(uint8_t *p, uint32_t v) {
    for (int i=0; i<4; ++i) p[i] = (uint8_t)(v >> (8*i));
}
void robot_init(robot_control_t *r) { memset(r, 0, sizeof(*r)); }
void robot_stop(robot_control_t *r,uint8_t error) {
    r->motion=ROBOT_STOP;r->speed=0;r->error=error;++r->revision;
}
bool robot_can_move(const robot_control_t *r) {
    return !r->uart_configured || (r->stm_online&&(r->stm_flags&1)&&!(r->stm_flags&16)&&r->stm_error==0&&r->link_error==0);
}
void robot_connection(robot_control_t *r, bool connected) {
    r->connected = connected;
    r->has_seq = false;
    r->ack = 0; r->rx_count = 0;
    robot_stop(r,connected ? ROBOT_OK : ROBOT_DISCONNECTED);
}
bool robot_command(robot_control_t *r, const uint8_t *d, size_t len, uint32_t now) {
    if (!r->connected || !d || len != ROBOT_CMD_SIZE || d[0] != 3 ||
        d[1] > ROBOT_BACKWARD || d[2] > 100 || d[3] > 1 ||
        (d[1] == ROBOT_STOP && d[2] != 0) || (d[1] != ROBOT_STOP && d[2] == 0) ||
        (d[3]==1&&(read32(d+8)<100||read32(d+8)>3600000||read32(d+12)<100||read32(d+12)>3600000))) {
        robot_stop(r,ROBOT_BAD_COMMAND);
        return false;
    }
    uint32_t seq = read32(d+4);
    uint32_t delta = seq - r->ack;
    bool fresh = !r->has_seq || (delta != 0 && delta < 0x80000000u);
    if (!fresh) {
        // An old STOP still stops, but duplicates never renew the motion lease.
        if (d[1] == ROBOT_STOP) robot_stop(r,r->error);
        return false;
    }
    if(d[1]!=ROBOT_STOP&&!robot_can_move(r)) {
        robot_stop(r,r->stm_online?ROBOT_STM_REJECTED:ROBOT_STM_OFFLINE);return false;
    }
    r->has_seq = true; r->ack = seq; ++r->rx_count;
    r->motion = d[1]; r->speed = d[2]; r->mode=d[3];
    r->forward_ms=read32(d+8);r->reverse_ms=read32(d+12); r->error = ROBOT_OK;
    r->last_motion_ms = now;
    ++r->revision;
    return true;
}
void robot_tick(robot_control_t *r, uint32_t now) {
    if (r->motion != ROBOT_STOP && (uint32_t)(now-r->last_motion_ms) >= ROBOT_TIMEOUT_MS) {
        robot_stop(r,ROBOT_TIMEOUT);
    } else if(r->motion!=ROBOT_STOP&&!robot_can_move(r)) {
        robot_stop(r,r->link_error?r->link_error:ROBOT_STM_REJECTED);
    }
}
void robot_status(const robot_control_t *r, uint32_t now, uint8_t out[ROBOT_STATUS_SIZE]) {
    memset(out, 0, ROBOT_STATUS_SIZE);
    out[0] = 3; out[1] = r->uart_configured ? r->stm_motion : r->motion;
    if(r->uart_configured&&r->stm_online&&(r->stm_flags&16))out[1]=5;
    bool demo=!r->uart_configured||(r->stm_online&&(r->stm_flags&2));
    out[2]=1|(demo?2:0)|(r->battery_valid?4:0)|(r->stm_online?8:0)|
        ((r->stm_online&&(r->stm_flags&1))?16:0)|((r->stm_online&&(r->stm_flags&8))?32:0)|
        ((r->stm_online&&r->stm_error)?64:0)|(r->uart_configured?128:0);
    uint16_t mv = r->battery_valid ? r->battery_mv : 0xffff;
    out[3] = (uint8_t)mv; out[4] = (uint8_t)(mv>>8);
    out[5] = r->motion; out[6] = r->speed; out[7] = r->error?r->error:r->link_error;
    write32(out+8,r->ack);write32(out+12,r->stm_ack);out[16]=(uint8_t)(now/1000);out[17]=(uint8_t)((now/1000)>>8);
    out[18]=r->uart_configured?r->stm_mode:r->mode;out[19]=r->uart_configured?r->stm_phase:r->motion;
}
