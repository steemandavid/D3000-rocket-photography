/* Host unit tests for crig_proto and crig_armdec.  Build+run: tests/host/run.sh */
#include <stdio.h>
#include <string.h>
#include "crig_proto.h"
#include "crig_armdec.h"

static int fails, checks;
#define CHECK(c, msg) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

static void t_crc(void)
{
    CHECK(crig_crc16((const uint8_t *)"123456789", 9) == 0x29B1, "CRC-16/CCITT-FALSE check value");
}

static void t_proto(void)
{
    uint8_t f[CRIG_MAX_FRAME];
    crig_pkt_t p = { .type = CRIG_T_SHOOT, .seq = 0x1234, .ack_seq = 0xBEEF, .len = 2, .payload = { CRIG_SRC_TRIG, 1 } }, q;
    size_t n = crig_encode(&p, f);
    CHECK(n == 14, "SHOOT frame length 14");
    CHECK(f[0] == 0x43 && f[1] == 0x52 && f[2] == 0x49 && f[3] == 0x47, "magic little-endian 'CRIG'");
    CHECK(crig_decode(f, n, &q), "decode own frame");
    CHECK(q.type == CRIG_T_SHOOT && q.seq == 0x1234 && q.ack_seq == 0xBEEF && q.payload[0] == 2 && q.payload[1] == 1, "fields round-trip");
    f[11] ^= 1; CHECK(!crig_decode(f, n, &q), "bad payload/crc rejected"); f[11] ^= 1;
    f[0] ^= 1; CHECK(!crig_decode(f, n, &q), "bad magic rejected"); f[0] ^= 1;
    f[4] = CRIG_VERSION + 1;  CHECK(!crig_decode(f, n, &q), "bad version rejected"); f[4] = CRIG_VERSION;
    CHECK(!crig_decode(f, n - 1, &q), "short frame rejected");
    f[5] = 0x03; CHECK(!crig_decode(f, n, &q), "unknown/removed type 0x03 rejected"); f[5] = CRIG_T_SHOOT;
    crig_pkt_t hb = { .type = CRIG_T_HEARTBEAT, .seq = 7, .len = 3, .payload = { 1, 3896 & 0xFF, 3896 >> 8 } };
    n = crig_encode(&hb, f);
    CHECK(n == 15 && crig_decode(f, n, &q) && q.len == 3 && q.payload[0] == 1 && (q.payload[1] | q.payload[2] << 8) == 3896,
          "HEARTBEAT v2: prepare + remote batt round-trip");
    hb.len = 1; n = crig_encode(&hb, f); CHECK(!crig_decode(f, n, &q), "old 1-byte HEARTBEAT rejected");
    crig_status_t s = { NODE_LOCKOUT, SH_FULL, 0x21, 7412, -57, 15000 }, t;
    crig_pkt_t st = { .type = CRIG_T_STATUS, .seq = 1, .ack_seq = 2, .len = CRIG_STATUS_LEN };
    crig_status_pack(&s, st.payload);
    n = crig_encode(&st, f);
    CHECK(n <= 32, "STATUS frame <= 32 bytes");
    CHECK(crig_decode(f, n, &q), "decode STATUS");
    crig_status_unpack(q.payload, &t);
    CHECK(memcmp(&s, &t, sizeof s) == 0 || (t.state == s.state && t.shutter == s.shutter && t.fault == s.fault &&
          t.vbat_mv == s.vbat_mv && t.rssi == s.rssi && t.burst_ms == s.burst_ms), "STATUS round-trip");
}

/* run a pattern of LED-on edges with period p_us from t0 for dur_us; returns the time after the last edge */
static int64_t pattern(armdec_t *d, int64_t t0, int64_t p_us, int64_t dur_us)
{
    int64_t t = t0;
    for (; t < t0 + dur_us; t += p_us) armdec_edge(d, t);
    return t;
}

