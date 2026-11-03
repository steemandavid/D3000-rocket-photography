/*
 * CRIG node bring-up test (ESP32-S3-DevKitC-1, N16R8)
 *
 * Safe by default: the three optocoupler outputs (HALF, FULL, PWR) stay LOW
 * (camera untouched) until you send 'a' (arm for 30 s) on the serial console.
 *
 * Pins (see crig_node_schematic.svg):
 *   IO1  VBAT_SENSE (ADC1_CH0, divider 100k / 47k)
 *   IO4  FULL_DRV   -> U3 PC817 -> camera FULL (white; J3 pin 1, orange)
 *   IO5  HALF_DRV   -> U4 PC817 -> camera HALF (yellow; J3 pin 2, red)
 *   (as built, bench-verified 2026-10-15; spec <= v0.12 had HALF/FULL the other way round)
 *   IO6  PWR_DRV    -> U5 PC817 -> camera ON/OFF (shorting it switches the camera OFF)
 *   IO15 buzzer (active, direct drive)
 *   IO48 (board v1.0) or IO38 (v1.1) on-board WS2812
 */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"



#define PIN_VBAT_SENSE 1
#define PIN_HALF       5
#define PIN_FULL       4
#define PIN_PWR        6
#define PIN_BUZZER     15
#define LED_PIN_A      48
#define LED_PIN_B      38

#define VBAT_DIV_NUM   147   /* (100k + 47k) */
#define VBAT_DIV_DEN   47
#define ARM_WINDOW_MS  30000
#define T_HALF_MS      1000   /* 'h' pulse */
#define T_FULL_HALF_MS 1000   /* 'f': HALF alone before FULL */
#define T_FULL_MS      500    /* 'f': HALF+FULL together */
#define T_PWR_MS       3000   /* 'g' */

static rmt_channel_handle_t s_rmt;
static rmt_encoder_handle_t s_enc;
static int s_led_pin = LED_PIN_A;
static int64_t s_armed_until_us;
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static adc_channel_t s_chan;

static void led_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t px[3] = { g, r, b }; /* WS2812: GRB order */
    rmt_transmit_config_t t = { .loop_count = 0 };
    ESP_ERROR_CHECK(rmt_transmit(s_rmt, s_enc, px, sizeof(px), &t));
    rmt_tx_wait_all_done(s_rmt, 100);
}

static void led_open(int pin)
{
    if (s_rmt) {
        led_rgb(0, 0, 0);
        rmt_disable(s_rmt);
        rmt_del_channel(s_rmt);
        rmt_del_encoder(s_enc);
        s_rmt = NULL;
    }
    rmt_tx_channel_config_t c = {
        .gpio_num = pin,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,   /* 0.1 us per tick */
        .mem_block_symbols = 64,
        .trans_queue_depth = 2,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&c, &s_rmt));
    rmt_bytes_encoder_config_t e = {
        .bit0 = { .level0 = 1, .duration0 = 3, .level1 = 0, .duration1 = 9 },  /* 0.3 us / 0.9 us */
        .bit1 = { .level0 = 1, .duration0 = 9, .level1 = 0, .duration1 = 3 },  /* 0.9 us / 0.3 us */
        .flags.msb_first = 1,
    };
    ESP_ERROR_CHECK(rmt_new_bytes_encoder(&e, &s_enc));
    ESP_ERROR_CHECK(rmt_enable(s_rmt));
    s_led_pin = pin;
}

static void beep(int ms)
{
    gpio_set_level(PIN_BUZZER, 1);
    vTaskDelay(pdMS_TO_TICKS(ms));
    gpio_set_level(PIN_BUZZER, 0);
}

static void outputs_idle(void)
{
    gpio_set_level(PIN_HALF, 0);
    gpio_set_level(PIN_FULL, 0);
    gpio_set_level(PIN_PWR, 0);
}

static bool s_hold;

static bool armed(void)
{
    return esp_timer_get_time() < s_armed_until_us;
}

static void led_selftest(int pin)
{
    printf("LED test on GPIO%d: RED, GREEN, BLUE (watch the on-board LED)\n", pin);
    led_open(pin);
    led_rgb(32, 0, 0);  vTaskDelay(pdMS_TO_TICKS(700));
    led_rgb(0, 32, 0);  vTaskDelay(pdMS_TO_TICKS(700));
    led_rgb(0, 0, 32);  vTaskDelay(pdMS_TO_TICKS(700));
    led_rgb(0, 0, 0);
}

