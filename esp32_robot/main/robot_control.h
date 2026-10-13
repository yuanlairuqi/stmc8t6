#ifndef ROBOT_CONTROL_H
#define ROBOT_CONTROL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ROBOT_CMD_SIZE 16
#define ROBOT_STATUS_SIZE 20
#define ROBOT_TIMEOUT_MS 800
enum { ROBOT_STOP, ROBOT_FORWARD, ROBOT_BACKWARD };
enum { ROBOT_OK, ROBOT_BAD_COMMAND, ROBOT_TIMEOUT, ROBOT_DISCONNECTED, ROBOT_ADC_ERROR,
    ROBOT_UART_UNCONFIGURED, ROBOT_STM_OFFLINE, ROBOT_STM_REJECTED, ROBOT_UART_TX_ERROR, ROBOT_STM_ACK_TIMEOUT };
typedef struct {
    uint8_t motion, speed, error, mode;
    uint32_t forward_ms, reverse_ms;
    uint32_t ack, rx_count, last_motion_ms;
    bool has_seq, connected, battery_valid;
    uint16_t battery_mv;
    uint32_t revision, stm_ack;
    bool uart_configured, stm_online;
    uint8_t stm_motion, stm_mode, stm_phase, stm_flags, stm_error, link_error;
} robot_control_t;
void robot_init(robot_control_t *r);
void robot_connection(robot_control_t *r, bool connected);
bool robot_command(robot_control_t *r, const uint8_t *data, size_t len, uint32_t now);
void robot_tick(robot_control_t *r, uint32_t now);
void robot_stop(robot_control_t *r,uint8_t error);
bool robot_can_move(const robot_control_t *r);
void robot_status(const robot_control_t *r, uint32_t now, uint8_t out[ROBOT_STATUS_SIZE]);
#endif
