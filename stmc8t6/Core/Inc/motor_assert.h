#ifndef MOTOR_ASSERT_H
#define MOTOR_ASSERT_H
/* Keep the existing ARMCC5 project usable without changing compiler/framework. */
#if defined(__CC_ARM)
#define MOTOR_ASSERT_JOIN_(a, b) a##b
#define MOTOR_ASSERT_JOIN(a, b) MOTOR_ASSERT_JOIN_(a, b)
#define MOTOR_STATIC_ASSERT(condition, message) \
  typedef char MOTOR_ASSERT_JOIN(motor_static_assert_line_, __LINE__)[(condition) ? 1 : -1]
#else
#define MOTOR_STATIC_ASSERT(condition, message) _Static_assert(condition, message)
#endif
#endif
