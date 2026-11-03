/*
 * CRIG remote hardware test (ESP32-S3-DevKitC-1U, N16R8), spec v0.15 / crig_remote_schematic v0.1.
 *
 * Console: UART0 over the board's USB-UART bridge port, 115200 8N1.
 *
 * Pins as wired (owner, 2026-11-03; wire colours Dutch):
 *   IO1  VBAT_ADC   (100k/100k)            geel/blauw
 *   IO4  SHOOT_BTN  (to GND, int. pull-up)  groen/zwart  (same colours as IO10!)
 *   IO5  TRIG_IN    (PC817 collector)       grijs/wit    (J1 not connected yet)
 *   IO8  I2C_SDA    OLED                    blauw/wit
 *   IO9  I2C_SCL    OLED                    wit/blauw
 *   IO10 RING_GRN                           groen/zwart
 *   IO11 RING_RED                           blauw/paars
 *   IO12 VIB        (Q1 base via 1k)        wit/grijs
 *   IO15 BUZZER     (active, direct)        geel/oranje
 *   IO48 on-board WS2812 (fixed, as in the RLC: rlc_common/include/pin_config.h PIN_RGB_LED 48)
 *   IO6  RLC_DET    not connected yet: not touched.
 *
 * At boot an automatic sequence exercises every output once, then a live loop runs:
 * SHOOT held -> ring red + vibration + RGB red; released -> ring green. Events are logged.
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/i2c_master.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"

#define PIN_VBAT     1
#define PIN_SHOOT    4
#define PIN_TRIG     5
#define PIN_SDA      8
#define PIN_SCL      9
#define PIN_RING_G   10
#define PIN_RING_R   11
#define PIN_VIB      12
#define PIN_BUZZER   15
#define PIN_RGB      48
#define OLED_ADDR    0x3C

/* ------------------------------------------------------------------ console */
static int con_getc(int timeout_ms)
{
    uint8_t c;
    return uart_read_bytes(UART_NUM_0, &c, 1, pdMS_TO_TICKS(timeout_ms)) == 1 ? c : -1;
}

/* ------------------------------------------------------------------ WS2812 */
static rmt_channel_handle_t s_rmt;
static rmt_encoder_handle_t s_enc;

static void led_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t px[3] = { g, r, b };
    rmt_transmit_config_t t = { .loop_count = 0 };
    rmt_transmit(s_rmt, s_enc, px, sizeof(px), &t);
    rmt_tx_wait_all_done(s_rmt, 100);
}

static void led_open(int pin)
{
    if (s_rmt) return;
    rmt_tx_channel_config_t c = {
        .gpio_num = pin, .clk_src = RMT_CLK_SRC_DEFAULT, .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64, .trans_queue_depth = 2,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&c, &s_rmt));
    rmt_bytes_encoder_config_t e = {
        .bit0 = { .level0 = 1, .duration0 = 3, .level1 = 0, .duration1 = 9 },
        .bit1 = { .level0 = 1, .duration0 = 9, .level1 = 0, .duration1 = 3 },
        .flags.msb_first = 1,
    };
    ESP_ERROR_CHECK(rmt_new_bytes_encoder(&e, &s_enc));
    ESP_ERROR_CHECK(rmt_enable(s_rmt));
}

static void led_selftest(int pin)
{
    printf("[RGB] GPIO%d: RED, GREEN, BLUE, 0.7 s each\n", pin);
    led_open(pin);
    led_rgb(32, 0, 0); vTaskDelay(pdMS_TO_TICKS(700));
    led_rgb(0, 32, 0); vTaskDelay(pdMS_TO_TICKS(700));
    led_rgb(0, 0, 32); vTaskDelay(pdMS_TO_TICKS(700));
    led_rgb(0, 0, 0);
}

/* ------------------------------------------------------------------ ADC */
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static adc_channel_t s_chan;

static void adc_init(void)
{
    adc_unit_t unit;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(PIN_VBAT, &unit, &s_chan));
    adc_oneshot_unit_init_cfg_t u = { .unit_id = unit };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&u, &s_adc));
    adc_oneshot_chan_cfg_t cc = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, s_chan, &cc));
    adc_cali_curve_fitting_config_t cal = { .unit_id = unit, .chan = s_chan,
                                            .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    if (adc_cali_create_scheme_curve_fitting(&cal, &s_cali) != ESP_OK) s_cali = NULL;
}

