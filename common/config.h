#ifndef CONFIG_H
#define CONFIG_H

#define I1_HOSTNAME       "inter1"
#define I2_HOSTNAME       "inter2"
#define X1_HOSTNAME       "cross"
#define CENTRAL_HOSTNAME  "central"
#define DISPLAY_HOSTNAME  "display"

#define I1_SERVICE        "i1_ctrl"
#define I2_SERVICE        "i2_ctrl"
#define X1_SERVICE        "x1_ctrl"
#define CENTRAL_SERVICE   "central_ctrl"
#define DISPLAY_SERVICE   "display_ctrl"

#define X1_GATE_TIMEOUT_MS    5000

#define IPC_SEND_TIMEOUT_MS 750

#define X1_WATCHDOG_MS 5000

#define STATUS_PERIOD_MS 1000

#define RAILWAY_PULSE_PRIORITY 30

#define QNET_SERVICE_PATH(host, service) service

#define IPC_USE_GLOBAL_NAMES 1

#endif
