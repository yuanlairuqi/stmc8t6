#ifndef MOTOR_MATH_H
#define MOTOR_MATH_H

#include <stdint.h>

/* Nearest whole pulse, exactly half a pulse rounds upwards. No float needed. */
static inline uint32_t Motor_PulsesForAngle(uint32_t pulses_per_rev,
                                          uint32_t angle_mdeg)
{
  return (uint32_t)(((uint64_t)pulses_per_rev * angle_mdeg + 180000ULL)
                    / 360000ULL);
}

#endif
