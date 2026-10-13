#include "robot_hal_adapter.h"
#include "robot_peer.h"
#include "robot_port.h"
// Example only. No existing STM32 project is modified.
// RX IRQ only stores bytes. Protocol parsing/motor hooks run in the main loop.
#define RX_CAPACITY 128
static UART_HandleTypeDef *robot_uart;
static uint8_t rx_byte,rx_ring[RX_CAPACITY];
static volatile uint16_t rx_head,rx_tail;
static volatile bool rx_fault;
static wire_parser_t parser;
static robot_peer_t peer;
static uint32_t last_status;
static bool transmit_pending,tx_inflight;
static uint8_t latest_reply[WIRE_SIZE],tx_bytes[WIRE_SIZE];
static void queue_status(const wire_frame_t *reply) {wire_encode(reply,latest_reply);transmit_pending=true;}
static void service_tx(void) {
    // UART TX IT keeps motor watchdog polling independent of serial transmit time.
    if(tx_inflight) {
        if(robot_uart->gState==HAL_UART_STATE_READY)tx_inflight=false;
        else return;
    }
    if(!transmit_pending)return;
    for(unsigned i=0;i<WIRE_SIZE;++i)tx_bytes[i]=latest_reply[i];
    if(HAL_UART_Transmit_IT(robot_uart,tx_bytes,WIRE_SIZE)==HAL_OK){transmit_pending=false;tx_inflight=true;}
}
void RobotUART_Init(UART_HandleTypeDef *uart) {
    robot_uart=uart;rx_head=rx_tail=0;rx_fault=false;parser.used=0;
    transmit_pending=false;tx_inflight=false;
    peer_hooks_t hooks={.ctx=NULL,.ready=RobotPort_Ready,.apply=RobotPort_Apply,
        .stop=RobotPort_Stop,.battery=RobotPort_Battery,.stopped=RobotPort_Stopped,.local_busy=RobotPort_LocalBusy,.active=RobotPort_Active};
    peer_init(&peer,ROBOT_PORT_HARDWARE_ENABLED!=0,&hooks);
    if(HAL_UART_Receive_IT(robot_uart,&rx_byte,1)!=HAL_OK)rx_fault=true;
}
void RobotUART_RxComplete(UART_HandleTypeDef *uart) {
    if(uart!=robot_uart)return;
    uint16_t next=(uint16_t)((rx_head+1)%RX_CAPACITY);
    if(next==rx_tail)rx_fault=true;
    else {rx_ring[rx_head]=rx_byte;rx_head=next;}
    if(HAL_UART_Receive_IT(robot_uart,&rx_byte,1)!=HAL_OK)rx_fault=true;
}
void RobotUART_Error(UART_HandleTypeDef *uart) {if(uart==robot_uart)rx_fault=true;}
void RobotUART_Poll(void) {
    if(!robot_uart)return;
    uint32_t now=HAL_GetTick();wire_frame_t frame,reply;
    // No protocol work in interrupts. RX overflow/error invalidates the lease.
    if(rx_fault) {
        uint32_t saved=__get_PRIMASK();__disable_irq();rx_fault=false;rx_tail=rx_head;__set_PRIMASK(saved);
        peer_force_stop(&peer,PEER_BAD_COMMAND);parser.used=0;
        HAL_UART_AbortReceive(robot_uart);
        if(HAL_UART_Receive_IT(robot_uart,&rx_byte,1)!=HAL_OK)rx_fault=true;
    }
    unsigned budget=RX_CAPACITY;
    while(budget--&&rx_tail!=rx_head) {
        uint8_t byte=rx_ring[rx_tail];rx_tail=(uint16_t)((rx_tail+1)%RX_CAPACITY);
        if(wire_feed(&parser,byte,&frame)&&peer_receive(&peer,&frame,now,&reply))queue_status(&reply);
    }
    peer_tick(&peer,now);
    if(peer.has_session&&(uint32_t)(now-last_status)>=100){peer_status(&peer,&reply);queue_status(&reply);last_status=now;}
    service_tx();
}
