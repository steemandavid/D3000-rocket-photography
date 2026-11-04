#pragma once
#include <stdint.h>
#include "driver/rmt_tx.h"
typedef struct { rmt_channel_handle_t ch; rmt_encoder_handle_t enc; } crig_ws_t;
esp_err_t crig_ws_init(crig_ws_t *w, int gpio);
void      crig_ws_set(crig_ws_t *w, uint8_t r, uint8_t g, uint8_t b);