static void adc_init(void)
{
    adc_unit_t unit;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(PIN_VBAT_SENSE, &unit, &s_chan));
    adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = unit };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&ucfg, &s_adc));
    adc_oneshot_chan_cfg_t ccfg = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, s_chan, &ccfg));
    adc_cali_curve_fitting_config_t cal = { .unit_id = unit, .chan = s_chan,
                                            .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    if (adc_cali_create_scheme_curve_fitting(&cal, &s_cali) != ESP_OK) {
        s_cali = NULL;
        printf("ADC calibration not available, using raw estimate\n");
    }
}

static void adc_report(void)
{
    int raw = 0, mv = 0, sum = 0;
    for (int i = 0; i < 16; i++) {
        adc_oneshot_read(s_adc, s_chan, &raw);
        sum += raw;
    }
    raw = sum / 16;
    if (s_cali) adc_cali_raw_to_voltage(s_cali, raw, &mv);
    else mv = raw * 3100 / 4095;
    printf("IO1 raw=%d  %d mV  -> pack ~ %.2f V (valid only with the 100k/47k divider fitted)\n",
           raw, mv, (mv * VBAT_DIV_NUM / (float)VBAT_DIV_DEN) / 1000.0f);
}

static void help(void)
{
    printf("\nCRIG bring-up commands:\n"
           "  ?  help\n"
           "  i  chip info\n"
           "  v  read IO1 (VBAT_SENSE)\n"
           "  b  beep (IO15)\n"
           "  l  toggle on-board LED pin (GPIO48 <-> GPIO38) and rerun the colour test\n"
           "  a  ARM the optocoupler outputs for %d s (REQUIRED for h/f/g)\n"
           "  d  disarm now\n"
           "  h  HALF  (IO5) %d ms          [needs a]\n"
           "  f  FULL  (IO5 = HALF, then IO4 = FULL added) %d + %d ms = one frame  [needs a]\n"
           "  g  PWR   (IO6) %d ms = switches the camera OFF while held  [needs a]\n"
           "  H  HOLD HALF (IO5) until d or the arm window ends  [needs a]\n"
           "  F  HOLD HALF+FULL (IO5+IO4) until d or the arm window ends  [needs a]\n"
           "  s  SWEEP FULL duration 10..800 ms (HALF lead 1000 ms), 1 attempt / 3 s  [needs a]\n"
           "  S  SWEEP HALF lead 0..800 ms (FULL 500 ms), 1 attempt / 3 s  [needs a]\n"
           "Outputs are LOW (camera untouched) at boot and after every action.\n\n",
           ARM_WINDOW_MS / 1000, T_HALF_MS, T_FULL_HALF_MS, T_FULL_MS, T_PWR_MS);
}

