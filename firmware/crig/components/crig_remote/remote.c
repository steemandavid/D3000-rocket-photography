/*
 * CRIG remote (spec v0.18 §5.2, §8.6, §9.2). ESP32-S3-DevKitC-1U N16R8, as built 2026-11-03.
 *
 *   IO1  VBAT (100k/100k)   IO4 SHOOT button (to GND)   IO5 TRIG (PC817 U4)   IO6 ARM (PC817 U6)
 *   IO8/IO9 OLED SDA/SCL    IO10 green LED   IO11 red LED   IO12 vibration motor (Q1)   IO15 buzzer
 *   IO48 on-board RGB LED
 *
 * Console (UART0, 115200): help | status | sim shoot|trig 0|1|real | sim arm none|1|10|real | sim badcrc
 * Events are logged as "EVT <ms> <what> ..." for the HIL tests.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "sdkconfig.h"
#include "crig_proto.h"
#include "crig_config.h"
#include "crig_link.h"
#include "crig_binding.h"
#include "crig_armdec.h"
#include "crig_ws2812.h"
#include "crig_console.h"
#include "crig_buzzer.h"
#include "oled.h"

#define PIN_VBAT   1
#define PIN_SHOOT  4
#define PIN_TRIG   5
#define PIN_ARM    6
#define PIN_SDA    8
#define PIN_SCL    9
#define PIN_LED_G  10
#define PIN_LED_R  11
#define PIN_VIB    12
#define PIN_BUZ    15
#define PIN_RGB    48
#define MS(x)      ((int64_t)(x) * 1000)

typedef struct { crig_pkt_t pkt; int8_t rssi; int64_t t; } rx_item_t;
typedef struct { int stable, raw; int64_t t; } deb_t;

static QueueHandle_t s_rxq, s_edgeq;
static volatile uint32_t s_rx_bad;

/* inputs */
static deb_t   s_btn_d, s_trig_d;
static int     s_sim_shoot = -1, s_sim_trig = -1, s_sim_arm = -1;   /* -1 = real input */
static esp_timer_handle_t s_sim_arm_tmr;
static volatile int64_t s_arm_last_edge, s_arm_last_per;   /* real ARM line (debounced pulse starts) */
static volatile uint32_t s_arm_edges;
static armdec_t s_armdec;
static arm_state_t s_arm = ARM_NONE;

/* link */
#define QN 128
static struct { uint16_t seq; int64_t t; bool ack; } s_q[QN];
static int      s_qi;
static uint16_t s_seq;
static int64_t  s_t_status, s_t_tx;
static bool     s_has_status, s_link, s_fresh;
static crig_status_t s_node;
static int8_t   s_rx_rssi;
static int      s_quality;
static uint32_t s_rx_ok, s_tx_err;
static int32_t  s_badcrc_seq = -1;
static int64_t  s_badcrc_t;
static bool     s_badcrc_acked;

/* shoot logic */
static bool s_request, s_need_release = true, s_active, s_trig_acc, s_btn, s_trig;
static uint8_t s_src = CRIG_SRC_BUTTON;

/* battery, outputs */
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static adc_channel_t s_chan;
static int  s_batt_mv;
static bool s_usb_only, s_batt_warn, s_batt_cut;
static int64_t s_t_cut;
static bool s_ready, s_vib;
static volatile bool s_shutdown;
static int s_sim_batt = -1;            /* test hook: override the cell voltage (mV), -1 = real */
static char s_screen[8][24];          /* last composed OLED text, for the 'screen' command */
static int64_t s_t_vib_off;
static crig_ws_t s_rgb;

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

static bool quiet(void) { return !s_active && s_node.shutter == SH_REST; }      /* FR-R09: no tones during a burst */
static void buzz(const uint16_t *p, int n) { if (quiet()) crig_buzz_pattern(p, n); }

