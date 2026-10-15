#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dispatch.h>
#include <sys/iomsg.h>
#include <sys/neutrino.h>
#include <sys/netmgr.h>
#include <time.h>
#include <unistd.h>
#include "config.h"
#include "console.h"
#include "message.h"

#define PULSE_MONITOR_TICK (_PULSE_CODE_MINAVAIL + 20)
#define PULSE_MONITOR_QUIT (_PULSE_CODE_MINAVAIL + 21)
#define CENTRAL_TIMEOUT_MS (3 * STATUS_PERIOD_MS)

typedef union {
    struct _pulse pulse;
    msg_header_t header;
    system_status_msg_t system;
} monitor_rx_t;

typedef struct {
    int self_coid;
} keyboard_args_t;

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static const char *phase_name(unsigned p)
{
    static const char *const names[PHASE_COUNT] = {
        "ALL_RED", "MAIN_GREEN", "MAIN_YELLOW", "SIDE_GREEN",
        "SIDE_YELLOW", "PED_WALK", "PED_CLEAR", "FAULT"
    };
    return p < PHASE_COUNT ? names[p] : "?";
}

static const char *mode_name(unsigned m)
{
    return m == MODE_SENSOR ? "SENSOR" : "FIXED";
}

static const char *rail_name(unsigned r)
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

static const char *gate_name(unsigned g)
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

static const char *pattern_name(unsigned p)
{
    switch (p) {
    case PAT_NIGHT: return "NIGHT";
    case PAT_PEAK: return "PEAK";
    case PAT_OFFPEAK: return "OFF-PEAK";
    default: return "-";
    }
}

static void print_link(int online)
{
    if (online)
        printf(UI_GREEN "ONLINE" UI_RESET);
    else
        printf(UI_RED "OFFLINE" UI_RESET);
}

static void print_light(unsigned light)
{
    if (light == LIGHT_GREEN)
        printf(UI_GREEN "GREEN" UI_RESET);
    else if (light == LIGHT_YELLOW)
        printf(UI_YELLOW "YELLOW" UI_RESET);
    else
        printf(UI_RED "RED" UI_RESET);
}

static void print_ped(unsigned ped)
{
    if (ped == PED_WALK)
        printf(UI_GREEN "WALK" UI_RESET);
    else if (ped == PED_FLASHING)
        printf(UI_YELLOW "FLASHING" UI_RESET);
    else
        printf(UI_RED "DON'T WALK" UI_RESET);
}

static void print_intersection(const char *name,
                               const char *side_road,
                               unsigned node,
                               const system_status_msg_t *s)
{
    const intersection_status_msg_t *st;
    int online;
    int have_data;

    st = node == NODE_I1 ? &s->i1 : &s->i2;

    online = (s->online_mask & NODE_MASK(node)) != 0;
    have_data = (s->data_mask & NODE_MASK(node)) != 0;

    printf("\n%s [", name);
    print_link(online);
    printf("]");

    if (!have_data) {
        printf(" | waiting for status\n");
        return;
    }

    printf(" | MODE=%s | PHASE=%s\n",
           mode_name(st->mode),
           phase_name(st->phase));

    printf("  VEH MAIN R3 Glen Huntly (EW)=");
    print_light(st->main_light);

    printf(" | VEH SIDE %s=", side_road);
    print_light(st->side_light);

    printf(" | PED GROUP=");
    print_ped(st->ped_light);

    printf("\n");

    printf("  X1=%s | X1 LINK=",
           rail_name(st->railway_state));

    if (st->x1_link_ok)
        printf(UI_GREEN "OK" UI_RESET);
    else
        printf(UI_RED "LOST" UI_RESET);

    printf(" | OVERRIDE=%s\n",
           st->override == OVERRIDE_MAIN
               ? "R3/MAIN"
               : st->override == OVERRIDE_SIDE
                     ? "SIDE"
                     : "NONE");
}

