#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "robot_control.h"
#include "battery.h"
#include "robot_bridge.h"
#include "uart_link.h"
#include "esp_random.h"
// UUIDs: 6e400001/2/3-b5a3-f393-e0a9-e50e24dcca9e, little endian in NimBLE.
static const ble_uuid128_t service_uuid = BLE_UUID128_INIT(
    0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x01,0x00,0x40,0x6e);
static const ble_uuid128_t command_uuid = BLE_UUID128_INIT(
    0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x02,0x00,0x40,0x6e);
static const ble_uuid128_t status_uuid = BLE_UUID128_INIT(
    0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x03,0x00,0x40,0x6e);
static robot_control_t robot;
static SemaphoreHandle_t lock;
static uint16_t status_handle, connection = BLE_HS_CONN_HANDLE_NONE;
static uint8_t own_addr_type;
static bool subscribed;
static robot_bridge_t bridge;
static bool uart_available;
static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time()/1000); }
static int access_status(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctx, void *arg) {
    (void)conn; (void)attr; (void)arg;
    uint8_t data[ROBOT_STATUS_SIZE];
    xSemaphoreTake(lock, portMAX_DELAY); robot_status(&robot, now_ms(), data); xSemaphoreGive(lock);
    return os_mbuf_append(ctx->om, data, sizeof(data)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}
static int access_command(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctx, void *arg) {
    (void)conn; (void)attr; (void)arg;
    uint8_t data[ROBOT_CMD_SIZE];
    uint16_t len = 0;
    if (OS_MBUF_PKTLEN(ctx->om) != sizeof(data) ||
        ble_hs_mbuf_to_flat(ctx->om, data, sizeof(data), &len) != 0) {
        xSemaphoreTake(lock, portMAX_DELAY);
        robot_stop(&robot,ROBOT_BAD_COMMAND);
        xSemaphoreGive(lock);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    bool accepted = robot_command(&robot, data, len, now_ms());
    uint8_t motion=robot.motion, speed=robot.speed;
    xSemaphoreGive(lock);
    ESP_LOGI("robot", "BLE command %s: motion=%u speed=%u; STM32 acknowledgement is separate",
        accepted ? "accepted" : "rejected", motion, speed);
    return accepted ? 0 : BLE_ATT_ERR_UNLIKELY;
}
static const struct ble_gatt_svc_def services[] = {
    {.type=BLE_GATT_SVC_TYPE_PRIMARY, .uuid=&service_uuid.u,
     .characteristics=(struct ble_gatt_chr_def[]) {
        {.uuid=&command_uuid.u, .access_cb=access_command, .flags=BLE_GATT_CHR_F_WRITE},
        {.uuid=&status_uuid.u, .access_cb=access_status,
         .flags=BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, .val_handle=&status_handle},
        {0}}}, {0}
};
static int gap_event(struct ble_gap_event *event, void *arg);
static void advertise(void) {
    struct ble_hs_adv_fields fields = {0}, response = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&service_uuid;
    fields.num_uuids128 = 1; fields.uuids128_is_complete = 1;
    const char *name = ble_svc_gap_device_name();
    response.name = (uint8_t *)name; response.name_len = strlen(name); response.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc == 0) rc = ble_gap_adv_rsp_set_fields(&response);
    struct ble_gap_adv_params params = {.conn_mode=BLE_GAP_CONN_MODE_UND, .disc_mode=BLE_GAP_DISC_MODE_GEN};
    if (rc == 0) rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc) ESP_LOGE("robot", "Advertising failed: %d", rc);
    else ESP_LOGI("robot", "Advertising ESP32-Robot");
}
static int gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    switch(event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            xSemaphoreTake(lock, portMAX_DELAY);
            connection=event->connect.conn_handle; subscribed=false;
            robot_connection(&robot, true); xSemaphoreGive(lock);
            ESP_LOGI("robot", "Phone connected");
        } else advertise();
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        xSemaphoreTake(lock, portMAX_DELAY);
        connection=BLE_HS_CONN_HANDLE_NONE; subscribed=false;
        robot_connection(&robot, false); xSemaphoreGive(lock);
        ESP_LOGI("robot", "Disconnected: control stopped"); advertise(); break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == status_handle) {
            xSemaphoreTake(lock, portMAX_DELAY);
            subscribed = event->subscribe.cur_notify; xSemaphoreGive(lock);
        }
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE: advertise(); break;
    default: break;
    }
    return 0;
}
static void sync_host(void) {
    int rc=ble_hs_util_ensure_addr(0); assert(rc == 0);
    rc=ble_hs_id_infer_auto(0, &own_addr_type); assert(rc == 0);
    advertise();
}
static void reset_host(int reason) {
    xSemaphoreTake(lock, portMAX_DELAY);
    subscribed=false; connection=BLE_HS_CONN_HANDLE_NONE; robot_connection(&robot, false);
    xSemaphoreGive(lock);
    ESP_LOGE("robot", "BLE host reset: %d", reason);
}
static void host_task(void *arg) { (void)arg; nimble_port_run(); nimble_port_freertos_deinit(); }
static void telemetry_task(void *arg) {
    (void)arg; uint32_t last_battery=0,last_notify=0;uint16_t esp_mv=0;bool esp_battery_valid=false;
    wire_parser_t parser={0};
    for (;;) {
        uint32_t now=now_ms(); uint16_t mv=0; bool sample=(uint32_t)(now-last_battery)>=500;
        bool valid=sample ? battery_read(&mv) : false;
        uint8_t data[ROBOT_STATUS_SIZE]; uint16_t conn; bool notify;
        uint8_t incoming[96];int count=uart_link_read(incoming,sizeof(incoming));wire_frame_t frame;
        for(int i=0;i<count;++i)if(wire_feed(&parser,incoming[i],&frame))bridge_receive(&bridge,&frame,now);
        bool transmit=false;wire_frame_t outgoing;
        xSemaphoreTake(lock, portMAX_DELAY);
        robot.stm_mode=bridge.peer_mode;robot.stm_phase=bridge.peer_phase;
        robot.stm_online=bridge.online;robot.stm_flags=bridge.peer_flags;robot.stm_motion=bridge.peer_motion;
        robot.stm_error=bridge.peer_error;robot.stm_ack=bridge.ack;
        robot.link_error=robot.uart_configured?(uart_available?bridge.error:ROBOT_UART_TX_ERROR):ROBOT_OK;
        robot_tick(&robot, now);
        if(sample) { esp_battery_valid=valid;esp_mv=mv;last_battery=now; }
        robot.battery_valid=esp_battery_valid;robot.battery_mv=esp_mv;
        if(bridge.online&&(bridge.peer_flags&PEER_BATTERY)){robot.battery_valid=true;robot.battery_mv=bridge.battery_mv;}
        // Always drive bridge requests from fresh BLE revisions. Ping/retry cannot renew a motion lease.
        bridge_parameters(&bridge,robot.mode,robot.forward_ms,robot.reverse_ms);
        bridge_request(&bridge,robot.revision,robot.motion,robot.speed,robot.ack,now);
        transmit=bridge_tick(&bridge,now,&outgoing);
        robot.stm_mode=bridge.peer_mode;robot.stm_phase=bridge.peer_phase;
        robot.stm_online=bridge.online;robot.stm_flags=bridge.peer_flags;
        robot.link_error=robot.uart_configured?(uart_available?bridge.error:ROBOT_UART_TX_ERROR):ROBOT_OK;
        robot_tick(&robot,now);
        // A timeout-generated stop supersedes any movement selected earlier in this tick.
        if(robot.motion==ROBOT_STOP&&transmit&&outgoing.type==WIRE_COMMAND&&outgoing.payload[0]!=0){
            bridge_request(&bridge,robot.revision,0,0,robot.ack,now);transmit=bridge_tick(&bridge,now,&outgoing);
        }
        robot_status(&robot, now, data); conn=connection; notify=subscribed;
        xSemaphoreGive(lock);
        if(transmit&&!uart_link_send(&outgoing))bridge_tx_failed(&bridge);
        if(notify && conn!=BLE_HS_CONN_HANDLE_NONE&&(uint32_t)(now-last_notify)>=100) {
            last_notify=now;
            struct os_mbuf *om=ble_hs_mbuf_from_flat(data, sizeof(data));
            if(om) { int rc=ble_gatts_notify_custom(conn, status_handle, om);
                if(rc && rc!=BLE_HS_ENOTCONN) ESP_LOGD("robot", "Notify rc=%d", rc); }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
void app_main(void) {
    esp_err_t ret=nvs_flash_init();
    if(ret==ESP_ERR_NVS_NO_FREE_PAGES || ret==ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase()); ret=nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    lock=xSemaphoreCreateMutex(); assert(lock); robot_init(&robot); battery_init();
    uart_available=uart_link_init();
#ifdef CONFIG_ROBOT_UART_ENABLED
    robot.uart_configured=true;
#endif
    bridge_init(&bridge,uart_available,esp_random());
    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb=sync_host; ble_hs_cfg.reset_cb=reset_host;
    ble_svc_gap_init(); ble_svc_gatt_init();
    ESP_ERROR_CHECK(ble_gatts_count_cfg(services)); ESP_ERROR_CHECK(ble_gatts_add_svcs(services));
    ESP_ERROR_CHECK(ble_svc_gap_device_name_set("ESP32-Robot"));
    nimble_port_freertos_init(host_task);
    if(xTaskCreate(telemetry_task, "telemetry", 4096, NULL, 5, NULL)!=pdPASS) abort();
    ESP_LOGI("robot", "Ready: BLE protocol v2; UART GPIOs and motor hooks require hardware configuration");
}
