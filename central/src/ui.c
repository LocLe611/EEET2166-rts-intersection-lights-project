#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "central.h"
#include "console.h"

const char *node_str(int n)
{
    switch (n) {
    case NODE_I1: return "I1";
    case NODE_I2: return "I2";
    case NODE_X1: return "X1";
    case NODE_CENTRAL: return "CENTRAL";
    case NODE_DISPLAY: return "DISPLAY";
    default: return "?";
    }
}

const char *side_road(int n)
{
    return n == NODE_I1 ? "R1" : n == NODE_I2 ? "R2" : "?";
}

const char *phase_str(int p)
{
    switch (p) {
    case PHASE_ALL_RED: return "ALL_RED";
    case PHASE_MAIN_GREEN: return "MAIN_GREEN";
    case PHASE_MAIN_YELLOW: return "MAIN_YELLOW";
    case PHASE_SIDE_GREEN: return "SIDE_GREEN";
    case PHASE_SIDE_YELLOW: return "SIDE_YELLOW";
    case PHASE_PED_WALK: return "PED_WALK";
    case PHASE_PED_CLEAR: return "PED_CLEAR";
    case PHASE_FAULT: return "FAULT";
    default: return "?";
    }
}

const char *light_str(int l)
{
    switch (l) {
    case LIGHT_RED: return "RED";
    case LIGHT_YELLOW: return "YELLOW";
    case LIGHT_GREEN: return "GREEN";
    default: return "?";
    }
}

const char *ped_str(int p)
{
    switch (p) {
    case PED_DONT_WALK: return "DONT_WALK";
    case PED_WALK: return "WALK";
    case PED_FLASHING: return "FLASHING";
    default: return "?";
    }
}

const char *mode_str(int m)
{
    switch (m) {
    case MODE_FIXED: return "FIXED";
    case MODE_SENSOR: return "SENSOR";
    default: return "?";
    }
}

const char *override_str(int o)
{
    switch (o) {
    case OVERRIDE_NONE: return "NONE";
    case OVERRIDE_MAIN: return "R3/MAIN";
    case OVERRIDE_SIDE: return "SIDE";
    default: return "?";
    }
}

const char *istate_str(int s)
{
    switch (s) {
    case INTERSECTION_NORMAL: return "NORMAL";
    case INTERSECTION_TRAIN_PREEMPT: return "TRAIN_PREEMPT";
    case INTERSECTION_FAULT: return "FAULT";
    default: return "?";
    }
}

const char *rail_str(int r)
{
    switch (r) {
    case RAIL_CLEAR: return "CLEAR";
    case RAIL_APPROACHING: return "APPROACHING";
    case RAIL_GATES_DOWN: return "GATES_DOWN";
    case RAIL_CROSSING: return "CROSSING";
    case RAIL_FAULT: return "FAULT";
    default: return "?";
    }
}

const char *gate_str(int g)
{
    switch (g) {
    case GATE_UP: return "UP";
    case GATE_LOWERING: return "LOWERING";
    case GATE_DOWN: return "DOWN";
    case GATE_RAISING: return "RAISING";
    case GATE_UNKNOWN: return "UNKNOWN";
    default: return "?";
    }
}

const char *msgtype_str(int t)
{
    switch (t) {
    case MSG_SET_MODE: return "SET_MODE";
    case MSG_OVERRIDE: return "OVERRIDE";
    case MSG_CLEAR_FAULT: return "CLEAR_FAULT";
    default: return "?";
    }
}

const char *result_str(int r)
{
    switch (r) {
    case RESULT_OK: return "OK";
    case RESULT_REJECTED: return "REJECTED";
    case RESULT_INVALID: return "INVALID";
    case RESULT_BUSY: return "BUSY";
    default: return "?";
    }
}

static const char *cmdres_str(int r)
{
    switch (r) {
    case CMDRES_PENDING: return "PENDING";
    case CMDRES_ACK: return "ACK";
    case CMDRES_REJECTED: return "REJECTED";
    case CMDRES_RETRY: return "RETRY";
    default: return "NONE";
    }
}

static void print_link(link_t link)
{
    if (link == LINK_ONLINE)
        printf(UI_GREEN "ONLINE" UI_RESET);
    else
        printf(UI_RED "OFFLINE" UI_RESET);
}

static void print_light(int light)
{
    const char *colour = light == LIGHT_GREEN ? UI_GREEN :
                         light == LIGHT_YELLOW ? UI_YELLOW : UI_RED;
    printf("%s%s%s", colour, light_str(light), UI_RESET);
}

