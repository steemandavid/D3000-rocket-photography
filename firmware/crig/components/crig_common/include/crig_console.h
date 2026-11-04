/* Line console on UART0 (the dev board's USB-UART bridge). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
void crig_console_init(void);
bool crig_console_poll(char *line, size_t n);   /* non-blocking; true when a full line was received */