static void info(void)
{
    esp_chip_info_t ci;
    esp_chip_info(&ci);
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    uint32_t flash = 0;
    esp_flash_get_size(NULL, &flash);
    printf("chip: ESP32-S3 rev v%d.%d, %d cores, flash %" PRIu32 " MB, PSRAM %u MB\n",
           ci.revision / 100, ci.revision % 100, ci.cores, flash / (1024 * 1024),
           (unsigned)(esp_psram_get_size() / (1024 * 1024)));
    printf("MAC (WiFi STA): %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void pulse(int pin, int ms, const char *name)
{
    if (!armed()) { printf("%s refused: not armed (send 'a' first)\n", name); return; }
    printf("%s: %d ms\n", name, ms);
    gpio_set_level(pin, 1);
    vTaskDelay(pdMS_TO_TICKS(ms));
    gpio_set_level(pin, 0);
}

static void do_full(void)
{
    if (!armed()) { printf("FULL refused: not armed (send 'a' first)\n"); return; }
    printf("FULL: HALF %d ms, then HALF+FULL %d ms\n", T_FULL_HALF_MS, T_FULL_MS);
    gpio_set_level(PIN_HALF, 1);
    vTaskDelay(pdMS_TO_TICKS(T_FULL_HALF_MS));
    gpio_set_level(PIN_FULL, 1);
    vTaskDelay(pdMS_TO_TICKS(T_FULL_MS));
    outputs_idle();
}

/* Timing sweep: one attempt = HALF, after lead_ms FULL as well, FULL kept for full_ms, then both off. */
static bool aborted(void)
{
    uint8_t c;
    if (usb_serial_jtag_read_bytes(&c, 1, 0) == 1 && c == 'd') {
        s_armed_until_us = 0; s_hold = false; outputs_idle(); printf("sweep aborted, disarmed\n");
        return true;
    }
    return false;
}

static void sweep(bool vary_full)
{
    static const int vals[] = { 0, 10, 20, 30, 50, 80, 120, 200, 400, 800 };
    const int n = sizeof(vals) / sizeof(vals[0]);
    /* vary_full: FULL duration varies, HALF lead fixed 1000 ms. else: HALF lead varies (0 = together), FULL fixed 500 ms */
    if (!armed()) { printf("sweep refused: not armed (send 'a' first)\n"); return; }
    s_armed_until_us = esp_timer_get_time() + 120LL * 1000 * 1000;   /* sweep needs longer than 30 s */
    printf("SWEEP %s: one attempt every 3 s, 'd' aborts. Watch/listen: note which attempts take a picture.\n",
           vary_full ? "FULL duration (HALF lead 1000 ms)" : "HALF lead (FULL 500 ms)");
    int start = vary_full ? 1 : 0;   /* a FULL duration of 0 is meaningless */
    for (int i = start; i < n; i++) {
        int lead = vary_full ? 1000 : vals[i];
        int full = vary_full ? vals[i] : 500;
        printf("attempt %d: HALF lead %d ms, FULL %d ms\n", i - start + 1, lead, full);
        gpio_set_level(PIN_HALF, 1);
        if (lead > 0) vTaskDelay(pdMS_TO_TICKS(lead));
        gpio_set_level(PIN_FULL, 1);
        vTaskDelay(pdMS_TO_TICKS(full));
        outputs_idle();
        for (int w = 0; w < 30; w++) { vTaskDelay(pdMS_TO_TICKS(100)); if (aborted()) return; }
    }
    s_armed_until_us = 0;
    printf("sweep done, disarmed\n");
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
    /* Outputs first, LOW: camera must never see a glitch. */
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_HALF) | (1ULL << PIN_FULL) | (1ULL << PIN_PWR) | (1ULL << PIN_BUZZER),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&out);
    outputs_idle();
    gpio_set_level(PIN_BUZZER, 0);

    usb_serial_jtag_driver_config_t ucfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_serial_jtag_driver_install(&ucfg);
    vTaskDelay(pdMS_TO_TICKS(1500));

    printf("\n=== CRIG node bring-up test ===\n");
    info();
    adc_init();
    adc_report();
    beep(120); vTaskDelay(pdMS_TO_TICKS(120)); beep(120);
    led_selftest(LED_PIN_A);
    led_selftest(LED_PIN_B);
    led_open(LED_PIN_A);
    help();

    int64_t last_report = 0;
    bool blink = false;
    while (1) {
        uint8_t c;
        if (read_cmd(&c)) {
            switch (c) {
            case '?': help(); break;
            case 'i': info(); break;
            case 'v': adc_report(); break;
            case 'b': beep(150); break;
            case 'l': led_selftest(s_led_pin == LED_PIN_A ? LED_PIN_B : LED_PIN_A); break;
            case 'a': s_armed_until_us = esp_timer_get_time() + (int64_t)ARM_WINDOW_MS * 1000;
                      printf("ARMED for %d s\n", ARM_WINDOW_MS / 1000); break;
            case 'd': s_armed_until_us = 0; s_hold = false; outputs_idle(); printf("disarmed\n"); break;
            case 'h': pulse(PIN_HALF, T_HALF_MS, "HALF"); break;
            case 'f': do_full(); break;
            case 'H': if (!armed()) printf("refused: not armed\n"); else { gpio_set_level(PIN_HALF, 1); s_hold = true; printf("HOLD HALF (d to release)\n"); } break;
            case 'F': if (!armed()) printf("refused: not armed\n"); else { gpio_set_level(PIN_HALF, 1); gpio_set_level(PIN_FULL, 1); s_hold = true; printf("HOLD HALF+FULL (d to release)\n"); } break;
            case 's': sweep(true); break;
            case 'S': sweep(false); break;
            case 'g': pulse(PIN_PWR, T_PWR_MS, "PWR"); break;
            case '\r': case '\n': break;
            default: printf("unknown '%c' (? for help)\n", c); break;
            }
        }
        if (s_hold && !armed()) { outputs_idle(); s_hold = false; printf("hold ended (arm window over)\n"); }
        /* heartbeat: dim blue when idle, dim orange while armed */
        int64_t now = esp_timer_get_time();
        if (now - last_report > 500000) {
            last_report = now;
            blink = !blink;
            if (armed()) led_rgb(blink ? 24 : 0, blink ? 8 : 0, 0);
            else led_rgb(0, 0, blink ? 8 : 0);
        }
    }
}
