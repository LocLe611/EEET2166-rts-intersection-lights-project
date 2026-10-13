/* common.c — shared state, change notification and per-intersection command queues. */
#include <errno.h>
#include <string.h>
#include "central.h"

central_t g_c;
cmdq_t    g_cmdq[NODE_COUNT];

static pthread_mutex_t seq_m = PTHREAD_MUTEX_INITIALIZER;
static uint32_t        seq_ctr;          /* central's per-sender sequence number */

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

/* Timed waits use the monotonic clock so a wall-clock change can't break them. */
void cond_init_mono(pthread_cond_t *cv)
{
    pthread_condattr_t a;
    pthread_condattr_init(&a);
    pthread_condattr_setclock(&a, CLOCK_MONOTONIC);
    pthread_cond_init(cv, &a);
    pthread_condattr_destroy(&a);
}

void ctl_init(void)
{
    static const uint32_t def_timing[TIM_COUNT] = TIMING_DEFAULT;

    memset(&g_c, 0, sizeof g_c);
    pthread_rwlock_init(&g_c.lock, NULL);
    pthread_mutex_init(&g_c.chg_m, NULL);
    cond_init_mono(&g_c.chg_cv);

    for (int n = 0; n < NODE_COUNT; n++) {
        g_c.node[n].link         = LINK_OFFLINE;
        g_c.node[n].desired_mode = MODE_FIXED;
        memcpy(g_c.node[n].desired_timing, def_timing, sizeof def_timing);
        pthread_mutex_init(&g_cmdq[n].m, NULL);
        cond_init_mono(&g_cmdq[n].cv);
    }
    g_c.sched_enabled = 1;
    g_c.sim_min       = SIM_START_MIN;
    g_c.pattern       = -1;
}

/* Wake every thread waiting for "the table changed" (dashboard, display link). */
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

uint32_t next_seq(void)
{
    pthread_mutex_lock(&seq_m);
    uint32_t s = ++seq_ctr;
    pthread_mutex_unlock(&seq_m);
    return s;
}

void fill_cmd(msg_cmd_t *c, uint16_t sub, int32_t value, const uint32_t *timing)
{
    memset(c, 0, sizeof *c);
    c->hdr.type    = MSG_CMD;
    c->hdr.subtype = sub;
    c->hdr.src     = NODE_CENTRAL;
    c->hdr.seq     = next_seq();
    c->value       = value;
    if (timing) memcpy(c->timing_ms, timing, sizeof c->timing_ms);
}

/* Producer side (menu + scheduler). Never touches the network, so the UI
 * stays responsive even when a node is down. */
int cmdq_push(node_id_t n, uint16_t sub, int32_t value, const uint32_t *timing)
{
    cmdq_t *q = &g_cmdq[n];
    msg_cmd_t c;
    fill_cmd(&c, sub, value, timing);

    pthread_mutex_lock(&q->m);
    if (q->count == CMDQ_LEN) {
        pthread_mutex_unlock(&q->m);
        log_msg("%s: command queue full, dropped %s", node_str(n), cmd_str(sub));
        return -1;
    }
    q->q[(q->head + q->count) % CMDQ_LEN] = c;
    q->count++;
    pthread_cond_signal(&q->cv);
    pthread_mutex_unlock(&q->m);

    pthread_rwlock_wrlock(&g_c.lock);
    g_c.node[n].last_cmd_seq = c.hdr.seq;
    g_c.node[n].last_cmd_sub = sub;
    g_c.node[n].last_cmd_res = CMDRES_PENDING;
    pthread_rwlock_unlock(&g_c.lock);

    log_msg("%s: queued %s #%u (value %d)", node_str(n), cmd_str(sub), c.hdr.seq, value);
    table_changed();
    return 0;
}

/* Called by the RX thread when a node comes online or restarts. */
void cmdq_request_resync(node_id_t n)
{
    cmdq_t *q = &g_cmdq[n];
    pthread_mutex_lock(&q->m);
    q->resync = 1;
    pthread_cond_signal(&q->cv);
    pthread_mutex_unlock(&q->m);
}
