#include "crig_link.h"
#include <string.h>
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_event.h"
#include "esp_log.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "crig_binding.h"
#ifndef CRIG_LINK_ENCRYPT
#define CRIG_LINK_ENCRYPT 1
#endif

static uint8_t s_peer[6];
static crig_rx_cb_t s_cb;
static volatile uint32_t s_foreign, s_tx_fail, s_tx_okcb;
static uint8_t s_last_foreign[6];

static void recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (memcmp(info->src_addr, s_peer, 6) != 0) { s_foreign++; memcpy(s_last_foreign, info->src_addr, 6); return; }
    if (s_cb) s_cb(data, len, info->rx_ctrl ? (int8_t)info->rx_ctrl->rssi : 0);
}

static void send_cb(const uint8_t *mac, esp_now_send_status_t st)
{
    if (st == ESP_NOW_SEND_SUCCESS) s_tx_okcb++; else s_tx_fail++;
}

esp_err_t crig_link_init(const uint8_t peer_mac[6], crig_rx_cb_t cb)
{
    memcpy(s_peer, peer_mac, 6);
    s_cb = cb;
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        e = nvs_flash_init();
    }
    ESP_ERROR_CHECK(e);
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_channel(CRIG_CHANNEL, WIFI_SECOND_CHAN_NONE));
    esp_log_level_set("wifi", ESP_LOG_WARN);
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(recv_cb));
    ESP_ERROR_CHECK(esp_now_register_send_cb(send_cb));
    static const uint8_t pmk[16] = CRIG_PMK;
    static const uint8_t lmk[16] = CRIG_LMK;
    ESP_ERROR_CHECK(esp_now_set_pmk(pmk));
    esp_now_peer_info_t p = { 0 };
    memcpy(p.peer_addr, s_peer, 6);
    p.channel = CRIG_CHANNEL;
    p.ifidx = WIFI_IF_STA;
    p.encrypt = CRIG_LINK_ENCRYPT;
    memcpy(p.lmk, lmk, 16);
    ESP_ERROR_CHECK(esp_now_add_peer(&p));
    return ESP_OK;
}

esp_err_t crig_link_send(const uint8_t *data, size_t len)
{
    return esp_now_send(s_peer, data, len);
}

uint32_t crig_link_foreign_count(void) { return s_foreign; }

uint32_t crig_link_tx_fail(void) { return s_tx_fail; }
uint32_t crig_link_tx_delivered(void) { return s_tx_okcb; }
const uint8_t *crig_link_last_foreign(void) { return s_last_foreign; }

void crig_link_scan(void)
{
    wifi_scan_config_t sc = { .show_hidden = true };
    if (esp_wifi_scan_start(&sc, true) != ESP_OK) { printf("SCAN failed\n"); return; }
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    wifi_ap_record_t r[12];
    uint16_t m = 12;
    esp_wifi_scan_get_ap_records(&m, r);
    printf("SCAN %u APs:", n);
    for (int i = 0; i < m; i++) printf(" ch%d/%d", r[i].primary, r[i].rssi);
    printf("\n");
    esp_wifi_set_channel(CRIG_CHANNEL, WIFI_SECOND_CHAN_NONE);
}

void crig_link_bcast_test(int n)
{
    static const uint8_t bc[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    if (!esp_now_is_peer_exist(bc)) {
        esp_now_peer_info_t p = { 0 };
        memcpy(p.peer_addr, bc, 6);
        p.channel = CRIG_CHANNEL; p.ifidx = WIFI_IF_STA; p.encrypt = false;
        esp_now_add_peer(&p);
    }
    int8_t pw = 0;
    esp_wifi_get_max_tx_power(&pw);
    uint8_t ch = 0; wifi_second_chan_t sc;
    esp_wifi_get_channel(&ch, &sc);
    uint8_t m[6]; esp_wifi_get_mac(WIFI_IF_STA, m);
    printf("BCAST txpower=%d (0.25 dBm) ch=%u mac=%02x:%02x:%02x:%02x:%02x:%02x\n", pw, ch, m[0], m[1], m[2], m[3], m[4], m[5]);
    static const uint8_t junk[8] = { 'B', 'C', 'A', 'S', 'T', 0, 0, 0 };
    for (int i = 0; i < n; i++) { esp_now_send(bc, junk, sizeof junk); vTaskDelay(pdMS_TO_TICKS(20)); }
}
