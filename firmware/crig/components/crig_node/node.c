/*
 * CRIG camera node (spec v0.18 §5.1, §6.2, §9.1, §9.3). ESP32-S3-DevKitC-1 N16R8.
 *
 *   IO1  VBAT sense (100k / 47k)      IO4  FULL  -> U3 PC817 (camera WHT)
 *   IO5  HALF  -> U4 PC817 (YEL)      IO6  PWR   -> U5 PC817 (GRN), never driven (cam_power_ctrl off)
 *   IO7 buzzer (active)               IO48 + IO38 on-board WS2812 (both driven: board revision not recorded)
 *
 * Console (UART0, 115200): help | status | test.  Events are logged as "EVT <ms> <what> ..." for the HIL tests.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "crig_proto.h"
#include "crig_config.h"
#include "crig_link.h"
#include "crig_binding.h"
#include "crig_ws2812.h"
#include "crig_console.h"
#include "crig_buzzer.h"
#include "sdkconfig.h"
#include <stdlib.h>

#define PIN_VBAT   1
#define PIN_FULL   4
#define PIN_HALF   5
#define PIN_PWR    6
#define PIN_BUZ    7          /* as wired (2026-11-06); the draft schematic said IO15 */
#define PIN_LED_A  48
#define PIN_LED_B  38
#define VBAT_NUM   147
#define VBAT_DEN   47
#define MS(x)      ((int64_t)(x) * 1000)

typedef struct { crig_pkt_t pkt; int8_t rssi; int64_t t; } rx_item_t;

static QueueHandle_t s_rxq;
static volatile uint32_t s_rx_bad;
static uint32_t s_rx_ok, s_tx_ok, s_tx_err;

static uint8_t  s_state = NODE_BOOT, s_shutter = SH_REST, s_fault;
static uint16_t s_vbat_mv, s_tx_seq;
static int8_t   s_rssi;
static uint32_t s_burst_ms;
static bool     s_has_rx, s_link, s_prep_req, s_prep_prev, s_prep_active;
static int64_t  s_t_rx, s_t_shoot, s_t_half, s_t_full, s_t_test, s_prep_end, s_prep_next;

static crig_ws_t s_led_a, s_led_b;
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static adc_channel_t s_chan;
static int s_sim_vbat = -1;           /* test hook: override the pack voltage (mV), -1 = real */

static int64_t now_us(void) { return esp_timer_get_time(); }

static void evt(const char *fmt, ...)
{
    char b[120];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    printf("EVT %lld %s\n", (long long)(now_us() / 1000), b);
}

static void set_shutter(uint8_t sh)
{
    if (sh == s_shutter) return;
    gpio_set_level(PIN_HALF, sh != SH_REST);    /* FULL asserts both (spec §6.2) */
    gpio_set_level(PIN_FULL, sh == SH_FULL);
    s_shutter = sh;
    if (sh != SH_REST) crig_buzz_stop();        /* FR-N18: never sound while not in REST */
    evt("shutter %s", crig_shutter_name(sh));
}

static void set_state(uint8_t st)
{
    if (st == s_state) return;
    evt("state %s -> %s", crig_state_name(s_state), crig_state_name(st));
    s_state = st;
}

static void buzz(const uint16_t *p, int n) { if (s_shutter == SH_REST) crig_buzz_pattern(p, n); }

/* ------------------------------------------------------------------ radio */
static void rx_cb(const uint8_t *d, int len, int8_t rssi)
{
    rx_item_t it;
    if (!crig_decode(d, len, &it.pkt) || it.pkt.type == CRIG_T_STATUS) { s_rx_bad++; return; }   /* FR-N11 */
    it.rssi = rssi;
    it.t = now_us();
    xQueueSend(s_rxq, &it, 0);
}

static void send_status(uint16_t ack)
{
    crig_status_t st = { s_state, s_shutter, s_fault, s_vbat_mv, s_rssi, s_burst_ms };
    crig_pkt_t p = { .type = CRIG_T_STATUS, .seq = ++s_tx_seq, .ack_seq = ack, .len = CRIG_STATUS_LEN };
    crig_status_pack(&st, p.payload);
    uint8_t f[CRIG_MAX_FRAME];
    size_t n = crig_encode(&p, f);
    if (crig_link_send(f, n) == ESP_OK) s_tx_ok++; else s_tx_err++;
}

