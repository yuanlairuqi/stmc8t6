#pragma once
#include "robot_wire.h"
bool uart_link_init(void);
int uart_link_read(uint8_t *data,size_t capacity);
bool uart_link_send(const wire_frame_t *frame);
