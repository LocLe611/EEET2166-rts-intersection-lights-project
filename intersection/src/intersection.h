/*
 * intersection.h — definitions shared by every file of the I1 / I2 controller:
 *                  priorities, internal pulse codes, and the controller state (ctrl_t).
 */
#ifndef INTERSECTION_H
#define INTERSECTION_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <sys/neutrino.h>
#include <sys/dispatch.h>

#include "config.h"  /* common/: hostnames, service names, timeouts   */
#include "message.h" /* common/: message structs, enums, railway pulse */
#include "rtutil.h"
#include "timing.h"

/* Thread priorities (bigger = more urgent). RAILWAY_PULSE_PRIORITY (config.h) is above all
 * of them: while the main thread handles a pulse it runs at that pulse's priority, and
 * queued pulses are received highest-priority first. */
#define PRIO_CONTROLLER 25
#define PRIO_UPLINK 15
#define PRIO_DISPLAY 12
#define PRIO_KEYBOARD 10

/* Pulse codes used only inside this process. PULSE_RAILWAY_EVENT is _PULSE_CODE_MINAVAIL,
 * so ours start well above it. */
enum
{
    PC_PHASE_TIMER = _PULSE_CODE_MINAVAIL + 10, /* phase timer expired                  */
    PC_X1_WATCHDOG,                             /* nothing heard from X1 for too long   */
    PC_KEY                                      /* simulated sensor, value = KEY_xxx    */
};

/* Simulated-sensor events carried in PC_KEY pulses */
enum
{
    KEY_PED = 1,
    KEY_SIDE_VEHICLE,
    KEY_INJECT_FAULT,
    KEY_QUIT
};

extern struct timespec g_t0; /* program start (set in main.c), for display timestamps */

/* Everything the controller knows. Rule: only the main thread WRITES to it after start-up.
 * The other threads read only the "identity" fields, which never change. */
typedef struct
{
    /* ---- identity: set in main(), read-only afterwards ---- */
    node_id_t node;   /* NODE_I1 / NODE_I2 */
    const char *name; /* "I1" / "I2"       */
    const char *main_road;
    const char *side_road;
    const char *central_path; /* where uplink name_open()s central */
    timing_t t;               /* already divided by TIME_SCALE     */
    bool standalone;          /* -s: don't require X1 heartbeats   */
    name_attach_t *attach;
    int self_coid; /* connection to our own channel (timers, keyboard) */

    /* ---- state machine (fsm.c) ---- */
    intersection_phase_t phase, prev_phase;
    intersection_phase_t last_green; /* PHASE_MAIN_GREEN / PHASE_SIDE_GREEN: which way next */
    struct timespec phase_start;
    oneshot_t phase_timer;
    bool rail_was_blocked;
    intersection_status_msg_t last_status; /* last thing published */

    /* ---- inputs (set by events.c, read by fsm.c) ---- */
    bool ped_latched; /* pedestrian request waiting          */
    bool side_demand; /* vehicle waiting on the side road    */
    operation_mode_t mode, pending_mode;
    bool mode_change_pending;
    override_t override;
    railway_state_t rail; /* last state heard from X1            */
    bool x1_link_ok;      /* X1 heard from within X1_WATCHDOG_MS */
    oneshot_t x1_watchdog;
    bool inject_conflict; /* demo of the safety interlock        */
    bool quit;
} ctrl_t;

#endif
