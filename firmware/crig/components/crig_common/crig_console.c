#include "crig_console.h"
#include <string.h>
#include "driver/uart.h"

static char s_buf[96];
static size_t s_len;

void crig_console_init(void)
{
    uart_driver_install(UART_NUM_0, 512, 0, 0, NULL, 0);
}

bool crig_console_poll(char *line, size_t n)
{
    uint8_t c;
    while (uart_read_bytes(UART_NUM_0, &c, 1, 0) == 1) {
        if (c == '\r' || c == '\n') {
            if (s_len == 0) continue;
            s_buf[s_len] = 0;
            strncpy(line, s_buf, n - 1);
            line[n - 1] = 0;
            s_len = 0;
            return true;
        }
        if (s_len < sizeof(s_buf) - 1) s_buf[s_len++] = (char)c;
    }
    return false;
}
