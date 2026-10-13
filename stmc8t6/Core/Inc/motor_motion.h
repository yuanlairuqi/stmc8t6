#ifndef MOTOR_MOTION_H
#define MOTOR_MOTION_H

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
  MOTOR_FAULT
} MotorState;

typedef struct {
  MotorState state;
  uint32_t target_pulses;
  uint32_t completed_pulses; /* completed timer periods, NOT encoder feedback */
  uint32_t active_batch;
} MotorStatus;

extern volatile MotorStatus g_motor_status; /* inspect using SWD */

/* One request per reset; repeated calls cannot launch another move. */
void Motor_StartOnce(TIM_HandleTypeDef *timer);
void Motor_Process(void);

#endif
