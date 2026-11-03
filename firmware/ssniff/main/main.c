/*
 * CRIG EN-EL9(a) S-line sniffer (ESP32-S3-DevKitC-1), listen only.
 *
 * Wiring (see docs/en-el9-s-pin/EN-EL9_S-pin_findings.md):
 *   camera S (J2 pin 2) --[10 kOhm]--+-- GPIO2 (ADC1_CH1)
 *                                    +--[96.8 kOhm]-- camera - (BLK) -- ESP32 GND   (phase 1 only)
 * GPIO2 is never configured as an output. Power the ESP32 before the camera.
 *
 * The ADC samples GPIO2 continuously at SAMPLE_HZ. A burst starts when a sample rises above the
 * threshold and ends after QUIET_MS below it; it is dumped with PRE_MS of pre-trigger samples:
 *   B <n> <t_us> <rate_hz> <nsamples> <pre> <ovf>   header (t_us = esp_timer time of the trigger sample)
 *   D <hex>                                         samples, 3 hex digits per sample = mV at GPIO2
 *   E <n>                                           end
 * Every STAT_MS without activity: S <t_us> <min_mV> <max_mV> <avg_mV> <rate_hz> <ovf>.
 * Keys: + / - threshold +/-50 mV, i info, s stats on/off.
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/usb_serial_jtag.h"

#define ADC_CH       ADC_CHANNEL_1          /* GPIO2 */
#define SAMPLE_HZ    80000
#define FRAME_BYTES  (256 * SOC_ADC_DIGI_RESULT_BYTES)
#define PRE_MS       5
#define QUIET_MS     20
#define MAX_MS       1000
#define STAT_MS      5000
#define PRE_N        (SAMPLE_HZ * PRE_MS / 1000)
#define QUIET_N      (SAMPLE_HZ * QUIET_MS / 1000)
#define MAX_N        (SAMPLE_HZ * MAX_MS / 1000)

static adc_continuous_handle_t adc;
static adc_cali_handle_t cali;
static volatile uint32_t ovf_count;
static int thr_mv = 400;
static bool stats_on = true;

static uint16_t ring[PRE_N];                /* pre-trigger samples (mV) */
static uint32_t ring_pos;
static uint16_t *burst;                     /* MAX_N samples (mV), in PSRAM */

static bool IRAM_ATTR on_ovf(adc_continuous_handle_t h, const adc_continuous_evt_data_t *e, void *u)
{
    ovf_count++;
    return false;
}

static void adc_start(void)
{
    adc_continuous_handle_cfg_t hc = { .max_store_buf_size = 64 * 1024, .conv_frame_size = FRAME_BYTES };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&hc, &adc));
    adc_digi_pattern_config_t pat = {
        .atten = ADC_ATTEN_DB_12, .channel = ADC_CH, .unit = ADC_UNIT_1, .bit_width = SOC_ADC_DIGI_MAX_BITWIDTH,
    };
    adc_continuous_config_t cfg = {
        .pattern_num = 1, .adc_pattern = &pat, .sample_freq_hz = SAMPLE_HZ,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1, .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
    };
    ESP_ERROR_CHECK(adc_continuous_config(adc, &cfg));
    adc_continuous_evt_cbs_t cbs = { .on_pool_ovf = on_ovf };
    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(adc, &cbs, NULL));

    adc_cali_curve_fitting_config_t cc = { .unit_id = ADC_UNIT_1, .chan = ADC_CH, .atten = ADC_ATTEN_DB_12,
                                           .bitwidth = ADC_BITWIDTH_DEFAULT };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cc, &cali));
    ESP_ERROR_CHECK(adc_continuous_start(adc));
}

/* Reject input bursts (terminal echo of the log), as in firmware/optotest. */
static bool read_cmd(uint8_t *c)
{
    if (usb_serial_jtag_read_bytes(c, 1, 0) != 1) return false;
    uint8_t d;
    if (usb_serial_jtag_read_bytes(&d, 1, pdMS_TO_TICKS(20)) == 1) {
        while (usb_serial_jtag_read_bytes(&d, 1, pdMS_TO_TICKS(50)) == 1) {}
        printf("# input burst ignored\n");
        return false;
    }
    return true;
}

static void dump(uint32_t n, int64_t t_trig, uint32_t len, uint32_t pre, uint32_t rate)
{
    printf("B %lu %lld %lu %lu %lu %lu\n", n, t_trig, rate, len, pre, ovf_count);
    char line[3 * 64 + 4];
    for (uint32_t i = 0; i < len; i += 64) {
        int p = 0;
        line[p++] = 'D'; line[p++] = ' ';
        for (uint32_t k = i; k < len && k < i + 64; k++) p += sprintf(line + p, "%03X", burst[k] & 0xFFF);
        line[p++] = '\n'; line[p] = 0;
        fputs(line, stdout);
    }
    printf("E %lu\n", n);
    fflush(stdout);
}

