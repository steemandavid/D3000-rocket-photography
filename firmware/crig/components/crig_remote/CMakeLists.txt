idf_component_register(SRCS "remote.c" "oled.c"
                       REQUIRES crig_common esp_adc esp_driver_gpio esp_driver_i2c esp_timer)
