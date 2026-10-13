#ifndef MOTOR_TEST_HAL_H
#define MOTOR_TEST_HAL_H
#include <stdint.h>
#include <stddef.h>
typedef struct {
  volatile uint32_t CR1, CR2, SMCR, DIER, SR, EGR, CCMR1, CCMR2;
  volatile uint32_t CCER, CNT, PSC, ARR, RCR, CCR1, CCR2, CCR3, CCR4, BDTR;
} TIM_TypeDef;
typedef struct { TIM_TypeDef *Instance; } TIM_HandleTypeDef;
typedef enum {GPIO_PIN_RESET=0, GPIO_PIN_SET} GPIO_PinState;
extern TIM_TypeDef test_tim1;
#define TIM1 (&test_tim1)
#define TIM_CR1_CEN (1U << 0)
#define TIM_CR1_UDIS (1U << 1)
#define TIM_CR1_OPM (1U << 3)
#define TIM_CR1_DIR (1U << 4)
#define TIM_CR1_CMS (3U << 5)
#define TIM_CR1_ARPE (1U << 7)
#define TIM_CCMR1_CC1S (3U << 0)
#define TIM_CCMR1_OC1FE (1U << 2)
#define TIM_CCMR1_OC1PE (1U << 3)
#define TIM_CCMR1_OC1M (7U << 4)
#define TIM_CCMR1_OC1M_Pos 4U
#define TIM_OCMODE_PWM2 (7U << 4)
#define TIM_CCER_CC1E (1U << 0)
#define TIM_CCER_CC1P (1U << 1)
#define TIM_CCER_CC1NE (1U << 2)
#define TIM_BDTR_MOE (1U << 15)
#define TIM_EGR_UG (1U << 0)
#define TIM_SR_UIF (1U << 0)
#define MOTOR_DIR_GPIO_Port ((void *)1)
#define MOTOR_DIR_Pin 1U
uint32_t HAL_GetTick(void);
void HAL_GPIO_WritePin(void *port, uint32_t pin, GPIO_PinState state);
#endif
