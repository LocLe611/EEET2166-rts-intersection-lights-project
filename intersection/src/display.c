#include <stdio.h>

#include "display.h"
#include "intersection.h"
#include "status_box.h"
#include "console.h"

static const char *phase_name(unsigned p)
{
    static const char *const names[PHASE_COUNT] = {
        "ALL_RED",
        "MAIN_GREEN",
        "MAIN_YELLOW",
        "SIDE_GREEN",
        "SIDE_YELLOW",
        "PED_WALK",
        "PED_CLEAR",
        "FAULT"
    };

    return p < PHASE_COUNT ? names[p] : "?";
}

static const char *light_name(unsigned light, bool fault)
{
    if (fault)
        return "FLASH RED";

    switch (light) {
    case LIGHT_RED:
        return "RED";
    case LIGHT_YELLOW:
        return "YELLOW";
    case LIGHT_GREEN:
        return "GREEN";
    default:
        return "?";
    }
}

static const char *light_colour(unsigned light, bool fault)
{
    if (fault)
        return UI_RED;

    switch (light) {
    case LIGHT_GREEN:
        return UI_GREEN;
    case LIGHT_YELLOW:
        return UI_YELLOW;
    default:
        return UI_RED;
    }
}

static const char *ped_name(unsigned ped)
{
    switch (ped) {
    case PED_DONT_WALK:
        return "DON'T WALK";
    case PED_WALK:
        return "WALK";
    case PED_FLASHING:
        return "FLASHING";
    default:
        return "?";
    }
}

static const char *ped_colour(unsigned ped)
{
    switch (ped) {
    case PED_WALK:
        return UI_GREEN;
    case PED_FLASHING:
        return UI_YELLOW;
    default:
        return UI_RED;
    }
}

static const char *rail_name(unsigned rail)
{
    switch (rail) {
    case RAIL_CLEAR:
        return "CLEAR";
    case RAIL_APPROACHING:
        return "APPROACHING";
    case RAIL_GATES_DOWN:
        return "GATES DOWN";
    case RAIL_CROSSING:
        return "CROSSING";
    case RAIL_FAULT:
        return "FAULT";
    default:
        return "?";
    }
}

static void print_status(const ctrl_t *c,
                         const intersection_status_msg_t *s,
                         bool central_up)
{
    struct timespec now;
    bool fault;
    const char *x1_state;
    const char *x1_note;

    ts_now(&now);

    fault = s->intersection_state == INTERSECTION_FAULT;

    x1_state = s->x1_link_ok
                   ? rail_name(s->railway_state)
                   : "NO LINK";

    if (!s->x1_link_ok ||
        s->intersection_state == INTERSECTION_TRAIN_PREEMPT)
        x1_note = " -> R3 HELD RED";
    else
        x1_note = "";

    printf("[%8.2f] [%s][STATUS] %-11s"
           " | MAIN %s=%s%-9s%s"
           " | SIDE %s=%s%-9s%s"
           " | PED GROUP=%s%-10s%s%s"
           " | MODE=%s%s%s"
           " | X1=%s%s"
           " | CENTRAL=%s\n",

           ts_diff_ms(&now, &g_t0) / 1000.0,
           c->name,
           phase_name(s->phase),

           c->main_road,
           light_colour(s->main_light, fault),
           light_name(s->main_light, fault),
           UI_RESET,

           c->side_road,
           light_colour(s->side_light, fault),
           light_name(s->side_light, fault),
           UI_RESET,

           ped_colour(s->ped_light),
           ped_name(s->ped_light),
           UI_RESET,
           s->ped_request ? " (REQ)" : "",

           s->mode == MODE_SENSOR ? "SENSOR" : "FIXED",
           s->override == OVERRIDE_MAIN
               ? " OVR-MAIN"
               : s->override == OVERRIDE_SIDE
                     ? " OVR-SIDE"
                     : "",
           s->side_demand ? " SIDE-WAITING" : "",

           x1_state,
           x1_note,

           central_up ? "UP" : "DOWN");
}

void *display_thread(void *arg)
{
    const ctrl_t *c = arg;
    intersection_status_msg_t status;
    uint32_t seen = 0;
    bool central_up;

    while (status_box_wait(&seen, 0, &status, &central_up))
        print_status(c, &status, central_up);

    return NULL;
}