static void print_x1(const system_status_msg_t *s)
{
    int online = (s->online_mask & NODE_MASK(NODE_X1)) != 0;
    int have_data = (s->data_mask & NODE_MASK(NODE_X1)) != 0;

    printf("\nX1 [");
    print_link(online);
    printf("]");

    if (!have_data) {
        printf(" | waiting for status\n");
        return;
    }

    printf(" | Railway=%s | Gate=%s | Warning=%s\n",
           rail_name(s->x1.railway_state),
           gate_name(s->x1.gate_state),
           s->x1.warning_lights_on ? "ON" : "OFF");
    printf("  Train signal=");
    if (s->x1.train_signal == TRAIN_SIGNAL_GREEN)
        printf(UI_GREEN "GREEN" UI_RESET);
    else
        printf(UI_RED "RED" UI_RESET);
    printf(" | Fault=0x%08X\n", (unsigned)s->x1.fault_flags);
}

static void print_snapshot(const system_status_msg_t *s, int central_online)
{
    printf("\n%s\n", UI_RULE);
    printf(" EEET2166 | SYSTEM MONITOR | Central [");
    print_link(central_online);
    printf("]\n");

    if (s == NULL) {
        printf(" Waiting for SYSTEM_STATUS from Central...\n");
        printf("%s\n", UI_RULE);
        return;
    }

    printf(" Time %02u:%02u | Pattern %s | Schedule %s\n",
           s->simulated_min / 60, s->simulated_min % 60,
           pattern_name(s->pattern), s->schedule_enabled ? "ON" : "OFF");

    if (s->alarm_mask)
        printf(UI_RED " ACTIVE FAULT MASK: 0x%X\n" UI_RESET, s->alarm_mask);

    print_intersection("I1", "R1 Grange", NODE_I1, s);
    print_intersection("I2", "R2 Booran", NODE_I2, s);
    print_x1(s);
    printf("%s\n", UI_RULE);
}

static int payload_changed(const system_status_msg_t *a,
                           const system_status_msg_t *b)
{
    const unsigned char *pa = (const unsigned char *)a + sizeof(msg_header_t);
    const unsigned char *pb = (const unsigned char *)b + sizeof(msg_header_t);
    return memcmp(pa, pb, sizeof(*a) - sizeof(msg_header_t)) != 0;
}

static int valid_system_status(const system_status_msg_t *s)
{
    if (s->header.sender != NODE_CENTRAL)
        return 0;
    if (s->schedule_enabled > 1 ||
        (s->pattern > PAT_OFFPEAK && s->pattern != PAT_UNKNOWN))
        return 0;
    if (s->i1.mode > MODE_SENSOR || s->i2.mode > MODE_SENSOR)
        return 0;
    if (s->i1.phase >= PHASE_COUNT || s->i2.phase >= PHASE_COUNT)
        return 0;
    if (s->x1.railway_state > RAIL_FAULT || s->x1.gate_state > GATE_UNKNOWN)
        return 0;
    return 1;
}

static void *keyboard_thread(void *arg)
{
    keyboard_args_t *a = arg;
    char line[32];

    while (fgets(line, sizeof line, stdin) != NULL) {
        int ch = toupper((unsigned char)line[0]);
        if (ch == 'Q') {
            MsgSendPulse(a->self_coid, -1, PULSE_MONITOR_QUIT, 0);
            return NULL;
        }
        if (ch != '\n' && ch != '\0')
            printf("[MONITOR] Only Q (quit) is available.\n");
    }

    MsgSendPulse(a->self_coid, -1, PULSE_MONITOR_QUIT, 0);
    return NULL;
}

static timer_t create_tick_timer(int self_coid)
{
    struct sigevent ev;
    struct itimerspec spec;
    timer_t timer_id;

    SIGEV_PULSE_INIT(&ev, self_coid, SIGEV_PULSE_PRIO_INHERIT,
                     PULSE_MONITOR_TICK, 0);
    if (timer_create(CLOCK_MONOTONIC, &ev, &timer_id) == -1)
        return (timer_t)-1;

    memset(&spec, 0, sizeof spec);
    spec.it_value.tv_sec = STATUS_PERIOD_MS / 1000;
    spec.it_value.tv_nsec = (long)(STATUS_PERIOD_MS % 1000) * 1000000L;
    spec.it_interval = spec.it_value;

    if (timer_settime(timer_id, 0, &spec, NULL) == -1) {
        timer_delete(timer_id);
        return (timer_t)-1;
    }

    return timer_id;
}

