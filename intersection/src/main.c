#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/neutrino.h>
#include <sys/netmgr.h>
#include <sys/dispatch.h>
#include <sys/iomsg.h>
#include <unistd.h>

#include "intersection.h"
#include "events.h"
#include "fsm.h"
#include "status_box.h"
#include "keyboard.h"
#include "display.h"
#include "uplink.h"
#include "console.h"

struct timespec g_t0;

static void usage(const char *prog)
{
    fprintf(stderr, "usage: %s <1|2> [-s] [-c <central service>]\n", prog);
    exit(EXIT_FAILURE);
}

static void parse_args(int argc, char *argv[], ctrl_t *c, const char **service)
{
    int id;
    int i;

    if (argc < 2)
        usage(argv[0]);

    id = atoi(argv[1]);
    if (id != 1 && id != 2)
        usage(argv[0]);

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0) {
            c->standalone = true;
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            c->central_path = argv[++i];
        } else {
            usage(argv[0]);
        }
    }

    if (id == 1) {
        c->node = NODE_I1;
        c->name = "I1";
        *service = I1_SERVICE;
        c->main_road = "R3 Glen Huntly";
        c->side_road = "R1 Grange";
    } else {
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

static int setup_ipc(ctrl_t *c, const char *service)
{
    c->attach = name_attach(NULL, service,
                            IPC_USE_GLOBAL_NAMES ? NAME_FLAG_ATTACH_GLOBAL : 0);
    if (!c->attach) {
        perror("name_attach");
        return -1;
    }

    c->self_coid = ConnectAttach(ND_LOCAL_NODE, 0, c->attach->chid,
                                 _NTO_SIDE_CHANNEL, 0);
    if (c->self_coid == -1) {
        perror("ConnectAttach");
        name_detach(c->attach, 0);
        c->attach = NULL;
        return -1;
    }

    if (oneshot_init(&c->phase_timer, c->self_coid,
                     PRIO_CONTROLLER, PC_PHASE_TIMER) == -1) {
        perror("timer_create");
        ConnectDetach(c->self_coid);
        c->self_coid = -1;
        name_detach(c->attach, 0);
        c->attach = NULL;
        return -1;
    }

    if (oneshot_init(&c->x1_watchdog, c->self_coid,
                     PRIO_CONTROLLER, PC_X1_WATCHDOG) == -1) {
        perror("timer_create");
        oneshot_destroy(&c->phase_timer);
        ConnectDetach(c->self_coid);
        c->self_coid = -1;
        name_detach(c->attach, 0);
        c->attach = NULL;
        return -1;
    }

    return 0;
}

static void run(ctrl_t *c)
{
    while (!c->quit) {
        union {
            uint16_t type;
            struct _pulse pulse;
            msg_header_t header;
            set_mode_msg_t set_mode;
            override_msg_t override;
            clear_fault_msg_t clear_fault;
        } rx;
        struct _msg_info info;
        msg_reply_t rep;
        int32_t result;
        int rcvid;

        rcvid = MsgReceive(c->attach->chid, &rx, sizeof rx, &info);
        if (rcvid == -1) {
            if (errno == EINTR)
                continue;
            perror("MsgReceive");
            break;
        }

        if (rcvid == 0) {
            events_handle_pulse(c, &rx.pulse);
            continue;
        }

        if (rx.type == _IO_CONNECT) {
            MsgReply(rcvid, EOK, NULL, 0);
            continue;
        }

        if (rx.type > _IO_BASE && rx.type <= _IO_MAX) {
            MsgError(rcvid, ENOSYS);
            continue;
        }

        result = events_handle_message(c, &rx, (int)info.msglen);
        if (result < 0) {
            MsgError(rcvid, ENOSYS);
            continue;
        }

        rep.result = result;
        rep.sequence = rx.header.sequence;
        MsgReply(rcvid, EOK, &rep, sizeof rep);
    }
}

int main(int argc, char *argv[])
{
    static ctrl_t c;
    const char *service = NULL;
    pthread_t display_tid;
    pthread_t uplink_tid;
    pthread_t keyboard_tid;
    int display_started = 0;
    int uplink_started = 0;
    int keyboard_started = 0;
    int box_ready = 0;
    int rc = EXIT_FAILURE;
    char hostname[64] = "?";

    setvbuf(stdout, NULL, _IOLBF, 0);
    c.self_coid = -1;
    ts_now(&g_t0);

    parse_args(argc, argv, &c, &service);
    c.session_id = make_session_id();

    if (setup_ipc(&c, service) != 0)
        goto cleanup;

    if (status_box_init() != 0) {
        fprintf(stderr, "status box init failed\n");
        goto cleanup;
    }
    box_ready = 1;

    set_my_priority(PRIO_CONTROLLER);

    printf("%s\n", UI_RULE);
    gethostname(hostname, sizeof hostname - 1);
    hostname[sizeof hostname - 1] = '\0';
    printf(" EEET2166 | %s LOCAL INTERSECTION CONTROLLER\n", c.name);
    printf(" Host    : %s\n", hostname);
    printf(" Service : /dev/name/%s/%s\n",
           IPC_USE_GLOBAL_NAMES ? "global" : "local", service);
    printf(" Roads   : %s / %s\n", c.main_road, c.side_road);
    printf(" Mode    : FIXED startup | TIME_SCALE %d | X1 %s\n",
           TIME_SCALE, c.standalone ? "standalone" : "heartbeat monitored");
    printf("%s\n", UI_RULE);

    fsm_start(&c);

    if (spawn_thread(&display_tid, display_thread, &c, PRIO_DISPLAY) != 0) {
        fprintf(stderr, "display thread create failed\n");
        goto cleanup;
    }
    display_started = 1;

    if (spawn_thread(&uplink_tid, uplink_thread, &c, PRIO_UPLINK) != 0) {
        fprintf(stderr, "uplink thread create failed\n");
        goto cleanup;
    }
    uplink_started = 1;

    if (spawn_thread(&keyboard_tid, keyboard_thread, &c, PRIO_KEYBOARD) != 0) {
        fprintf(stderr, "keyboard thread create failed\n");
        goto cleanup;
    }
    keyboard_started = 1;

    run(&c);
    rc = EXIT_SUCCESS;

cleanup:
    if (box_ready)
        status_box_shutdown();

    if (c.attach) {
        oneshot_cancel(&c.phase_timer);
        oneshot_cancel(&c.x1_watchdog);
        oneshot_destroy(&c.phase_timer);
        oneshot_destroy(&c.x1_watchdog);
    }

    if (keyboard_started)
        pthread_cancel(keyboard_tid);
    if (uplink_started)
        pthread_cancel(uplink_tid);

    if (c.self_coid != -1) {
        ConnectDetach(c.self_coid);
        c.self_coid = -1;
    }

    if (c.attach) {
        name_detach(c.attach, 0);
        c.attach = NULL;
    }

    if (display_started)
        pthread_join(display_tid, NULL);
    if (uplink_started)
        pthread_join(uplink_tid, NULL);
    if (keyboard_started)
        pthread_join(keyboard_tid, NULL);

    if (box_ready)
        printf("[%s] Controller stopped.\n", c.name);

    return rc;
}
