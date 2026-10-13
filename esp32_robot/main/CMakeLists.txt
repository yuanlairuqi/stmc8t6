idf_component_register(SRCS "main.c" "robot_control.c" "battery.c" "robot_bridge.c" "uart_link.c" "../shared/robot_wire.c"
    INCLUDE_DIRS "." "../shared" REQUIRES bt nvs_flash esp_timer esp_adc esp_driver_uart esp_driver_gpio esp_hw_support)
