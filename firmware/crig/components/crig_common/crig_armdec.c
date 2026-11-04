#include "crig_armdec.h"
#include <string.h>

#define GLITCH_US        20000      /* edges closer than this are ignored */
#define FAST_MIN_US      70000      /* 10 Hz pattern: 70..130 ms periods */
#define FAST_MAX_US      130000
#define SLOW_MIN_US      700000     /* 1 Hz pattern: 0.7..1.3 s periods */
#define SLOW_MAX_US      1300000
#define FAST_NEEDED      3          /* consecutive fast periods to declare ARMED */
#define ARMED_HOLD_US    300000     /* leave ARMED after this long without a fast period */

void armdec_init(armdec_t *d, int64_t lost_us)
{
    memset(d, 0, sizeof(*d));
    d->lost_us = lost_us;
}

void armdec_edge(armdec_t *d, int64_t t)
{
    if (d->has_edge) {
        int64_t p = t - d->last_edge;
        if (p < GLITCH_US) return;
        if (p >= FAST_MIN_US && p <= FAST_MAX_US) {
            if (++d->fast_count >= FAST_NEEDED) { d->armed = true; d->seen = true; }
            if (d->armed) d->last_fast = t;
        } else {
            d->fast_count = 0;
            if (p >= SLOW_MIN_US && p <= SLOW_MAX_US) { d->slow_ok = true; d->seen = true; }
        }
    }
    d->has_edge = true;
    d->last_edge = t;
}

arm_state_t armdec_poll(armdec_t *d, int64_t now)
{
    if (d->armed && now - d->last_fast > ARMED_HOLD_US) { d->armed = false; d->fast_count = 0; }
    if (!d->has_edge || now - d->last_edge > d->lost_us) {
        d->armed = false; d->slow_ok = false; d->fast_count = 0;
        return d->seen ? ARM_LOST : ARM_NONE;
    }
    if (d->armed) return ARM_ARMED;
    if (d->slow_ok) return ARM_CONNECTED;
    return d->seen ? ARM_LOST : ARM_NONE;
}

const char *armdec_name(arm_state_t s)
{
    static const char *n[] = { "NONE", "CONNECTED", "ARMED", "LOST" };
    return (unsigned)s <= ARM_LOST ? n[s] : "?";
}
