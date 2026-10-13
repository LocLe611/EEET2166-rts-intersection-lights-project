/* link.c — outbound command links. One thread per node (I1, I2, X1), so a node that
 * is down or slow can never hold up commands to another. Nodes are reached by their
 * Qnet path, the same way the intersection and X1 reach central. */
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/dispatch.h>
#include <sys/neutrino.h>
#include "central.h"

static const char *path_for(node_id_t n)
{
    switch (n) {
    case NODE_I1: return QNET_SERVICE_PATH(I1_HOSTNAME, I1_SERVICE);
    case NODE_I2: return QNET_SERVICE_PATH(I2_HOSTNAME, I2_SERVICE);
    default:      return QNET_SERVICE_PATH(X1_HOSTNAME, X1_SERVICE);
    }
}

static void set_cmd_state(node_id_t n, const cmd_t *c, cmdres_t res)
{
    pthread_rwlock_wrlock(&g_c.lock);
    g_c.node[n].last_cmd_seq  = c->m.header.sequence;
    g_c.node[n].last_cmd_type = c->m.header.type;
    g_c.node[n].last_cmd_res  = res;
    pthread_rwlock_unlock(&g_c.lock);
    table_changed();
}

/* X1 sends no heartbeat, so a failed send is our only evidence that it is down. */
static void x1_link_down(node_id_t n)
{
    if (n != NODE_X1 || X1_SENDS_HEARTBEAT) return;
    pthread_rwlock_wrlock(&g_c.lock);
    int was = g_c.node[NODE_X1].link;
    g_c.node[NODE_X1].link = LINK_OFFLINE;
    pthread_rwlock_unlock(&g_c.lock);
    if (was == LINK_ONLINE) log_msg("X1: OFFLINE (command could not be delivered)");
}

/* What the link thread should do with a command after trying to send it. */
typedef enum { SENT_DONE, SENT_RETRY } sent_t;

static sent_t send_cmd(node_id_t n, int *coid, const cmd_t *c)
{
    if (*coid == -1) {
        *coid = name_open(path_for(n), 0);
        if (*coid == -1) { x1_link_down(n); return SENT_RETRY; }
        log_msg("%s: command link opened", node_str(n));
    }

    msg_reply_t r;
    memset(&r, 0, sizeof r);
    uint64_t ns = (uint64_t)IPC_SEND_TIMEOUT_MS * 1000000ULL;
    /* TimerTimeout covers only the NEXT kernel call: nothing may go in between. */
    TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_SEND | _NTO_TIMEOUT_REPLY, NULL, &ns, NULL);
    if (MsgSend(*coid, &c->m, c->len, &r, sizeof r) == -1) {
        if (errno == ENOSYS) {                 /* node is up but doesn't know this message */
            set_cmd_state(n, c, CMDRES_REJECTED);
            log_msg("%s: %s #%u not supported by node", node_str(n),
                    msgtype_str(c->m.header.type), c->m.header.sequence);
            return SENT_DONE;                  /* retrying won't help */
        }
        log_msg("%s: send failed (%s), closing link", node_str(n), strerror(errno));
        name_close(*coid);
        *coid = -1;
        x1_link_down(n);
        return SENT_RETRY;                     /* down or timed out: keep the command */
    }

    switch (r.result) {
    case RESULT_OK:
        set_cmd_state(n, c, CMDRES_ACK);
        break;
    case RESULT_BUSY:
        set_cmd_state(n, c, CMDRES_RETRY);
        log_msg("%s: %s #%u -> BUSY, will retry", node_str(n), msgtype_str(c->m.header.type),
                c->m.header.sequence);
        return SENT_RETRY;
    default:                                   /* RESULT_REJECTED / RESULT_INVALID */
        set_cmd_state(n, c, CMDRES_REJECTED);
        break;
    }
    log_msg("%s: %s #%u -> %s", node_str(n), msgtype_str(c->m.header.type),
            c->m.header.sequence, result_str(r.result));
    return SENT_DONE;
}

/* Wait RECONNECT_MS before retrying, without sleep(): a timed wait on the queue condvar. */
static void backoff(cmdq_t *q)
{
    struct timespec dl;
    abs_deadline(&dl, RECONNECT_MS);
    pthread_mutex_lock(&q->m);
    while (pthread_cond_timedwait(&q->cv, &q->m, &dl) != ETIMEDOUT)
        ;
    pthread_mutex_unlock(&q->m);
}

/* An intersection (re)appeared: put it back in the state central wants.
 * Anything still queued is dropped first: the menu and scheduler always update
 * desired_* BEFORE queuing, so the two messages below already include it. */
static int resync(node_id_t n, int *coid)
{
    cmdq_t *q = &g_cmdq[n];
    pthread_mutex_lock(&q->m);
    int dropped = q->count;
    q->head = q->count = 0;
    pthread_mutex_unlock(&q->m);
    if (dropped) log_msg("%s: %d queued command(s) superseded by resync", node_str(n), dropped);

    operation_mode_t mode;
    override_t ovr;
    pthread_rwlock_rdlock(&g_c.lock);
    mode = g_c.node[n].desired_mode;
    ovr  = g_c.node[n].desired_override;
    pthread_rwlock_unlock(&g_c.lock);

    log_msg("%s: resync -> mode %s, override %s", node_str(n), mode_str(mode), override_str(ovr));
    cmd_t c;
    build_set_mode(&c, mode);
    if (send_cmd(n, coid, &c) == SENT_RETRY) return -1;
    build_override(&c, ovr);
    if (send_cmd(n, coid, &c) == SENT_RETRY) return -1;
    return 0;
}

void *link_thread(void *arg)
{
    node_id_t n = (node_id_t)(intptr_t)arg;
    cmdq_t *q   = &g_cmdq[n];
    int coid    = -1;

    for (;;) {
        cmd_t c;
        int do_resync = 0;

        pthread_mutex_lock(&q->m);
        while (q->count == 0 && !q->resync)
            pthread_cond_wait(&q->cv, &q->m);        /* sleep until there is work */
        if (q->resync) { do_resync = 1; q->resync = 0; }
        else           c = q->q[q->head];             /* peek; remove only once delivered */
        pthread_mutex_unlock(&q->m);

        if (do_resync) {
            if (resync(n, &coid) < 0) { cmdq_request_resync(n); backoff(q); }
            continue;
        }
        if (send_cmd(n, &coid, &c) == SENT_RETRY) {
            set_cmd_state(n, &c, CMDRES_RETRY);
            backoff(q);
            continue;
        }
        pthread_mutex_lock(&q->m);                    /* only this thread removes */
        q->head = (q->head + 1) % CMDQ_LEN;
        q->count--;
        pthread_mutex_unlock(&q->m);
    }
    return NULL;
}