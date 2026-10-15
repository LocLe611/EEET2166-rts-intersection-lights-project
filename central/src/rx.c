#include <errno.h>
#include <string.h>
#include <sys/dispatch.h>
#include <sys/iomsg.h>
#include <sys/neutrino.h>
#include "central.h"

#define F_CAME_ONLINE 1
#define F_RESTARTED   2
#define SESSION_SWITCH_GRACE_MS (2 * STATUS_PERIOD_MS)

typedef enum {
    SEQ_NEW,
    SEQ_DUPLICATE,
    SEQ_STALE,
    SEQ_RESTART,
    SEQ_FOREIGN
} seq_state_t;

static seq_state_t sequence_state(const node_info_t *ni,
                                  uint32_t session,
                                  uint32_t seq)
{
    uint64_t age;

    if (!ni->have_data)
        return SEQ_NEW;

    if (session != ni->last_rx_session) {
        age = now_ms() - ni->last_seen_ms;
        if (ni->link == LINK_ONLINE && age < SESSION_SWITCH_GRACE_MS)
            return SEQ_FOREIGN;
        return SEQ_RESTART;
    }

    if (seq == ni->last_rx_seq)
        return SEQ_DUPLICATE;
    if (seq < ni->last_rx_seq)
        return SEQ_STALE;
    return SEQ_NEW;
}

static int mark_heard(node_info_t *ni,
                      uint32_t session,
                      uint32_t seq,
                      int restarted)
{
    int flags = 0;

    if (ni->link == LINK_OFFLINE)
        flags |= F_CAME_ONLINE;
    if (restarted)
        flags |= F_RESTARTED;

    ni->link = LINK_ONLINE;
    ni->last_seen_ms = now_ms();
    ni->last_rx_session = session;
    ni->last_rx_seq = seq;
    ni->have_data = 1;
    return flags;
}

static int duplicate_heard(node_info_t *ni)
{
    int came_online = ni->link == LINK_OFFLINE;

    ni->link = LINK_ONLINE;
    ni->last_seen_ms = now_ms();
    return came_online;
}

static int valid_intersection_status(const intersection_status_msg_t *s)
{
    if (s->mode > MODE_SENSOR || s->phase >= PHASE_COUNT)
        return 0;
    if (s->intersection_state > INTERSECTION_FAULT)
        return 0;
    if (s->main_light > LIGHT_GREEN || s->side_light > LIGHT_GREEN)
        return 0;
    if (s->ped_light > PED_FLASHING)
        return 0;
    if (s->railway_state > RAIL_FAULT || s->override > OVERRIDE_SIDE)
        return 0;
    if (s->x1_link_ok > 1 || s->ped_request > 1 || s->side_demand > 1)
        return 0;
    return 1;
}

