
#include <string.h>

#include "fsm.h"
#include "status_box.h"

static void enter_phase(ctrl_t *c, intersection_phase_t p);

static const struct
{
    uint8_t main, side, ped;
} OUTPUTS[PHASE_COUNT] = {
    [PHASE_ALL_RED] = {LIGHT_RED, LIGHT_RED, PED_DONT_WALK},
    [PHASE_MAIN_GREEN] = {LIGHT_GREEN, LIGHT_RED, PED_DONT_WALK},
    [PHASE_MAIN_YELLOW] = {LIGHT_YELLOW, LIGHT_RED, PED_DONT_WALK},
    [PHASE_SIDE_GREEN] = {LIGHT_RED, LIGHT_GREEN, PED_DONT_WALK},
    [PHASE_SIDE_YELLOW] = {LIGHT_RED, LIGHT_YELLOW, PED_DONT_WALK},
    [PHASE_PED_WALK] = {LIGHT_RED, LIGHT_RED, PED_WALK},
    [PHASE_PED_CLEAR] = {LIGHT_RED, LIGHT_RED, PED_FLASHING},
    [PHASE_FAULT] = {LIGHT_RED, LIGHT_RED, PED_DONT_WALK},
};

static uint32_t phase_duration(const ctrl_t *c, intersection_phase_t p)
{
    switch (p)
    {
    case PHASE_MAIN_GREEN:
        return c->t.main_green_ms;
    case PHASE_SIDE_GREEN:
        return c->t.side_green_ms;
    case PHASE_MAIN_YELLOW:
    case PHASE_SIDE_YELLOW:
        return c->t.yellow_ms;
    case PHASE_ALL_RED:
        return c->t.all_red_ms;
    case PHASE_PED_WALK:
        return c->t.walk_ms;
    case PHASE_PED_CLEAR:
        return c->t.ped_clear_ms;
    default:
        return 0;
    }
}

static uint32_t ms_in_phase(const ctrl_t *c)
{
    struct timespec now;
    int64_t d;
    ts_now(&now);
    d = ts_diff_ms(&now, &c->phase_start);
    return d < 0 ? 0 : (uint32_t)d;
}

/* Fail-safe: unknown X1 blocks R3. */
static bool rail_blocked(const ctrl_t *c)
{
    if (!c->x1_link_ok)
        return true;
    return c->rail != RAIL_CLEAR;
}

/* Never allow conflicting greens. */
static bool outputs_safe(uint8_t main, uint8_t side, uint8_t ped)
{
    if (main != LIGHT_RED && side != LIGHT_RED)
        return false;
    if (ped != PED_DONT_WALK && (main != LIGHT_RED || side != LIGHT_RED))
        return false;
    return true;
}

static void update_outputs(ctrl_t *c)
{
    intersection_status_msg_t s;
    uint8_t main = OUTPUTS[c->phase].main;
    uint8_t side = OUTPUTS[c->phase].side;
    uint8_t ped = OUTPUTS[c->phase].ped;

    if (c->inject_conflict)
    {
        c->inject_conflict = false;
        main = side = LIGHT_GREEN;
    }
    if (!outputs_safe(main, side, ped))
    {
        enter_phase(c, PHASE_FAULT);
        return;
    }

    memset(&s, 0, sizeof s);
    s.intersection_state = c->phase == PHASE_FAULT ? INTERSECTION_FAULT
                           : rail_blocked(c)       ? INTERSECTION_TRAIN_PREEMPT
                                                   : INTERSECTION_NORMAL;
    s.mode = (uint8_t)c->mode;
    s.phase = (uint8_t)c->phase;
    s.main_light = main;
    s.side_light = side;
    s.ped_light = ped;
    s.railway_state = (uint8_t)c->rail;
    s.x1_link_ok = c->x1_link_ok;
    s.override = (uint8_t)c->override;
    s.ped_request = c->ped_latched;
    s.side_demand = c->side_demand;

    if (memcmp(&s, &c->last_status, sizeof s) != 0)
    {
        c->last_status = s;
        status_box_publish(&s);
    }
}

static void enter_phase(ctrl_t *c, intersection_phase_t p)
{
    uint32_t ms;

    if (p == PHASE_MAIN_GREEN && rail_blocked(c))
        p = PHASE_FAULT;

    c->prev_phase = c->phase;
    c->phase = p;
    ts_now(&c->phase_start);

    switch (p)
    {
    case PHASE_MAIN_GREEN:
        c->last_green = p;
        break;
    case PHASE_SIDE_GREEN:
        c->last_green = p;
        c->side_demand = false;
        break;
    case PHASE_PED_WALK:
        c->ped_latched = false;
        break;
    case PHASE_ALL_RED:
        
        if (c->mode_change_pending && !rail_blocked(c))
        {
            c->mode = c->pending_mode;
            c->mode_change_pending = false;
        }
        break;
    default:
        break;
    }

    ms = phase_duration(c, p);
    if (ms)
        oneshot_arm(&c->phase_timer, ms);
    else
        oneshot_cancel(&c->phase_timer);

    update_outputs(c);
}