static void t_armdec(void)
{
    armdec_t d;
    armdec_init(&d, 2500000);
    CHECK(armdec_poll(&d, 0) == ARM_NONE, "NONE at start");
    int64_t t = pattern(&d, 1000000, 1000000, 3000000);            /* edges at 1, 2, 3 s */
    CHECK(armdec_poll(&d, t - 1000000 + 10000) == ARM_CONNECTED, "1 Hz -> CONNECTED");
    int64_t ta = t;
    t = pattern(&d, ta, 100000, 1000000);                          /* 10 Hz for 1 s */
    /* ARMED after 3 fast periods: edge #4 at ta + 300 ms */
    armdec_t d2 = d;
    CHECK(armdec_poll(&d, t - 100000 + 5000) == ARM_ARMED, "10 Hz -> ARMED");
    armdec_t e; armdec_init(&e, 2500000);
    pattern(&e, 0, 1000000, 3000000);
    armdec_edge(&e, 3000000); armdec_edge(&e, 3100000); armdec_edge(&e, 3200000);
    CHECK(armdec_poll(&e, 3210000) != ARM_ARMED, "2 fast periods not yet ARMED");
    armdec_edge(&e, 3300000);
    CHECK(armdec_poll(&e, 3310000) == ARM_ARMED, "3 fast periods -> ARMED (~0.3 s)");
    (void)d2;
    /* back to 1 Hz: armed drops 300 ms after the last fast edge */
    int64_t last_fast = t - 100000;
    CHECK(armdec_poll(&d, last_fast + 250000) == ARM_ARMED, "still ARMED 250 ms after last fast edge");
    CHECK(armdec_poll(&d, last_fast + 350000) == ARM_CONNECTED, "CONNECTED 350 ms after last fast edge");
    /* pattern stops (stuck high or low): LOST after 2.5 s */
    CHECK(armdec_poll(&d, last_fast + 2400000) == ARM_CONNECTED, "CONNECTED until 2.5 s without edges");
    CHECK(armdec_poll(&d, last_fast + 2600000) == ARM_LOST, "LOST after 2.5 s without edges");
    /* RLC as built: a pattern change starts in its LOW phase, so after a disarm the first
       1 Hz pulse starts ~950 ms after the switch (~1.05 s after the last fast pulse) */
    armdec_t r; armdec_init(&r, 2500000);
    int64_t tr = pattern(&r, 0, 1000000, 3000000);
    tr = pattern(&r, tr - 1000000 + 1000000, 100000, 1000000);
    int64_t lf = tr - 100000;
    CHECK(armdec_poll(&r, lf + 350000) == ARM_CONNECTED, "LOW-start disarm: CONNECTED at 0.35 s");
    armdec_edge(&r, lf + 1050000);
    CHECK(armdec_poll(&r, lf + 1100000) == ARM_CONNECTED, "LOW-start disarm: CONNECTED after 1st slow pulse");
    armdec_edge(&r, lf + 2050000);
    CHECK(armdec_poll(&r, lf + 2100000) == ARM_CONNECTED, "LOW-start disarm: never ARMED/LOST");
    /* stuck while armed: TRIG gating must drop within 0.3 s */
    armdec_t g; armdec_init(&g, 2500000);
    int64_t tg = pattern(&g, 0, 100000, 1000000);
    CHECK(armdec_poll(&g, tg - 100000 + 290000) == ARM_ARMED, "armed until 0.3 s after pattern stops");
    CHECK(armdec_poll(&g, tg - 100000 + 310000) != ARM_ARMED, "not armed 0.31 s after pattern stops");
    /* glitches and wrong periods never arm */
    armdec_t h; armdec_init(&h, 2500000);
    for (int64_t x = 0; x < 2000000; x += 37000) armdec_edge(&h, x);           /* 27 Hz noise */
    CHECK(armdec_poll(&h, 2000000) != ARM_ARMED, "27 Hz noise does not arm");
    armdec_t k; armdec_init(&k, 2500000);
    for (int64_t x = 0; x < 2000000; x += 5000) armdec_edge(&k, x);            /* 200 Hz glitches */
    CHECK(armdec_poll(&k, 2000000) == ARM_NONE, "200 Hz glitches ignored");
    armdec_t m; armdec_init(&m, 2500000);
    for (int64_t x = 0; x < 3000000; x += 500000) armdec_edge(&m, x);          /* 2 Hz: neither */
    CHECK(armdec_poll(&m, 3000000) == ARM_NONE, "2 Hz is neither pattern");
}

int main(void)
{
    t_crc(); t_proto(); t_armdec();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
