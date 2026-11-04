idf_component_register(SRCS "crig_proto.c" "crig_armdec.c" "crig_link.c" "crig_ws2812.c" "crig_console.c" "crig_buzzer.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_wifi nvs_flash esp_event esp_driver_rmt esp_driver_uart esp_driver_gpio esp_timer)