static intersection_phase_t next_after_all_red(const ctrl_t *c)
{
    if (rail_blocked(c))
        return PHASE_SIDE_GREEN;

    if (c->override == OVERRIDE_MAIN)
        return PHASE_MAIN_GREEN;
    if (c->override == OVERRIDE_SIDE)
        return PHASE_SIDE_GREEN;

    if (c->ped_latched && c->prev_phase != PHASE_PED_CLEAR)
        return PHASE_PED_WALK;

    if (c->last_green == PHASE_SIDE_GREEN)
        return PHASE_MAIN_GREEN;
    if (c->mode == MODE_SENSOR && !c->side_demand)
        return PHASE_MAIN_GREEN;
    return PHASE_SIDE_GREEN;
}


static bool keep_green(const ctrl_t *c)
{
    if (c->phase == PHASE_SIDE_GREEN)
        return rail_blocked(c) || c->override == OVERRIDE_SIDE;


    if (rail_blocked(c))
        return false;
    if (c->override == OVERRIDE_MAIN)
        return true;
    if (c->override == OVERRIDE_SIDE)
        return false;

    return c->mode == MODE_SENSOR && !c->mode_change_pending && !c->side_demand && !c->ped_latched;
}

static void finish_green(ctrl_t *c)
{
    if (keep_green(c))
    {
        oneshot_arm(&c->phase_timer, c->t.extend_ms);
        return;
    }
    enter_phase(c, c->phase == PHASE_MAIN_GREEN ? PHASE_MAIN_YELLOW : PHASE_SIDE_YELLOW);
}

static void end_green_asap(ctrl_t *c)
{
    uint32_t in_phase;
    if (c->phase != PHASE_MAIN_GREEN && c->phase != PHASE_SIDE_GREEN)
        return;
    in_phase = ms_in_phase(c);
    if (in_phase >= c->t.min_green_ms)
        finish_green(c);
    else
        oneshot_arm(&c->phase_timer, c->t.min_green_ms - in_phase);
}

void fsm_start(ctrl_t *c)
{
    
    c->mode = c->pending_mode = MODE_FIXED;
    c->mode_change_pending = false;
    c->override = OVERRIDE_NONE;
    c->rail = RAIL_CLEAR;
    c->x1_link_ok = c->standalone;
    c->ped_latched = c->side_demand = false;

    c->phase = c->prev_phase = PHASE_ALL_RED;
    c->last_green = PHASE_SIDE_GREEN;
    c->rail_was_blocked = rail_blocked(c);
    memset(&c->last_status, 0xFF, sizeof c->last_status);

    enter_phase(c, PHASE_ALL_RED);
}

void fsm_on_phase_timer(ctrl_t *c)
{
    switch (c->phase)
    {
    case PHASE_MAIN_GREEN:
    case PHASE_SIDE_GREEN:
        finish_green(c);
        break;
    case PHASE_MAIN_YELLOW:
    case PHASE_SIDE_YELLOW:
        enter_phase(c, PHASE_ALL_RED);
        break;
    case PHASE_ALL_RED:
        enter_phase(c, next_after_all_red(c));
        break;
    case PHASE_PED_WALK:
        enter_phase(c, PHASE_PED_CLEAR);
        break;
    case PHASE_PED_CLEAR:
        enter_phase(c, PHASE_ALL_RED);
        break;
    default:
        break;
    }
}

void fsm_rail_changed(ctrl_t *c)
{
    bool blocked = rail_blocked(c);

    if (blocked && !c->rail_was_blocked)
    {

        switch (c->phase)
        {
        case PHASE_MAIN_GREEN:
            end_green_asap(c);
            break;
        case PHASE_PED_WALK:
            enter_phase(c, PHASE_PED_CLEAR);
            break;
        default:
            break; 
        }
    }
    
    c->rail_was_blocked = blocked;
    update_outputs(c);
}

void fsm_override_changed(ctrl_t *c)
{
    if ((c->override == OVERRIDE_MAIN && c->phase == PHASE_SIDE_GREEN) ||
        (c->override == OVERRIDE_SIDE && c->phase == PHASE_MAIN_GREEN))
        end_green_asap(c);
    update_outputs(c);
}

void fsm_clear_fault(ctrl_t *c)
{
    if (c->phase == PHASE_FAULT)
        enter_phase(c, PHASE_ALL_RED);
}

void fsm_refresh(ctrl_t *c)
{
    update_outputs(c);
}
