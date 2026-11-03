idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_driver_rmt esp_driver_i2c esp_driver_uart esp_adc esp_psram spi_flash esp_timer)
