/*
 * events.c — what each incoming pulse or message does to the controller's inputs.
 * The state machine itself (fsm.c) decides what the lights do about it.
 */
#include "events.h"
#include "fsm.h"

/* ===================================== pulses ===================================== */

/* PULSE_RAILWAY_EVENT from X1. Every one of them is also a heartbeat. */
static void on_railway_pulse(ctrl_t *c, int32_t value)
{
    if (unpack_railway_sender(value) != NODE_X1)
        return; /* not from X1: ignore */

    switch (unpack_railway_event(value))
    {
    case RAIL_EVENT_APPROACH:
        c->rail = RAIL_APPROACHING;
        break;
    case RAIL_EVENT_CROSSING:
        c->rail = RAIL_CROSSING;
        break;
    case RAIL_EVENT_CLEAR:
        c->rail = RAIL_CLEAR;
        break;
    default:
        c->rail = RAIL_FAULT;
        break; /* RAIL_EVENT_FAULT, or garbage:
                  uncertain = unsafe */
    }
    /* unpack_railway_sequence(value) is available if you want to log missed events. Each
     * pulse carries X1's whole current state, so a missed one is corrected by the next. */
    c->x1_link_ok = true;
    if (!c->standalone)
        oneshot_arm(&c->x1_watchdog, X1_WATCHDOG_MS);
    fsm_rail_changed(c);
}

static void on_x1_watchdog(ctrl_t *c)
{
    c->x1_link_ok = false; /* heard nothing from X1: assume a train (fail-safe) */
    fsm_rail_changed(c);
}

static void on_key(ctrl_t *c, int key)
{
    switch (key)
    {
    case KEY_PED:
        if (c->phase != PHASE_PED_WALK && c->phase != PHASE_PED_CLEAR)
            c->ped_latched = true; /* latch: repeated presses have no extra effect */
        break;
    case KEY_SIDE_VEHICLE:
        c->side_demand = true;
        break;
    case KEY_INJECT_FAULT:
        c->inject_conflict = true;
        break;
    case KEY_QUIT:
        c->quit = true;
        return;
    default:
        return;
    }
    fsm_refresh(c);
}

void events_handle_pulse(ctrl_t *c, const struct _pulse *p)
{
    switch (p->code)
    {
    case _PULSE_CODE_DISCONNECT:
        /* A client (central, X1, ...) closed its connection or died. Release its server
         * connection and KEEP RUNNING — the lab template leaves its loop here; an
         * intersection must never stop because another node went away. */
        ConnectDetach(p->scoid);
        break;
    case _PULSE_CODE_UNBLOCK: /* we always reply at once, nothing to do */
        break;
    case PULSE_RAILWAY_EVENT:
        on_railway_pulse(c, p->value.sival_int);
        break;
    case PC_PHASE_TIMER:
        if (oneshot_is_due(&c->phase_timer))
            fsm_on_phase_timer(c);
        break;
    case PC_X1_WATCHDOG:
        if (oneshot_is_due(&c->x1_watchdog))
            on_x1_watchdog(c);
        break;
    case PC_KEY:
        on_key(c, p->value.sival_int);
        break;
    default: /* e.g. _PULSE_CODE_COIDDEATH: uplink handles it */
        break;
    }
}

/* ==================================== messages ==================================== */

int32_t events_handle_message(ctrl_t *c, const void *msg, int len)
{
    const msg_header_t *h = msg;

    if (len < (int)sizeof *h)
        return RESULT_INVALID;

    switch (h->type)
    {
    case MSG_SET_MODE:
    {
        const set_mode_msg_t *m = msg;
        if (len < (int)sizeof *m)
            return RESULT_INVALID;
        if (m->mode != MODE_FIXED && m->mode != MODE_SENSOR)
            return RESULT_INVALID;
        c->pending_mode = (operation_mode_t)m->mode; /* applied at the next all-red */
        c->mode_change_pending = (c->pending_mode != c->mode);
        fsm_refresh(c);
        return RESULT_OK;
    }
    case MSG_OVERRIDE:
    {
        const override_msg_t *m = msg;
        if (len < (int)sizeof *m)
            return RESULT_INVALID;
        if (m->override > OVERRIDE_SIDE)
            return RESULT_INVALID;
        c->override = (override_t)m->override;
        fsm_override_changed(c);
        return RESULT_OK;
    }
    case MSG_CLEAR_FAULT:
        if (len < (int)sizeof(clear_fault_msg_t))
            return RESULT_INVALID;
        fsm_clear_fault(c);
        return RESULT_OK;
    default:
        return -1; /* not for this node */
    }
}