/* ------------------------------------------------------------------ radio */
static void rx_cb(const uint8_t *d, int len, int8_t rssi)
{
    rx_item_t it;
    if (!crig_decode(d, len, &it.pkt) || it.pkt.type != CRIG_T_STATUS) { s_rx_bad++; return; }
    it.rssi = rssi;
    it.t = now_us();
    xQueueSend(s_rxq, &it, 0);
}

static void tx(uint8_t type, const uint8_t *pl, uint8_t len, bool corrupt)
{
    crig_pkt_t p = { .type = type, .seq = ++s_seq, .ack_seq = 0, .len = len };
    memcpy(p.payload, pl, len);
    uint8_t f[CRIG_MAX_FRAME];
    size_t n = crig_encode(&p, f);
    if (corrupt) { f[n - 1] ^= 0x5A; s_badcrc_seq = p.seq; s_badcrc_t = now_us(); s_badcrc_acked = false; }
    else { s_q[s_qi] = (typeof(s_q[0])){ p.seq, now_us(), false }; s_qi = (s_qi + 1) % QN; }
    if (crig_link_send(f, n) != ESP_OK) s_tx_err++;
    s_t_tx = now_us();
}

static void on_status(const rx_item_t *it)
{
    crig_status_t st;
    crig_status_unpack(it->pkt.payload, &st);
    if (st.state != s_node.state || !s_has_status) evt("node state %s", crig_state_name(st.state));
    if (st.shutter != s_node.shutter || !s_has_status) evt("node shutter %s", crig_shutter_name(st.shutter));
    if (st.state == NODE_LOCKOUT && s_node.state != NODE_LOCKOUT) {
        evt("lockout");
        static const uint16_t p[] = { 60, 60, 60, 60, 60 };
        buzz(p, 5);
    }
    s_node = st;
    s_has_status = true;
    s_t_status = it->t;
    s_rx_rssi = it->rssi;
    s_rx_ok++;
    if (s_badcrc_seq >= 0 && it->pkt.ack_seq == (uint16_t)s_badcrc_seq) s_badcrc_acked = true;
    for (int i = 0; i < QN; i++)
        if (s_q[i].seq == it->pkt.ack_seq && s_q[i].t) { s_q[i].ack = true; break; }
}

static int link_quality(int64_t t)
{
    int sent = 0, ack = 0;
    for (int i = 0; i < QN; i++) {
        if (!s_q[i].t) continue;
        int64_t age = t - s_q[i].t;
        if (age < MS(150) || age > MS(LINK_Q_WINDOW_MS)) continue;   /* too young to be acked / too old */
        sent++;
        if (s_q[i].ack) ack++;
    }
    return sent ? ack * 100 / sent : 0;
}

/* ------------------------------------------------------------------ inputs */
/* The ARM line is sampled every 1 ms and debounced (ARM_DEB_MS) instead of using an edge interrupt:
   the PC817 turns off slowly against the internal pull-up, and that slow, noisy rising edge at the end
   of each pulse produced a second falling-edge interrupt (bench, 2026-11-06: periods 100 + 900 ms at
   1 Hz; at 10 Hz every 50 ms, so ARMED could never be decoded). A pulse start = debounced high -> low;
   its time is that of the first low sample. The interrupt stays only to count raw edges (aisr). */
#define ARM_DEB_MS 3
static volatile uint32_t s_arm_isr;
static void IRAM_ATTR arm_isr(void *arg) { s_arm_isr++; }

static void arm_poll_cb(void *arg)
{
    static int stable = 1, cnt;
    static int64_t t_first;
    int lv = gpio_get_level(PIN_ARM);
    int64_t t = esp_timer_get_time();
    if (lv == stable) { cnt = 0; return; }
    if (cnt++ == 0) t_first = t;
    if (cnt < ARM_DEB_MS) return;
    stable = lv; cnt = 0;
    if (lv) return;                          /* pulse end */
    if (s_arm_last_edge) s_arm_last_per = t_first - s_arm_last_edge;
    s_arm_last_edge = t_first; s_arm_edges++;
    if (s_sim_arm < 0) xQueueSend(s_edgeq, &t_first, 0);
}

