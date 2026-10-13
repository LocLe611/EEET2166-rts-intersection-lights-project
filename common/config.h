#ifndef CONFIG_H
#define CONFIG_H
/* config.h — names, timings and tunables. The ONLY place these live. */

/* Service names (name_attach / name_open, global via GNS) */
#define SVC_I1       "i1_ctrl"
#define SVC_I2       "i2_ctrl"
#define SVC_X1       "x1_ctrl"
#define SVC_CENTRAL  "central_ctrl"
#define SVC_DISPLAY  "display_ctrl"
/* Hostnames for scripts/docs: rts-i1, rts-i2, rts-x1, rts-central, rts-display */

/* Link supervision */
#define HEARTBEAT_MS        1000
#define OFFLINE_TIMEOUT_MS  3000
#define RECONNECT_MS        1500

/* Default phase timings (sped up for demo) */
#define T_NS_GREEN_MS   8000
#define T_EW_GREEN_MS   5000
#define T_YELLOW_MS     2000
#define T_ALL_RED_MS    1000
#define T_EXTEND_MS     2000
#define T_WALK_MS       3000
#define T_PED_CLEAR_MS  2000

/* Timing presets for CMD_SET_TIMING. Order = tim_idx_t in tl_msg.h:
 * NS green, EW green, yellow, all-red, extend, walk, ped clear (ms) */
#define TIMING_DEFAULT { T_NS_GREEN_MS, T_EW_GREEN_MS, T_YELLOW_MS, T_ALL_RED_MS, \
                         T_EXTEND_MS, T_WALK_MS, T_PED_CLEAR_MS }
#define TIMING_PEAK    { 12000, 6000, 2000, 1000, 2000, 3000, 2000 }
#define TIMING_NIGHT   {  6000, 4000, 2000, 1000, 3000, 3000, 2000 }

/* Central controller */
#define CENTRAL_LOG_PATH   "/tmp/central.log"   /* /fs/central.log on a BeagleBone */
#define TICK_MS            1000                 /* watchdog + sim clock period    */
#define SIM_MIN_PER_TICK   1                    /* 1 real second = 1 sim minute   */
#define SIM_START_MIN      (6 * 60 + 50)        /* start at 06:50, just before peak */
#define PRIO_HIGH          12                   /* RX server, tick thread         */
#define PRIO_NORMAL        10                   /* links, dashboard, menu         */
#define PRIO_LOW           9                    /* logger                         */

/* Intersection */
#define T_MIN_GREEN_MS     2000                 /* green left after a ped walk    */
#define PRIO_SM            14                   /* state machine (only light writer) */
#define PRIO_COMMS_RX      12
#define PRIO_COMMS_TX      11
#define PRIO_DISPLAY       10

/* Sensors (keyboard-simulated) */
#define SENSOR_DEBOUNCE_MS 250

#endif /* CONFIG_H */