static int vbat_mv(int *pin_mv)
{
    int raw, sum = 0, mv = 0;
    for (int i = 0; i < 16; i++) { adc_oneshot_read(s_adc, s_chan, &raw); sum += raw; }
    raw = sum / 16;
    if (s_cali) adc_cali_raw_to_voltage(s_cali, raw, &mv); else mv = raw * 3100 / 4095;
    if (pin_mv) *pin_mv = mv;
    return mv * 2;   /* 100k / 100k */
}

static void vbat_report(void)
{
    int pin;
    int v = vbat_mv(&pin);
    printf("[VBAT] IO1 %d mV -> battery %.2f V%s\n", pin, v / 1000.0f,
           v < 500 ? "  (SW1 off or no cell: divider is after the switch)" : "");
}

/* ------------------------------------------------------------------ OLED (SSD1306, I2C) */
static i2c_master_bus_handle_t s_bus;
static int s_sda = PIN_SDA, s_scl = PIN_SCL;
static i2c_master_dev_handle_t s_oled;
static bool s_oled_ok;
static uint8_t s_fb[128 * 8];

/* Read SDA/SCL as plain GPIO with internal pull-up and then pull-down, before the I2C driver owns them.
 * Idle I2C lines must read 1 with pull-up (module pull-ups to its VCC). 0 with pull-up = held low:
 * short to GND, or the module unpowered (its pull-ups / ESD diodes then sink the line). */
