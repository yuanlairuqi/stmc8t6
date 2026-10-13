#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "motor_motion.h"
#include "motor_config.h"
#include "motor_math.h"

TIM_TypeDef test_tim1;
static uint32_t tick;
static uint32_t dir;
uint32_t HAL_GetTick(void) { return tick; }
void HAL_GPIO_WritePin(void *port, uint32_t pin, GPIO_PinState state)
{ (void)port; (void)pin; dir=(uint32_t)state; }

/* Independent timer model: upcount PWM2, 8-bit repetition counter and OPM.
 * This checks software sequencing; it is NOT a measurement of STM32 hardware.
 */
static uint32_t SimulateBatch(void)
{
  uint32_t repeats=test_tim1.RCR;
  uint32_t edges=0U;
  uint32_t high_ticks=0U;
  uint32_t periods=0U;
  uint32_t ticks=0U;
  unsigned previous=0U;
  assert(test_tim1.CNT==0U);
  assert(test_tim1.RCR<=255U);
  assert((test_tim1.CR1 & TIM_CR1_OPM)!=0U);
  assert((test_tim1.CCER & TIM_CCER_CC1E)!=0U);
  assert((test_tim1.BDTR & TIM_BDTR_MOE)!=0U);
  assert(test_tim1.SR==0U);
  while ((test_tim1.CR1 & TIM_CR1_CEN)!=0U) {
    unsigned high=test_tim1.CNT>=test_tim1.CCR1;
    edges += high && !previous;
    high_ticks += high;
    previous=high;
    ++ticks;
    if (test_tim1.CNT==test_tim1.ARR) {
      ++periods;
      test_tim1.CNT=0U;
      if (repeats==0U) {
        test_tim1.SR |= TIM_SR_UIF;
        test_tim1.CR1 &= ~TIM_CR1_CEN;
      } else { --repeats; }
    } else { ++test_tim1.CNT; }
  }
  assert(test_tim1.CNT<test_tim1.CCR1); /* final output inactive */
  assert(edges==periods);
  assert(ticks==periods*(MOTOR_TIMER_PERIOD+1U));
  assert(high_ticks==periods*(MOTOR_TIMER_PERIOD+1U-MOTOR_TIMER_COMPARE));
  return edges;
}

