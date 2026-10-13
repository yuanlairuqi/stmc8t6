#include "motor_motion.h"
#include "motor_config.h"
#include "motor_math.h"

volatile MotorStatus g_motor_status = {MOTOR_NOT_STARTED, 0U, 0U, 0U, 1U};
static TIM_TypeDef *motor_timer;
static uint32_t wait_started;
static uint8_t continuous_direction;
static uint32_t continuous_period;

_Static_assert(MOTOR_DRIVER_PULSES_PER_REV > 0U, "Set a nonzero pulses/rev");
_Static_assert(MOTOR_TARGET_ANGLE_MDEG <= 360000U, "Angle must be 0..360 degrees");
_Static_assert(MOTOR_TIMER_COMPARE > 0U &&
               MOTOR_TIMER_COMPARE <= MOTOR_TIMER_PERIOD,
               "PWM2 needs both an inactive interval and an active interval");
_Static_assert(MOTOR_TIMER_PERIOD <= 65535U, "F103 TIM1 ARR is 16-bit");
_Static_assert(MOTOR_TIMER_PRESCALER <= 65535U, "F103 TIM1 PSC is 16-bit");
_Static_assert(MOTOR_DIRECTION_LEVEL <= 1U, "DIR must be 0 or 1");
_Static_assert(MOTOR_CONTINUOUS_ENABLED <= 1U, "Enable flag must be 0 or 1");
_Static_assert(MOTOR_CONTINUOUS_MIN_HZ > 0U &&
               MOTOR_CONTINUOUS_MAX_HZ >= MOTOR_CONTINUOUS_MIN_HZ,
               "Set a positive continuous frequency range");
_Static_assert(MOTOR_TIMER_INPUT_HZ / (MOTOR_TIMER_PRESCALER + 1U) /
               MOTOR_CONTINUOUS_MIN_HZ <= 65536U, "Continuous ARR is 16-bit");
_Static_assert(MOTOR_CONTINUOUS_MAX_HZ <=
               MOTOR_TIMER_INPUT_HZ / (MOTOR_TIMER_PRESCALER + 1U) / 2U,
               "Continuous PWM requires high and low ticks");

static void StartBatch(void)
{
  uint32_t remaining = g_motor_status.target_pulses
                       - g_motor_status.completed_pulses;
  uint32_t count = remaining > MOTOR_TIMER_BATCH_MAX
                   ? MOTOR_TIMER_BATCH_MAX : remaining;

  /* Hardware stops after RCR+1 overflows, independent of interrupt latency.
   * PWM2 is low at CNT=0: first edge is delayed until CCR1; final overflow
   * returns CNT to zero / PUL low, then OPM clears CEN without an extra edge.
   * UG loads PSC/ARR/CCR/RCR BEFORE starting and SR is cleared afterwards.
   */
  motor_timer->CR1 &= ~TIM_CR1_CEN;
  motor_timer->CNT = 0U;
  motor_timer->RCR = count - 1U;
  motor_timer->EGR = TIM_EGR_UG;
  motor_timer->SR = 0U;
  g_motor_status.active_batch = count;
  g_motor_status.state = MOTOR_RUNNING;
  motor_timer->CR1 |= TIM_CR1_CEN;
}

void Motor_StartOnce(TIM_HandleTypeDef *timer)
{
  if (g_motor_status.state != MOTOR_NOT_STARTED) {
    return;
  }
  if (timer == NULL || timer->Instance != TIM1) {
    g_motor_status.state = MOTOR_FAULT;
    return;
  }

  motor_timer = timer->Instance;
  motor_timer->CR1 &= ~TIM_CR1_CEN;
  motor_timer->DIER = 0U; /* no timer interrupt or DMA is used */
  motor_timer->CCER &= ~(TIM_CCER_CC1E | TIM_CCER_CC1NE);
  motor_timer->BDTR &= ~TIM_BDTR_MOE;

  /* Check the generated mode; refuse to move after incompatible regeneration. */
  if ((motor_timer->CCMR1 & (TIM_CCMR1_CC1S | TIM_CCMR1_OC1M)) !=
      TIM_OCMODE_PWM2 || (motor_timer->CCER & TIM_CCER_CC1P) != 0U) {
    g_motor_status.state = MOTOR_FAULT;
    return;
  }
  g_motor_status.target_pulses = Motor_PulsesForAngle(
      MOTOR_DRIVER_PULSES_PER_REV, MOTOR_TARGET_ANGLE_MDEG);
  if (g_motor_status.target_pulses == 0U) {
    g_motor_status.state = MOTOR_FAULT; /* requested move is below half a pulse */
    return;
  }

  /* Normal up-counting, software start, no external triggers or fast forcing. */
  motor_timer->CR1 &= ~(TIM_CR1_DIR | TIM_CR1_CMS | TIM_CR1_UDIS);
  motor_timer->CR1 |= TIM_CR1_OPM | TIM_CR1_ARPE;
  motor_timer->SMCR = 0U;
  motor_timer->CCMR1 &= ~TIM_CCMR1_OC1FE;
  motor_timer->CCMR1 |= TIM_CCMR1_OC1PE;
  motor_timer->PSC = MOTOR_TIMER_PRESCALER;
  motor_timer->ARR = MOTOR_TIMER_PERIOD;
  motor_timer->CCR1 = MOTOR_TIMER_COMPARE;
  motor_timer->RCR = 0U;
  motor_timer->CNT = 0U;
  motor_timer->EGR = TIM_EGR_UG;
  motor_timer->SR = 0U;
  motor_timer->CCER |= TIM_CCER_CC1E;
  motor_timer->BDTR |= TIM_BDTR_MOE; /* PWM2 at CNT=0 actively holds PA8 low */

  HAL_GPIO_WritePin(MOTOR_DIR_GPIO_Port, MOTOR_DIR_Pin,
                   MOTOR_DIRECTION_LEVEL ? GPIO_PIN_SET : GPIO_PIN_RESET);
  wait_started = HAL_GetTick();
  g_motor_status.state = MOTOR_WAITING;
}

