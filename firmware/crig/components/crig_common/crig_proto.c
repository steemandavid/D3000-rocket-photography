#include "crig_proto.h"
#include <string.h>

uint16_t crig_crc16(const uint8_t *d, size_t n)
{
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)d[i] << 8;
        for (int b = 0; b < 8; b++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
    return c;
}

int crig_payload_len(uint8_t type)
{
    switch (type) {
    case CRIG_T_HEARTBEAT: return 3;            /* prepare, remote batt mV (uint16 LE, 0 = no cell) */
    case CRIG_T_SHOOT:     return 2;            /* source, prepare */
    case CRIG_T_STATUS:    return CRIG_STATUS_LEN;
    default:               return -1;
    }
}

static void put16(uint8_t *o, uint16_t v) { o[0] = v & 0xFF; o[1] = v >> 8; }
static uint16_t get16(const uint8_t *i) { return (uint16_t)(i[0] | (i[1] << 8)); }
static void put32(uint8_t *o, uint32_t v) { for (int k = 0; k < 4; k++) o[k] = (v >> (8 * k)) & 0xFF; }
static uint32_t get32(const uint8_t *i) { return (uint32_t)i[0] | ((uint32_t)i[1] << 8) | ((uint32_t)i[2] << 16) | ((uint32_t)i[3] << 24); }

size_t crig_encode(const crig_pkt_t *p, uint8_t *o)
{
    put32(o, CRIG_MAGIC);
    o[4] = CRIG_VERSION;
    o[5] = p->type;
    put16(o + 6, p->seq);
    put16(o + 8, p->ack_seq);
    size_t n = p->len > CRIG_MAX_PAYLOAD ? CRIG_MAX_PAYLOAD : p->len;
    memcpy(o + CRIG_HDR_LEN, p->payload, n);
    put16(o + CRIG_HDR_LEN + n, crig_crc16(o, CRIG_HDR_LEN + n));
    return CRIG_HDR_LEN + n + 2;
}

bool crig_decode(const uint8_t *in, size_t n, crig_pkt_t *p)
{
    if (n < CRIG_HDR_LEN + 2 || n > CRIG_MAX_FRAME) return false;
    if (get32(in) != CRIG_MAGIC || in[4] != CRIG_VERSION) return false;
    int len = crig_payload_len(in[5]);
    if (len < 0 || n != (size_t)(CRIG_HDR_LEN + len + 2)) return false;
    if (get16(in + CRIG_HDR_LEN + len) != crig_crc16(in, CRIG_HDR_LEN + len)) return false;
    p->type = in[5];
    p->seq = get16(in + 6);
    p->ack_seq = get16(in + 8);
    p->len = (uint8_t)len;
    memcpy(p->payload, in + CRIG_HDR_LEN, len);
    return true;
}

void crig_status_pack(const crig_status_t *s, uint8_t *o)
{
    o[0] = s->state; o[1] = s->shutter; o[2] = s->fault;
    put16(o + 3, s->vbat_mv);
    o[5] = (uint8_t)s->rssi;
    put32(o + 6, s->burst_ms);
}

void crig_status_unpack(const uint8_t *i, crig_status_t *s)
{
    s->state = i[0]; s->shutter = i[1]; s->fault = i[2];
    s->vbat_mv = get16(i + 3);
    s->rssi = (int8_t)i[5];
    s->burst_ms = get32(i + 6);
}

const char *crig_state_name(uint8_t st)
{
    static const char *n[] = { "BOOT", "IDLE", "SHOOTING", "LOCKOUT", "TEST", "CAL", "FAULT" };
    return st < NODE_STATE_N ? n[st] : "?";
}

const char *crig_shutter_name(uint8_t sh)
{
    static const char *n[] = { "REST", "HALF", "FULL" };
    return sh <= SH_FULL ? n[sh] : "?";
}
