#pragma once
#include "stm32f1xx_hal.h"
void RobotUART_Init(UART_HandleTypeDef *uart);
void RobotUART_Poll(void);
void RobotUART_RxComplete(UART_HandleTypeDef *uart);
void RobotUART_Error(UART_HandleTypeDef *uart);
