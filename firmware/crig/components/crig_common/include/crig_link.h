/* ESP-NOW link to the single bound peer (spec §7.1, §7.4). */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

typedef void (*crig_rx_cb_t)(const uint8_t *data, int len, int8_t rssi);   /* called in the Wi-Fi task */

esp_err_t crig_link_init(const uint8_t peer_mac[6], crig_rx_cb_t cb);
esp_err_t crig_link_send(const uint8_t *data, size_t len);
uint32_t  crig_link_foreign_count(void);    /* frames from other MACs, dropped */
uint32_t  crig_link_tx_fail(void);        /* send callback: not acknowledged by the peer MAC layer */
uint32_t  crig_link_tx_delivered(void);
const uint8_t *crig_link_last_foreign(void);
void crig_link_scan(void);                 /* diagnostic: Wi-Fi scan, prints AP channels/RSSI */
void crig_link_bcast_test(int n);          /* diagnostic: n unencrypted broadcast frames */
