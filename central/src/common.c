#include <errno.h>
#include <string.h>
#include "central.h"

central_t g_c;
cmdq_t g_cmdq[NODE_SLOTS];

static pthread_mutex_t seq_m = PTHREAD_MUTEX_INITIALIZER;
static uint32_t seq_ctr;

uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

void abs_deadline(struct timespec *ts, unsigned ms)
{
    clock_gettime(CLOCK_MONOTONIC, ts);
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}

static void cond_init_mono(pthread_cond_t *cv)
{
    pthread_condattr_t a;
    pthread_condattr_init(&a);
    pthread_condattr_setclock(&a, CLOCK_MONOTONIC);
    pthread_cond_init(cv, &a);
    pthread_condattr_destroy(&a);
}

void ctl_init(void)
{
    memset(&g_c, 0, sizeof g_c);
    pthread_rwlock_init(&g_c.lock, NULL);

    for (int n = 0; n < NODE_SLOTS; n++) {
        g_c.node[n].link = LINK_OFFLINE;
        g_c.node[n].desired_mode = MODE_FIXED;
        g_c.node[n].desired_override = OVERRIDE_NONE;
        pthread_mutex_init(&g_cmdq[n].m, NULL);
        cond_init_mono(&g_cmdq[n].cv);
    }

    g_c.sched_enabled = 0;
    g_c.sim_min = SIM_START_MIN;
    g_c.pattern = PAT_OFFPEAK;
    g_c.session_id = make_session_id();
}

static uint32_t next_seq(void)
{
    uint32_t s;
    pthread_mutex_lock(&seq_m);
    s = ++seq_ctr;
    pthread_mutex_unlock(&seq_m);
    return s;
}

static void fill_header(msg_header_t *h, uint16_t type, uint16_t subtype)
{
    memset(h, 0, sizeof *h);
    h->type = type;
    h->subtype = subtype;
    h->sender = NODE_CENTRAL;
    h->session = g_c.session_id;
    h->sequence = next_seq();
}

void build_set_mode(cmd_t *c, operation_mode_t mode)
{
    memset(c, 0, sizeof *c);
    fill_header(&c->m.header, MSG_SET_MODE, SUBTYPE_NONE);
    c->m.set_mode.mode = (uint8_t)mode;
    c->len = sizeof c->m.set_mode;
}

void build_override(cmd_t *c, override_t ovr)
{
    memset(c, 0, sizeof *c);
    fill_header(&c->m.header, MSG_OVERRIDE, SUBTYPE_NONE);
    c->m.override.override = (uint8_t)ovr;
    c->len = sizeof c->m.override;
}

void build_clear_fault(cmd_t *c)
{
    memset(c, 0, sizeof *c);
    fill_header(&c->m.header, MSG_CLEAR_FAULT, SUBTYPE_OPERATOR_CLEAR);
    c->len = sizeof c->m.clear_fault;
}

int cmdq_push(node_id_t n, const cmd_t *c)
{
    cmdq_t *q = &g_cmdq[n];

    pthread_mutex_lock(&q->m);
    if (q->count == CMDQ_LEN) {
        pthread_mutex_unlock(&q->m);
        log_msg("%s: command queue full, dropped %s",
                node_str(n), msgtype_str(c->m.header.type));
        return -1;
    }

    q->q[(q->head + q->count) % CMDQ_LEN] = *c;
    q->count++;
    pthread_cond_signal(&q->cv);
    pthread_mutex_unlock(&q->m);

    pthread_rwlock_wrlock(&g_c.lock);
    g_c.node[n].last_cmd_seq = c->m.header.sequence;
    g_c.node[n].last_cmd_type = c->m.header.type;
    g_c.node[n].last_cmd_res = CMDRES_PENDING;
    pthread_rwlock_unlock(&g_c.lock);

    log_msg("%s: queued %s #%u",
            node_str(n), msgtype_str(c->m.header.type), c->m.header.sequence);
    return 0;
}

void cmdq_request_resync(node_id_t n)
{
    cmdq_t *q = &g_cmdq[n];
    pthread_mutex_lock(&q->m);
    q->resync = 1;
    pthread_cond_signal(&q->cv);
    pthread_mutex_unlock(&q->m);
}
