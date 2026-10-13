#ifndef ROBOT_UART_TEST_HAL_H
#define ROBOT_UART_TEST_HAL_H
#include "motor_test_hal.h"
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { void *Instance; uint32_t gState; } UART_HandleTypeDef;
#define USART2 ((void *)2)
#define HAL_UART_STATE_READY 0U
#define HAL_UART_STATE_BUSY_TX 1U
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t mask);
#define __DMB() do { } while (0)
void test_clear_ore(UART_HandleTypeDef *uart);
#define __HAL_UART_CLEAR_OREFLAG(u) test_clear_ore(u)
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *);
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *);
#endif
