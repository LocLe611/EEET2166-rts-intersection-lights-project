
#include <stdio.h>

#include "events.h"
#include "fsm.h"

static void on_railway_pulse(ctrl_t *c, int32_t value)
{
    bool was_up = c->x1_link_ok;

    if (unpack_railway_sender(value) != NODE_X1)
        return;

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
        break;
    }
    
    c->x1_link_ok = true;
    if (!c->standalone)
        oneshot_arm(&c->x1_watchdog, X1_WATCHDOG_MS);

    if (!was_up)
        printf("[%s] X1 link restored.\n", c->name);

    fsm_rail_changed(c);
}

static void on_x1_watchdog(ctrl_t *c)
{
    if (!c->x1_link_ok)
        return;

    c->x1_link_ok = false;
    printf("[%s] X1 heartbeat lost - R3 held RED.\n", c->name);
    fsm_rail_changed(c);
}

static void on_key(ctrl_t *c, int key)
{
    switch (key)
    {
    case KEY_PED:
        if (c->phase != PHASE_PED_WALK && c->phase != PHASE_PED_CLEAR)
            c->ped_latched = true;
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
        ConnectDetach(p->scoid);
        break;
    case _PULSE_CODE_UNBLOCK:
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
    default:
        break;
    }
}

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
        c->pending_mode = (operation_mode_t)m->mode;
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
        return -1;
    }
}
