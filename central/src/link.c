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

static int name_flags(void)
{
    return IPC_USE_GLOBAL_NAMES ? NAME_FLAG_ATTACH_GLOBAL : 0;
}

static void set_cmd_state(node_id_t n, const cmd_t *c, cmdres_t res)
{
    pthread_rwlock_wrlock(&g_c.lock);
    g_c.node[n].last_cmd_seq = c->m.header.sequence;
    g_c.node[n].last_cmd_type = c->m.header.type;
    g_c.node[n].last_cmd_res = res;
    pthread_rwlock_unlock(&g_c.lock);
}

typedef enum { SENT_DONE, SENT_RETRY } sent_t;

static sent_t send_cmd(node_id_t n, int *coid, const cmd_t *c)
{
    msg_reply_t r;
    uint64_t ns;

    if (*coid == -1) {
        *coid = name_open(path_for(n), name_flags());
        if (*coid == -1)
            return SENT_RETRY;
        log_msg("%s: command link opened", node_str(n));
    }

    memset(&r, 0, sizeof r);
    ns = (uint64_t)IPC_SEND_TIMEOUT_MS * 1000000ULL;
    TimerTimeout(CLOCK_MONOTONIC,
                 _NTO_TIMEOUT_SEND | _NTO_TIMEOUT_REPLY,
                 NULL, &ns, NULL);

    if (MsgSend(*coid, &c->m, c->len, &r, sizeof r) == -1) {
        if (errno == ENOSYS) {
            set_cmd_state(n, c, CMDRES_REJECTED);
            log_msg("%s: %s #%u not supported",
                    node_str(n), msgtype_str(c->m.header.type), c->m.header.sequence);
            return SENT_DONE;
        }

        log_msg("%s: send failed (%s)", node_str(n), strerror(errno));
        name_close(*coid);
        *coid = -1;
        return SENT_RETRY;
    }


    if (r.sequence != c->m.header.sequence) {
        set_cmd_state(n, c, CMDRES_RETRY);
        log_msg("%s: reply sequence mismatch for %s #%u",
                node_str(n), msgtype_str(c->m.header.type),
                c->m.header.sequence);
        name_close(*coid);
        *coid = -1;
        return SENT_RETRY;
    }

    if (r.result == RESULT_OK) {
        set_cmd_state(n, c, CMDRES_ACK);
    } else if (r.result == RESULT_BUSY) {
        set_cmd_state(n, c, CMDRES_RETRY);
        log_msg("%s: %s #%u -> BUSY, retrying",
                node_str(n), msgtype_str(c->m.header.type), c->m.header.sequence);
        return SENT_RETRY;
    } else {
        set_cmd_state(n, c, CMDRES_REJECTED);
    }

    log_msg("%s: %s #%u -> %s",
            node_str(n), msgtype_str(c->m.header.type),
            c->m.header.sequence, result_str(r.result));
    return SENT_DONE;
}

static void backoff(cmdq_t *q)
{
    struct timespec dl;

    abs_deadline(&dl, RECONNECT_MS);
    pthread_mutex_lock(&q->m);
    pthread_cond_timedwait(&q->cv, &q->m, &dl);
    pthread_mutex_unlock(&q->m);
}

static int resync(node_id_t n, int *coid)
{
    operation_mode_t mode;
    override_t ovr;
    cmd_t c;

    if (n == NODE_X1)
        return 0;

    pthread_rwlock_rdlock(&g_c.lock);
    mode = g_c.node[n].desired_mode;
    ovr = g_c.node[n].desired_override;
    pthread_rwlock_unlock(&g_c.lock);

    log_msg("%s: resync -> mode %s, override %s",
            node_str(n), mode_str(mode), override_str(ovr));

    build_set_mode(&c, mode);
    if (send_cmd(n, coid, &c) == SENT_RETRY)
        return -1;

    build_override(&c, ovr);
    if (send_cmd(n, coid, &c) == SENT_RETRY)
        return -1;

    return 0;
}

void *link_thread(void *arg)
{
    node_id_t n = (node_id_t)(intptr_t)arg;
    cmdq_t *q = &g_cmdq[n];
    int coid = -1;

    for (;;) {
        cmd_t c;
        int do_resync = 0;

        pthread_mutex_lock(&q->m);
        while (q->count == 0 && !q->resync)
            pthread_cond_wait(&q->cv, &q->m);

        if (q->resync) {
            do_resync = 1;
            q->resync = 0;
        } else {
            c = q->q[q->head];
        }
        pthread_mutex_unlock(&q->m);

        if (do_resync) {
            if (resync(n, &coid) < 0) {
                cmdq_request_resync(n);
                backoff(q);
            }
            continue;
        }

        if (send_cmd(n, &coid, &c) == SENT_RETRY) {
            set_cmd_state(n, &c, CMDRES_RETRY);
            backoff(q);
            continue;
        }

        pthread_mutex_lock(&q->m);
        q->head = (q->head + 1) % CMDQ_LEN;
        q->count--;
        pthread_mutex_unlock(&q->m);
    }

    return NULL;
}
