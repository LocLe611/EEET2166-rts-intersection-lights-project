#ifndef CONFIG_H
#define CONFIG_H

/* =========================
 * QNX node names
 * ========================= */

#define I1_HOSTNAME       "rts-i1"
#define I2_HOSTNAME       "rts-i2"
#define X1_HOSTNAME       "rts-x1"
#define CENTRAL_HOSTNAME  "rts-central"
#define DISPLAY_HOSTNAME  "rts-display"

/* =========================
 * Service names
 * ========================= */

#define I1_SERVICE        "i1_ctrl"
#define I2_SERVICE        "i2_ctrl"
#define X1_SERVICE        "x1_ctrl"
#define CENTRAL_SERVICE   "central_ctrl"
#define DISPLAY_SERVICE   "display_ctrl"

/* =========================
 * X1 timing
 * ========================= */

#define X1_GATE_LOWER_TIME_MS 2000
#define X1_GATE_TIMEOUT_MS    5000

/* =========================
 * IPC timing
 * ========================= */

#define IPC_SEND_TIMEOUT_MS 750

/*
 * Intersection watchdog.
 *
 * If enabled by the intersection controller, this is how
 * long I1/I2 wait before treating X1 communication as lost.
 */
#define X1_WATCHDOG_MS 5000

/*
 * Intersection -> Central status heartbeat/retry period.
 */
#define STATUS_PERIOD_MS 1000

/*
 * Railway events should have a higher priority than the
 * normal local-controller threads.
 */
#define RAILWAY_PULSE_PRIORITY 30

/*
 * Compatibility macro currently used by intersection/main.c.
 *
 * At the moment Central is not implemented, so the service
 * name itself is sufficient for local testing.
 *
 * When we configure GNS for the multi-VM system later,
 * name_open() should use NAME_FLAG_ATTACH_GLOBAL instead.
 */
#define QNET_SERVICE_PATH(host, service) service

#endif
