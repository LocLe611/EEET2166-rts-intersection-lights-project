/* rx.c — central's server thread. Receives MSG_STATUS from I1/I2 and MSG_TRAIN
 * from X1 on the global name SVC_CENTRAL, updates the node table, and replies
 * with an explicit msg_reply_t to every message (rule 8). */
#include <errno.h>
#include <string.h>
#include <sys/dispatch.h>
#include <sys/neutrino.h>
#include "central.h"

/* Stale/duplicate check (rule 6). A sender that restarts begins again at seq 1. */
static int is_stale(const node_info_t *ni, uint32_t seq)
{
    return ni->have_data && seq <= ni->last_rx_seq && seq != 1;
}

static uint16_t on_status(const msg_status_t *s)
{
    node_id_t n = (node_id_t)s->hdr.src;
    if (n != NODE_I1 && n != NODE_I2) return RES_BAD_MSG;

    pthread_rwlock_wrlock(&g_c.lock);
    node_info_t *ni = &g_c.node[n];
    if (is_stale(ni, s->hdr.seq)) {
        pthread_rwlock_unlock(&g_c.lock);
        return RES_STALE;
    }
    int came_online = (ni->link == LINK_OFFLINE);
    int restarted   = ni->have_data && s->hdr.seq == 1 && ni->last_rx_seq > 1;
    ni->link         = LINK_ONLINE;
    ni->last_seen_ms = now_ms();
    ni->last_rx_seq  = s->hdr.seq;
    ni->st           = *s;
    ni->have_data    = 1;
    pthread_rwlock_unlock(&g_c.lock);

    if (came_online) log_msg("%s: ONLINE", node_str(n));
    if (restarted)   log_msg("%s: restart detected (seq back to 1)", node_str(n));
    if (came_online || restarted) cmdq_request_resync(n);   /* push desired mode back */

    if (s->hdr.subtype == ST_CHANGE)
        log_msg("%s: %-11s NS=%s EW=%s ped NS=%s EW=%s mode=%s",
                node_str(n), phase_str(s->phase), colour_str(s->ns), colour_str(s->ew),
                ped_str(s->ns_ped), ped_str(s->ew_ped), mode_str(s->mode));
    table_changed();
    return RES_OK;
}

static uint16_t on_train(const msg_train_t *t)
{
    if (t->hdr.src != NODE_X1) return RES_BAD_MSG;
    if (t->hdr.subtype < TR_BOOMS_UP || t->hdr.subtype > TR_BOOM_FAULT) return RES_BAD_MSG;

    pthread_rwlock_wrlock(&g_c.lock);
    node_info_t *ni = &g_c.node[NODE_X1];
    if (is_stale(ni, t->hdr.seq)) {
        pthread_rwlock_unlock(&g_c.lock);
        return RES_STALE;
    }
    int came_online = (ni->link == LINK_OFFLINE);
    int changed     = !ni->have_data || ni->tr.hdr.subtype != t->hdr.subtype
                      || ni->tr.line != t->line;
    int new_alarm   = (t->hdr.subtype == TR_BOOM_FAULT && !g_c.boom_alarm);
    if (t->hdr.subtype == TR_BOOM_FAULT) g_c.boom_alarm = 1;   /* latched */
    ni->link         = LINK_ONLINE;
    ni->last_seen_ms = now_ms();
    ni->last_rx_seq  = t->hdr.seq;
    ni->tr           = *t;
    ni->have_data    = 1;
    pthread_rwlock_unlock(&g_c.lock);

    if (came_online) log_msg("X1: ONLINE");
    if (changed)     log_msg("X1: %s (line %u)", train_str(t->hdr.subtype), t->line);
    if (new_alarm)   log_msg("ALARM: boom gate fault reported by X1 (line %u)", t->line);
    table_changed();
    return RES_OK;
}

void *rx_thread(void *arg)
{
    name_attach_t *att = (name_attach_t *)arg;
    msg_rx_t msg;                                   /* receive union (rule 5) */
    struct _msg_info info;

    for (;;) {
        int rcvid = MsgReceive(att->chid, &msg, sizeof msg, &info);
        if (rcvid == -1) continue;

        if (rcvid == 0) {                           /* pulse */
            switch (msg.pulse.code) {
            case _PULSE_CODE_DISCONNECT:            /* a client called name_close / died */
                ConnectDetach(msg.pulse.scoid);
                break;
            case _PULSE_CODE_UNBLOCK:
            default:
                break;
            }
            continue;
        }

        if (msg.hdr.type == _IO_CONNECT) {          /* from a client's name_open() */
            MsgReply(rcvid, EOK, NULL, 0);
            continue;
        }
        if (msg.hdr.type > _IO_BASE && msg.hdr.type <= _IO_MAX) {
            MsgError(rcvid, ENOSYS);                /* other system I/O: not ours */
            continue;
        }

        msg_reply_t r = { .result = RES_BAD_MSG, .src = NODE_CENTRAL, .seq = msg.hdr.seq };
        switch (msg.hdr.type) {
        case MSG_STATUS:
            if (info.srcmsglen >= sizeof msg.status) r.result = on_status(&msg.status);
            break;
        case MSG_TRAIN:
            if (info.srcmsglen >= sizeof msg.train)  r.result = on_train(&msg.train);
            break;
        default:
            break;
        }
        if (r.result == RES_BAD_MSG)
            log_msg("RX: bad message type 0x%x from src %u", msg.hdr.type, msg.hdr.src);
        MsgReply(rcvid, EOK, &r, sizeof r);
    }
    return NULL;
}
