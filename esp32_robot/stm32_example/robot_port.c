#include "robot_port.h"
bool RobotPort_Ready(void *ctx) {
    (void)ctx;
    // TODO(HARDWARE): driver-enable, faults, supply readiness and control interface.
    return false;
}
bool RobotPort_Apply(void *ctx,uint8_t motion,uint8_t speed) {
    (void)ctx;(void)motion;(void)speed;
    // TODO(HARDWARE): motion 1=forward rotation, 2=reverse rotation; speed 1..100%.
    // Implement verified frequency mapping, same-direction updates, ramp/dead time
    // and DIR setup/hold. Return true on accepted output change; Active reports startup.
    return false;
}
void RobotPort_Stop(void *ctx) {
    (void)ctx;
    // TODO(HARDWARE): put the selected driver into a verified stop/brake state.
}
bool RobotPort_Battery(void *ctx,uint16_t *mv) {
    (void)ctx;(void)mv;
    // TODO(HARDWARE): calibrated ADC + actual divider ratio. Unknown by default.
    return false;
}
bool RobotPort_Stopped(void *ctx) {(void)ctx;/* TODO(HARDWARE): verified output stop status. */return false;}
bool RobotPort_LocalBusy(void *ctx) {(void)ctx;/* Optional existing local action. */return false;}
bool RobotPort_Active(void *ctx) {(void)ctx;/* Actual output-generation state, not encoder feedback. */return false;}
