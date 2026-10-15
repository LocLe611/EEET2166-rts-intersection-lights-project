#include <errno.h>
#include <string.h>
#include <sys/dispatch.h>
#include <sys/neutrino.h>
#include "central.h"

static int name_flags(void)
{
    return IPC_USE_GLOBAL_NAMES ? NAME_FLAG_ATTACH_GLOBAL : 0;
}

static void set_display_link(int up)
{
    int changed;

    pthread_rwlock_wrlock(&g_c.lock);
    changed = g_c.node[NODE_DISPLAY].link != (up ? LINK_ONLINE : LINK_OFFLINE);
    g_c.node[NODE_DISPLAY].link = up ? LINK_ONLINE : LINK_OFFLINE;
    if (up)
        g_c.node[NODE_DISPLAY].last_seen_ms = now_ms();
    pthread_rwlock_unlock(&g_c.lock);

    if (changed)
        log_msg("Display: %s", up ? "ONLINE" : "OFFLINE");
}

static void build_system_status(system_status_msg_t *m, uint32_t seq)
{
    node_info_t i1;
    node_info_t i2;
    node_info_t x1;

    memset(m, 0, sizeof *m);

    pthread_rwlock_rdlock(&g_c.lock);
    i1 = g_c.node[NODE_I1];
    i2 = g_c.node[NODE_I2];
    x1 = g_c.node[NODE_X1];

    if (i1.link == LINK_ONLINE)
        m->online_mask |= NODE_MASK(NODE_I1);
    if (i2.link == LINK_ONLINE)
        m->online_mask |= NODE_MASK(NODE_I2);
    if (x1.link == LINK_ONLINE)
        m->online_mask |= NODE_MASK(NODE_X1);

    if (i1.have_data)
        m->data_mask |= NODE_MASK(NODE_I1);
    if (i2.have_data)
        m->data_mask |= NODE_MASK(NODE_I2);
    if (x1.have_data)
        m->data_mask |= NODE_MASK(NODE_X1);

    m->alarm_mask = g_c.alarms;
    m->simulated_min = (uint16_t)g_c.sim_min;
    m->schedule_enabled = g_c.sched_enabled ? 1u : 0u;
    m->pattern = g_c.pattern < 0 ? PAT_UNKNOWN : (uint8_t)g_c.pattern;

    m->i1 = i1.ist;
    m->i2 = i2.ist;
    m->x1 = x1.rail;
    pthread_rwlock_unlock(&g_c.lock);

    memset(&m->i1.header, 0, sizeof m->i1.header);
    memset(&m->i2.header, 0, sizeof m->i2.header);
    memset(&m->x1.header, 0, sizeof m->x1.header);

    m->header.type = MSG_SYSTEM_STATUS;
    m->header.subtype = SUBTYPE_STATUS_UPDATE;
    m->header.sender = NODE_CENTRAL;
    m->header.session = g_c.session_id;
    m->header.sequence = seq;
}

static int send_snapshot(int *coid, uint32_t seq)
{
    system_status_msg_t m;
    msg_reply_t reply;
    uint64_t timeout_ns;

    if (*coid == -1) {
        *coid = name_open(DISPLAY_SERVICE, name_flags());
        if (*coid == -1) {
            set_display_link(0);
            return -1;
        }
    }

    build_system_status(&m, seq);
    memset(&reply, 0, sizeof reply);

    timeout_ns = (uint64_t)IPC_SEND_TIMEOUT_MS * 1000000ULL;
    TimerTimeout(CLOCK_MONOTONIC,
                 _NTO_TIMEOUT_SEND | _NTO_TIMEOUT_REPLY,
                 NULL, &timeout_ns, NULL);

    if (MsgSend(*coid, &m, sizeof m, &reply, sizeof reply) == -1 ||
        reply.result != RESULT_OK || reply.sequence != seq) {
        name_close(*coid);
        *coid = -1;
        set_display_link(0);
        return -1;
    }

    set_display_link(1);
    return 0;
}

void *display_link_thread(void *arg)
{
    int chid;
    int self_coid;
    int display_coid = -1;
    uint32_t seq = 0;
    timer_t timer_id;
    struct sigevent ev;
    struct itimerspec spec;
    struct _pulse pulse;

    (void)arg;

    chid = ChannelCreate(0);
    if (chid == -1)
        return NULL;

    self_coid = ConnectAttach(0, 0, chid, _NTO_SIDE_CHANNEL, 0);
    if (self_coid == -1) {
        ChannelDestroy(chid);
        return NULL;
    }

    SIGEV_PULSE_INIT(&ev, self_coid, SIGEV_PULSE_PRIO_INHERIT,
                     PC_DISPLAY_FEED, 0);
    if (timer_create(CLOCK_MONOTONIC, &ev, &timer_id) == -1) {
        ConnectDetach(self_coid);
        ChannelDestroy(chid);
        return NULL;
    }

    memset(&spec, 0, sizeof spec);
    spec.it_value.tv_sec = STATUS_PERIOD_MS / 1000;
    spec.it_value.tv_nsec = (long)(STATUS_PERIOD_MS % 1000) * 1000000L;
    spec.it_interval = spec.it_value;
    timer_settime(timer_id, 0, &spec, NULL);

    send_snapshot(&display_coid, ++seq);

    for (;;) {
        if (MsgReceive(chid, &pulse, sizeof pulse, NULL) != 0)
            continue;
        if (pulse.code == PC_DISPLAY_FEED)
            send_snapshot(&display_coid, ++seq);
    }

    return NULL;
}
