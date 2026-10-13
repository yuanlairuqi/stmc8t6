#include <assert.h>
#include <stdio.h>
#include "motor_motion.h"
#include "motor_config.h"
TIM_TypeDef test_tim1;
static uint32_t tick;
static uint32_t dir;
uint32_t HAL_GetTick(void) { return tick; }
void HAL_GPIO_WritePin(void *p, uint32_t n, GPIO_PinState v)
{ (void)p; (void)n; dir = (uint32_t)v; }
int main(void)
{
  TIM_HandleTypeDef timer = {TIM1};
  test_tim1.CCMR1 = TIM_OCMODE_PWM2;
  tick = UINT32_MAX - 3U;
  Motor_StartOnce(&timer);
  assert(!Motor_SetContinuous(3, 20));
  assert(!Motor_SetContinuous(1, 101));
  assert(g_motor_status.state == MOTOR_WAITING);
  if (MOTOR_CONTINUOUS_ENABLED == 0U) {
    assert(!Motor_ContinuousReady());
    assert(!Motor_SetContinuous(1, 20));
    assert(g_motor_status.state == MOTOR_WAITING);
    assert(Motor_SetContinuous(1, 0));
    assert(g_motor_status.state == MOTOR_ABORTED);
    puts("PASS continuous mode default disabled; zero speed stops; bad values rejected");
    return 0;
  }
  assert(Motor_ContinuousReady());
  assert(Motor_SetContinuous(1, 100));
  assert(g_motor_status.state == MOTOR_CONTINUOUS_HOLD && Motor_OutputStopped());
  tick += MOTOR_DIRECTION_HOLD_MS - 1;
  Motor_Process(); assert(g_motor_status.state == MOTOR_CONTINUOUS_HOLD);
  assert(Motor_SetContinuous(1, 100)); /* must not renew hold delay */
  ++tick; Motor_Process();
  assert(g_motor_status.state == MOTOR_CONTINUOUS_WAITING && dir == MOTOR_DIRECTION_LEVEL);
  tick += MOTOR_DIRECTION_SETUP_MS - 1;
  Motor_Process(); assert(g_motor_status.state == MOTOR_CONTINUOUS_WAITING);
  assert(Motor_SetContinuous(1, 100)); ++tick; Motor_Process();
  assert(g_motor_status.state == MOTOR_CONTINUOUS_RUNNING);
  assert(Motor_OutputActive());
  assert((test_tim1.CR1 & TIM_CR1_CEN) && !(test_tim1.CR1 & TIM_CR1_OPM));
  assert(test_tim1.ARR == 999 && test_tim1.CCR1 == 500 && test_tim1.RCR == 0);
  test_tim1.CNT = 700; test_tim1.EGR = 0;
  assert(Motor_SetContinuous(1, 1));
  assert(test_tim1.ARR == 9999 && test_tim1.CCR1 == 5000);
  assert(test_tim1.CNT == 700 && test_tim1.EGR == 0 && !(test_tim1.CR1 & TIM_CR1_UDIS));
  assert(Motor_SetContinuous(2, 100));
  assert(Motor_OutputStopped() && dir == MOTOR_DIRECTION_LEVEL);
  assert(!Motor_OutputActive());
  tick += MOTOR_DIRECTION_HOLD_MS; Motor_Process();
  assert(dir == !MOTOR_DIRECTION_LEVEL && g_motor_status.state == MOTOR_CONTINUOUS_WAITING);
  assert(Motor_SetContinuous(0, 0));
  tick += MOTOR_DIRECTION_SETUP_MS; Motor_Process();
  assert(g_motor_status.state == MOTOR_ABORTED && Motor_OutputStopped());
  Motor_StartOnce(&timer); assert(g_motor_status.state == MOTOR_ABORTED);
  assert(Motor_SetContinuous(2, 100));
  tick += MOTOR_DIRECTION_HOLD_MS; Motor_Process();
  tick += MOTOR_DIRECTION_SETUP_MS; Motor_Process();
  assert(g_motor_status.state == MOTOR_CONTINUOUS_RUNNING);
  test_tim1.CR1 &= ~TIM_CR1_CEN; Motor_Process();
  assert(g_motor_status.state == MOTOR_FAULT && Motor_OutputStopped());
  assert(!Motor_ContinuousReady() && !Motor_SetContinuous(1, 100));
  assert(g_motor_status.pulse_count_exact == 0);
  puts("PASS enabled continuous PWM, percent endpoints, no counter reset on speed change, DIR hold/setup, STOP, wrap, fault");
  return 0;
}
