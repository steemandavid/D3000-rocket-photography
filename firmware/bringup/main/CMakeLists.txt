idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_driver_rmt esp_adc esp_driver_usb_serial_jtag esp_psram spi_flash esp_timer)
