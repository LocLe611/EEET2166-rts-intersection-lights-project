#ifndef TL_TYPES_H
#define TL_TYPES_H
/* tl_types.h — shared enums. Same numeric meaning on every node. */

typedef enum { RED, YELLOW, GREEN, FLASH_YELLOW } colour_t;
typedef enum { DONT_WALK, WALK, FLASH_DONT_WALK } ped_t;

typedef enum {
    PH_STARTUP,                  /* all red on boot */
    PH_NS_GREEN, PH_NS_YELLOW, PH_ALL_RED_1,
    PH_EW_GREEN, PH_EW_YELLOW, PH_ALL_RED_2,
    PH_FLASH,                    /* fault mode */
    PH_NS_WALK, PH_NS_PED_CLEAR, /* NS green + ped WALK / flashing DONT WALK */
    PH_EW_WALK, PH_EW_PED_CLEAR  /* EW green + ped WALK / flashing DONT WALK */
} phase_t;

typedef enum { MODE_FIXED, MODE_SENSOR, MODE_OVERRIDE_NS, MODE_OVERRIDE_EW, MODE_FAULT } tl_mode_t;

typedef enum {                   /* sent as pulse code _PULSE_CODE_MINAVAIL + type on QNX */
    EV_TIMER = 1, EV_CAR_NS, EV_CAR_EW, EV_PED_NS, EV_PED_EW,
    EV_TRAIN,                    /* value: train_state_t */
    EV_CMD_MODE, EV_CMD_OVERRIDE, EV_CMD_TIMING,
    EV_QUIT
} ev_type_t;

/* Railway crossing as seen by an intersection. Anything but CLEAR holds R3 red. */
typedef enum { TRAIN_CLEAR = 0, TRAIN_DOWN, TRAIN_FAULT, TRAIN_UNKNOWN } train_state_t;

typedef struct { ev_type_t type; int value; } event_t;

typedef struct { colour_t ns, ew; ped_t ns_ped, ew_ped; } lights_t;

#endif /* TL_TYPES_H */