int main(int argc,char **argv)
{
  TIM_HandleTypeDef timer={TIM1};
  uint32_t edges=0U;
  uint32_t batches=0U;
  uint32_t expected=Motor_PulsesForAngle(MOTOR_DRIVER_PULSES_PER_REV,22500U);
  assert(Motor_PulsesForAngle(1600U,22500U)==100U);
  assert(Motor_PulsesForAngle(200U,22500U)==13U); /* 23.4 deg, +0.9 deg */
  assert(Motor_PulsesForAngle(7U,22500U)==0U);
  assert(Motor_PulsesForAngle(8U,22500U)==1U);
  assert(Motor_PulsesForAngle(UINT32_MAX,360000U)==UINT32_MAX);
  tick=UINT32_MAX-500U; /* verify wait across SysTick wrap */
  test_tim1.CCMR1=TIM_OCMODE_PWM2;
  if (argc>1 && strcmp(argv[1],"null-timer")==0) {
    Motor_StartOnce(NULL);
    assert(g_motor_status.state==MOTOR_FAULT);
    assert((test_tim1.CR1 & TIM_CR1_CEN)==0U);
    Motor_StartOnce(&timer);
    assert(g_motor_status.state==MOTOR_FAULT);
    puts("PASS NULL timer rejected and no restart from fault");
    return 0;
  }
  if (argc>1 && strcmp(argv[1],"wrong-timer")==0) {
    TIM_TypeDef other_timer={0};
    TIM_HandleTypeDef other_handle={&other_timer};
    Motor_StartOnce(&other_handle);
    assert(g_motor_status.state==MOTOR_FAULT);
    assert((other_timer.CR1 & TIM_CR1_CEN)==0U);
    puts("PASS non-TIM1 handle rejected");
    return 0;
  }
  if (argc>1 && strcmp(argv[1],"inverted-polarity")==0) {
    test_tim1.CCER=TIM_CCER_CC1P;
    Motor_StartOnce(&timer);
    assert(g_motor_status.state==MOTOR_FAULT);
    assert((test_tim1.CR1 & TIM_CR1_CEN)==0U);
    assert((test_tim1.BDTR & TIM_BDTR_MOE)==0U);
    puts("PASS inverted PWM polarity rejected");
    return 0;
  }
  if (argc>1 && strcmp(argv[1],"bad-mode")==0) {
    test_tim1.CCMR1=0U;
    Motor_StartOnce(&timer);
    assert(g_motor_status.state==MOTOR_FAULT);
    assert((test_tim1.CR1 & TIM_CR1_CEN)==0U);
    puts("PASS incompatible PWM mode rejected");
    return 0;
  }
  Motor_StartOnce(&timer);
  if (expected==0U) {
    assert(g_motor_status.state==MOTOR_FAULT);
    puts("PASS zero-pulse request rejected");
    return 0;
  }
  assert(dir==MOTOR_DIRECTION_LEVEL);
  assert(g_motor_status.state==MOTOR_WAITING);
  tick += MOTOR_STARTUP_WAIT_MS-1U;
  Motor_StartOnce(&timer); /* repeated wait request must not reset the deadline */
  Motor_Process();
  assert(g_motor_status.state==MOTOR_WAITING);
  ++tick;
  Motor_Process();
  assert(g_motor_status.state==MOTOR_RUNNING);
  assert(test_tim1.PSC==MOTOR_TIMER_PRESCALER);
  assert(test_tim1.ARR==MOTOR_TIMER_PERIOD);
  assert(test_tim1.CCR1==MOTOR_TIMER_COMPARE);
  assert((test_tim1.CR1 & (TIM_CR1_DIR | TIM_CR1_CMS | TIM_CR1_UDIS))==0U);
  assert((test_tim1.CCMR1 & TIM_CCMR1_OC1PE)!=0U);
  assert(test_tim1.DIER==0U);
  if (argc>1 && strcmp(argv[1],"unexpected-stop")==0) {
    test_tim1.CNT=test_tim1.CCR1; /* interrupted during the active pulse */
    test_tim1.CR1 &= ~TIM_CR1_CEN;
    Motor_Process();
    assert(g_motor_status.state==MOTOR_FAULT);
    assert(g_motor_status.completed_pulses==0U);
    assert(g_motor_status.pulse_count_exact==0U);
    assert(test_tim1.CNT<test_tim1.CCR1);
    puts("PASS unexpected stop rejected");
    return 0;
  }
  while(g_motor_status.state==MOTOR_RUNNING) {
    Motor_StartOnce(&timer); /* repeated request must not restart */
    Motor_Process(); /* active hardware must not be falsely counted complete */
    assert(g_motor_status.state==MOTOR_RUNNING);
    edges += SimulateBatch();
    ++batches;
    /* Delay processing by 1 second: hardware must stay stopped / low. */
    tick += 1000U;
    assert((test_tim1.CR1 & TIM_CR1_CEN)==0U);
    Motor_Process();
  }
  assert(g_motor_status.state==MOTOR_DONE);
  assert(edges==expected);
  assert(g_motor_status.completed_pulses==expected);
  assert(batches==(expected+255U)/256U);
  Motor_StartOnce(&timer);
  for (unsigned i=0;i<100U;++i) { ++tick; Motor_Process(); }
  assert(g_motor_status.state==MOTOR_DONE);
  assert((test_tim1.CR1 & TIM_CR1_CEN)==0U);
  assert((test_tim1.CCER & TIM_CCER_CC1E)!=0U);
  assert((test_tim1.BDTR & TIM_BDTR_MOE)!=0U);
  printf("PASS ppr=%u pulses=%u batches=%u, no restart, final PUL low\n",
         MOTOR_DRIVER_PULSES_PER_REV,edges,batches);
  return 0;
}