void app_main(void)
{
    usb_serial_jtag_driver_config_t ucfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ucfg.tx_buffer_size = 16 * 1024;
    usb_serial_jtag_driver_install(&ucfg);
    burst = heap_caps_malloc(MAX_N * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!burst) { printf("# no PSRAM buffer\n"); return; }
    vTaskDelay(pdMS_TO_TICKS(1000));
    printf("\n# === CRIG S-line sniffer: GPIO2/ADC1_CH1, %d Hz, thr %d mV, pre %d ms, quiet %d ms, max %d ms ===\n",
           SAMPLE_HZ, thr_mv, PRE_MS, QUIET_MS, MAX_MS);
    printf("# keys: + / - threshold, i info, s stats on/off\n");
    adc_start();

    static uint8_t buf[FRAME_BYTES];
    uint32_t total = 0, nburst = 0;
    int64_t t_rate0 = esp_timer_get_time(); uint32_t n_rate0 = 0, rate = SAMPLE_HZ;
    bool cap = false; uint32_t len = 0, pre = 0, quiet = 0; int64_t t_trig = 0;
    int smin = 9999, smax = 0; uint64_t ssum = 0; uint32_t scnt = 0; int64_t t_stat = esp_timer_get_time();

    while (1) {
        uint32_t got = 0;
        if (adc_continuous_read(adc, buf, FRAME_BYTES, &got, 100) != ESP_OK) got = 0;
        int64_t now = esp_timer_get_time();
        uint32_t nsamp = got / SOC_ADC_DIGI_RESULT_BYTES;
        for (uint32_t i = 0; i < nsamp; i++) {
            adc_digi_output_data_t *d = (adc_digi_output_data_t *)&buf[i * SOC_ADC_DIGI_RESULT_BYTES];
            if (d->type2.channel != ADC_CH) continue;
            int mv = 0;
            adc_cali_raw_to_voltage(cali, d->type2.data, &mv);
            total++;
            if (!cap) {
                if (mv < smin) smin = mv;
                if (mv > smax) smax = mv;
                ssum += mv; scnt++;
                if (mv >= thr_mv) {
                    cap = true; quiet = 0; len = 0;
                    /* time of this sample: frame end minus the samples still to come in this frame */
                    t_trig = now - (int64_t)(nsamp - i) * 1000000 / rate;
                    pre = 0;
                    for (uint32_t k = 0; k < PRE_N; k++) {          /* oldest first */
                        if (total - 1 < PRE_N - k) continue;        /* ring not yet full after boot */
                        burst[len++] = ring[(ring_pos + k) % PRE_N]; pre++;
                    }
                } else {
                    ring[ring_pos] = mv; ring_pos = (ring_pos + 1) % PRE_N;
                }
            }
            if (cap) {
                burst[len++] = mv;
                quiet = (mv >= thr_mv) ? 0 : quiet + 1;
                if (quiet >= QUIET_N || len >= MAX_N) {
                    dump(++nburst, t_trig, len, pre, rate);
                    cap = false; ring_pos = 0; total = 0;   /* restart pre-trigger ring: dump time = gap */
                    smin = 9999; smax = 0; ssum = 0; scnt = 0; t_stat = esp_timer_get_time();
                }
            }
        }
        n_rate0 += nsamp;                           /* measured sample rate, used for timestamps */
        if (now - t_rate0 >= 2000000) {
            rate = (uint32_t)((uint64_t)n_rate0 * 1000000 / (uint64_t)(now - t_rate0));
            t_rate0 = now; n_rate0 = 0;
        }
        if (!cap && stats_on && scnt && now - t_stat >= STAT_MS * 1000) {
            printf("S %lld %d %d %d %lu %lu\n", now, smin, smax, (int)(ssum / scnt), rate, ovf_count);
            smin = 9999; smax = 0; ssum = 0; scnt = 0; t_stat = now;
        }
        uint8_t c;
        if (!cap && read_cmd(&c)) {
            switch (c) {
            case '+': thr_mv += 50; printf("# thr %d mV\n", thr_mv); break;
            case '-': if (thr_mv > 100) thr_mv -= 50; printf("# thr %d mV\n", thr_mv); break;
            case 's': stats_on = !stats_on; printf("# stats %s\n", stats_on ? "on" : "off"); break;
            case 'i': printf("# thr %d mV, rate %lu Hz, bursts %lu, ovf %lu\n", thr_mv, rate, nburst, ovf_count); break;
            default: break;
            }
        }
    }
}
