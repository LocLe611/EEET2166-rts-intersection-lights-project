/* rx.c — central's server thread. Receives status from I1/I2 (MSG_LIGHT_STATUS,
 * MSG_INTERSECTION_STATUS) and X1 (MSG_RAILWAY_STATUS, MSG_FAULT_STATUS), updates
 * the node table, and replies to every message with an explicit msg_reply_t. */
#include <errno.h>
#include <string.h>
#include <sys/dispatch.h>
#include <sys/neutrino.h>
#include <sys/iomsg.h>
#include "central.h"

/* Old or duplicate message? A sender that restarts begins again at sequence 1. */
static int is_stale(const node_info_t *ni, uint32_t seq)
{
    return ni->have_data && seq <= ni->last_rx_seq && seq != 1;
}

/* Common bookkeeping for any accepted message. Called with the write lock held.
 * Returns flags describing what changed. */
#define F_CAME_ONLINE 1
#define F_RESTARTED   2
static int mark_heard(node_info_t *ni, uint32_t seq)
{
    int f = 0;
    if (ni->link == LINK_OFFLINE)                                f |= F_CAME_ONLINE;
    if (ni->have_data && seq == 1 && ni->last_rx_seq > 1)        f |= F_RESTARTED;
    ni->link         = LINK_ONLINE;
    ni->last_seen_ms = now_ms();
    ni->last_rx_seq  = seq;
    ni->have_data    = 1;
    return f;
}

static int32_t on_status(uint16_t type, const intersection_status_msg_t *s)
{
    node_id_t n = (node_id_t)s->header.sender;
    if (n != NODE_I1 && n != NODE_I2) return RESULT_INVALID;

    pthread_rwlock_wrlock(&g_c.lock);
    node_info_t *ni = &g_c.node[n];
    if (is_stale(ni, s->header.sequence)) {
        pthread_rwlock_unlock(&g_c.lock);
        return RESULT_REJECTED;
    }
    intersection_status_msg_t old = ni->ist;
    int had    = ni->have_data;
    int f      = mark_heard(ni, s->header.sequence);
    ni->ist    = *s;
    int fault_now = s->intersection_state == INTERSECTION_FAULT &&
                    (!had || old.intersection_state != INTERSECTION_FAULT);
    if (fault_now) g_c.alarms |= 1u << n;
    pthread_rwlock_unlock(&g_c.lock);

    if (f & F_CAME_ONLINE) log_msg("%s: ONLINE", node_str(n));
    if (f & F_RESTARTED)   log_msg("%s: restart detected (sequence back to 1)", node_str(n));
    if (f & (F_CAME_ONLINE | F_RESTARTED)) cmdq_request_resync(n);   /* put it back in step */

    if (type == MSG_LIGHT_STATUS)
        log_msg("%s: %-11s R3=%s %s=%s ped=%s", node_str(n), phase_str(s->phase),
                light_str(s->main_light), side_road(n), light_str(s->side_light), ped_str(s->ped_light));
    if (had && old.intersection_state != s->intersection_state)
        log_msg("%s: state %s", node_str(n), istate_str(s->intersection_state));
    if (had && old.mode != s->mode)
        log_msg("%s: now in %s mode", node_str(n), mode_str(s->mode));
    if (had && old.override != s->override)
        log_msg("%s: override %s", node_str(n), override_str(s->override));
    if (fault_now)
        log_msg("ALARM: %s entered FAULT (flashing) - clear with [3]", node_str(n));

    table_changed();
    return RESULT_OK;
}