static int32_t on_status(uint16_t type, const intersection_status_msg_t *s)
{
    node_id_t n = (node_id_t)s->header.sender;
    intersection_status_msg_t old;
    node_info_t *ni;
    int had;
    int flags;
    int fault_now;
    int came_online;
    int policy_valid;
    seq_state_t seq_state;

    if ((n != NODE_I1 && n != NODE_I2) || !valid_intersection_status(s))
        return RESULT_INVALID;

    pthread_rwlock_wrlock(&g_c.lock);
    ni = &g_c.node[n];

    seq_state = sequence_state(ni, s->header.session, s->header.sequence);
    if (seq_state == SEQ_DUPLICATE) {
        came_online = duplicate_heard(ni);
        pthread_rwlock_unlock(&g_c.lock);
        if (came_online)
            log_msg("%s: ONLINE", node_str(n));
        return RESULT_OK;
    }

    if (seq_state == SEQ_STALE) {
        pthread_rwlock_unlock(&g_c.lock);
        return RESULT_OK;
    }

    if (seq_state == SEQ_FOREIGN) {
        int log_conflict = ni->foreign_session != s->header.session;
        ni->foreign_session = s->header.session;
        pthread_rwlock_unlock(&g_c.lock);
        if (log_conflict)
            log_msg("%s: duplicate sender ignored", node_str(n));
        return RESULT_OK;
    }

    old = ni->ist;
    had = ni->have_data;
    policy_valid = ni->policy_valid;

    flags = mark_heard(ni,
                       s->header.session,
                       s->header.sequence,
                       seq_state == SEQ_RESTART);
    ni->ist = *s;
    if (seq_state == SEQ_RESTART)
        ni->foreign_session = 0;

    if (!ni->policy_valid) {
        ni->desired_mode = (operation_mode_t)s->mode;
        ni->desired_override = (override_t)s->override;
    }

    fault_now = s->intersection_state == INTERSECTION_FAULT &&
                (!had || old.intersection_state != INTERSECTION_FAULT);
    if (s->intersection_state == INTERSECTION_FAULT)
        g_c.alarms |= NODE_MASK(n);
    else
        g_c.alarms &= ~NODE_MASK(n);

    pthread_rwlock_unlock(&g_c.lock);

    if (flags & F_CAME_ONLINE)
        log_msg("%s: ONLINE", node_str(n));
    if (flags & F_RESTARTED)
        log_msg("%s: restart detected", node_str(n));

    if (!had || (!policy_valid && (flags & F_RESTARTED)))
        log_msg("%s: local mode %s, override %s",
                node_str(n), mode_str(s->mode), override_str(s->override));

    if ((flags & F_RESTARTED) && policy_valid)
        cmdq_request_resync(n);

    if (type == MSG_LIGHT_STATUS)
        log_msg("%s: %-11s R3=%s %s=%s ped=%s",
                node_str(n), phase_str(s->phase),
                light_str(s->main_light), side_road(n),
                light_str(s->side_light), ped_str(s->ped_light));

    if (had && old.intersection_state != s->intersection_state)
        log_msg("%s: state %s", node_str(n), istate_str(s->intersection_state));
    if (had && old.mode != s->mode)
        log_msg("%s: now in %s mode", node_str(n), mode_str(s->mode));
    if (had && old.override != s->override)
        log_msg("%s: override %s", node_str(n), override_str(s->override));
    if (fault_now)
        log_msg("ALARM: %s entered FAULT", node_str(n));

    return RESULT_OK;
}

static int32_t on_rail(const railway_status_msg_t *r)
{
    node_info_t *ni;
    railway_status_msg_t old;
    int had;
    int flags;
    int fault_now;
    int came_online;
    seq_state_t seq_state;

    if (r->header.sender != NODE_X1 ||
        r->railway_state > RAIL_FAULT ||
        r->gate_state > GATE_UNKNOWN ||
        r->train_signal > TRAIN_SIGNAL_GREEN ||
        r->warning_lights_on > 1)
        return RESULT_INVALID;

    pthread_rwlock_wrlock(&g_c.lock);
    ni = &g_c.node[NODE_X1];

    seq_state = sequence_state(ni, r->header.session, r->header.sequence);
    if (seq_state == SEQ_DUPLICATE) {
        came_online = duplicate_heard(ni);
        pthread_rwlock_unlock(&g_c.lock);
        if (came_online)
            log_msg("X1: ONLINE");
        return RESULT_OK;
    }

    if (seq_state == SEQ_STALE) {
        pthread_rwlock_unlock(&g_c.lock);
        return RESULT_OK;
    }

    if (seq_state == SEQ_FOREIGN) {
        int log_conflict = ni->foreign_session != r->header.session;
        ni->foreign_session = r->header.session;
        pthread_rwlock_unlock(&g_c.lock);
        if (log_conflict)
            log_msg("X1: duplicate sender ignored");
        return RESULT_OK;
    }

    old = ni->rail;
    had = ni->have_data;
    flags = mark_heard(ni,
                       r->header.session,
                       r->header.sequence,
                       seq_state == SEQ_RESTART);
    ni->rail = *r;
    if (seq_state == SEQ_RESTART)
        ni->foreign_session = 0;

    fault_now = r->railway_state == RAIL_FAULT &&
                (!had || old.railway_state != RAIL_FAULT);
    if (r->railway_state == RAIL_FAULT)
        g_c.alarms |= NODE_MASK(NODE_X1);
    else
        g_c.alarms &= ~NODE_MASK(NODE_X1);

    pthread_rwlock_unlock(&g_c.lock);

    if (flags & F_CAME_ONLINE)
        log_msg("X1: ONLINE");
    if (flags & F_RESTARTED)
        log_msg("X1: restart detected");

    if (!had || old.railway_state != r->railway_state ||
        old.gate_state != r->gate_state)
        log_msg("X1: railway %s, gate %s, train signal %s",
                rail_str(r->railway_state), gate_str(r->gate_state),
                r->train_signal == TRAIN_SIGNAL_GREEN ? "GREEN" : "RED");

    if (fault_now)
        log_msg("ALARM: X1 railway FAULT (flags 0x%X)",
                (unsigned)r->fault_flags);

    return RESULT_OK;
}

