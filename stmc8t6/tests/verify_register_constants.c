#include "main.h"
#include "motor_config.h"
#include <stddef.h>

/* Check the host register model's assumptions against the actual ST headers. */
_Static_assert(TIM_CR1_CEN == (1U << 0), "CEN bit mismatch");
_Static_assert(TIM_CR1_UDIS == (1U << 1), "UDIS bit mismatch");
_Static_assert(TIM_CR1_OPM == (1U << 3), "OPM bit mismatch");
_Static_assert(TIM_CR1_DIR == (1U << 4), "DIR bit mismatch");
_Static_assert(TIM_CR1_CMS == (3U << 5), "CMS mask mismatch");
_Static_assert(TIM_CR1_ARPE == (1U << 7), "ARPE bit mismatch");
_Static_assert(TIM_CCMR1_CC1S == 3U, "CC1S mask mismatch");
_Static_assert(TIM_CCMR1_OC1FE == (1U << 2), "OC1FE bit mismatch");
_Static_assert(TIM_CCMR1_OC1PE == (1U << 3), "OC1PE bit mismatch");
_Static_assert(TIM_CCMR1_OC1M == (7U << 4), "OC1M mask mismatch");
_Static_assert(TIM_OCMODE_PWM2 == (7U << 4), "PWM2 mode mismatch");
_Static_assert(TIM_CCER_CC1E == 1U, "CC1E bit mismatch");
_Static_assert(TIM_CCER_CC1P == (1U << 1), "CC1P bit mismatch");
_Static_assert(TIM_CCER_CC1NE == (1U << 2), "CC1NE bit mismatch");
_Static_assert(TIM_BDTR_MOE == (1U << 15), "MOE bit mismatch");
_Static_assert(TIM_EGR_UG == 1U && TIM_SR_UIF == 1U, "Update bits mismatch");
_Static_assert(offsetof(TIM_TypeDef, RCR) == 0x30U, "TIM1 RCR layout mismatch");
_Static_assert(TIM_RCR_REP == 255U, "F103 repetition counter must be 8-bit");
_Static_assert(MOTOR_PUL_Pin == GPIO_PIN_8, "PUL pin mismatch");
_Static_assert(MOTOR_DIR_Pin == GPIO_PIN_0, "DIR pin mismatch");
_Static_assert(MOTOR_TIMER_INPUT_HZ / (MOTOR_TIMER_PRESCALER + 1U) /
               (MOTOR_TIMER_PERIOD + 1U) == 100U, "Pulse frequency mismatch");
