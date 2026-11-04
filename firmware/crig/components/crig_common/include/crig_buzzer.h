/* Active buzzer, direct GPIO drive. Patterns: on/off durations in ms, non-blocking. */
#pragma once
#include <stdint.h>
void crig_buzz_init(int gpio);
void crig_buzz_pattern(const uint16_t *on_off_ms, int n);   /* n <= 8 */
void crig_buzz_stop(void);
