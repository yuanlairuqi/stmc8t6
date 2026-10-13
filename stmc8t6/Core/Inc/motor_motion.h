#ifndef MOTOR_MOTION_H
#define MOTOR_MOTION_H
#include <stdbool.h>

#ifdef MOTOR_HOST_TEST
#include "motor_test_hal.h"
#else
#include "main.h"
#endif

typedef enum {
  MOTOR_NOT_STARTED = 0,
  MOTOR_WAITING,
  MOTOR_RUNNING,
  MOTOR_DONE,
  MOTOR_FAULT,
  MOTOR_ABORTED,
  MOTOR_CONTINUOUS_HOLD,
  MOTOR_CONTINUOUS_WAITING,
  MOTOR_CONTINUOUS_RUNNING
} MotorState;

typedef struct {
  MotorState state;
  uint32_t target_pulses;
  uint32_t completed_pulses; /* completed timer periods, NOT encoder feedback */
  uint32_t active_batch;
  uint8_t pulse_count_exact; /* 0 after interrupting an unfinished hardware batch */
} MotorStatus;

extern volatile MotorStatus g_motor_status; /* inspect using SWD */

/* One request per reset; repeated calls cannot launch another move. */
void Motor_StartOnce(TIM_HandleTypeDef *timer);
void Motor_Process(void);
/* Cancels the pending/current one-shot; cannot restart it before reset. */
void Motor_Stop(void);
bool Motor_OutputStopped(void); /* register state, not encoder feedback */
bool Motor_ContinuousReady(void);
bool Motor_OutputActive(void); /* continuous timer generation enabled */
bool Motor_SetContinuous(uint8_t direction, uint8_t speed_percent);

#endif