static void print_ped(int ped)
{
    const char *colour = ped == PED_WALK ? UI_GREEN :
                         ped == PED_FLASHING ? UI_YELLOW : UI_RED;
    printf("%s%s%s", colour, ped_str(ped), UI_RESET);
}

static void print_last_cmd(const node_info_t *n)
{
    if (n->last_cmd_seq == 0) {
        printf("none");
        return;
    }
    printf("#%u %s %s", (unsigned)n->last_cmd_seq,
           msgtype_str(n->last_cmd_type), cmdres_str(n->last_cmd_res));
}

static void print_intersection(int id, const node_info_t *n)
{
    printf("\n%s  [", node_str(id));
    print_link(n->link);
    printf("]\n");

    if (!n->have_data) {
        printf("  Status   : waiting for first update\n");
        return;
    }

    printf("  State    : %s | Mode %s | Phase %s\n",
           istate_str(n->ist.intersection_state),
           mode_str(n->ist.mode), phase_str(n->ist.phase));
    printf("  Signals  : R3=");
    print_light(n->ist.main_light);
    printf(" | %s=", side_road(id));
    print_light(n->ist.side_light);
    printf(" | Ped=");
    print_ped(n->ist.ped_light);
    printf("\n");
    printf("  Railway  : %s | X1 link %s | Override %s\n",
           rail_str(n->ist.railway_state),
           n->ist.x1_link_ok ? "OK" : "LOST",
           override_str(n->ist.override));
    printf("  Demand   : pedestrian=%s | side-road=%s\n",
           n->ist.ped_request ? "YES" : "NO",
           n->ist.side_demand ? "YES" : "NO");
    printf("  Command  : ");
    print_last_cmd(n);
    printf("\n");
}

static void print_x1(const node_info_t *x)
{
    printf("\nX1  [");
    print_link(x->link);
    printf("]\n");

    if (!x->have_data) {
        printf("  Status   : waiting for first update\n");
        return;
    }

    printf("  Railway  : %s | Gate %s | Warning %s\n",
           rail_str(x->rail.railway_state), gate_str(x->rail.gate_state),
           x->rail.warning_lights_on ? "ON" : "OFF");
    printf("  Train    : ");
    if (x->rail.train_signal == TRAIN_SIGNAL_GREEN)
        printf(UI_GREEN "GREEN" UI_RESET);
    else
        printf(UI_RED "RED" UI_RESET);
    printf(" | Fault flags 0x%08X\n", (unsigned)x->rail.fault_flags);
}

static void print_status(void)
{
    node_info_t copy[NODE_SLOTS];
    unsigned alarms;
    unsigned sim_min;
    int schedule;
    int pattern;

    pthread_rwlock_rdlock(&g_c.lock);
    memcpy(copy, g_c.node, sizeof copy);
    alarms = g_c.alarms;
    sim_min = g_c.sim_min;
    schedule = g_c.sched_enabled;
    pattern = g_c.pattern;
    pthread_rwlock_unlock(&g_c.lock);

    printf("\n%s\n", UI_RULE);
    printf(" SYSTEM STATUS | %02u:%02u | %s | Schedule %s\n",
           sim_min / 60, sim_min % 60,
           pattern_str(pattern), schedule ? "ON" : "OFF");
    if (alarms)
        printf(UI_RED " ACTIVE FAULT MASK: 0x%X\n" UI_RESET, alarms);

    print_intersection(NODE_I1, &copy[NODE_I1]);
    print_intersection(NODE_I2, &copy[NODE_I2]);
    print_x1(&copy[NODE_X1]);

    printf("\nDisplay [");
    print_link(copy[NODE_DISPLAY].link);
    printf("]\n%s\n", UI_RULE);
}

static void do_mode(node_id_t n, operation_mode_t mode)
{
    cmd_t c;

    pthread_rwlock_wrlock(&g_c.lock);
    g_c.node[n].desired_mode = mode;
    g_c.node[n].policy_valid = 1;
    g_c.node[n].sched_paused = 1;
    pthread_rwlock_unlock(&g_c.lock);

    build_set_mode(&c, mode);
    log_msg("Operator: %s mode -> %s", node_str(n), mode_str(mode));
    cmdq_push(n, &c);
}

static void do_override(node_id_t n, override_t ovr)
{
    cmd_t c;

    pthread_rwlock_wrlock(&g_c.lock);
    g_c.node[n].desired_override = ovr;
    g_c.node[n].policy_valid = 1;
    if (ovr == OVERRIDE_NONE && g_c.sched_enabled)
        g_c.pattern = -1;
    pthread_rwlock_unlock(&g_c.lock);

    build_override(&c, ovr);
    log_msg("Operator: %s override -> %s", node_str(n), override_str(ovr));
    cmdq_push(n, &c);
}

