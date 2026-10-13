#pragma once
#include <stdbool.h>
#include <stdint.h>
// TODO(HARDWARE): set to 1 ONLY after implementing/verifying all motor hooks.
#define ROBOT_PORT_HARDWARE_ENABLED 0
bool RobotPort_Ready(void *ctx);
bool RobotPort_Apply(void *ctx,uint8_t motion,uint8_t speed);
void RobotPort_Stop(void *ctx);
bool RobotPort_Battery(void *ctx,uint16_t *mv);
bool RobotPort_Stopped(void *ctx);
bool RobotPort_LocalBusy(void *ctx);
bool RobotPort_Active(void *ctx);