/* ------------------------------------------------------------------ shutter sequencing */
static void start_burst(uint8_t src)
{
    s_burst_ms = 0;
    if (s_shutter == SH_HALF) {
        /* a prepare pulse is already holding HALF: the camera is awake, keep its start time */
    } else {
        set_shutter(SH_HALF);
        s_t_half = now_us();
    }
    s_prep_active = false;
    set_state(NODE_SHOOTING);
    evt("burst start src=%s", src == CRIG_SRC_TRIG ? "TRIG" : "BUTTON");
}

static void end_burst(uint8_t next, const char *why)
{
    if (s_shutter == SH_FULL) s_burst_ms = (uint32_t)((now_us() - s_t_full) / 1000);
    set_shutter(SH_REST);
    set_state(next);
    /* the camera is awake after a burst: no wake pulse straight after it (seen on the RLC bench: PREPARE
       is still 1 for the ~0.3 s the remote needs to see the ARM pattern drop after TRIG) */
    s_prep_next = now_us() + MS(T_PREPARE_KEEPALIVE_MS);
    evt("burst end ms=%lu reason=%s", (unsigned long)s_burst_ms, why);
}

static void handle(const rx_item_t *it)
{
    s_t_rx = it->t;
    s_has_rx = true;
    s_rssi = it->rssi;
    s_rx_ok++;
    switch (it->pkt.type) {
    case CRIG_T_HEARTBEAT:
        s_prep_req = it->pkt.payload[0] != 0;
        if (s_state == NODE_LOCKOUT) { set_state(NODE_IDLE); evt("lockout cleared"); }
        break;
    case CRIG_T_SHOOT:
        s_t_shoot = it->t;
        s_prep_req = it->pkt.payload[1] != 0;
        if (s_state == NODE_IDLE) start_burst(it->pkt.payload[0]);
        break;
    }
    send_status(it->pkt.seq);    /* FR-N09 */
}

static void tick(void)
{
    int64_t t = now_us();

    bool lu = s_has_rx && t - s_t_rx < MS(T_LINK_LOST_MS);
    if (lu != s_link) {
        s_link = lu;
        evt("link %s", lu ? "up" : "down");
        static const uint16_t up[] = { 100, 100, 100 }, down[] = { 400 };
        if (lu) buzz(up, 3); else { buzz(down, 1); s_prep_req = false; }
    }

    /* PREPARE (FR-N19): HALF wake pulse on the rising edge, repeated while prepare = 1 and IDLE */
    if (s_prep_req && !s_prep_prev) s_prep_next = t;
    if (s_prep_req != s_prep_prev) evt("prepare %d", s_prep_req);
    s_prep_prev = s_prep_req;
    if (s_state == NODE_IDLE) {
        if (s_prep_active && t >= s_prep_end) { set_shutter(SH_REST); s_prep_active = false; }
        if (s_prep_req && !s_prep_active && s_shutter == SH_REST && t >= s_prep_next) {
            set_shutter(SH_HALF);
            s_t_half = t;
            s_prep_active = true;
            s_prep_end = t + MS(T_PREPARE_WAKE_MS);
            s_prep_next = t + MS(T_PREPARE_KEEPALIVE_MS);
            evt("prepare pulse");
        }
    }

    switch (s_state) {
    case NODE_SHOOTING:
        if (t - s_t_shoot > MS(T_SHOOT_TIMEOUT_MS)) end_burst(NODE_IDLE, "release");
        else if (s_shutter == SH_HALF && t - s_t_half >= MS(T_PREFIRE_MS)) { set_shutter(SH_FULL); s_t_full = t; }
        else if (s_shutter == SH_FULL) {
            s_burst_ms = (uint32_t)((t - s_t_full) / 1000);
            if (s_burst_ms >= T_MAX_BURST_MS) end_burst(NODE_LOCKOUT, "max_burst");
        }
        break;
    case NODE_TEST:
        if (s_shutter == SH_HALF && t - s_t_test >= MS(T_PREFIRE_MS)) { set_shutter(SH_FULL); s_t_full = t; }
        else if (s_shutter == SH_FULL && t - s_t_full >= MS(T_TEST_FULL_MS)) {
            set_shutter(SH_REST); set_state(NODE_IDLE); evt("test done");
        }
        break;
    case NODE_FAULT:
        if (!(s_fault & CRIG_F_PACK_CUTOFF)) { set_state(NODE_IDLE); evt("fault cleared"); }
        break;
    default:
        break;
    }

    if ((s_fault & CRIG_F_PACK_CUTOFF) && s_state != NODE_FAULT) {   /* FR-N10 */
        set_shutter(SH_REST);
        s_prep_active = false;
        set_state(NODE_FAULT);
    }
}