void Motor_Process(void)
{
  if (g_motor_status.state == MOTOR_CONTINUOUS_HOLD) {
    if ((uint32_t)(HAL_GetTick() - wait_started) >= MOTOR_DIRECTION_HOLD_MS) {
      uint8_t level = continuous_direction == 1U ? MOTOR_DIRECTION_LEVEL :
                                                  (uint8_t)!MOTOR_DIRECTION_LEVEL;
      HAL_GPIO_WritePin(MOTOR_DIR_GPIO_Port, MOTOR_DIR_Pin,
                       level ? GPIO_PIN_SET : GPIO_PIN_RESET);
      wait_started = HAL_GetTick();
      g_motor_status.state = MOTOR_CONTINUOUS_WAITING;
    }
  } else if (g_motor_status.state == MOTOR_CONTINUOUS_WAITING) {
    if ((uint32_t)(HAL_GetTick() - wait_started) >= MOTOR_DIRECTION_SETUP_MS) {
      motor_timer->CR1 &= ~(TIM_CR1_CEN | TIM_CR1_OPM | TIM_CR1_UDIS |
                            TIM_CR1_DIR | TIM_CR1_CMS);
      motor_timer->CR1 |= TIM_CR1_ARPE;
      motor_timer->CCMR1 = (motor_timer->CCMR1 & ~TIM_CCMR1_OC1M) |
                           TIM_OCMODE_PWM2 | TIM_CCMR1_OC1PE;
      motor_timer->PSC = MOTOR_TIMER_PRESCALER;
      motor_timer->ARR = continuous_period - 1U;
      motor_timer->CCR1 = continuous_period / 2U;
      motor_timer->RCR = 0U;
      motor_timer->CNT = 0U;
      motor_timer->EGR = TIM_EGR_UG;
      motor_timer->SR = 0U;
      motor_timer->CCER |= TIM_CCER_CC1E;
      motor_timer->BDTR |= TIM_BDTR_MOE;
      g_motor_status.state = MOTOR_CONTINUOUS_RUNNING;
      motor_timer->CR1 |= TIM_CR1_CEN;
    }
  } else if (g_motor_status.state == MOTOR_CONTINUOUS_RUNNING) {
    if ((motor_timer->CR1 & TIM_CR1_CEN) == 0U) {
      g_motor_status.state = MOTOR_FAULT;
      Motor_Stop();
    }
  } else if (g_motor_status.state == MOTOR_WAITING) {
    if ((uint32_t)(HAL_GetTick() - wait_started) >= MOTOR_STARTUP_WAIT_MS) {
      StartBatch();
    }
  } else if (g_motor_status.state == MOTOR_RUNNING) {
    if ((motor_timer->CR1 & TIM_CR1_CEN) == 0U) {
      /* UIF distinguishes a completed hardware batch from an unexpected stop. */
      if ((motor_timer->SR & TIM_SR_UIF) == 0U) {
        motor_timer->CNT = 0U; /* force PWM2 inactive after an unexpected stop */
        g_motor_status.pulse_count_exact = 0U;
        g_motor_status.state = MOTOR_FAULT;
        return;
      }
      g_motor_status.completed_pulses += g_motor_status.active_batch;
      g_motor_status.active_batch = 0U;
      if (g_motor_status.completed_pulses < g_motor_status.target_pulses) {
        StartBatch(); /* >256 pulses: complete hardware batches, with low gaps */
      } else {
        /* Keep channel enabled / CNT=0 so PA8 remains driven low. No restart. */
        g_motor_status.state = MOTOR_DONE;
      }
    }
  }
}

