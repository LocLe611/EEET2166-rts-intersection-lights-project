/* common.c — shared state, change notification, command building and queues. */
#include <errno.h>
#include <string.h>
#include "central.h"

central_t g_c;
cmdq_t    g_cmdq[NODE_SLOTS];

static pthread_mutex_t seq_m = PTHREAD_MUTEX_INITIALIZER;
static uint32_t        seq_ctr;          /* central's own message sequence number */

uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

void abs_deadline(struct timespec *ts, unsigned ms)
{
    clock_gettime(CLOCK_MONOTONIC, ts);
    ts->tv_sec  += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) { ts->tv_sec++; ts->tv_nsec -= 1000000000L; }
}

/* Timed waits use the monotonic clock so a change of wall-clock time can't break them. */
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
    pthread_mutex_init(&g_c.chg_m, NULL);
    cond_init_mono(&g_c.chg_cv);

    for (int n = 0; n < NODE_SLOTS; n++) {
        g_c.node[n].link             = LINK_OFFLINE;
        g_c.node[n].desired_mode     = MODE_FIXED;      /* intersections boot in FIXED */
        g_c.node[n].desired_override = OVERRIDE_NONE;
        pthread_mutex_init(&g_cmdq[n].m, NULL);
        cond_init_mono(&g_cmdq[n].cv);
    }
    g_c.sched_enabled = 1;
    g_c.sim_min       = SIM_START_MIN;
    g_c.pattern       = -1;
}

/* Wake every thread waiting for "the table changed" (the dashboard). */
void table_changed(void)
{
    pthread_mutex_lock(&g_c.chg_m);
    g_c.chg_seq++;
    pthread_cond_broadcast(&g_c.chg_cv);
    pthread_mutex_unlock(&g_c.chg_m);
}

/* Block until the change counter moves past `last` or timeout_ms passes. */
unsigned table_wait_change(unsigned last, unsigned timeout_ms)
{
    struct timespec dl;
    abs_deadline(&dl, timeout_ms);
    pthread_mutex_lock(&g_c.chg_m);
    while (g_c.chg_seq == last)
        if (pthread_cond_timedwait(&g_c.chg_cv, &g_c.chg_m, &dl) == ETIMEDOUT)
            break;
    unsigned s = g_c.chg_seq;
    pthread_mutex_unlock(&g_c.chg_m);
    return s;
}

static uint32_t next_seq(void)
{
    pthread_mutex_lock(&seq_m);
    uint32_t s = ++seq_ctr;
    pthread_mutex_unlock(&seq_m);
    return s;
}

/* ---------- building the team's command messages ---------- */

static void fill_header(msg_header_t *h, uint16_t type, uint16_t subtype)
{
    memset(h, 0, sizeof *h);
    h->type     = type;
    h->subtype  = subtype;
    h->sender   = NODE_CENTRAL;
    h->sequence = next_seq();
}

void build_set_mode(cmd_t *c, operation_mode_t mode)
{
    memset(c, 0, sizeof *c);
    fill_header(&c->m.header, MSG_SET_MODE, SUBTYPE_NONE);
    c->m.set_mode.mode = (uint8_t)mode;
    c->len = sizeof c->m.set_mode;
}

/* OVERRIDE_NONE clears an override: there is no separate "clear override" message. */
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

/* ---------- queues ---------- */

/* Producer side (menu + scheduler). Never touches the network, so the UI stays
 * responsive even when a node is down. */
int cmdq_push(node_id_t n, const cmd_t *c)
{
    cmdq_t *q = &g_cmdq[n];

    /* Intersection offline: don't queue. The caller has already updated desired_*,
     * and resync sends that the moment the intersection reappears. (A clear-fault
     * is simply dropped: a restarted intersection boots out of fault anyway.)
     * X1 has no heartbeat, so its commands are always queued and retried. */
    if (n != NODE_X1) {
        pthread_rwlock_wrlock(&g_c.lock);
        int offline = g_c.node[n].link != LINK_ONLINE;
        if (offline) {
            g_c.node[n].last_cmd_seq  = c->m.header.sequence;
            g_c.node[n].last_cmd_type = c->m.header.type;
            g_c.node[n].last_cmd_res  = CMDRES_WAIT;
        }
        pthread_rwlock_unlock(&g_c.lock);
        if (offline) {
            log_msg("%s: offline - %s #%u will be applied by resync on reconnect", node_str(n),
                    msgtype_str(c->m.header.type), c->m.header.sequence);
            table_changed();
            return 0;
        }
    }

    pthread_mutex_lock(&q->m);
    if (q->count == CMDQ_LEN) {
        pthread_mutex_unlock(&q->m);
        log_msg("%s: command queue full, dropped %s", node_str(n), msgtype_str(c->m.header.type));
        return -1;
    }
    q->q[(q->head + q->count) % CMDQ_LEN] = *c;
    q->count++;
    pthread_cond_signal(&q->cv);
    pthread_mutex_unlock(&q->m);

    pthread_rwlock_wrlock(&g_c.lock);
    g_c.node[n].last_cmd_seq  = c->m.header.sequence;
    g_c.node[n].last_cmd_type = c->m.header.type;
    g_c.node[n].last_cmd_res  = CMDRES_PENDING;
    pthread_rwlock_unlock(&g_c.lock);

    log_msg("%s: queued %s #%u", node_str(n), msgtype_str(c->m.header.type), c->m.header.sequence);
    table_changed();
    return 0;
}

/* Called by the RX thread when an intersection comes online or restarts. */
void cmdq_request_resync(node_id_t n)
{
    cmdq_t *q = &g_cmdq[n];
    pthread_mutex_lock(&q->m);
    q->resync = 1;
    pthread_cond_signal(&q->cv);
    pthread_mutex_unlock(&q->m);
}