static void i2c_lines_report(void)
{
    int up[2], dn[2], pins[2] = { PIN_SDA, PIN_SCL };
    for (int i = 0; i < 2; i++) {
        gpio_reset_pin(pins[i]);
        gpio_set_direction(pins[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode(pins[i], GPIO_PULLUP_ONLY);   vTaskDelay(pdMS_TO_TICKS(5)); up[i] = gpio_get_level(pins[i]);
        gpio_set_pull_mode(pins[i], GPIO_PULLDOWN_ONLY); vTaskDelay(pdMS_TO_TICKS(5)); dn[i] = gpio_get_level(pins[i]);
        gpio_set_pull_mode(pins[i], GPIO_FLOATING);
    }
    for (int i = 0; i < 2; i++)
        printf("[I2C] %s IO%d: int. pull-up -> %d, int. pull-down -> %d  => %s\n", i ? "SCL" : "SDA", pins[i], up[i], dn[i],
               (up[i] && dn[i]) ? "driven high by module pull-up (good)" :
               (!up[i] && !dn[i]) ? "HELD LOW (short to GND or module unpowered)" :
               (up[i] && !dn[i]) ? "floating: no module pull-up seen (open wire / module unpowered?)" : "odd");
}


/* Software I2C probe (open-drain GPIO, ~20 kHz): unambiguous ACK / NACK, independent of the I2C driver. */
static int bb_sda, bb_scl;
static void bb_d(int v) { gpio_set_level(bb_sda, v); esp_rom_delay_us(25); }
static void bb_c(int v)
{
    gpio_set_level(bb_scl, v); esp_rom_delay_us(25);
    if (v) { int t = 0; while (!gpio_get_level(bb_scl) && t++ < 1000) esp_rom_delay_us(1); }
}
static int bb_probe(int addr)
{
    bb_d(1); bb_c(1); bb_d(0); bb_c(0);                     /* START */
    uint8_t b = addr << 1;
    for (int i = 7; i >= 0; i--) { bb_d((b >> i) & 1); bb_c(1); bb_c(0); }
    bb_d(1); bb_c(1);
    int ack = !gpio_get_level(bb_sda);
    bb_c(0); bb_d(0); bb_c(1); bb_d(1);                     /* STOP */
    return ack;
}
static void bb_scan(int sda, int scl)
{
    bb_sda = sda; bb_scl = scl;
    int pins[2] = { sda, scl };
    for (int i = 0; i < 2; i++) {
        gpio_reset_pin(pins[i]);
        gpio_set_direction(pins[i], GPIO_MODE_INPUT_OUTPUT_OD);
        gpio_set_pull_mode(pins[i], GPIO_PULLUP_ONLY);
        gpio_set_level(pins[i], 1);
    }
    esp_rom_delay_us(100);
    int n = 0;
    printf("[I2C-BB] software scan SDA=IO%d SCL=IO%d:", sda, scl);
    for (int a = 0x08; a < 0x78; a++) if (bb_probe(a)) { printf(" 0x%02X", a); n++; }
    printf("%s\n", n == 0 ? " no ACK" : (n > 100 ? "  (ACK everywhere = SDA stuck low)" : ""));
    for (int i = 0; i < 2; i++) gpio_reset_pin(pins[i]);
}

static void i2c_init(void)
{
    i2c_master_bus_config_t bc = {
        .i2c_port = I2C_NUM_0, .sda_io_num = s_sda, .scl_io_num = s_scl,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bc, &s_bus));
}

static int i2c_scan(void)
{
    int found = 0, n = 0;
    printf("[I2C] scan SDA=IO%d SCL=IO%d:", s_sda, s_scl);
    for (int a = 0x08; a < 0x78; a++) {
        esp_err_t r = i2c_master_probe(s_bus, a, 20);
        if (r == ESP_ERR_TIMEOUT && a == 0x08) printf(" (probe TIMEOUT, not NACK)");
        if (r == ESP_OK) { printf(" 0x%02X", a); n++; if (a == OLED_ADDR) found = 1; }
    }
    printf("%s\n", n ? "" : " nothing found (check wiring / SDA-SCL swap / VCC)");
    return found;
}

static esp_err_t oled_cmds(const uint8_t *c, int n)
{
    uint8_t buf[32];
    buf[0] = 0x00;
    memcpy(buf + 1, c, n);
    return i2c_master_transmit(s_oled, buf, n + 1, 100);
}

static void oled_flush(void)
{
    if (!s_oled_ok) return;
    static const uint8_t win[] = { 0x21, 0, 127, 0x22, 0, 7 };
    oled_cmds(win, sizeof(win));
    static uint8_t buf[1 + 128];
    for (int p = 0; p < 8; p++) {
        buf[0] = 0x40;
        memcpy(buf + 1, s_fb + p * 128, 128);
        i2c_master_transmit(s_oled, buf, sizeof(buf), 100);
    }
}

static bool oled_init(void)
{
    i2c_device_config_t dc = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = OLED_ADDR, .scl_speed_hz = 400000 };
    if (i2c_master_bus_add_device(s_bus, &dc, &s_oled) != ESP_OK) return false;
    static const uint8_t init[] = { 0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x00,
                                    0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF };
    s_oled_ok = oled_cmds(init, sizeof(init)) == ESP_OK;
    return s_oled_ok;
}

/* 5x7 font, only the characters used here (lowercase is shown as uppercase) */
static const char FONT_CH[] = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ:.-%=/";
static const uint8_t FONT[][5] = {
    {0,0,0,0,0},
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},
    {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
    {0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},
    {0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},
    {0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},
    {0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},
    {0x00,0x36,0x36,0x00,0x00},{0x00,0x60,0x60,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x23,0x13,0x08,0x64,0x62},
    {0x14,0x14,0x14,0x14,0x14},{0x20,0x10,0x08,0x04,0x02},
};

static void oled_text(int col, int page, const char *s)
{
    for (; *s && col <= 122; s++, col += 6) {
        const char *p = strchr(FONT_CH, toupper((unsigned char)*s));
        int idx = p ? (int)(p - FONT_CH) : 0;
        memcpy(s_fb + page * 128 + col, FONT[idx], 5);
        s_fb[page * 128 + col + 5] = 0;
    }
}

