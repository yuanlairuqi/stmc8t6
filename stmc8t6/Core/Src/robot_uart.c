#include "robot_uart.h"
#include "robot_peer.h"
#include "motor_motion.h"
#include <string.h>

#define RX_CAPACITY 256U
static UART_HandleTypeDef *link_uart;
static uint8_t rx_byte, rx_ring[RX_CAPACITY];
static volatile uint16_t rx_head, rx_tail;
static volatile uint8_t rx_fault;
static wire_parser_t parser;
static robot_peer_t peer;
static uint32_t last_rx, last_status, tx_started;
static uint8_t tx_pending, tx_inflight;
static uint8_t latest_status[WIRE_SIZE], tx_bytes[WIRE_SIZE];
volatile RobotUARTStatus g_robot_uart_status;

static bool Ready(void *ctx)
{
  (void)ctx;
  return Motor_ContinuousReady();
}
static bool Apply(void *ctx, uint8_t motion, uint8_t speed)
{
  (void)ctx;
  return Motor_SetContinuous(motion, speed);
}
static void Stop(void *ctx) { (void)ctx; Motor_Stop(); }
static bool Stopped(void *ctx) { (void)ctx; return Motor_OutputStopped(); }
static bool Active(void *ctx) { (void)ctx; return Motor_OutputActive(); }
static bool LocalBusy(void *ctx)
{
  (void)ctx;
  return g_motor_status.state == MOTOR_WAITING ||
         g_motor_status.state == MOTOR_RUNNING;
}

static void QueueStatus(wire_frame_t *frame)
{
  if (g_motor_status.state == MOTOR_FAULT) {
    frame->payload[3] = PEER_DRIVER_ERROR;
    frame->payload[2] &= (uint8_t)~PEER_APPLIED;
  }
  wire_encode(frame, latest_status);
  tx_pending = 1U; /* One latest status + one immutable frame in flight. */
}

static void ServiceTX(uint32_t now)
{
  if (tx_inflight) {
    if (link_uart->gState == HAL_UART_STATE_READY) {
      tx_inflight = 0U;
      ++g_robot_uart_status.frames_sent;
    } else if ((uint32_t)(now - tx_started) >= ROBOT_UART_TX_TIMEOUT_MS) {
      HAL_UART_AbortTransmit(link_uart); /* No DMA configured: no blocking wait. */
      tx_inflight = 0U;
      ++g_robot_uart_status.tx_faults;
      peer_force_stop(&peer, PEER_DRIVER_ERROR);
      wire_frame_t frame;
      peer_status(&peer, &frame);
      QueueStatus(&frame);
    } else { return; }
  }
  if (!tx_pending) { return; }
  memcpy(tx_bytes, latest_status, WIRE_SIZE);
  if (HAL_UART_Transmit_IT(link_uart, tx_bytes, WIRE_SIZE) == HAL_OK) {
    tx_pending = 0U;
    tx_inflight = 1U;
    tx_started = now;
  } else {
    ++g_robot_uart_status.tx_faults;
    peer_force_stop(&peer, PEER_DRIVER_ERROR);
    wire_frame_t frame;
    peer_status(&peer, &frame);
    QueueStatus(&frame);
  }
}

void RobotUART_Init(UART_HandleTypeDef *uart)
{
  if (link_uart != NULL) { return; } /* initialization is once per reset */
  if (uart == NULL || uart->Instance != USART2) { return; }
  link_uart = uart;
  peer_hooks_t hooks = {.ctx = NULL, .ready = Ready, .apply = Apply,
                        .stop = Stop, .battery = NULL,
                        .stopped = Stopped, .local_busy = LocalBusy, .active = Active};
  peer_init(&peer, true, &hooks); /* Hardware adapter, never report DEMO. */
  last_rx = last_status = HAL_GetTick();
  if (HAL_UART_Receive_IT(link_uart, &rx_byte, 1U) != HAL_OK) {
    rx_fault = 1U;
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
  if (uart != link_uart) { return; }
  uint16_t next = (uint16_t)((rx_head + 1U) % RX_CAPACITY);
  if (next == rx_tail) { rx_fault = 1U; }
  else {
    rx_ring[rx_head] = rx_byte;
    __DMB();
    rx_head = next;
  }
  if (HAL_UART_Receive_IT(link_uart, &rx_byte, 1U) != HAL_OK) {
    rx_fault = 1U;
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
  if (uart == link_uart) { rx_fault = 1U; }
}

void RobotUART_Poll(void)
{
  if (link_uart == NULL) { return; }
  uint32_t now = HAL_GetTick();
  wire_frame_t frame, reply;
  if (rx_fault) {
    uint32_t saved = __get_PRIMASK();
    __disable_irq();
    rx_fault = 0U;
    rx_tail = rx_head;
    parser.used = 0U;
    HAL_UART_AbortReceive(link_uart);
    __HAL_UART_CLEAR_OREFLAG(link_uart); /* F1: reads SR then DR. */
    if (HAL_UART_Receive_IT(link_uart, &rx_byte, 1U) != HAL_OK) {
      rx_fault = 1U;
    }
    __set_PRIMASK(saved);
    ++g_robot_uart_status.rx_faults;
    peer_force_stop(&peer, PEER_BAD_COMMAND);
    g_robot_uart_status.linked = 0U;
    if (peer.has_session) { peer_status(&peer, &reply); QueueStatus(&reply); }
  }
  unsigned budget = RX_CAPACITY;
  while (budget-- && rx_tail != rx_head) {
    __DMB();
    uint8_t byte = rx_ring[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1U) % RX_CAPACITY);
    if (wire_feed(&parser, byte, &frame)) {
      ++g_robot_uart_status.frames_received;
      if (peer_receive(&peer, &frame, now, &reply)) {
        last_rx = now;
        g_robot_uart_status.linked = 1U;
        QueueStatus(&reply);
      }
    }
  }
  if (g_robot_uart_status.linked &&
      (uint32_t)(now - last_rx) >= ROBOT_UART_LINK_TIMEOUT_MS) {
    g_robot_uart_status.linked = 0U;
    ++g_robot_uart_status.link_timeouts;
    peer_force_stop(&peer, PEER_TIMEOUT);
    peer_status(&peer, &reply);
    QueueStatus(&reply);
  }
  peer_tick(&peer, now);
  if (peer.has_session && (uint32_t)(now - last_status) >= ROBOT_UART_STATUS_MS) {
    peer_status(&peer, &reply);
    QueueStatus(&reply);
    last_status = now;
  }
  ServiceTX(now);
}
