#ifndef MOTOR_CONFIG_H
#define MOTOR_CONFIG_H

#include <stdint.h>

/* EXAMPLE ONLY: confirm T60 SW1-SW4 / software setting before connecting it. */
#ifndef MOTOR_DRIVER_PULSES_PER_REV
#define MOTOR_DRIVER_PULSES_PER_REV 1600U
#endif
#define MOTOR_TARGET_ANGLE_MDEG     22500U
#define MOTOR_DIRECTION_LEVEL      0U /* GPIO low; physical direction unverified */
#define MOTOR_STARTUP_WAIT_MS       1000U /* suggested wait, NOT a T60 specification */

/* Matches .ioc: TIM1 = 72 MHz, PSC = 71, ARR = 9999, CCR1 = 5000. */
#define MOTOR_TIMER_INPUT_HZ        72000000U
#define MOTOR_TIMER_PRESCALER       71U
#define MOTOR_TIMER_PERIOD          9999U
#define MOTOR_TIMER_COMPARE         5000U
#define MOTOR_TIMER_BATCH_MAX       256U /* F103 TIM1 RCR has 8 bits */

/* Hardware-dependent continuous mode is OFF until the interface is verified.
 * Values below are software examples, NOT confirmed T60 timing/speed ratings.
 */
#ifndef MOTOR_CONTINUOUS_ENABLED
#define MOTOR_CONTINUOUS_ENABLED    0U
#endif
#define MOTOR_CONTINUOUS_MIN_HZ     100U
#define MOTOR_CONTINUOUS_MAX_HZ     1000U
#define MOTOR_DIRECTION_HOLD_MS     2U
#define MOTOR_DIRECTION_SETUP_MS    10U

#endif