static void oled_pattern(void)
{
    printf("[OLED] full white 0.8 s, then checkerboard 0.8 s, then text\n");
    memset(s_fb, 0xFF, sizeof(s_fb)); oled_flush(); vTaskDelay(pdMS_TO_TICKS(800));
    for (int i = 0; i < (int)sizeof(s_fb); i++) s_fb[i] = ((i / 8) & 1) ? 0xF0 : 0x0F;
    oled_flush(); vTaskDelay(pdMS_TO_TICKS(800));
    memset(s_fb, 0, sizeof(s_fb));
    oled_text(0, 0, "CRIG REMOTE HW TEST");
    oled_text(0, 2, "0123456789 :.-%=/");
    oled_text(0, 3, "ABCDEFGHIJKLMNOPQRSTU");
    oled_text(0, 4, "VWXYZ");
    oled_flush();
}

/* ------------------------------------------------------------------ outputs */
static void beep(int ms) { gpio_set_level(PIN_BUZZER, 1); vTaskDelay(pdMS_TO_TICKS(ms)); gpio_set_level(PIN_BUZZER, 0); }
static void ring(int g, int r) { gpio_set_level(PIN_RING_G, g); gpio_set_level(PIN_RING_R, r); }

static void info(void)
{
    esp_chip_info_t ci;
    esp_chip_info(&ci);
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    uint32_t flash = 0;
    esp_flash_get_size(NULL, &flash);
    printf("[INFO] ESP32-S3 rev v%d.%d, flash %" PRIu32 " MB, PSRAM %u MB, MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
           ci.revision / 100, ci.revision % 100, flash >> 20, (unsigned)(esp_psram_get_size() >> 20),
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void inputs_report(void)
{
    printf("[IN] SHOOT(IO4)=%s  TRIG(IO5)=%s\n",
           gpio_get_level(PIN_SHOOT) ? "released(1)" : "PRESSED(0)",
           gpio_get_level(PIN_TRIG) ? "inactive(1)" : "ACTIVE(0)");
}

static void autotest(void)
{
    printf("\n--- automatic output sequence ---\n");
    printf("[BUZZER] IO15: 2 short beeps\n");
    beep(120); vTaskDelay(pdMS_TO_TICKS(150)); beep(120);
    vTaskDelay(pdMS_TO_TICKS(500));
    printf("[VIB] IO12: motor on 0.6 s\n");
    gpio_set_level(PIN_VIB, 1); vTaskDelay(pdMS_TO_TICKS(600)); gpio_set_level(PIN_VIB, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
    printf("[RING] IO10 green 1.5 s\n");
    ring(1, 0); vTaskDelay(pdMS_TO_TICKS(1500));
    printf("[RING] IO11 red 1.5 s\n");
    ring(0, 1); vTaskDelay(pdMS_TO_TICKS(1500));
    ring(0, 0);
    led_selftest(PIN_RGB);
    if (s_oled_ok) oled_pattern(); else printf("[OLED] skipped: not found at 0x3C\n");
    printf("--- sequence done ---\n");
}

static void help(void)
{
    printf("\nCommands (one letter): ? help | i info | v battery | n inputs | s I2C scan | p I2C line levels + rescan | o OLED pattern\n"
           "  b beep | m motor 0.6 s | g ring green 1.5 s | r ring red 1.5 s | l RGB LED test (IO48)\n"
           "  A rerun the automatic sequence\n"
           "Live: hold SHOOT -> ring red + motor + RGB red; released -> ring green. Events are logged.\n\n");
}

static void live_screen(int shoot, int trig, int vb, int presses)
{
    if (!s_oled_ok) return;
    char l[24];
    memset(s_fb, 0, sizeof(s_fb));
    oled_text(0, 0, "CRIG REMOTE HW TEST");
    snprintf(l, sizeof l, "SHOOT: %s", shoot ? "PRESSED" : "-");      oled_text(0, 2, l);
    snprintf(l, sizeof l, "TRIG:  %s", trig ? "ACTIVE" : "-");        oled_text(0, 3, l);
    snprintf(l, sizeof l, "VBAT:  %d.%02d V", vb / 1000, (vb % 1000) / 10); oled_text(0, 4, l);
    snprintf(l, sizeof l, "PRESSES: %d", presses);                    oled_text(0, 6, l);
    oled_flush();
}

void app_main(void)
{
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_RING_G) | (1ULL << PIN_RING_R) | (1ULL << PIN_VIB) | (1ULL << PIN_BUZZER),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&out);
    ring(0, 0); gpio_set_level(PIN_VIB, 0); gpio_set_level(PIN_BUZZER, 0);
    gpio_config_t in = {
        .pin_bit_mask = (1ULL << PIN_SHOOT) | (1ULL << PIN_TRIG),
        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&in);

    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(300));

    printf("\n=== CRIG remote hardware test v1 ===\n");
    info();
    adc_init();
    vbat_report();
    inputs_report();
    i2c_lines_report();
    bb_scan(PIN_SDA, PIN_SCL);
    bb_scan(PIN_SCL, PIN_SDA);
    esp_log_level_set("i2c.master", ESP_LOG_NONE);
    i2c_init();
    if (!i2c_scan()) {
        printf("[I2C] retry with SDA and SCL exchanged (wire swap test)\n");
        i2c_del_master_bus(s_bus);
        s_sda = PIN_SCL; s_scl = PIN_SDA;
        i2c_init();
        if (i2c_scan()) printf("[I2C] *** OLED answers with SDA=IO9 / SCL=IO8: the two wires are SWAPPED ***\n");
        else { i2c_del_master_bus(s_bus); s_sda = PIN_SDA; s_scl = PIN_SCL; i2c_init(); }
    }
    if (i2c_master_probe(s_bus, OLED_ADDR, 20) == ESP_OK) printf("[OLED] init %s\n", oled_init() ? "OK" : "FAILED");
    autotest();
    help();

    int last_shoot = -1, last_trig = -1, presses = 0;
    int64_t last_screen = 0, pressed_at = 0;
    while (1) {
        int c = con_getc(20);
        switch (c) {
        case '?': help(); break;
        case 'i': info(); break;
        case 'v': vbat_report(); break;
        case 'n': inputs_report(); break;
        case 's': i2c_scan(); break;
        case 'p':
            if (s_oled) { i2c_master_bus_rm_device(s_oled); s_oled = NULL; }
            i2c_del_master_bus(s_bus); s_oled_ok = false;
            i2c_lines_report(); i2c_init();
            if (i2c_scan()) printf("[OLED] init %s\n", oled_init() ? "OK" : "FAILED");
            break;
        case 'o': if (s_oled_ok) oled_pattern(); else printf("[OLED] not available\n"); break;
        case 'b': printf("[BUZZER] beep\n"); beep(150); break;
        case 'm': printf("[VIB] 0.6 s\n"); gpio_set_level(PIN_VIB, 1); vTaskDelay(pdMS_TO_TICKS(600)); gpio_set_level(PIN_VIB, 0); break;
        case 'g': printf("[RING] green 1.5 s\n"); ring(1, 0); vTaskDelay(pdMS_TO_TICKS(1500)); break;
        case 'r': printf("[RING] red 1.5 s\n"); ring(0, 1); vTaskDelay(pdMS_TO_TICKS(1500)); break;
        case 'l': led_selftest(PIN_RGB); break;
        case 'A': autotest(); break;
        default: break;
        }

        int shoot = !gpio_get_level(PIN_SHOOT);
        int trig = !gpio_get_level(PIN_TRIG);
        int64_t now = esp_timer_get_time();
        if (shoot != last_shoot) {
            if (last_shoot != -1) {
                if (shoot) { presses++; pressed_at = now; printf("[EVT] SHOOT pressed (#%d)\n", presses); }
                else printf("[EVT] SHOOT released after %lld ms\n", (now - pressed_at) / 1000);
            }
            last_shoot = shoot;
            ring(!shoot, shoot);
            gpio_set_level(PIN_VIB, shoot);
            led_rgb(shoot ? 32 : 0, shoot ? 0 : 6, 0);
            last_screen = 0;
        }
        if (trig != last_trig) {
            if (last_trig != -1) printf("[EVT] TRIG %s\n", trig ? "ACTIVE" : "inactive");
            last_trig = trig;
            last_screen = 0;
        }
        if (now - last_screen > 1000000) {
            last_screen = now;
            live_screen(shoot, trig, vbat_mv(NULL), presses);
        }
    }
}
