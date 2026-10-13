#ifndef ROBOT_UART_H
#define ROBOT_UART_H
#ifdef ROBOT_UART_HOST_TEST
#include "robot_uart_test_hal.h"
#else
#include "main.h"
#endif
#define ROBOT_UART_LINK_TIMEOUT_MS 600U
#define ROBOT_UART_STATUS_MS 100U
#define ROBOT_UART_TX_TIMEOUT_MS 100U
typedef struct {
  uint32_t frames_received, frames_sent, rx_faults, tx_faults, link_timeouts;
  uint8_t linked;
} RobotUARTStatus;
extern volatile RobotUARTStatus g_robot_uart_status; /* SWD diagnostics */
void RobotUART_Init(UART_HandleTypeDef *uart);
void RobotUART_Poll(void);
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart);
#endif
