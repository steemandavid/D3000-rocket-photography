/*
 * CRIG optocoupler manual test (ESP32-S3-DevKitC-1)
 *
 * No arming. A key turns one optocoupler ON, Enter (or 'x') turns ALL of them OFF:
 *   h  IO5  U4 -> HALF line   (camera yellow; connector J3 pin 2, red)
 *   f  IO4  U3 -> FULL line   (camera white;  connector J3 pin 1, orange)
 *   p  IO6  U5 -> PWR line    (camera green;  connector J3 pin 3, brown; ON = camera switches OFF)
 * As built, bench-verified 2026-10-15 (spec <= v0.12 had HALF/FULL swapped). A picture needs h AND f.
 * Several can be on at once. Safety: everything switches off after AUTO_OFF_MS without a key.
 * Outputs are LOW at boot.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"

#define PIN_HALF    5
#define PIN_FULL    4
#define PIN_PWR     6
#define AUTO_OFF_MS 30000

static void all_off(void)
{
    gpio_set_level(PIN_HALF, 0);
    gpio_set_level(PIN_FULL, 0);
    gpio_set_level(PIN_PWR, 0);
}

static void state(void)
{
    printf("state: h/HALF(IO5)=%d f/FULL(IO4)=%d p/PWR(IO6)=%d\n",
           gpio_get_level(PIN_HALF), gpio_get_level(PIN_FULL), gpio_get_level(PIN_PWR));
}

/* Reject input bursts: a human (or term.py) sends one character at a time. Several characters in a row are
 * almost certainly the console log echoed back by a terminal with echo on, which would otherwise be executed
 * as commands (it once armed the outputs and started a sweep). Burst input is drained and ignored. */
static bool read_cmd(uint8_t *c)
{
    if (usb_serial_jtag_read_bytes(c, 1, pdMS_TO_TICKS(100)) != 1) return false;
    uint8_t d;
    if (usb_serial_jtag_read_bytes(&d, 1, pdMS_TO_TICKS(20)) == 1) {
        while (usb_serial_jtag_read_bytes(&d, 1, pdMS_TO_TICKS(50)) == 1) {}
        printf("input burst ignored\n");
        return false;
    }
    return true;
}

void app_main(void)
{
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_HALF) | (1ULL << PIN_FULL) | (1ULL << PIN_PWR),
        .mode = GPIO_MODE_INPUT_OUTPUT,   /* input too, so gpio_get_level() reads the pin back */
    };
    gpio_config(&out);
    all_off();

    usb_serial_jtag_driver_config_t ucfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_serial_jtag_driver_install(&ucfg);
    vTaskDelay(pdMS_TO_TICKS(1000));
    printf("\n=== CRIG optocoupler test: h=HALF(IO5) f=FULL(IO4) p=PWR(IO6) ON, Enter=all OFF, auto-off %d s ===\n",
           AUTO_OFF_MS / 1000);
    state();

    int64_t last_key = 0;
    while (1) {
        uint8_t c;
        if (read_cmd(&c)) {
            last_key = esp_timer_get_time();
            switch (c) {
            case 'h': gpio_set_level(PIN_HALF, 1); state(); break;
            case 'f': gpio_set_level(PIN_FULL, 1); state(); break;
            case 'p': gpio_set_level(PIN_PWR, 1);  state(); break;
            case '\r': case '\n': case 'x': all_off(); printf("all OFF\n"); state(); break;
            default: break;
            }
        }
        if ((gpio_get_level(PIN_HALF) || gpio_get_level(PIN_FULL) || gpio_get_level(PIN_PWR)) &&
            esp_timer_get_time() - last_key > (int64_t)AUTO_OFF_MS * 1000) {
            all_off();
            printf("auto-off after %d s\n", AUTO_OFF_MS / 1000);
            state();
        }
    }
}
