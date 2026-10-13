/*
 * display.c — prints the intersection status whenever it changes.
 *
 * Terminal output can block (a telnet pty with a full buffer), which is why printing lives
 * in this thread and not in the controller, and why it happens AFTER the status box is
 * unlocked (status_box_wait copies the status out first).
 */
#include <stdio.h>

#include "display.h"
#include "intersection.h"
#include "status_box.h"

#ifndef USE_COLOUR
#define USE_COLOUR 1 /* ANSI colours (Tera Term supports them); -DUSE_COLOUR=0 to turn off */
#endif

/* ==================================== names ==================================== */

static const char *phase_name(unsigned p)
{
    static const char *const n[PHASE_COUNT] = {
        "ALL_RED", "MAIN_GREEN", "MAIN_YELLOW", "SIDE_GREEN",
        "SIDE_YELLOW", "PED_WALK", "PED_CLEAR", "FAULT"};
    return p < PHASE_COUNT ? n[p] : "?";
}

static const char *light_name(unsigned l, bool fault)
{
    if (fault)
        return "FLASH RED";
    switch (l)
    {
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

static const char *ped_name(unsigned p)
{
    switch (p)
    {
    case PED_DONT_WALK:
        return "DON'T WALK";
    case PED_WALK:
        return "WALK";
    case PED_FLASHING:
        return "FLASH DW";
    default:
        return "?";
    }
}

static const char *rail_name(unsigned r)
{
    switch (r)
    {
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

static const char *colour(uint8_t l)
{
#if USE_COLOUR
    switch (l)
    {
    case LIGHT_GREEN:
        return "\033[32m";
    case LIGHT_YELLOW:
        return "\033[33m";
    default:
        return "\033[31m";
    }
#else
    (void)l;
    return "";
#endif
}

#if USE_COLOUR
#define RESET "\033[0m"
#else
#define RESET ""
#endif

/* ==================================== output ==================================== */

//static void print_status(const ctrl_t *c, const intersection_status_msg_t *s, bool central_up)
//{
//    struct timespec now;
//    bool fault = s->intersection_state == INTERSECTION_FAULT;
//
//    ts_now(&now);
//    printf("[%8.2f] %s %-11s | %s: %s%-6s" RESET " | %s: %s%-6s" RESET
//           " | PED %-10s%s | %s%s%s | X1 %s%s | central %s\n",
//           ts_diff_ms(&now, &g_t0) / 1000.0, c->name, phase_name(s->phase),
//           c->main_road, colour(s->main_light), light_name(s->main_light, fault),
//           c->side_road, colour(s->side_light), light_name(s->side_light, fault),
//           ped_name(s->ped_light), s->ped_request ? " (req)" : "",
//           s->mode == MODE_SENSOR ? "SENSOR" : "FIXED",
//           s->override == OVERRIDE_MAIN ? " OVR-MAIN" : s->override == OVERRIDE_SIDE ? " OVR-SIDE"
//                                                                                     : "",
//           s->side_demand ? " side-waiting" : "",
//           s->x1_link_ok ? rail_name(s->railway_state) : "NO LINK",
//           s->intersection_state == INTERSECTION_TRAIN_PREEMPT ? " -> R3 HELD RED" : "",
//           central_up ? "up" : "down");
//}

static const char *head_state(const head_t *h, const intersection_status_msg_t *s,
                              bool fault, const char **col)
{
    switch (h->group)
    {
    case GRP_MAIN:
        *col = colour(s->main_light);
        return light_name(s->main_light, fault);
    case GRP_SIDE:
        *col = colour(s->side_light);
        return light_name(s->side_light, fault);
    default:
        *col = colour(s->ped_light == PED_WALK ? LIGHT_GREEN : LIGHT_RED);
        return ped_name(s->ped_light);
    }
}

static void print_status(const ctrl_t *c, const intersection_status_msg_t *s, bool central_up)
{
    char buf[1024];
    size_t n = 0;
    struct timespec now;
    bool fault = s->intersection_state == INTERSECTION_FAULT;
    int i;

    ts_now(&now);
    n += snprintf(buf + n, sizeof buf - n,
                  "[%8.2f] %s %-11s | %s%s%s%s | X1 %s%s | central %s\n",
                  ts_diff_ms(&now, &g_t0) / 1000.0, c->name, phase_name(s->phase),
                  s->mode == MODE_SENSOR ? "SENSOR" : "FIXED",
                  s->override == OVERRIDE_MAIN ? " OVR-MAIN"
                  : s->override == OVERRIDE_SIDE ? " OVR-SIDE" : "",
                  s->side_demand ? " side-waiting" : "",
                  s->ped_request ? " ped-req" : "",
                  s->x1_link_ok ? rail_name(s->railway_state) : "NO LINK",
                  s->intersection_state == INTERSECTION_TRAIN_PREEMPT ? " -> R3 HELD RED" : "",
                  central_up ? "up" : "down");

    for (i = 0; i < c->n_heads && n < sizeof buf; i++)
    {
        const char *col;
        const char *txt = head_state(&c->heads[i], s, fault, &col);
        n += snprintf(buf + n, sizeof buf - n, "%s%-10s %s%-10s" RESET "%s",
                      (i % 4 == 0) ? "           " : "",
                      c->heads[i].name, col, txt,
                      (i % 4 == 3 || i == c->n_heads - 1) ? "\n" : "  ");
    }
    fputs(buf, stdout);     /* one write: the uplink's messages can't split the block */
}

void *display_thread(void *arg)
{
    const ctrl_t *c = arg;
    intersection_status_msg_t s;
    uint32_t seen = 0;
    bool central_up;

    while (status_box_wait(&seen, 0, &s, &central_up)) /* sleeps until something changes */
        print_status(c, &s, central_up);
    return NULL;
}
