#include "crig_buzzer.h"
#include <string.h>
#include "driver/gpio.h"
#include "esp_timer.h"

static int s_pin = -1;
static esp_timer_handle_t s_tmr;
static uint16_t s_seq[8];
static volatile int s_n, s_i;

static void step(void *arg)
{
    if (s_i >= s_n) { gpio_set_level(s_pin, 0); return; }
    gpio_set_level(s_pin, (s_i & 1) == 0);
    esp_timer_start_once(s_tmr, (uint64_t)s_seq[s_i] * 1000);
    s_i++;
}

void crig_buzz_init(int gpio)
{
    s_pin = gpio;
    gpio_reset_pin(gpio);
    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(gpio, 0);
    esp_timer_create_args_t a = { .callback = step, .name = "buzz" };
    esp_timer_create(&a, &s_tmr);
}

void crig_buzz_pattern(const uint16_t *seq, int n)
{
    if (s_pin < 0) return;
    esp_timer_stop(s_tmr);
    if (n > 8) n = 8;
    memcpy(s_seq, seq, n * sizeof(uint16_t));
    s_n = n; s_i = 0;
    step(NULL);
}

void crig_buzz_stop(void)
{
    if (s_pin < 0) return;
    esp_timer_stop(s_tmr);
    s_n = 0; s_i = 0;
    gpio_set_level(s_pin, 0);
}