static void sim_arm_cb(void *arg)
{
    int64_t t = esp_timer_get_time();
    xQueueSend(s_edgeq, &t, 0);
}

static void sim_arm_set(int mode)          /* -1 real, 0 no pulses, 1 = 1 Hz, 10 = 10 Hz */
{
    esp_timer_stop(s_sim_arm_tmr);
    if ((mode < 0) != (s_sim_arm < 0)) {     /* switching between the real line and the simulator: */
        xQueueReset(s_edgeq);                /* start from a clean decoder (NONE); sim-to-sim keeps */
        armdec_init(&s_armdec, MS(T_ARM_LOST_MS));   /* its state so disarm/hold timing is tested */
    }
    s_sim_arm = mode;
    if (mode == 1) esp_timer_start_periodic(s_sim_arm_tmr, 1000000);
    else if (mode == 10) esp_timer_start_periodic(s_sim_arm_tmr, 100000);
}

static int debounce(deb_t *d, int raw, int64_t t, int ms)
{
    if (raw != d->raw) { d->raw = raw; d->t = t; }
    else if (raw != d->stable && t - d->t >= MS(ms)) d->stable = raw;
    return d->stable;
}

/* ------------------------------------------------------------------ battery */
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

static int s_braw, s_bpin;               /* last ADC raw value and pin voltage (diagnostics) */
static void batt_update(int64_t t)
{
    /* the motor load disturbs the reading (and gpio_get_level() of an output-only pin is always 0): skip while it runs */
    if (s_vib || t - s_t_vib_off < MS(300)) return;
    int raw, sum = 0, mv = 0;
    for (int i = 0; i < 8; i++) { adc_oneshot_read(s_adc, s_chan, &raw); sum += raw; }
    raw = sum / 8;
    if (s_cali) adc_cali_raw_to_voltage(s_cali, raw, &mv); else mv = raw * 3100 / 4095;
    s_braw = raw; s_bpin = mv;
    int v = mv * 2;
    s_batt_mv = s_batt_mv ? (s_batt_mv * 3 + v) / 4 : v;
    if (s_sim_batt >= 0) s_batt_mv = s_sim_batt;
    bool usb = s_usb_only ? s_batt_mv < V_REMOTE_NO_CELL_MV + V_HYST_MV : s_batt_mv < V_REMOTE_NO_CELL_MV;
    static int64_t t_chg;
    if (usb == s_usb_only) t_chg = 0;
    else if (!t_chg) t_chg = t;
    else if (t - t_chg >= MS(2000)) {                 /* cell present / absent only after 2 s */
        s_usb_only = usb; t_chg = 0;
        evt("batt %s mv=%d", usb ? "none (switch off / USB power)" : "present", s_batt_mv);
    }
    usb = s_usb_only;
    static int64_t t_low;
    bool w = s_batt_warn, c = s_batt_cut;
    if (usb) { w = c = false; t_low = 0; }
    else {
        if (s_batt_mv < V_REMOTE_WARN_MV) w = true; else if (s_batt_mv > V_REMOTE_WARN_MV + V_HYST_MV) w = false;
        if (s_batt_mv < V_REMOTE_CUTOFF_MV) { if (!t_low) t_low = t; if (t - t_low >= MS(T_BATT_CUTOFF_MS)) c = true; }
        else { t_low = 0; if (s_batt_mv > V_REMOTE_CUTOFF_MV + V_HYST_MV) c = false; }
    }
    if (w != s_batt_warn) evt("batt warn %d mv=%d", w, s_batt_mv);
    if (c != s_batt_cut) { evt("batt cutoff %d mv=%d", c, s_batt_mv); s_t_cut = t; }
    s_batt_warn = w; s_batt_cut = c;
    if (s_batt_cut && t - s_t_cut > MS(T_BATT_SHUTDOWN_MS)) {     /* FR-R11: clean shutdown */
        evt("batt empty: shutting down");
        s_shutdown = true;                       /* stops the display task redrawing the main page */
        vTaskDelay(pdMS_TO_TICKS(300));
        oled_clear(); oled_text(0, 3, "REMOTE BATT EMPTY"); oled_flush();
        gpio_set_level(PIN_VIB, 0); gpio_set_level(PIN_LED_G, 0); gpio_set_level(PIN_LED_R, 0);
        crig_ws_set(&s_rgb, 0, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(2000));
        oled_off();                              /* the OLED stays powered from 3V3 in deep sleep */
        esp_deep_sleep_start();
    }
}