/* ------------------------------------------------------------------ battery, LED, console */
static void adc_init(void)
{
    adc_unit_t unit;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(PIN_VBAT, &unit, &s_chan));
    adc_oneshot_unit_init_cfg_t u = { .unit_id = unit };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&u, &s_adc));
    adc_oneshot_chan_cfg_t c = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, s_chan, &c));
    adc_cali_curve_fitting_config_t cal = { .unit_id = unit, .chan = s_chan, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    if (adc_cali_create_scheme_curve_fitting(&cal, &s_cali) != ESP_OK) s_cali = NULL;
}

static void vbat_update(void)
{
    int raw, sum = 0, mv = 0;
    for (int i = 0; i < 8; i++) { adc_oneshot_read(s_adc, s_chan, &raw); sum += raw; }
    raw = sum / 8;
    if (s_cali) adc_cali_raw_to_voltage(s_cali, raw, &mv); else mv = raw * 3100 / 4095;
    int v = mv * VBAT_NUM / VBAT_DEN;
    s_vbat_mv = (uint16_t)(s_vbat_mv ? (s_vbat_mv * 3 + v) / 4 : v);
    if (s_sim_vbat >= 0) s_vbat_mv = (uint16_t)s_sim_vbat;
    uint8_t f = s_fault & ~(CRIG_F_NO_PACK | CRIG_F_PREPARING);
    if (s_vbat_mv < V_NO_SUPPLY_MV) {
        f = (f & ~(CRIG_F_PACK_WARN | CRIG_F_PACK_CUTOFF)) | CRIG_F_NO_PACK;
    } else {
        if (s_vbat_mv < V_NODE_WARN_MV) f |= CRIG_F_PACK_WARN; else if (s_vbat_mv > V_NODE_WARN_MV + V_HYST_MV) f &= ~CRIG_F_PACK_WARN;
        if (s_vbat_mv < V_NODE_CUTOFF_MV) f |= CRIG_F_PACK_CUTOFF; else if (s_vbat_mv > V_NODE_CUTOFF_MV + V_HYST_MV) f &= ~CRIG_F_PACK_CUTOFF;
    }
    f |= CRIG_F_CAM_ON;                         /* camera power control disabled (cam_power_ctrl off) */
    if (s_prep_req) f |= CRIG_F_PREPARING;
    if (f != s_fault) evt("fault 0x%02x vbat=%u", f, s_vbat_mv);
    s_fault = f;
}

static void led_update(void)
{
    int64_t ms = now_us() / 1000;
    uint8_t r = 0, g = 0, b = 0;
    const uint8_t L = 12;
    switch (s_state) {
    case NODE_SHOOTING: case NODE_TEST: r = L; break;
    case NODE_LOCKOUT: case NODE_FAULT:
        if ((ms / 250) & 1) { r = L; g = L / 2; } else r = L;
        break;
    case NODE_CAL: if ((ms / 500) & 1) b = L; break;
    default:
        if (s_link) { if ((ms / 500) & 1) g = L; }      /* slow green 1 Hz */
        else if ((ms / 125) & 1) g = L;                 /* fast green 4 Hz */
        break;
    }
    crig_ws_set(&s_led_a, r, g, b);
    crig_ws_set(&s_led_b, r, g, b);
}

