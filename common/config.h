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

#endif
