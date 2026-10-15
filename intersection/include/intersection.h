#ifndef INTERSECTION_H
#define INTERSECTION_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <sys/neutrino.h>
#include <sys/dispatch.h>

#include "config.h"
#include "message.h"
#include "rutil.h"
#include "timing.h"

#define PRIO_CONTROLLER 25
#define PRIO_UPLINK 15
#define PRIO_DISPLAY 12
#define PRIO_KEYBOARD 10

enum {
    PC_PHASE_TIMER = _PULSE_CODE_MINAVAIL + 10,
    PC_X1_WATCHDOG,
    PC_KEY
};

enum {
    KEY_PED = 1,
    KEY_SIDE_VEHICLE,
    KEY_INJECT_FAULT,
    KEY_QUIT
};

extern struct timespec g_t0;

typedef struct {
    node_id_t node;
    const char *name;
    const char *main_road;
    const char *side_road;
    const char *central_path;
    timing_t t;
    bool standalone;
    uint32_t session_id;

    name_attach_t *attach;
    int self_coid;

    intersection_phase_t phase;
    intersection_phase_t prev_phase;
    intersection_phase_t last_green;
    struct timespec phase_start;
    oneshot_t phase_timer;
    bool rail_was_blocked;
    intersection_status_msg_t last_status;

    bool ped_latched;
    bool side_demand;
    operation_mode_t mode;
    operation_mode_t pending_mode;
    bool mode_change_pending;
    override_t override;
    railway_state_t rail;
    bool x1_link_ok;
    oneshot_t x1_watchdog;
    bool inject_conflict;
    bool quit;
} ctrl_t;

#endif
