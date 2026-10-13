#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "robot_uart.h"
#include "robot_wire.h"
#include "robot_peer.h"
#include "motor_motion.h"
#include "motor_config.h"
#include "robot_bridge.h" /* pinned ESP32 bridge snapshot, host tests only */
TIM_TypeDef test_tim1;
static uint32_t tick, mask, abort_rx, abort_tx, clear_ore, tx_calls;
static uint32_t mock_tx_at;
static uint8_t *rx_slot, *tx_slot;
static HAL_StatusTypeDef tx_result = HAL_OK;
static HAL_StatusTypeDef rx_result = HAL_OK;
static UART_HandleTypeDef uart = {USART2, HAL_UART_STATE_READY};
uint32_t HAL_GetTick(void) { return tick; }
void HAL_GPIO_WritePin(void *p, uint32_t n, GPIO_PinState v)
{ (void)p; (void)n; (void)v; }
uint32_t __get_PRIMASK(void) { return mask; }
void __disable_irq(void) { mask = 1U; }
void __set_PRIMASK(uint32_t m) { mask = m; }
void test_clear_ore(UART_HandleTypeDef *u) { assert(u == &uart); ++clear_ore; }
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *u, uint8_t *p, uint16_t n)
{ assert(u == &uart && n == 1); rx_slot = p; return rx_result; }
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *u, uint8_t *p, uint16_t n)
{
  assert(u == &uart && n == WIRE_SIZE);
  if (tx_result != HAL_OK) { return tx_result; }
  assert(u->gState == HAL_UART_STATE_READY);
  tx_slot = p; u->gState = HAL_UART_STATE_BUSY_TX; ++tx_calls; return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *u)
{ assert(u == &uart && mask == 1); ++abort_rx; return HAL_OK; }
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *u)
{ assert(u == &uart); ++abort_tx; u->gState = HAL_UART_STATE_READY; return HAL_OK; }
static wire_frame_t Command(uint8_t op, uint32_t token)
{
  wire_frame_t f = {.type = WIRE_COMMAND, .session = 42, .token = token};
  f.payload[0] = op; f.payload[1] = op ? 20 : 0;
  f.payload[2] = 0xf4; f.payload[3] = 1; /* lease 500 */
  wire_put32(f.payload + 4, token + 1000);
  return f;
}
static void Byte(uint8_t b) { *rx_slot = b; HAL_UART_RxCpltCallback(&uart); }
static void Frame(const wire_frame_t *f, bool split)
{
  uint8_t b[WIRE_SIZE]; wire_encode(f, b);
  for (unsigned i = 0; i < WIRE_SIZE; ++i) {
    Byte(b[i]); if (split) { RobotUART_Poll(); }
  }
  if (!split) { RobotUART_Poll(); }
}
static wire_frame_t Reply(void)
{
  wire_frame_t f; assert(tx_slot != NULL);
  assert(wire_decode(tx_slot, &f)); assert(f.type == WIRE_STATUS);
  return f;
}
static void CompleteTX(void) { uart.gState = HAL_UART_STATE_READY; RobotUART_Poll(); }
static void Advance(uint32_t ms)
{
  while (ms--) {
    ++tick;
    if (uart.gState == HAL_UART_STATE_READY) { mock_tx_at = tick; }
    if ((uint32_t)(tick - mock_tx_at) >= 3U) { uart.gState = HAL_UART_STATE_READY; mock_tx_at = tick; }
    RobotUART_Poll(); Motor_Process();
  }
}
static void ExpectStopped(bool exact)
{
  assert(g_motor_status.state == MOTOR_ABORTED);
  assert(Motor_OutputStopped());
  assert(g_motor_status.pulse_count_exact == (exact ? 1 : 0));
  TIM_HandleTypeDef timer = {TIM1}; Motor_StartOnce(&timer);
  tick += 5000; Motor_Process();
  assert(g_motor_status.state == MOTOR_ABORTED);
}
int main(int argc, char **argv)
{
  assert(argc == 2); const char *scenario = argv[1];
  TIM_HandleTypeDef timer = {TIM1}; test_tim1.CCMR1 = TIM_OCMODE_PWM2;
  tick = UINT32_MAX - 500U; /* all tests exercise unsigned time arithmetic */
  RobotUART_Init(NULL);
  UART_HandleTypeDef other = {(void *)3, HAL_UART_STATE_READY};
  RobotUART_Init(&other); assert(rx_slot == NULL);
  if (!strcmp(scenario, "rx-rearm-fail")) { rx_result = HAL_BUSY; }
  RobotUART_Init(&uart); Motor_StartOnce(&timer);
  assert(g_motor_status.state == MOTOR_WAITING);
  HAL_UART_ErrorCallback(&other); RobotUART_Poll();
  if (!strcmp(scenario, "rx-rearm-fail")) {
    assert(g_robot_uart_status.rx_faults >= 1); ExpectStopped(true);
    rx_result = HAL_OK; RobotUART_Poll();
    puts("PASS RX rearm failure cancels pending move and recovers"); return 0;
  }
  assert(g_robot_uart_status.rx_faults == 0);
  wire_frame_t stop = Command(0, 1), move = Command(1, 2), reply;
  if (!strcmp(scenario, "enabled-cycle")) {
    assert(MOTOR_CONTINUOUS_ENABLED == 1U);
    robot_bridge_t bridge; wire_frame_t frame;
    bridge_init(&bridge, true, 0x87654321U);
    assert(bridge_tick(&bridge, tick, &frame)); Frame(&frame, true); reply = Reply();
    assert(bridge_receive(&bridge, &reply, tick));
    assert(bridge.peer_flags == (PEER_READY | PEER_APPLIED)); CompleteTX();
    bridge_parameters(&bridge, 1, 300, 200);
    bridge_request(&bridge, 1, 1, 50, 201, tick);
    assert(bridge_tick(&bridge, tick, &frame)); Frame(&frame, false); reply = Reply();
    assert(bridge_receive(&bridge, &reply, tick) && !(bridge.peer_flags & PEER_APPLIED));
    assert(g_motor_status.state == MOTOR_CONTINUOUS_HOLD);
    CompleteTX(); Advance(12); assert(Motor_OutputActive());
    wire_frame_t ping = {.type = WIRE_PING, .session = bridge.session};
    Frame(&ping, false); CompleteTX(); reply = Reply();
    assert(bridge_receive(&bridge, &reply, tick) && (bridge.peer_flags & PEER_APPLIED));
    Advance(288); assert(g_motor_status.state == MOTOR_CONTINUOUS_HOLD);
    assert(!Motor_OutputActive()); CompleteTX(); CompleteTX(); reply = Reply();
    assert(bridge_receive(&bridge, &reply, tick) && bridge.peer_phase == 2);
    Advance(12); assert(Motor_OutputActive());
    test_tim1.CNT = 123;
    bridge_request(&bridge, 2, 1, 50, 202, tick); /* same parameters, fresh token */
    assert(bridge_tick(&bridge, tick, &frame)); Frame(&frame, false); CompleteTX(); reply = Reply();
    assert(bridge_receive(&bridge, &reply, tick) && bridge.peer_phase == 2);
    assert(test_tim1.CNT == 123 && Motor_OutputActive());
    uint32_t previous_arr = test_tim1.ARR;
    bridge_request(&bridge, 3, 1, 60, 203, tick); /* speed-only update during reverse */
    assert(bridge_tick(&bridge, tick, &frame)); Frame(&frame, false); CompleteTX(); reply = Reply();
    assert(bridge_receive(&bridge, &reply, tick) && bridge.peer_phase == 2 && bridge.peer_speed == 60);
    assert(test_tim1.CNT == 123 && Motor_OutputActive() && test_tim1.ARR != previous_arr);
    Advance(188); assert(g_motor_status.state == MOTOR_CONTINUOUS_HOLD);
    CompleteTX(); CompleteTX(); reply = Reply();
    assert(bridge_receive(&bridge, &reply, tick) && bridge.peer_phase == 1);
    Advance(12); assert(Motor_OutputActive());
    /* PINGs keep the UART online but cannot renew the motion lease. */
    Frame(&ping, false); CompleteTX(); Advance(300);
    assert(g_motor_status.state == MOTOR_ABORTED && Motor_OutputStopped());
    Frame(&ping, false); /* Explicit query need not wait for the next 100ms report. */
    CompleteTX(); CompleteTX(); reply = Reply();
    assert(reply.payload[3] == PEER_TIMEOUT && reply.payload[0] == 0);
    puts("PASS enabled UART cycle: 300/200ms phases, active after DIR delay, fresh heartbeat/speed change preserve reverse phase, lease expires despite PING");
    return 0;
  }
  if (!strcmp(scenario, "cross-bridge")) {
    robot_bridge_t bridge;
    bridge_init(&bridge, true, 0xdeadbeefU);
    wire_frame_t frame;
    assert(bridge_tick(&bridge, tick, &frame)); /* actual ESP startup STOP */
    Frame(&frame, true); reply = Reply();
    assert(bridge_receive(&bridge, &reply, tick));
    assert(bridge.online && bridge.confirmed && bridge.peer_error == PEER_OK);
    assert((bridge.peer_flags & PEER_APPLIED) && !(bridge.peer_flags & PEER_READY));
    assert(g_motor_status.state == MOTOR_ABORTED);
    CompleteTX();
    bridge_request(&bridge, 1, 1, 40, 201, tick + 10);
    assert(bridge_tick(&bridge, tick + 10, &frame)); Frame(&frame, false);
    reply = Reply(); assert(bridge_receive(&bridge, &reply, tick + 10));
    assert(bridge.error == BRIDGE_REJECTED && bridge.peer_error == PEER_NOT_READY);
    assert(bridge.ack == 201 && !(bridge.peer_flags & PEER_APPLIED));
    CompleteTX();
    bridge_request(&bridge, 2, 0, 0, 202, tick + 20);
    assert(bridge_tick(&bridge, tick + 20, &frame)); Frame(&frame, false);
    reply = Reply(); assert(bridge_receive(&bridge, &reply, tick + 20));
    assert(bridge.error == BRIDGE_OK && bridge.ack == 202);
    CompleteTX();
    /* Both peers run through 10 real encoded/decoded PING/STATUS exchanges. */
    for (unsigned i = 0; i < 10; ++i) {
      tick += 220;
      assert(bridge_tick(&bridge, tick, &frame) && frame.type == WIRE_PING);
      Frame(&frame, true); reply = Reply();
      assert(bridge_receive(&bridge, &reply, tick) && bridge.online);
      CompleteTX(); CompleteTX(); /* allow periodic status TX to finish too */
    }
    tick += 600; RobotUART_Poll(); CompleteTX();
    assert(!g_robot_uart_status.linked && Reply().payload[3] == PEER_TIMEOUT);
    /* ESP times out independently and establishes a new STOP confirmation. */
    assert(bridge_tick(&bridge, tick, &frame) && frame.payload[0] == 0);
    Frame(&frame, true); CompleteTX(); reply = Reply();
    assert(bridge_receive(&bridge, &reply, tick) && bridge.online && bridge.peer_error == 0);
    bridge_init(&bridge, true, 0x12345678U);
    assert(bridge_tick(&bridge, tick, &frame)); Frame(&frame, true); CompleteTX();
    assert(bridge_receive(&bridge, &(wire_frame_t){0}, tick) == false);
    reply = Reply(); assert(bridge_receive(&bridge, &reply, tick));
    assert(bridge.online && reply.session == 0x12345678U && g_motor_status.state == MOTOR_ABORTED);
    puts("PASS actual ESP bridge + STM HAL adapter roundtrip, rejection, STOP ACK, ping, timeout, ESP reboot");
    return 0;
  }
  if (!strcmp(scenario, "standalone")) {
    tick += 1000; RobotUART_Poll(); Motor_Process();
    assert(g_motor_status.state == MOTOR_RUNNING);
    assert(!Motor_OutputStopped());
    test_tim1.CR1 &= ~TIM_CR1_CEN; test_tim1.SR = TIM_SR_UIF; test_tim1.CNT = 0;
    Motor_Process(); assert(g_motor_status.state == MOTOR_DONE);
    assert(g_motor_status.completed_pulses == 100 && tx_calls == 0);
    puts("PASS disconnected UART retains autonomous one-shot"); return 0;
  }
  if (!strcmp(scenario, "running-stop") || !strcmp(scenario, "finished-batch-stop")) {
    tick += 1000; Motor_Process(); assert(g_motor_status.state == MOTOR_RUNNING);
    test_tim1.CNT = MOTOR_TIMER_COMPARE;
    if (!strcmp(scenario, "finished-batch-stop")) {
      test_tim1.CR1 &= ~TIM_CR1_CEN; test_tim1.SR = TIM_SR_UIF;
    }
    Frame(&stop, false);
    bool exact = !strcmp(scenario, "finished-batch-stop");
    assert(g_motor_status.completed_pulses == (exact ? 100U : 0U));
    ExpectStopped(exact); reply = Reply();
    assert(reply.payload[3] == PEER_OK && (reply.payload[2] & PEER_APPLIED));
    puts("PASS STOP during running/finished batch, count certainty bounded"); return 0;
  }
  if (!strcmp(scenario, "rx-overflow") || !strcmp(scenario, "uart-error")) {
    if (!strcmp(scenario, "rx-overflow")) {
      for (unsigned i = 0; i < 256; ++i) { Byte(0xa5); }
    } else { HAL_UART_ErrorCallback(&uart); }
    mask = 1; RobotUART_Poll(); assert(mask == 1); mask = 0;
    assert(abort_rx == 1 && clear_ore == 1 && g_robot_uart_status.rx_faults == 1);
    ExpectStopped(true); Frame(&stop, true); reply = Reply();
    assert(reply.token == 1 && reply.payload[3] == PEER_OK);
    puts("PASS overflow/UART error stops, flushes, rearms and resynchronizes"); return 0;
  }
  if (!strcmp(scenario, "bad-stream")) {
    uint8_t b[WIRE_SIZE]; wire_encode(&stop, b); b[18] ^= 0x80;
    Byte(0x00); Byte(0xa5); Byte(0xa5); Byte(0x00);
    for (unsigned i = 0; i < WIRE_SIZE; ++i) { Byte(b[i]); }
    RobotUART_Poll(); assert(tx_calls == 0 && g_motor_status.state == MOTOR_WAITING);
    Frame(&stop, true); assert(Reply().token == 1); ExpectStopped(true);
    puts("PASS noise/CRC failure ignored; fragmented valid frame recovers"); return 0;
  }
  if (!strcmp(scenario, "new-session-move")) {
    Frame(&move, true); assert(tx_calls == 0 && g_motor_status.state == MOTOR_WAITING);
    Frame(&stop, true); assert(Reply().token == 1); ExpectStopped(true);
    puts("PASS new session requires STOP before any movement"); return 0;
  }
  Frame(&stop, true); reply = Reply();
  assert(reply.session == 42 && reply.token == 1 && wire_u32(reply.payload + 6) == 1001);
  assert(reply.payload[0] == 0 && reply.payload[1] == 0 && reply.payload[3] == PEER_OK);
  assert(reply.payload[2] == PEER_APPLIED); /* READY/DEMO/BATTERY/BUSY all clear */
  assert(reply.payload[4] == 0xff && reply.payload[5] == 0xff);
  assert(g_motor_status.state == MOTOR_ABORTED);
  if (!strcmp(scenario, "tx-buffer")) {
    uint8_t original[WIRE_SIZE]; memcpy(original, tx_slot, WIRE_SIZE);
    Frame(&move, false); assert(memcmp(original, tx_slot, WIRE_SIZE) == 0);
    CompleteTX(); reply = Reply(); assert(reply.token == 2);
    assert(reply.payload[3] == PEER_NOT_READY && !(reply.payload[2] & PEER_APPLIED));
    puts("PASS immutable in-flight frame; pending status coalesces; unsupported move rejected");
  } else if (!strcmp(scenario, "tx-timeout")) {
    tick += 99; RobotUART_Poll(); assert(abort_tx == 0);
    ++tick; RobotUART_Poll(); assert(abort_tx == 1 && g_robot_uart_status.tx_faults == 1);
    reply = Reply(); assert(reply.payload[3] == PEER_DRIVER_ERROR);
    puts("PASS stalled TX aborts at 100ms without blocking main loop");
  } else if (!strcmp(scenario, "tx-start-fail")) {
    CompleteTX(); tx_result = HAL_BUSY; move = Command(0, 2); Frame(&move, false);
    assert(g_robot_uart_status.tx_faults == 1); tx_result = HAL_OK; RobotUART_Poll();
    assert(Reply().payload[3] == PEER_DRIVER_ERROR);
    puts("PASS TX start failure returns driver error and retries latest status");
  } else if (!strcmp(scenario, "link-timeout")) {
    CompleteTX(); tick += 599; RobotUART_Poll(); assert(g_robot_uart_status.linked);
    CompleteTX(); ++tick; RobotUART_Poll(); assert(!g_robot_uart_status.linked);
    assert(g_robot_uart_status.link_timeouts == 1);
    CompleteTX(); assert(Reply().payload[3] == PEER_TIMEOUT);
    wire_frame_t ping = {.type = WIRE_PING, .session = 42}; Frame(&ping, false);
    assert(g_robot_uart_status.linked); CompleteTX(); assert(Reply().payload[3] == PEER_TIMEOUT);
    stop = Command(0, 3); Frame(&stop, false); CompleteTX();
    assert(Reply().payload[3] == PEER_OK);
    puts("PASS 600ms link timeout, ping does not clear stop reason, explicit STOP recovers");
  } else if (!strcmp(scenario, "duplicate")) {
    CompleteTX(); Frame(&stop, false); assert(Reply().token == 1);
    CompleteTX(); move.token = 0; Frame(&move, false); assert(Reply().token == 1);
    CompleteTX(); stop = Command(0, 0); stop.session = 43; Frame(&stop, false);
    assert(Reply().session == 43 && Reply().token == 0);
    puts("PASS duplicates/old tokens cannot restart; new session STOP accepted");
  } else if (!strcmp(scenario, "waiting-stop")) {
    ExpectStopped(true); puts("PASS STOP cancels waiting one-shot permanently until reset");
  } else { assert(!"unknown test"); }
  return 0;
}