static int32_t on_x1_fault(const fault_status_msg_t *fs)
{
    node_info_t *ni;
    seq_state_t seq_state;

    if (fs->header.sender != NODE_X1 ||
        fs->railway_state > RAIL_FAULT ||
        fs->gate_state > GATE_UNKNOWN)
        return RESULT_INVALID;

    pthread_rwlock_wrlock(&g_c.lock);
    ni = &g_c.node[NODE_X1];

    seq_state = sequence_state(ni, fs->header.session, fs->header.sequence);
    if (seq_state == SEQ_DUPLICATE) {
        duplicate_heard(ni);
        pthread_rwlock_unlock(&g_c.lock);
        return RESULT_OK;
    }

    if (seq_state == SEQ_STALE || seq_state == SEQ_FOREIGN) {
        pthread_rwlock_unlock(&g_c.lock);
        return RESULT_OK;
    }

    mark_heard(ni,
               fs->header.session,
               fs->header.sequence,
               seq_state == SEQ_RESTART);
    ni->fault = *fs;
    ni->have_fault = 1;
    g_c.alarms |= NODE_MASK(NODE_X1);
    pthread_rwlock_unlock(&g_c.lock);

    log_msg("ALARM: X1 fault flags 0x%X%s%s",
            (unsigned)fs->fault_flags,
            (fs->fault_flags & X1_FAULT_GATE_TIMEOUT) ? " [gate timeout]" : "",
            (fs->fault_flags & X1_FAULT_GATE_SENSOR) ? " [gate sensor]" : "");
    return RESULT_OK;
}

void *rx_thread(void *arg)
{
    name_attach_t *att = (name_attach_t *)arg;
    central_rx_t msg;
    struct _msg_info info;

    for (;;) {
        int rcvid = MsgReceive(att->chid, &msg, sizeof msg, &info);
        msg_reply_t reply;

        if (rcvid == -1) {
            if (errno == EINTR)
                continue;
            break;
        }

        if (rcvid == 0) {
            if (msg.pulse.code == _PULSE_CODE_DISCONNECT)
                ConnectDetach(msg.pulse.scoid);
            continue;
        }

        if (msg.header.type == _IO_CONNECT) {
            MsgReply(rcvid, EOK, NULL, 0);
            continue;
        }

        if (msg.header.type > _IO_BASE && msg.header.type <= _IO_MAX) {
            MsgError(rcvid, ENOSYS);
            continue;
        }

        reply.result = RESULT_INVALID;
        reply.sequence = msg.header.sequence;

        switch (msg.header.type) {
        case MSG_LIGHT_STATUS:
        case MSG_INTERSECTION_STATUS:
            if ((size_t)info.msglen >= sizeof msg.istatus)
                reply.result = on_status(msg.header.type, &msg.istatus);
            else
                log_msg("RX: short intersection status from node %u (%u/%u bytes)",
                        msg.header.sender, (unsigned)info.msglen,
                        (unsigned)sizeof msg.istatus);
            break;
        case MSG_RAILWAY_STATUS:
            if ((size_t)info.msglen >= sizeof msg.rail)
                reply.result = on_rail(&msg.rail);
            else
                log_msg("RX: short railway status (%u/%u bytes)",
                        (unsigned)info.msglen, (unsigned)sizeof msg.rail);
            break;
        case MSG_FAULT_STATUS:
            if ((size_t)info.msglen >= sizeof msg.fault)
                reply.result = on_x1_fault(&msg.fault);
            else
                log_msg("RX: short fault status (%u/%u bytes)",
                        (unsigned)info.msglen, (unsigned)sizeof msg.fault);
            break;
        default:
            log_msg("RX: unsupported message type %u from node %u",
                    msg.header.type, msg.header.sender);
            break;
        }

        if (reply.result == RESULT_INVALID &&
            (msg.header.type == MSG_LIGHT_STATUS ||
             msg.header.type == MSG_INTERSECTION_STATUS ||
             msg.header.type == MSG_RAILWAY_STATUS ||
             msg.header.type == MSG_FAULT_STATUS))
            log_msg("RX: rejected payload type %u from node %u",
                    msg.header.type, msg.header.sender);

        MsgReply(rcvid, EOK, &reply, sizeof reply);
    }

    return NULL;
}