/* ------------------------------------------------------------------ outputs */
static const char *warning(void)
{
    if (!s_link) return "NO LINK";
    if (s_batt_cut) return "REMOTE BATT EMPTY";
    if (s_node.state == NODE_FAULT) return "NODE FAULT";
    if (s_node.state == NODE_LOCKOUT) return "LOCKOUT";
    if (s_arm == ARM_LOST) return "RLC LOST";
    if (s_trig && s_need_release) return "TRIG ACTIVE AT BOOT";
    if (s_node.fault & CRIG_F_PACK_WARN) return "NODE BATT LOW";
    if (s_batt_warn) return "REMOTE BATT LOW";
    return NULL;
}

static void outputs(int64_t t)
{
    bool rlc_mode = s_arm != ARM_NONE;
    bool ready = s_fresh && s_quality >= LINK_Q_READY_PCT && s_node.state != NODE_FAULT && s_node.state != NODE_LOCKOUT &&
                 !(s_node.fault & CRIG_F_PACK_CUTOFF) && !s_batt_cut &&
                 (!rlc_mode || s_arm == ARM_CONNECTED || s_arm == ARM_ARMED);          /* FR-R13 */
    if (ready != s_ready) { s_ready = ready; evt("ready %d", ready); }
    gpio_set_level(PIN_LED_G, ready);
    gpio_set_level(PIN_LED_R, !ready);
    bool vib = s_active && s_fresh && s_node.shutter == SH_FULL;                       /* FR-R09 */
    if (s_vib && !vib) s_t_vib_off = t;
    s_vib = vib;
    gpio_set_level(PIN_VIB, vib);

    int64_t ms = t / 1000;
    uint8_t r = 0, g = 0;
    const uint8_t L = 12;
    if (s_batt_cut || s_node.state == NODE_FAULT) { r = L; if ((ms / 250) & 1) g = L / 2; }
    else if (!s_link) { if ((ms / 125) & 1) g = L; }
    else if (s_active && s_node.shutter == SH_FULL) r = L;
    else if (warning()) { if ((ms / 500) & 1) { r = L; g = L / 2; } }
    else if ((ms / 500) & 1) g = L;
    crig_ws_set(&s_rgb, r, g, 0);
}

static void scr(int row, const char *t)
{
    oled_text(0, row, t);
    strncpy(s_screen[row], t, sizeof s_screen[row] - 1);
}

