#include <errno.h>
#include <string.h>
#include <sys/neutrino.h>
#include <sys/siginfo.h>
#include "central.h"

const char *pattern_str(int p)
{
    switch (p) {
    case PAT_PEAK:    return "PEAK";
    case PAT_OFFPEAK: return "OFF-PEAK";
    case PAT_NIGHT:   return "NIGHT";
    default:          return "-";
    }
}

static int pattern_for(unsigned m)
{
    if ((m >= 7 * 60 && m < 9 * 60 + 30) ||
        (m >= 16 * 60 + 30 && m < 19 * 60))
        return PAT_PEAK;
    if (m >= 22 * 60 || m < 6 * 60)
        return PAT_NIGHT;
    return PAT_OFFPEAK;
}

void apply_pattern(int pat)
{
    operation_mode_t mode = pat == PAT_PEAK ? MODE_FIXED : MODE_SENSOR;

    for (node_id_t n = NODE_I1; n <= NODE_I2; n++) {
        int skip;
        cmd_t c;

        pthread_rwlock_wrlock(&g_c.lock);
        skip = g_c.node[n].sched_paused ||
               g_c.node[n].desired_override != OVERRIDE_NONE;
        if (!skip) {
            g_c.node[n].desired_mode = mode;
            g_c.node[n].policy_valid = 1;
        }
        pthread_rwlock_unlock(&g_c.lock);

        if (skip) {
            log_msg("schedule: %s skipped (manual control)", node_str(n));
            continue;
        }

        build_set_mode(&c, mode);
        cmdq_push(n, &c);
    }
}

static void watchdog(void)
{
    static const node_id_t watched[] = { NODE_I1, NODE_I2, NODE_X1 };
    int went_offline[3] = { 0, 0, 0 };
    uint64_t now = now_ms();

    pthread_rwlock_wrlock(&g_c.lock);
    for (int i = 0; i < 3; i++) {
        node_info_t *ni = &g_c.node[watched[i]];
        if (ni->link == LINK_ONLINE &&
            now - ni->last_seen_ms > OFFLINE_TIMEOUT_MS) {
            ni->link = LINK_OFFLINE;
            went_offline[i] = 1;
        }
    }
    pthread_rwlock_unlock(&g_c.lock);

    for (int i = 0; i < 3; i++) {
        if (went_offline[i])
            log_msg("%s: OFFLINE (no status for %d ms)",
                    node_str(watched[i]), OFFLINE_TIMEOUT_MS);
    }
}

static void sim_clock_and_schedule(void)
{
    int pat;
    int changed;
    int enabled;
    unsigned m;

    pthread_rwlock_wrlock(&g_c.lock);
    g_c.sim_min = (g_c.sim_min + SIM_MIN_PER_TICK) % (24 * 60);
    m = g_c.sim_min;
    pat = pattern_for(m);
    changed = pat != g_c.pattern;
    enabled = g_c.sched_enabled;
    g_c.pattern = pat;
    pthread_rwlock_unlock(&g_c.lock);

    if (changed && enabled) {
        log_msg("schedule: %02u:%02u -> %s",
                m / 60, m % 60, pattern_str(pat));
        apply_pattern(pat);
    }
}

void *tick_thread(void *arg)
{
    int chid;
    int coid;
    struct sigevent ev;
    timer_t tid;
    struct itimerspec its;
    struct _pulse p;

    (void)arg;

    chid = ChannelCreate(0);
    if (chid == -1) {
        log_msg("tick: ChannelCreate failed (%s)", strerror(errno));
        return NULL;
    }

    coid = ConnectAttach(0, 0, chid, _NTO_SIDE_CHANNEL, 0);
    if (coid == -1) {
        log_msg("tick: ConnectAttach failed (%s)", strerror(errno));
        ChannelDestroy(chid);
        return NULL;
    }

    SIGEV_PULSE_INIT(&ev, coid, SIGEV_PULSE_PRIO_INHERIT, PC_TICK, 0);
    if (timer_create(CLOCK_MONOTONIC, &ev, &tid) == -1) {
        log_msg("tick: timer_create failed (%s)", strerror(errno));
        ConnectDetach(coid);
        ChannelDestroy(chid);
        return NULL;
    }

    memset(&its, 0, sizeof its);
    its.it_value.tv_sec = TICK_MS / 1000;
    its.it_value.tv_nsec = (long)(TICK_MS % 1000) * 1000000L;
    its.it_interval = its.it_value;
    if (timer_settime(tid, 0, &its, NULL) == -1) {
        log_msg("tick: timer_settime failed (%s)", strerror(errno));
        timer_delete(tid);
        ConnectDetach(coid);
        ChannelDestroy(chid);
        return NULL;
    }

    for (;;) {
        if (MsgReceive(chid, &p, sizeof p, NULL) != 0)
            continue;
        if (p.code != PC_TICK)
            continue;

        watchdog();
        sim_clock_and_schedule();
    }

    return NULL;
}