static void print_status(void)
{
    printf("STATUS state=%s shutter=%s link=%d rssi=%d vbat=%u fault=0x%02x burst=%lu prep=%d rx=%lu bad=%lu foreign=%lu tx=%lu txerr=%lu txfail=%lu txdel=%lu\n",
           crig_state_name(s_state), crig_shutter_name(s_shutter), s_link, s_rssi, s_vbat_mv, s_fault,
           (unsigned long)s_burst_ms, s_prep_req, (unsigned long)s_rx_ok, (unsigned long)s_rx_bad,
           (unsigned long)crig_link_foreign_count(), (unsigned long)s_tx_ok, (unsigned long)s_tx_err,
           (unsigned long)crig_link_tx_fail(), (unsigned long)crig_link_tx_delivered());
}

static void console(void)
{
    char l[64];
    if (!crig_console_poll(l, sizeof l)) return;
    if (!strcmp(l, "status")) print_status();
    else if (!strcmp(l, "test")) {
        if (s_state == NODE_IDLE && s_shutter == SH_REST) {
            set_state(NODE_TEST); set_shutter(SH_HALF); s_t_test = now_us();
        } else printf("ERR test refused in %s\n", crig_state_name(s_state));
    }
#if CONFIG_CRIG_TEST_HOOKS
    else if (!strncmp(l, "sim vbat ", 9)) {
        s_sim_vbat = !strcmp(l + 9, "real") ? -1 : atoi(l + 9);
        printf("OK sim vbat %d\n", s_sim_vbat);
    }
#endif
    else if (!strncmp(l, "beep ", 5)) {      /* buzzer check: one tone of <ms> (silent while shooting) */
        uint16_t ms[] = { (uint16_t)atoi(l + 5) };
        buzz(ms, 1); printf("OK beep %u\n", ms[0]);
    }
    else if (!strcmp(l, "reboot")) { printf("OK reboot\n"); fflush(stdout); vTaskDelay(pdMS_TO_TICKS(50)); esp_restart(); }
    else if (!strcmp(l, "scan")) crig_link_scan();
    else if (!strcmp(l, "bcast")) crig_link_bcast_test(20);
    else if (!strcmp(l, "help")) printf("commands: status | test | scan | bcast | beep <ms> | reboot | sim vbat <mv>|real\n");
    else printf("ERR unknown '%s'\n", l);
}

void crig_node_main(void)
{
    /* outputs LOW first: the camera must never see a glitch */
    gpio_config_t o = { .pin_bit_mask = (1ULL << PIN_HALF) | (1ULL << PIN_FULL) | (1ULL << PIN_PWR), .mode = GPIO_MODE_OUTPUT };
    gpio_config(&o);
    gpio_set_level(PIN_HALF, 0); gpio_set_level(PIN_FULL, 0); gpio_set_level(PIN_PWR, 0);
    crig_buzz_init(PIN_BUZ);
    crig_console_init();
    esp_log_level_set("*", ESP_LOG_WARN);
    printf("\nCRIG node fw %s\n", __DATE__ " " __TIME__);
    adc_init();
    vbat_update();
    crig_ws_init(&s_led_a, PIN_LED_A);
    crig_ws_init(&s_led_b, PIN_LED_B);
    s_rxq = xQueueCreate(16, sizeof(rx_item_t));
    static const uint8_t peer[6] = CRIG_REMOTE_MAC;
    ESP_ERROR_CHECK(crig_link_init(peer, rx_cb));
    set_state(NODE_IDLE);                      /* FR-N02 */
    evt("boot shutter=%s", crig_shutter_name(s_shutter));
    static const uint16_t boot[] = { 200 };
    buzz(boot, 1);

    int64_t t_slow = 0;
    while (1) {
        rx_item_t it;
        while (xQueueReceive(s_rxq, &it, 0) == pdTRUE) handle(&it);
        tick();
        int64_t t = now_us();
        if (t - t_slow >= MS(50)) {
            t_slow = t;
            static int n;
            if (++n % 4 == 0) vbat_update();
            led_update();
            console();
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
