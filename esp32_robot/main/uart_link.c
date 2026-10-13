#include "uart_link.h"
#include "sdkconfig.h"
#include "esp_log.h"
#ifdef CONFIG_ROBOT_UART_ENABLED
#include "driver/uart.h"
#include "driver/gpio.h"
static bool available;
bool uart_link_init(void) {
    // -1 means NOT DECIDED, not UART_PIN_NO_CHANGE for TX/RX.
    if(CONFIG_ROBOT_UART_TX_GPIO<0||CONFIG_ROBOT_UART_RX_GPIO<0||
       !GPIO_IS_VALID_OUTPUT_GPIO(CONFIG_ROBOT_UART_TX_GPIO)||!GPIO_IS_VALID_GPIO(CONFIG_ROBOT_UART_RX_GPIO)||
       CONFIG_ROBOT_UART_TX_GPIO==CONFIG_ROBOT_UART_RX_GPIO) {
        ESP_LOGE("uart_link","UART pins not configured; no GPIO is touched");return false;
    }
    uart_config_t cfg={.baud_rate=CONFIG_ROBOT_UART_BAUD,.data_bits=UART_DATA_8_BITS,
        .parity=UART_PARITY_DISABLE,.stop_bits=UART_STOP_BITS_1,.flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};
    esp_err_t rc=uart_param_config(CONFIG_ROBOT_UART_PORT,&cfg);
    if(rc==ESP_OK)rc=uart_set_pin(CONFIG_ROBOT_UART_PORT,CONFIG_ROBOT_UART_TX_GPIO,CONFIG_ROBOT_UART_RX_GPIO,
        UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE);
    if(rc==ESP_OK)rc=uart_driver_install(CONFIG_ROBOT_UART_PORT,512,512,0,NULL,0);
    available=rc==ESP_OK;
    if(!available)ESP_LOGE("uart_link","UART init failed: %s",esp_err_to_name(rc));
    return available;
}
int uart_link_read(uint8_t *data,size_t n) {return available?uart_read_bytes(CONFIG_ROBOT_UART_PORT,data,n,0):0;}
bool uart_link_send(const wire_frame_t *frame) {
    if(!available)return false;
    uint8_t bytes[WIRE_SIZE];wire_encode(frame,bytes);
    return uart_write_bytes(CONFIG_ROBOT_UART_PORT,bytes,sizeof(bytes))==sizeof(bytes);
}
#else
bool uart_link_init(void) {ESP_LOGI("uart_link","UART disabled: BLE demo only");return false;}
int uart_link_read(uint8_t *data,size_t n) {(void)data;(void)n;return 0;}
bool uart_link_send(const wire_frame_t *f) {(void)f;return false;}
#endif
