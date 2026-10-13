#pragma once
#include <stdbool.h>
#include <stdint.h>
void battery_init(void);
bool battery_read(uint16_t *mv);