int main(void)
{
    name_attach_t *attach;
    int self_coid;
    timer_t tick_timer;
    pthread_t keyboard_tid;
    keyboard_args_t keyboard_args;
    system_status_msg_t current;
    int have_status = 0;
    int central_online = 0;
    uint64_t last_rx_ms = 0;
    int running = 1;
    char hostname[64] = "?";

    setvbuf(stdout, NULL, _IOLBF, 0);
    memset(&current, 0, sizeof current);

    attach = name_attach(NULL, DISPLAY_SERVICE,
                         IPC_USE_GLOBAL_NAMES ? NAME_FLAG_ATTACH_GLOBAL : 0);
    if (attach == NULL) {
        perror("name_attach");
        return EXIT_FAILURE;
    }

    self_coid = ConnectAttach(ND_LOCAL_NODE, 0, attach->chid,
                              _NTO_SIDE_CHANNEL, 0);
    if (self_coid == -1) {
        perror("ConnectAttach");
        name_detach(attach, 0);
        return EXIT_FAILURE;
    }

    tick_timer = create_tick_timer(self_coid);
    if (tick_timer == (timer_t)-1) {
        perror("timer_create");
        ConnectDetach(self_coid);
        name_detach(attach, 0);
        return EXIT_FAILURE;
    }

    keyboard_args.self_coid = self_coid;
    if (pthread_create(&keyboard_tid, NULL, keyboard_thread, &keyboard_args) != 0) {
        timer_delete(tick_timer);
        ConnectDetach(self_coid);
        name_detach(attach, 0);
        return EXIT_FAILURE;
    }

    printf("%s\n", UI_RULE);
    gethostname(hostname, sizeof hostname - 1);
    hostname[sizeof hostname - 1] = '\0';
    printf(" EEET2166 | NODE 5 SYSTEM MONITOR\n");
    printf(" Host    : %s\n", hostname);
    printf(" Service : /dev/name/%s/%s\n",
           IPC_USE_GLOBAL_NAMES ? "global" : "local", DISPLAY_SERVICE);
    printf(" Role    : Monitoring only - no control commands\n");
    printf(" Command : Q + Enter to quit\n");
    printf("%s\n", UI_RULE);
    print_snapshot(NULL, 0);

    while (running) {
        monitor_rx_t rx;
        struct _msg_info info;
        int rcvid;

        memset(&rx, 0, sizeof rx);
        memset(&info, 0, sizeof info);
        rcvid = MsgReceive(attach->chid, &rx, sizeof rx, &info);

        if (rcvid == -1) {
            if (errno == EINTR)
                continue;
            perror("MsgReceive");
            break;
        }

        if (rcvid == 0) {
            if (rx.pulse.code == PULSE_MONITOR_QUIT) {
                running = 0;
            } else if (rx.pulse.code == PULSE_MONITOR_TICK &&
                       central_online &&
                       now_ms() - last_rx_ms > CENTRAL_TIMEOUT_MS) {
                central_online = 0;
                print_snapshot(have_status ? &current : NULL, 0);
            } else if (rx.pulse.code == _PULSE_CODE_DISCONNECT) {
                ConnectDetach(rx.pulse.scoid);
            }
            continue;
        }

        if (rx.header.type == _IO_CONNECT) {
            MsgReply(rcvid, EOK, NULL, 0);
            continue;
        }

        if (rx.header.type > _IO_BASE && rx.header.type <= _IO_MAX) {
            MsgError(rcvid, ENOSYS);
            continue;
        }

        if (rx.header.type == MSG_SYSTEM_STATUS &&
            (size_t)info.msglen >= sizeof rx.system &&
            valid_system_status(&rx.system)) {
            msg_reply_t reply;
            int changed = !have_status || payload_changed(&current, &rx.system);
            int link_changed = !central_online;

            current = rx.system;
            have_status = 1;
            central_online = 1;
            last_rx_ms = now_ms();

            reply.result = RESULT_OK;
            reply.sequence = rx.system.header.sequence;
            MsgReply(rcvid, EOK, &reply, sizeof reply);

            if (changed || link_changed)
                print_snapshot(&current, 1);
        } else {
            msg_reply_t reply;
            reply.result = RESULT_INVALID;
            reply.sequence = rx.header.sequence;
            MsgReply(rcvid, EOK, &reply, sizeof reply);
        }
    }

    pthread_cancel(keyboard_tid);
    pthread_join(keyboard_tid, NULL);
    timer_delete(tick_timer);
    ConnectDetach(self_coid);
    name_detach(attach, 0);
    printf("[MONITOR] Stopped.\n");
    return EXIT_SUCCESS;
}