static void display_task(void *arg)
{
    char l[40];
    while (1) {
        if (s_shutdown) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
        oled_clear();
        memset(s_screen, 0, sizeof s_screen);
        snprintf(l, sizeof l, "LINK %3d%% RSSI %d", s_link ? s_quality : 0, s_fresh ? s_node.rssi : 0);
        scr(0, l);
        snprintf(l, sizeof l, "NODE %s %s", s_fresh ? crig_state_name(s_node.state) : "--", s_fresh ? crig_shutter_name(s_node.shutter) : "");
        scr(1, l);
        char nv[16], rv[16];
        if (!s_fresh) strcpy(nv, "--");
        else if (s_node.fault & CRIG_F_NO_PACK) strcpy(nv, "NOPACK");
        else snprintf(nv, sizeof nv, "%d.%dV", s_node.vbat_mv / 1000, (s_node.vbat_mv % 1000) / 100);
        if (s_usb_only) strcpy(rv, "USB"); else snprintf(rv, sizeof rv, "%d.%02dV", s_batt_mv / 1000, (s_batt_mv % 1000) / 10);
        snprintf(l, sizeof l, "N %s R %s", nv, rv);
        scr(2, l);
        snprintf(l, sizeof l, "RLC %s", armdec_name(s_arm));
        scr(3, l);
        uint32_t b = s_fresh ? s_node.burst_ms : 0;
        snprintf(l, sizeof l, "%s %lu.%luS", s_src == CRIG_SRC_TRIG ? "TRIG" : "BUTTON", (unsigned long)(b / 1000), (unsigned long)((b % 1000) / 100));
        scr(4, l);
        snprintf(l, sizeof l, "FRAMES ~%lu", (unsigned long)(b / 333));
        scr(5, l);
        const char *w = warning();
        scr(7, w ? w : (s_active ? ">> SHOOTING <<" : (s_ready ? "READY" : "")));
        oled_flush();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/* ------------------------------------------------------------------ console */
static int parse01(const char *v) { return !strcmp(v, "real") ? -1 : atoi(v) != 0; }

static void print_status(void)
{
    printf("STATUS link=%d fresh=%d q=%d node=%s sh=%s nv=%u nf=0x%02x nrssi=%d rssi=%d burst=%lu arm=%s btn=%d trig=%d "
           "req=%d act=%d nr=%d src=%s batt=%d usb=%d ready=%d rx=%lu bad=%lu txerr=%lu foreign=%lu txfail=%lu txdel=%lu "
           "aedges=%lu aper=%ld aage=%ld aisr=%lu simarm=%d braw=%d bpin=%d\n",
           s_link, s_fresh, s_quality, crig_state_name(s_node.state), crig_shutter_name(s_node.shutter), s_node.vbat_mv,
           s_node.fault, s_node.rssi, s_rx_rssi, (unsigned long)s_node.burst_ms, armdec_name(s_arm), s_btn, s_trig,
           s_request, s_active, s_need_release, s_src == CRIG_SRC_TRIG ? "TRIG" : "BUTTON", s_batt_mv, s_usb_only, s_ready,
           (unsigned long)s_rx_ok, (unsigned long)s_rx_bad, (unsigned long)s_tx_err,
           (unsigned long)crig_link_foreign_count(), (unsigned long)crig_link_tx_fail(), (unsigned long)crig_link_tx_delivered(),
           (unsigned long)s_arm_edges, (long)(s_arm_last_per / 1000),
           s_arm_last_edge ? (long)((esp_timer_get_time() - s_arm_last_edge) / 1000) : -1L, (unsigned long)s_arm_isr, s_sim_arm, s_braw, s_bpin);
}

static void console(void)
{
    char l[64], a[16] = "", b[16] = "";
    if (!crig_console_poll(l, sizeof l)) return;
    int n = sscanf(l, "%*s %15s %15s", a, b);
    if (!strcmp(l, "status")) print_status();
    else if (!strcmp(l, "screen")) { for (int i = 0; i < 8; i++) printf("SCREEN %d |%s|\n", i, s_screen[i]); }
    else if (!strncmp(l, "beep ", 5)) {      /* buzzer check: one tone of <ms> (silent while shooting) */
        uint16_t ms[] = { (uint16_t)atoi(l + 5) };
        buzz(ms, 1); printf("OK beep %u\n", ms[0]);
    }
    else if (!strcmp(l, "scan")) crig_link_scan();
    else if (!strcmp(l, "bcast")) crig_link_bcast_test(20);
    else if (!strcmp(l, "reboot")) { printf("OK reboot\n"); fflush(stdout); vTaskDelay(pdMS_TO_TICKS(50)); esp_restart(); }
    else if (!strcmp(l, "help")) printf("commands: status | screen | scan | bcast | beep <ms> | reboot | sim shoot|trig 0|1|real | sim arm none|1|10|real | sim batt <mv>|real | sim badcrc\n");
#if CONFIG_CRIG_TEST_HOOKS
    else if (!strncmp(l, "sim ", 4) && n >= 1) {
        if (!strcmp(a, "shoot") && n == 2) { s_sim_shoot = parse01(b); printf("OK sim shoot %d\n", s_sim_shoot); }
        else if (!strcmp(a, "trig") && n == 2) { s_sim_trig = parse01(b); printf("OK sim trig %d\n", s_sim_trig); }
        else if (!strcmp(a, "arm") && n == 2) {
            int m = !strcmp(b, "real") ? -1 : !strcmp(b, "none") ? 0 : atoi(b);
            if (m == -1 || m == 0 || m == 1 || m == 10) { sim_arm_set(m); printf("OK sim arm %d\n", m); }
            else printf("ERR arm mode\n");
        } else if (!strcmp(a, "batt") && n == 2) {
            s_sim_batt = !strcmp(b, "real") ? -1 : atoi(b);
            printf("OK sim batt %d\n", s_sim_batt);
        } else if (!strcmp(a, "badcrc")) {
            uint8_t pl[2] = { CRIG_SRC_BUTTON, 0 };
            tx(CRIG_T_SHOOT, pl, 2, true);
            printf("OK sim badcrc seq=%ld\n", (long)s_badcrc_seq);
        } else printf("ERR sim\n");
    }
#endif
    else printf("ERR unknown '%s'\n", l);
}

/* ------------------------------------------------------------------ main loop */
void crig_remote_main(void)
{
    gpio_config_t o = { .pin_bit_mask = (1ULL << PIN_LED_G) | (1ULL << PIN_LED_R) | (1ULL << PIN_VIB), .mode = GPIO_MODE_OUTPUT };
    gpio_config(&o);
    gpio_set_level(PIN_LED_G, 0); gpio_set_level(PIN_LED_R, 1); gpio_set_level(PIN_VIB, 0);   /* red at power-up */
    gpio_config_t in = { .pin_bit_mask = (1ULL << PIN_SHOOT) | (1ULL << PIN_TRIG), .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&in);
    gpio_config_t ai = { .pin_bit_mask = 1ULL << PIN_ARM, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE, .intr_type = GPIO_INTR_NEGEDGE };
    gpio_config(&ai);
    crig_buzz_init(PIN_BUZ);
    crig_console_init();
    esp_log_level_set("*", ESP_LOG_WARN);
    printf("\nCRIG remote fw %s\n", __DATE__ " " __TIME__);
    adc_init();
    crig_ws_init(&s_rgb, PIN_RGB);
    if (!oled_init(PIN_SDA, PIN_SCL)) printf("WARN OLED not found\n");
    s_rxq = xQueueCreate(16, sizeof(rx_item_t));
    s_edgeq = xQueueCreate(32, sizeof(int64_t));
    armdec_init(&s_armdec, MS(T_ARM_LOST_MS));
    esp_timer_create_args_t ta = { .callback = sim_arm_cb, .name = "simarm" };
    esp_timer_create(&ta, &s_sim_arm_tmr);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_ARM, arm_isr, NULL);
    esp_timer_create_args_t tp = { .callback = arm_poll_cb, .name = "armpoll" };
    esp_timer_handle_t tpoll;
    esp_timer_create(&tp, &tpoll);
    esp_timer_start_periodic(tpoll, 1000);
    static const uint8_t peer[6] = CRIG_NODE_MAC;
    ESP_ERROR_CHECK(crig_link_init(peer, rx_cb));
    batt_update(now_us());
    s_usb_only = s_batt_mv < V_REMOTE_NO_CELL_MV;
    evt("batt %s mv=%d", s_usb_only ? "none (switch off / USB power)" : "present", s_batt_mv);
    xTaskCreate(display_task, "disp", 4096, NULL, 2, NULL);
    evt("boot");
    static const uint16_t boot[] = { 80 };
    crig_buzz_pattern(boot, 1);

    int64_t t_out = 0, t_batt = 0;
    while (1) {
        int64_t t = now_us();

        rx_item_t it;
        while (xQueueReceive(s_rxq, &it, 0) == pdTRUE) on_status(&it);
        int64_t e;
        while (xQueueReceive(s_edgeq, &e, 0) == pdTRUE) armdec_edge(&s_armdec, e);
        arm_state_t a = armdec_poll(&s_armdec, t);
        if (a != s_arm) {
            evt("rlc %s", armdec_name(a));
            if (a == ARM_LOST) { static const uint16_t p[] = { 300, 150, 300 }; buzz(p, 3); }
            else if (s_arm == ARM_LOST) { static const uint16_t p[] = { 100, 100, 100 }; buzz(p, 3); }   /* RLC restored */
            s_arm = a;
        }

        /* inputs (active LOW) */
        int braw = !gpio_get_level(PIN_SHOOT), traw = !gpio_get_level(PIN_TRIG);
        int bd = debounce(&s_btn_d, braw, t, T_BTN_DEBOUNCE_MS), td = debounce(&s_trig_d, traw, t, T_TRIG_DEBOUNCE_MS);
        s_btn = s_sim_shoot >= 0 ? s_sim_shoot : bd;
        s_trig = s_sim_trig >= 0 ? s_sim_trig : td;
        s_trig_acc = s_trig && s_arm == ARM_ARMED;                                 /* §8.6 */
        s_request = s_btn || s_trig_acc;                                           /* FR-R02 */

        /* link */
        bool lu = s_has_status && t - s_t_status < MS(T_LINK_LOST_MS);
        s_fresh = s_has_status && t - s_t_status < MS(T_STATUS_STALE_MS);
        if (lu != s_link) {
            s_link = lu;
            evt("link %s", lu ? "up" : "down");
            static const uint16_t up[] = { 40, 60, 40 }, down[] = { 400 };
            if (lu) buzz(up, 3); else buzz(down, 1);
        }
        s_quality = link_quality(t);

        /* FR-R03 / FR-R08: SHOOT must have been inactive once since boot, link recovery or LOCKOUT */
        if (s_request && (!s_link || (s_fresh && s_node.state == NODE_LOCKOUT))) s_need_release = true;
        if (!s_request) s_need_release = false;
        bool act = s_request && !s_need_release && s_link && !s_batt_cut;
        if (act && !s_active) s_src = s_btn ? CRIG_SRC_BUTTON : CRIG_SRC_TRIG;
        if (act != s_active) {
            s_active = act;
            if (act) { crig_buzz_stop(); evt("shoot start src=%s", s_src == CRIG_SRC_TRIG ? "TRIG" : "BUTTON"); }
            else evt("shoot stop");
        }

        /* transmit: SHOOT at f_shoot while active, HEARTBEAT at f_heartbeat otherwise (FR-R01, FR-R02, FR-R15) */
        uint8_t prep = s_arm == ARM_ARMED;
        if (s_active ? t - s_t_tx >= MS(T_SHOOT_PERIOD_MS) : t - s_t_tx >= MS(T_HEARTBEAT_MS)) {
            if (s_active) { uint8_t pl[2] = { s_src, prep }; tx(CRIG_T_SHOOT, pl, 2, false); }
            else { uint8_t pl[1] = { prep }; tx(CRIG_T_HEARTBEAT, pl, 1, false); }
        }
        if (s_badcrc_seq >= 0 && t - s_badcrc_t > MS(400)) {
            evt("badcrc acked=%d", s_badcrc_acked);
            s_badcrc_seq = -1;
        }

        if (t - t_out >= MS(20)) { t_out = t; outputs(t); console(); }
        if (t - t_batt >= MS(500)) { t_batt = t; batt_update(t); }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