static void do_clear_fault(node_id_t n)
{
    cmd_t c;
    build_clear_fault(&c);
    log_msg("Operator: clear fault -> %s", node_str(n));
    cmdq_push(n, &c);
}

static int read_choice(const char *prompt, char *out)
{
    char line[64];
    size_t i;

    printf("%s", prompt);
    fflush(stdout);
    if (fgets(line, sizeof line, stdin) == NULL)
        return -1;

    for (i = 0; line[i] != '\0'; ++i) {
        if (!isspace((unsigned char)line[i])) {
            *out = (char)tolower((unsigned char)line[i]);
            return 0;
        }
    }

    *out = '\0';
    return 0;
}

static int choose_intersections(void)
{
    char c;

    if (read_choice("Target [1] I1  [2] I2  [b] both: ", &c) < 0)
        return 0;
    if (c == '1') return 1;
    if (c == '2') return 2;
    if (c == 'b') return 3;
    printf("Invalid target.\n");
    return 0;
}

static void print_menu(void)
{
    printf("\nCENTRAL COMMANDS\n");
    printf("  1  Set FIXED / SENSOR mode\n");
    printf("  2  Set / clear intersection override\n");
    printf("  3  Clear controller fault\n");
    printf("  4  Toggle automatic time-of-day schedule\n");
    printf("  5  Show system status\n");
    printf("  +  Advance simulated time by one hour\n");
    printf("  q  Quit\n");
}

void *menu_thread(void *arg)
{
    (void)arg;

    printf(" Automatic schedule starts OFF.\n");
    print_menu();

    for (;;) {
        char ch;

        if (read_choice("\nCentral> ", &ch) < 0) {
            log_msg("Console closed; Central shutting down");
            return NULL;
        }

        if (ch == '1') {
            int mask = choose_intersections();
            char m;
            operation_mode_t mode;

            if (!mask)
                continue;
            if (read_choice("Mode [f] FIXED  [s] SENSOR: ", &m) < 0)
                continue;
            if (m != 'f' && m != 's') {
                printf("Invalid mode.\n");
                continue;
            }

            mode = m == 'f' ? MODE_FIXED : MODE_SENSOR;
            if (mask & 1) do_mode(NODE_I1, mode);
            if (mask & 2) do_mode(NODE_I2, mode);
        } else if (ch == '2') {
            int mask = choose_intersections();
            char o;
            override_t ov;

            if (!mask)
                continue;
            if (read_choice("Override [m] R3/main  [s] side  [n] none: ", &o) < 0)
                continue;

            if (o == 'm') ov = OVERRIDE_MAIN;
            else if (o == 's') ov = OVERRIDE_SIDE;
            else if (o == 'n') ov = OVERRIDE_NONE;
            else {
                printf("Invalid override.\n");
                continue;
            }

            if (mask & 1) do_override(NODE_I1, ov);
            if (mask & 2) do_override(NODE_I2, ov);
        } else if (ch == '3') {
            char t;

            if (read_choice("Clear fault [1] I1  [2] I2  [x] X1: ", &t) < 0)
                continue;
            if (t == '1') do_clear_fault(NODE_I1);
            else if (t == '2') do_clear_fault(NODE_I2);
            else if (t == 'x') do_clear_fault(NODE_X1);
            else printf("Invalid target.\n");
        } else if (ch == '4') {
            int on;

            pthread_rwlock_wrlock(&g_c.lock);
            on = g_c.sched_enabled = !g_c.sched_enabled;
            if (on) {
                g_c.pattern = -1;
                g_c.node[NODE_I1].sched_paused = 0;
                g_c.node[NODE_I2].sched_paused = 0;
            }
            pthread_rwlock_unlock(&g_c.lock);
            log_msg("Operator: automatic schedule %s", on ? "ON" : "OFF");
        } else if (ch == '5') {
            print_status();
        } else if (ch == '+') {
            pthread_rwlock_wrlock(&g_c.lock);
            g_c.sim_min = (g_c.sim_min + 60) % (24 * 60);
            if (g_c.sched_enabled)
                g_c.pattern = -1;
            pthread_rwlock_unlock(&g_c.lock);
            log_msg("Operator: simulated clock +1 hour");
        } else if (ch == 'q') {
            log_msg("Operator: Central shutting down");
            return NULL;
        } else if (ch == 'h' || ch == '?') {
            print_menu();
        } else {
            printf("Unknown option. Press h for commands.\n");
        }
    }
}
