#ifndef CONFIG_H
#define CONFIG_H

#define I1_HOSTNAME       "rts-i1"
#define I2_HOSTNAME       "rts-i2"
#define X1_HOSTNAME       "rts-x1"
#define CENTRAL_HOSTNAME  "rts-central"
#define DISPLAY_HOSTNAME  "rts-display"

#define I1_SERVICE        "i1_ctrl"
#define I2_SERVICE        "i2_ctrl"
#define X1_SERVICE        "x1_ctrl"
#define CENTRAL_SERVICE   "central_ctrl"
#define DISPLAY_SERVICE   "display_ctrl"

#define X1_GATE_LOWER_TIME_MS 2000
#define X1_GATE_TIMEOUT_MS    5000
#define IPC_SEND_TIMEOUT_MS    750

/* Timing for links between nodes (real ms, NOT divided by TIME_SCALE) */
#define X1_HEARTBEAT_MS     500    /* X1 resends its current state at least this often */
#define X1_WATCHDOG_MS      1500   /* I1/I2: 3 missed heartbeats -> assume train        */
#define STATUS_PERIOD_MS    1000   /* I1/I2 -> central periodic status (heartbeat)      */

/* Above the intersection controller (25): train events jump the queue */
#define RAILWAY_PULSE_PRIORITY  30

/* name_attach(NULL, svc, 0) registers under /dev/name/local, so over QNET it's: */
#define QNET_SERVICE_PATH(host, svc)  "/net/" host "/dev/name/local/" svc

#endif
