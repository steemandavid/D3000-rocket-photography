#include "crig_ws2812.h"
#include "driver/rmt_encoder.h"

esp_err_t crig_ws_init(crig_ws_t *w, int gpio)
{
    rmt_tx_channel_config_t c = {
        .gpio_num = gpio, .clk_src = RMT_CLK_SRC_DEFAULT, .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 48, .trans_queue_depth = 2,
    };
    esp_err_t e = rmt_new_tx_channel(&c, &w->ch);
    if (e != ESP_OK) return e;
    rmt_bytes_encoder_config_t ec = {
        .bit0 = { .level0 = 1, .duration0 = 3, .level1 = 0, .duration1 = 9 },
        .bit1 = { .level0 = 1, .duration0 = 9, .level1 = 0, .duration1 = 3 },
        .flags.msb_first = 1,
    };
    e = rmt_new_bytes_encoder(&ec, &w->enc);
    if (e != ESP_OK) return e;
    return rmt_enable(w->ch);
}

void crig_ws_set(crig_ws_t *w, uint8_t r, uint8_t g, uint8_t b)
{
    if (!w->ch) return;
    uint8_t px[3] = { g, r, b };
    rmt_transmit_config_t t = { .loop_count = 0 };
    if (rmt_transmit(w->ch, w->enc, px, sizeof(px), &t) == ESP_OK) rmt_tx_wait_all_done(w->ch, 10);
}