void Motor_Stop(void)
{
  if (motor_timer == NULL || g_motor_status.state == MOTOR_NOT_STARTED) {
    return; /* UART initialization precedes the original startup request. */
  }
  uint32_t was_running = motor_timer->CR1 & TIM_CR1_CEN;
  motor_timer->CR1 &= ~TIM_CR1_CEN;
  /* Force OC1 inactive: also terminates a pulse already high. No extra edge.
   * STOP may truncate the last pulse; physical acceptance is not measurable here.
   */
  motor_timer->CCMR1 = (motor_timer->CCMR1 & ~TIM_CCMR1_OC1M)
                      | (4U << TIM_CCMR1_OC1M_Pos);
  motor_timer->CNT = 0U;
  if (g_motor_status.state == MOTOR_RUNNING) {
    if (was_running == 0U && (motor_timer->SR & TIM_SR_UIF) != 0U) {
      g_motor_status.completed_pulses += g_motor_status.active_batch;
    } else {
      g_motor_status.pulse_count_exact = 0U;
    }
  }
  motor_timer->SR = 0U;
  g_motor_status.active_batch = 0U;
  if (g_motor_status.state == MOTOR_WAITING ||
      g_motor_status.state == MOTOR_RUNNING ||
      g_motor_status.state == MOTOR_CONTINUOUS_HOLD ||
      g_motor_status.state == MOTOR_CONTINUOUS_WAITING ||
      g_motor_status.state == MOTOR_CONTINUOUS_RUNNING) {
    g_motor_status.state = MOTOR_ABORTED;
  }
}

bool Motor_ContinuousReady(void)
{
  return MOTOR_CONTINUOUS_ENABLED != 0U && motor_timer != NULL &&
         g_motor_status.state != MOTOR_FAULT;
}

bool Motor_OutputActive(void)
{
  return motor_timer != NULL &&
      g_motor_status.state == MOTOR_CONTINUOUS_RUNNING &&
      (motor_timer->CR1 & TIM_CR1_CEN) != 0U &&
      (motor_timer->CCER & TIM_CCER_CC1E) != 0U &&
      (motor_timer->BDTR & TIM_BDTR_MOE) != 0U &&
      (motor_timer->CCMR1 & TIM_CCMR1_OC1M) == TIM_OCMODE_PWM2;
}

bool Motor_SetContinuous(uint8_t direction, uint8_t speed_percent)
{
  if (direction > 2U || speed_percent > 100U) { return false; }
  if (direction == 0U || speed_percent == 0U) {
    Motor_Stop();
    return Motor_OutputStopped();
  }
  if (!Motor_ContinuousReady()) { return false; }
  uint32_t hz = MOTOR_CONTINUOUS_MIN_HZ +
      ((MOTOR_CONTINUOUS_MAX_HZ - MOTOR_CONTINUOUS_MIN_HZ) *
       (speed_percent - 1U) + 49U) / 99U;
  uint32_t count_hz = MOTOR_TIMER_INPUT_HZ / (MOTOR_TIMER_PRESCALER + 1U);
  uint32_t period = (count_hz + hz / 2U) / hz;
  if (period < 2U || period > 65536U) { return false; }
  if (continuous_direction == direction &&
      (g_motor_status.state == MOTOR_CONTINUOUS_RUNNING ||
       g_motor_status.state == MOTOR_CONTINUOUS_WAITING ||
       g_motor_status.state == MOTOR_CONTINUOUS_HOLD)) {
    if (period != continuous_period &&
        g_motor_status.state == MOTOR_CONTINUOUS_RUNNING) {
      /* Prevent update between the paired preload writes. No forced UG,
       * no counter reset: both values take effect on a later natural update. */
      motor_timer->CR1 |= TIM_CR1_UDIS;
      motor_timer->ARR = period - 1U;
      motor_timer->CCR1 = period / 2U;
      motor_timer->CR1 &= ~TIM_CR1_UDIS;
    }
    continuous_period = period;
    return true; /* same direction does not restart its direction delay */
  }
  Motor_Stop();
  continuous_direction = direction;
  continuous_period = period;
  g_motor_status.pulse_count_exact = 0U; /* continuous output is not counted */
  wait_started = HAL_GetTick();
  g_motor_status.state = MOTOR_CONTINUOUS_HOLD;
  return true;
}

bool Motor_OutputStopped(void)
{
  if (motor_timer == NULL) { return false; }
  return (motor_timer->CR1 & TIM_CR1_CEN) == 0U &&
      ((motor_timer->CCER & TIM_CCER_CC1E) == 0U ||
       (motor_timer->BDTR & TIM_BDTR_MOE) == 0U ||
       (motor_timer->CCMR1 & TIM_CCMR1_OC1M) == (4U << TIM_CCMR1_OC1M_Pos) ||
       ((motor_timer->CCMR1 & TIM_CCMR1_OC1M) == TIM_OCMODE_PWM2 &&
        motor_timer->CNT < motor_timer->CCR1));
}
