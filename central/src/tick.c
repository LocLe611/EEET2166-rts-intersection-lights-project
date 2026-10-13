/* tick.c — one thread driven by a periodic QNX timer pulse (no sleep()).
 * Every TICK_MS: (1) link watchdog, (2) advance the simulated time of day,
 * (3) apply the time-of-day schedule when the pattern changes. */
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

/* Peak 07:00-09:30 and 16:30-19:00, night 22:00-06:00, otherwise off-peak. */
static int pattern_for(unsigned m)
{
    if ((m >= 7 * 60 && m < 9 * 60 + 30) || (m >= 16 * 60 + 30 && m < 19 * 60)) return PAT_PEAK;
    if (m >= 22 * 60 || m < 6 * 60) return PAT_NIGHT;
    return PAT_OFFPEAK;
}

/* Peak = fixed timing; off-peak and night = sensor driven (spec: "commonly used"). */
void apply_pattern(int pat)
{
    operation_mode_t mode = (pat == PAT_PEAK) ? MODE_FIXED : MODE_SENSOR;

    for (node_id_t n = NODE_I1; n <= NODE_I2; n++) {
        int skip;
        pthread_rwlock_wrlock(&g_c.lock);
        node_info_t *ni = &g_c.node[n];
        skip = ni->sched_paused || ni->desired_override != OVERRIDE_NONE;   /* operator wins */
        if (!skip) ni->desired_mode = mode;
        pthread_rwlock_unlock(&g_c.lock);

        if (skip) { log_msg("schedule: %s skipped (under manual control)", node_str(n)); continue; }
        cmd_t c;
        build_set_mode(&c, mode);
        cmdq_push(n, &c);
    }
}

static void watchdog(void)
{
    static const node_id_t watched[] = { NODE_I1, NODE_I2, NODE_X1 };
    int went_offline[3] = { 0 };
    uint64_t now = now_ms();

    pthread_rwlock_wrlock(&g_c.lock);
    for (int i = 0; i < 3; i++) {
        if (watched[i] == NODE_X1 && !X1_SENDS_HEARTBEAT) continue;   /* see central.h */
        node_info_t *ni = &g_c.node[watched[i]];
        if (ni->link == LINK_ONLINE && now - ni->last_seen_ms > OFFLINE_TIMEOUT_MS) {
            ni->link = LINK_OFFLINE;
            went_offline[i] = 1;
        }
    }
    pthread_rwlock_unlock(&g_c.lock);

    for (int i = 0; i < 3; i++)
        if (went_offline[i])
            log_msg("%s: OFFLINE (no message for %d ms)", node_str(watched[i]), OFFLINE_TIMEOUT_MS);
}

static void sim_clock_and_schedule(void)
{
    int pat, changed, enabled;
    unsigned m;

    pthread_rwlock_wrlock(&g_c.lock);
    g_c.sim_min = (g_c.sim_min + SIM_MIN_PER_TICK) % (24 * 60);
    m       = g_c.sim_min;
    pat     = pattern_for(m);
    changed = (pat != g_c.pattern);
    enabled = g_c.sched_enabled;
    if (enabled) g_c.pattern = pat;
    pthread_rwlock_unlock(&g_c.lock);

    if (changed && enabled) {
        log_msg("schedule: %02u:%02u -> %s", m / 60, m % 60, pattern_str(pat));
        apply_pattern(pat);
    }
}

void *tick_thread(void *arg)
{
    (void)arg;
    int chid = ChannelCreate(0);
    int coid = ConnectAttach(0, 0, chid, _NTO_SIDE_CHANNEL, 0);
    if (chid == -1 || coid == -1) { log_msg("tick: channel setup failed (%s)", strerror(errno)); return NULL; }

    struct sigevent ev;
    SIGEV_PULSE_INIT(&ev, coid, SIGEV_PULSE_PRIO_INHERIT, PC_TICK, 0);
    timer_t tid;
    if (timer_create(CLOCK_MONOTONIC, &ev, &tid) == -1) {
        log_msg("tick: timer_create failed (%s)", strerror(errno));
        return NULL;
    }
    struct itimerspec its;
    memset(&its, 0, sizeof its);
    its.it_value.tv_sec  = TICK_MS / 1000;
    its.it_value.tv_nsec = (long)(TICK_MS % 1000) * 1000000L;
    its.it_interval      = its.it_value;          /* periodic */
    timer_settime(tid, 0, &its, NULL);

    struct _pulse p;
    for (;;) {
        if (MsgReceive(chid, &p, sizeof p, NULL) != 0) continue;   /* pulses only */
        if (p.code != PC_TICK) continue;
        watchdog();
        sim_clock_and_schedule();
        table_changed();                           /* clock moved: redraw */
    }
    return NULL;
}