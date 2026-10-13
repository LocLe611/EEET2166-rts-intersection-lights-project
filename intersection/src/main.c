/*
 * main.c — I1 / I2 local intersection controller (EEET2166 Group 11)
 *
 * Usage:   intersection <1|2> [-s] [-c <central path>]
 *            1|2   which intersection (service name, roads, timing table)
 *            -s    don't require X1 heartbeats (X1 not running yet, or not sending
 *                  heartbeats yet). Without -s, silence from X1 holds R3 red.
 *            -c    path name_open() uses to reach central
 *                  (default QNET_SERVICE_PATH(CENTRAL_HOSTNAME, CENTRAL_SERVICE))
 *
 * Files (report Fig 15, Node 1 / Node 2):
 *   main.c        start-up, the MsgReceive event loop, shutdown          <- you are here
 *   events.c      what each pulse / message from central does to the inputs
 *   fsm.c         the light state machine, priority hierarchy, safety interlock
 *   keyboard.c    thread: simulated sensors ("Vehicle sensor", "Pedestrian input")
 *   display.c     thread: terminal "Light output"
 *   uplink.c      thread: status -> central ("Communication")
 *   status_box.c  mutex + condition variable: controller -> display / uplink
 *   rtutil.c      monotonic time, one-shot pulse timers, thread creation
 *   timing.c      the timing table — edit timings here
 *   intersection.h  priorities, internal pulse codes, ctrl_t
 *
 * Threading rule: only the main thread changes ctrl_t (and so the lights). Everything else
 * reaches it as a pulse or message on one channel, handled one at a time, in priority order.
 * The main thread never sends to anyone, so it can't take part in a send deadlock.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/neutrino.h>
#include <sys/netmgr.h>   /* ND_LOCAL_NODE                    */
#include <sys/dispatch.h> /* name_attach                       */
#include <sys/iomsg.h>    /* _IO_CONNECT, _IO_BASE, _IO_MAX    */

#include "intersection.h"
#include "events.h"
#include "fsm.h"
#include "status_box.h"
#include "keyboard.h"
#include "display.h"
#include "uplink.h"

struct timespec g_t0;

static void usage(const char *prog)
{
    fprintf(stderr, "usage: %s <1|2> [-s] [-c <central path>]\n", prog);
    exit(EXIT_FAILURE);
}

static void parse_args(int argc, char *argv[], ctrl_t *c, const char **service)
{
    int id, i;

    if (argc < 2)
        usage(argv[0]);
    id = atoi(argv[1]);
    if (id != 1 && id != 2)
        usage(argv[0]);
    for (i = 2; i < argc; i++)
    {
        if (strcmp(argv[i], "-s") == 0)
            c->standalone = true;
        else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc)
            c->central_path = argv[++i];
        else
            usage(argv[0]);
    }

    if (id == 1)
    {
        c->node = NODE_I1;
        c->name = "I1";
        *service = I1_SERVICE;
        c->main_road = "R3 Glen Huntly";
        c->side_road = "R1 Grange";
    }
    else
    {
        c->node = NODE_I2;
        c->name = "I2";
        *service = I2_SERVICE;
        c->main_road = "R3 Glen Huntly";
        c->side_road = "R2 Booran";
    }
    if (!c->central_path)
        c->central_path = QNET_SERVICE_PATH(CENTRAL_HOSTNAME, CENTRAL_SERVICE);
    timing_load(id, &c->t);
}

/* One named channel receives everything; timers and the keyboard reach it through a
 * side-channel connection to ourselves. */
static int setup_ipc(ctrl_t *c, const char *service)
{
    c->attach = name_attach(NULL, service, 0);
    if (c->attach == NULL)
    {
        perror("name_attach (is another copy already running?)");
        return -1;
    }
    c->self_coid = ConnectAttach(ND_LOCAL_NODE, 0, c->attach->chid, _NTO_SIDE_CHANNEL, 0);
    if (c->self_coid == -1)
    {
        perror("ConnectAttach");
        return -1;
    }

    if (oneshot_init(&c->phase_timer, c->self_coid, PRIO_CONTROLLER, PC_PHASE_TIMER) == -1 ||
        oneshot_init(&c->x1_watchdog, c->self_coid, PRIO_CONTROLLER, PC_X1_WATCHDOG) == -1)
    {
        perror("timer_create");
        return -1;
    }
    return 0;
}