static int32_t on_rail(const railway_status_msg_t *r)
{
    if (r->header.sender != NODE_X1) return RESULT_INVALID;

    pthread_rwlock_wrlock(&g_c.lock);
    node_info_t *ni = &g_c.node[NODE_X1];
    if (is_stale(ni, r->header.sequence)) {
        pthread_rwlock_unlock(&g_c.lock);
        return RESULT_REJECTED;
    }
    railway_status_msg_t old = ni->rail;
    int had = ni->have_data;
    int f   = mark_heard(ni, r->header.sequence);
    ni->rail = *r;
    int fault_now = r->railway_state == RAIL_FAULT && (!had || old.railway_state != RAIL_FAULT);
    if (fault_now) g_c.alarms |= 1u << NODE_X1;
    pthread_rwlock_unlock(&g_c.lock);

    if (f & F_CAME_ONLINE) log_msg("X1: ONLINE");
    if (f & F_RESTARTED)   log_msg("X1: restart detected (sequence back to 1)");
    if (!had || old.railway_state != r->railway_state || old.gate_state != r->gate_state)
        log_msg("X1: railway %s, gate %s, train signal %s", rail_str(r->railway_state),
                gate_str(r->gate_state), r->train_signal == TRAIN_SIGNAL_GREEN ? "GREEN" : "RED");
    if (fault_now) log_msg("ALARM: X1 railway FAULT (flags 0x%X)", (unsigned)r->fault_flags);
    table_changed();
    return RESULT_OK;
}

static int32_t on_x1_fault(const fault_status_msg_t *fs)
{
    if (fs->header.sender != NODE_X1) return RESULT_INVALID;

    pthread_rwlock_wrlock(&g_c.lock);
    node_info_t *ni = &g_c.node[NODE_X1];
    if (is_stale(ni, fs->header.sequence)) {
        pthread_rwlock_unlock(&g_c.lock);
        return RESULT_REJECTED;
    }
    mark_heard(ni, fs->header.sequence);
    ni->fault      = *fs;
    ni->have_fault = 1;
    g_c.alarms    |= 1u << NODE_X1;
    pthread_rwlock_unlock(&g_c.lock);

    log_msg("ALARM: X1 fault report flags 0x%X%s%s, safe state %s", (unsigned)fs->fault_flags,
            (fs->fault_flags & X1_FAULT_GATE_TIMEOUT) ? " [gate timeout]" : "",
            (fs->fault_flags & X1_FAULT_GATE_SENSOR)  ? " [gate sensor]"  : "",
            fs->safe_state_applied ? "applied" : "NOT applied");
    table_changed();
    return RESULT_OK;
}

void *rx_thread(void *arg)
{
    name_attach_t *att = (name_attach_t *)arg;
    central_rx_t msg;
    struct _msg_info info;

    for (;;) {
        int rcvid = MsgReceive(att->chid, &msg, sizeof msg, &info);
        if (rcvid == -1) continue;

        if (rcvid == 0) {                                   /* pulse */
            if (msg.pulse.code == _PULSE_CODE_DISCONNECT)   /* a client closed / died */
                ConnectDetach(msg.pulse.scoid);
            continue;
        }
        if (msg.header.type == _IO_CONNECT) {               /* from a client's name_open() */
            MsgReply(rcvid, EOK, NULL, 0);
            continue;
        }
        if (msg.header.type > _IO_BASE && msg.header.type <= _IO_MAX) {
            MsgError(rcvid, ENOSYS);                        /* other system I/O: not ours */
            continue;
        }

        msg_reply_t r = { .result = RESULT_INVALID, .sequence = msg.header.sequence };
        switch (msg.header.type) {
        case MSG_LIGHT_STATUS:                              /* I1/I2: lamps changed      */
        case MSG_INTERSECTION_STATUS:                       /* I1/I2: other / heartbeat  */
            if (info.srcmsglen >= sizeof msg.istatus) r.result = on_status(msg.header.type, &msg.istatus);
            break;
        case MSG_RAILWAY_STATUS:
            if (info.srcmsglen >= sizeof msg.rail)    r.result = on_rail(&msg.rail);
            break;
        case MSG_FAULT_STATUS:
            if (info.srcmsglen >= sizeof msg.fault)   r.result = on_x1_fault(&msg.fault);
            break;
        default:
            break;
        }
        if (r.result == RESULT_INVALID)
            log_msg("RX: invalid message type %u from node %u", msg.header.type, msg.header.sender);
        MsgReply(rcvid, EOK, &r, sizeof r);
    }
    return NULL;
}