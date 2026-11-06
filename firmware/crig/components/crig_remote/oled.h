#pragma once
#include <stdbool.h>
bool oled_init(int sda, int scl);
void oled_clear(void);
void oled_text(int col, int row, const char *s);   /* 21 columns x 8 rows, 5x7 font, upper case */
void oled_flush(void);
void oled_off(void);                                /* display off + charge pump off (before deep sleep) */
