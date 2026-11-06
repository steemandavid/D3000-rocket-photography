#include "crig_console.h"
#include <string.h>
#include "driver/uart.h"
#include "hal/usb_serial_jtag_ll.h"

static char s_buf[96];
static size_t s_len;

void crig_console_init(void)
{
    uart_driver_install(UART_NUM_0, 512, 0, 0, NULL, 0);
}

/* One byte from UART0 (USB-UART bridge) or, failing that, from the native USB-Serial/JTAG port.
   The JTAG RX FIFO is read directly (no driver), so the secondary console output keeps working. */
static bool getc_any(uint8_t *c)
{
    if (uart_read_bytes(UART_NUM_0, c, 1, 0) == 1) return true;
    return usb_serial_jtag_ll_rxfifo_data_available() && usb_serial_jtag_ll_read_rxfifo(c, 1) == 1;
}

bool crig_console_poll(char *line, size_t n)
{
    uint8_t c;
    while (getc_any(&c)) {
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