/* The event loop: block in MsgReceive, handle one pulse or message, repeat. */
static void run(ctrl_t *c)
{
    while (!c->quit)
    {
        union
        {
            uint16_t type; /* first 16 bits of anything we can receive */
            struct _pulse pulse;
            msg_header_t header;
            set_mode_msg_t set_mode;
            override_msg_t override;
            clear_fault_msg_t clear_fault;
        } rx;
        struct _msg_info info;
        msg_reply_t rep;
        int32_t result;
        int rcvid = MsgReceive(c->attach->chid, &rx, sizeof rx, &info);

        if (rcvid == -1)
        {
            if (errno == EINTR)
                continue;
            perror("MsgReceive");
            break;
        }
        if (rcvid == 0)
        { /* pulse */
            events_handle_pulse(c, &rx.pulse);
            continue;
        }
        if (rx.type == _IO_CONNECT)
        { /* name_open() handshake */
            MsgReply(rcvid, EOK, NULL, 0);
            continue;
        }
        if (rx.type > _IO_BASE && rx.type <= _IO_MAX)
        { /* other QNX I/O messages */
            MsgError(rcvid, ENOSYS);
            continue;
        }
        result = events_handle_message(c, &rx, (int)info.msglen);
        if (result < 0)
        {
            MsgError(rcvid, ENOSYS);
            continue;
        }
        /* Reply straight away: central's MsgSend is never held up by the light cycle.
         * The command itself takes effect at the next safe point. */
        rep.result = result;
        rep.sequence = rx.header.sequence;
        MsgReply(rcvid, EOK, &rep, sizeof rep);
    }
}

int main(int argc, char *argv[])
{
    static ctrl_t c; /* static: zero-initialised, not on the stack */
    const char *service = NULL;
    pthread_t display_tid, uplink_tid, keyboard_tid;

    setvbuf(stdout, NULL, _IOLBF, 0);
    ts_now(&g_t0);

    parse_args(argc, argv, &c, &service);
    if (setup_ipc(&c, service) != 0)
        return EXIT_FAILURE;
    if (status_box_init() != 0)
    {
        fprintf(stderr, "status box init failed\n");
        return EXIT_FAILURE;
    }
    set_my_priority(PRIO_CONTROLLER);

    printf("%s controller up: /dev/name/local/%s, TIME_SCALE %d, %s\n",
           c.name, service, TIME_SCALE,
           c.standalone ? "X1 heartbeat NOT required (-s)" : "expecting X1 heartbeats");

    fsm_start(&c); /* all red; the cycle then runs off timer pulses */

    if (spawn_thread(&display_tid, display_thread, &c, PRIO_DISPLAY) != 0 ||
        spawn_thread(&uplink_tid, uplink_thread, &c, PRIO_UPLINK) != 0 ||
        spawn_thread(&keyboard_tid, keyboard_thread, &c, PRIO_KEYBOARD) != 0)
    {
        fprintf(stderr, "pthread_create failed\n");
        return EXIT_FAILURE;
    }
    pthread_detach(keyboard_tid); /* never joined: it may be blocked in getchar() */

    run(&c);

    /* ---- shutdown ---- */
    printf("%s shutting down\n", c.name);
    status_box_shutdown();
    pthread_join(display_tid, NULL);
    pthread_join(uplink_tid, NULL);
    oneshot_destroy(&c.phase_timer);
    oneshot_destroy(&c.x1_watchdog);
    ConnectDetach(c.self_coid);
    name_detach(c.attach, 0);
    return EXIT_SUCCESS;
}
