/* ARM heartbeat decoder (spec §8.3, §8.6). Pure C, host-testable.
 * Feed the times (us) at which the RLC drives the ARM LED ON (falling edge on IO6, the opto inverts). */
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum { ARM_NONE = 0, ARM_CONNECTED, ARM_ARMED, ARM_LOST } arm_state_t;

typedef struct {
    bool    has_edge, seen, armed, slow_ok;
    int64_t last_edge, last_fast;
    int     fast_count;
    int64_t lost_us;
} armdec_t;

void        armdec_init(armdec_t *d, int64_t lost_us);
void        armdec_edge(armdec_t *d, int64_t t_us);
arm_state_t armdec_poll(armdec_t *d, int64_t now_us);
const char *armdec_name(arm_state_t s);
