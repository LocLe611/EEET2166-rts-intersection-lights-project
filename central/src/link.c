/* link.c — outbound links. One thread per intersection (a dead I1 can never
 * stall I2) plus one for the display node. */
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/dispatch.h>
#include <sys/neutrino.h>
#include "central.h"

static const char *svc_for(node_id_t n) { return n == NODE_I1 ? SVC_I1 : SVC_I2; }

static void set_cmd_state(node_id_t n, const msg_cmd_t *c, cmdres_t res)
{
    pthread_rwlock_wrlock(&g_c.lock);
    g_c.node[n].last_cmd_seq = c->hdr.seq;
    g_c.node[n].last_cmd_sub = c->hdr.subtype;
    g_c.node[n].last_cmd_res = res;
    pthread_rwlock_unlock(&g_c.lock);
    table_changed();
}

/* Open the link if needed and send one command. Returns the result code from
 * the intersection's reply, or -1 if the link is down (command is kept). */
static int send_cmd(node_id_t n, int *coid, msg_cmd_t *c)
{
    if (*coid == -1) {
        *coid = name_open(svc_for(n), NAME_FLAG_ATTACH_GLOBAL);
        if (*coid == -1) return -1;
        log_msg("%s: command link opened", node_str(n));
    }
    msg_reply_t r;
    if (MsgSend(*coid, c, sizeof *c, &r, sizeof r) == -1) {
        log_msg("%s: MsgSend failed (%s), closing link", node_str(n), strerror(errno));
        name_close(*coid);
        *coid = -1;
        return -1;
    }
    /* RES_STALE means the node already had it (our reply was lost): treat as done. */
    set_cmd_state(n, c, (r.result == RES_OK || r.result == RES_STALE) ? CMDRES_ACK : CMDRES_REJECTED);
    log_msg("%s: %s #%u -> %s", node_str(n), cmd_str(c->hdr.subtype), c->hdr.seq, result_str(r.result));
    return r.result;
}

/* Wait RECONNECT_MS before retrying, without sleep(): timed wait on the queue condvar. */
static void backoff(cmdq_t *q)
{
    struct timespec dl;
    abs_deadline(&dl, RECONNECT_MS);
    pthread_mutex_lock(&q->m);
    while (pthread_cond_timedwait(&q->cv, &q->m, &dl) != ETIMEDOUT)
        ;
    pthread_mutex_unlock(&q->m);
}

/* After a node (re)appears, push the state central wants it to be in. */
static int resync(node_id_t n, int *coid)
{
    tl_mode_t mode; int ovr; uint32_t timing[TIM_COUNT];
    pthread_rwlock_rdlock(&g_c.lock);
    mode = g_c.node[n].desired_mode;
    ovr  = g_c.node[n].override_dir;
    memcpy(timing, g_c.node[n].desired_timing, sizeof timing);
    pthread_rwlock_unlock(&g_c.lock);

    log_msg("%s: resync -> timing, mode %s%s", node_str(n), mode_str(mode), ovr ? ", override" : "");
    msg_cmd_t c;
    fill_cmd(&c, CMD_SET_TIMING, 0, timing);
    if (send_cmd(n, coid, &c) < 0) return -1;
    fill_cmd(&c, CMD_SET_MODE, mode, NULL);
    if (send_cmd(n, coid, &c) < 0) return -1;
    if (ovr) {
        fill_cmd(&c, CMD_OVERRIDE, ovr, NULL);
        if (send_cmd(n, coid, &c) < 0) return -1;
    }
    return 0;
}

void *link_thread(void *arg)
{
    node_id_t n = (node_id_t)(intptr_t)arg;
    cmdq_t *q   = &g_cmdq[n];
    int coid    = -1;

    for (;;) {
        msg_cmd_t c;
        int do_resync = 0;

        pthread_mutex_lock(&q->m);
        while (q->count == 0 && !q->resync)
            pthread_cond_wait(&q->cv, &q->m);
        if (q->resync) { do_resync = 1; q->resync = 0; }
        else           c = q->q[q->head];                 /* peek, pop after success */
        pthread_mutex_unlock(&q->m);

        if (do_resync) {
            if (resync(n, &coid) < 0) { cmdq_request_resync(n); backoff(q); }
            continue;
        }
        if (send_cmd(n, &coid, &c) < 0) {                 /* link down: keep it, retry */
            set_cmd_state(n, &c, CMDRES_RETRY);
            backoff(q);
            continue;
        }
        pthread_mutex_lock(&q->m);                        /* only this thread pops */
        q->head = (q->head + 1) % CMDQ_LEN;
        q->count--;
        pthread_mutex_unlock(&q->m);
    }
    return NULL;
}

/* Display link: always sends the NEWEST snapshot (never queues), on every
 * table change and at least once per HEARTBEAT_MS. A dead display can't back up central. */
static void set_display_link(link_t l)
{
    int changed;
    pthread_rwlock_wrlock(&g_c.lock);
    changed = g_c.node[NODE_DISPLAY].link != l;
    g_c.node[NODE_DISPLAY].link = l;
    pthread_rwlock_unlock(&g_c.lock);
    if (changed) {
        log_msg("DISPLAY: %s", l == LINK_ONLINE ? "ONLINE" : "OFFLINE");
        table_changed();
    }
}

void *display_link_thread(void *arg)
{
    (void)arg;
    int coid = -1;
    unsigned seen = 0;
    uint64_t next_try = 0;

    for (;;) {
        seen = table_wait_change(seen, HEARTBEAT_MS);

        if (coid == -1) {
            if (now_ms() < next_try) continue;
            coid = name_open(SVC_DISPLAY, NAME_FLAG_ATTACH_GLOBAL);
            if (coid == -1) { next_try = now_ms() + RECONNECT_MS; set_display_link(LINK_OFFLINE); continue; }
        }

        msg_snapshot_t s;
        memset(&s, 0, sizeof s);
        s.hdr.type = MSG_SNAPSHOT;
        s.hdr.src  = NODE_CENTRAL;
        s.hdr.seq  = next_seq();
        pthread_rwlock_rdlock(&g_c.lock);
        s.link[0] = (uint8_t)g_c.node[NODE_I1].link;
        s.link[1] = (uint8_t)g_c.node[NODE_I2].link;
        s.link[2] = (uint8_t)g_c.node[NODE_X1].link;
        s.i1 = g_c.node[NODE_I1].st;
        s.i2 = g_c.node[NODE_I2].st;
        s.x1 = g_c.node[NODE_X1].tr;
        pthread_rwlock_unlock(&g_c.lock);

        msg_reply_t r;
        if (MsgSend(coid, &s, sizeof s, &r, sizeof r) == -1) {
            name_close(coid);
            coid = -1;
            next_try = now_ms() + RECONNECT_MS;
            set_display_link(LINK_OFFLINE);
            continue;
        }
        set_display_link(LINK_ONLINE);
    }
    return NULL;
}
