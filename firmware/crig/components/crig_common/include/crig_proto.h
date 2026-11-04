/* CRIG radio protocol (spec §7.2, §7.3). Pure C, also built by the host unit tests. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define CRIG_MAGIC        0x47495243u   /* "CRIG" */
#define CRIG_VERSION      1
#define CRIG_HDR_LEN      10
#define CRIG_MAX_PAYLOAD  18
#define CRIG_MAX_FRAME    (CRIG_HDR_LEN + CRIG_MAX_PAYLOAD + 2)
#define CRIG_STATUS_LEN   10

enum { CRIG_T_HEARTBEAT = 0x01, CRIG_T_SHOOT = 0x02, CRIG_T_STATUS = 0x81 };
enum { CRIG_SRC_BUTTON = 1, CRIG_SRC_TRIG = 2 };

typedef enum { NODE_BOOT = 0, NODE_IDLE, NODE_SHOOTING, NODE_LOCKOUT, NODE_TEST, NODE_CAL, NODE_FAULT, NODE_STATE_N } crig_node_state_t;
typedef enum { SH_REST = 0, SH_HALF, SH_FULL } crig_shutter_t;

/* STATUS fault bits (spec §7.3; bits 5, 6 are v0.19 extensions) */
#define CRIG_F_PACK_WARN    0x01
#define CRIG_F_PACK_CUTOFF  0x02
#define CRIG_F_ACT_FAULT    0x04
#define CRIG_F_CFG_INVALID  0x08
#define CRIG_F_CAM_ON       0x10    /* 1 = camera on or power control disabled */
#define CRIG_F_NO_PACK      0x20    /* no pack sensed (bench / USB power): no cutoff applied */
#define CRIG_F_PREPARING    0x40    /* node is preparing the camera (FR-N19) */

typedef struct {
    uint8_t  type;
    uint16_t seq;
    uint16_t ack_seq;
    uint8_t  len;
    uint8_t  payload[CRIG_MAX_PAYLOAD];
} crig_pkt_t;

typedef struct {
    uint8_t  state, shutter, fault;
    uint16_t vbat_mv;
    int8_t   rssi;
    uint32_t burst_ms;
} crig_status_t;

uint16_t crig_crc16(const uint8_t *d, size_t n);              /* CRC-16/CCITT-FALSE */
int      crig_payload_len(uint8_t type);                       /* -1 = unknown type */
size_t   crig_encode(const crig_pkt_t *p, uint8_t *out);       /* out >= CRIG_MAX_FRAME; returns length */
bool     crig_decode(const uint8_t *in, size_t n, crig_pkt_t *p);
void     crig_status_pack(const crig_status_t *s, uint8_t *payload);
void     crig_status_unpack(const uint8_t *payload, crig_status_t *s);
const char *crig_state_name(uint8_t st);
const char *crig_shutter_name(uint8_t sh);
