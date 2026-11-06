/* CRIG timing and threshold parameters (spec §13). Compile-time for now; NVS/console editing later. */
#pragma once

#define T_SHOOT_TIMEOUT_MS      300     /* node: REST if no SHOOT for this long (> 3 x SHOOT period) */
#define T_PREFIRE_MS            100     /* node: HALF before FULL */
#define T_MAX_BURST_MS          15000   /* node: FULL cap -> LOCKOUT (> RLC cam_max_ms 10 s) */
#define T_TEST_FULL_MS          150     /* node: FULL time of a console test cycle */
#define T_LINK_LOST_MS          2000    /* both: no valid packet for this long = NO LINK (indication only) */
#define T_PREPARE_WAKE_MS       300     /* node: HALF pulse when prepare rises (FR-N19) */
#define T_PREPARE_KEEPALIVE_MS  4000    /* node: HALF repeat while prepare = 1 */
#define T_HEARTBEAT_MS          200     /* remote: f_heartbeat 5 Hz */
#define T_SHOOT_PERIOD_MS       50      /* remote: f_shoot 20 Hz */
#define T_TRIG_DEBOUNCE_MS      10      /* remote */
#define T_BTN_DEBOUNCE_MS       20      /* remote */
#define T_STATUS_STALE_MS       1000    /* remote: readiness red if no STATUS for this long */
#define T_ARM_LOST_MS           2500    /* remote: no ARM edge for this long = not connected / LOST */
#define LINK_Q_WINDOW_MS        2000    /* remote: link quality window (FR-R07) */
#define LINK_Q_READY_PCT        50      /* remote: minimum link quality for "ready" */

#define V_NODE_WARN_MV          7000
#define V_NODE_CUTOFF_MV        6600
#define V_REMOTE_WARN_MV        3500
#define V_REMOTE_CUTOFF_MV      3200
#define V_NO_SUPPLY_MV          4500    /* node: below this no pack is connected (bench / USB power; USB back-feeds ~3.7 V onto +VBAT through the buck): no cutoff */
#define V_HYST_MV               200
#define V_REMOTE_NO_CELL_MV     2500    /* remote: below this no cell is connected (switch off / USB); a protected Li-ion never sits this low */
#define T_BATT_CUTOFF_MS        3000    /* remote: cutoff only after this long continuously below v_remote_cutoff */
#define T_BATT_SHUTDOWN_MS      10000   /* remote: deep sleep this long after cutoff */
