#ifndef CENTRAL_H
#define CENTRAL_H
/*
 * central.h — central controller internal API (EEET2166 Group 11)
 *
 * Uses the team's common/message.h and common/config.h. Settings that only
 * central uses live here, so the shared config.h stays small.
 *
 * Shared data and the primitive that protects each one:
 *   g_c.node[], sim clock, schedule, alarms  -> pthread_rwlock_t g_c.lock
 *   change counter g_c.chg_seq               -> mutex g_c.chg_m + condvar g_c.chg_cv
 *   g_cmdq[n] (I1, I2, X1)                   -> mutex + condvar inside cmdq_t
 *   log queue + recent-events ring           -> mutex + condvar in log.c
 *   stdout / prompt text                     -> mutex in ui.c
 * Lock rule: no thread ever holds two of these at once, so no deadlock is possible.
 */
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include <sys/neutrino.h>
#include "config.h"    /* common/: hostnames, service names, IPC_SEND_TIMEOUT_MS, STATUS_PERIOD_MS */
#include "message.h"   /* common/: message structs and enums */

/* ---------- central-only settings ---------- */
#define CENTRAL_LOG_PATH    "/tmp/central.log"       /* /fs/central.log on a BeagleBone */
#define OFFLINE_TIMEOUT_MS  (3 * STATUS_PERIOD_MS)   /* I1/I2: three missed heartbeats  */
#define RECONNECT_MS        1500                     /* retry a failed link this often  */
#define TICK_MS             1000                     /* watchdog + sim clock period     */
#define SIM_MIN_PER_TICK    1                        /* 1 real second = 1 sim minute    */
#define SIM_START_MIN       (6 * 60 + 50)            /* start at 06:50, just before peak */

/* X1 currently reports only when its state changes (no periodic heartbeat), so its
 * silence can't be treated as a failure. Set to 1 once X1 resends its status every
 * STATUS_PERIOD_MS; X1 then goes OFFLINE after OFFLINE_TIMEOUT_MS like I1/I2. */
#define X1_SENDS_HEARTBEAT  0

#define PRIO_HIGH    12      /* RX server, tick (watchdog)   */
#define PRIO_NORMAL  10      /* link threads, dashboard      */
#define PRIO_LOW      9      /* logger                       */

#define PC_TICK  (_PULSE_CODE_MINAVAIL + 0)   /* our 1 s timer pulse (user code range) */

/* ---------- node table ---------- */
typedef enum { LINK_OFFLINE = 0, LINK_ONLINE } link_t;
/* WAIT = intersection offline: not queued; resync sends the desired state on reconnect */
typedef enum { CMDRES_NONE = 0, CMDRES_PENDING, CMDRES_ACK, CMDRES_REJECTED, CMDRES_RETRY, CMDRES_WAIT } cmdres_t;
typedef enum { PAT_NIGHT = 0, PAT_PEAK, PAT_OFFPEAK } pattern_t;

typedef struct {
    /* link supervision: written by the RX thread, watchdog and (X1) link thread */
    link_t   link;
    uint64_t last_seen_ms;
    uint32_t last_rx_seq;
    int      have_data;

    intersection_status_msg_t ist;     /* I1/I2: last status received        */
    railway_status_msg_t      rail;    /* X1: last railway status received   */
    fault_status_msg_t        fault;   /* X1: last fault report received     */
    int                       have_fault;

    /* what central wants an intersection to be doing (sent again on resync) */
    operation_mode_t desired_mode;
    override_t       desired_override;
    int              sched_paused;     /* operator took manual control */

    /* last command, for the dashboard */
    uint32_t last_cmd_seq;
    uint16_t last_cmd_type;            /* MSG_SET_MODE / MSG_OVERRIDE / MSG_CLEAR_FAULT */
    cmdres_t last_cmd_res;
} node_info_t;

#define NODE_SLOTS (NODE_DISPLAY + 1)  /* indexed by node_id_t; slot 0 unused */

typedef struct {
    pthread_rwlock_t lock;             /* protects everything below except chg_* */
    node_info_t      node[NODE_SLOTS];
    unsigned         alarms;           /* bit per node_id_t; latched until acknowledged */
    int              sched_enabled;
    unsigned         sim_min;          /* minutes since midnight */
    int              pattern;          /* pattern_t, -1 = re-apply on next tick */

    pthread_mutex_t  chg_m;            /* "something changed" notification */
    pthread_cond_t   chg_cv;
    unsigned         chg_seq;
} central_t;

/* ---------- commands ---------- */
typedef struct {                       /* one queued command: any of the three types */
    union {
        msg_header_t      header;
        set_mode_msg_t    set_mode;
        override_msg_t    override;
        clear_fault_msg_t clear_fault;
    } m;
    size_t len;                        /* bytes to send */
} cmd_t;

#define CMDQ_LEN 16
typedef struct {
    pthread_mutex_t m;
    pthread_cond_t  cv;                /* CLOCK_MONOTONIC */
    cmd_t           q[CMDQ_LEN];
    int             head, count;
    int             resync;            /* set by the RX thread when a node (re)appears */
} cmdq_t;

/* ---------- receive buffer for the RX server ---------- */
typedef union {
    msg_header_t              header;
    struct _pulse             pulse;
    intersection_status_msg_t istatus;
    railway_status_msg_t      rail;
    fault_status_msg_t        fault;
} central_rx_t;

extern central_t g_c;
extern cmdq_t    g_cmdq[NODE_SLOTS];   /* NODE_I1, NODE_I2, NODE_X1 used */

/* common.c */
uint64_t now_ms(void);
void     abs_deadline(struct timespec *ts, unsigned ms);
void     ctl_init(void);
void     table_changed(void);
unsigned table_wait_change(unsigned last, unsigned timeout_ms);
void     build_set_mode(cmd_t *c, operation_mode_t mode);
void     build_override(cmd_t *c, override_t ovr);
void     build_clear_fault(cmd_t *c);
int      cmdq_push(node_id_t n, const cmd_t *c);
void     cmdq_request_resync(node_id_t n);

/* thread entry points */
void *rx_thread(void *arg);            /* arg = name_attach_t*            */
void *link_thread(void *arg);          /* arg = (void*)(intptr_t)node_id  */
void *tick_thread(void *arg);
void *dashboard_thread(void *arg);
void *menu_thread(void *arg);
void *logger_thread(void *arg);

/* tick.c */
void        apply_pattern(int pat);
const char *pattern_str(int p);

/* log.c */
#define LOG_LINE   160
#define LOG_RECENT 6
int  log_init(const char *path);
void log_msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int  log_recent(char out[][LOG_LINE], int max);

/* ui.c — enum -> text */
const char *node_str(int n);
const char *phase_str(int p);
const char *light_str(int l);
const char *ped_str(int p);
const char *mode_str(int m);
const char *override_str(int o);
const char *istate_str(int s);
const char *rail_str(int r);
const char *gate_str(int g);
const char *msgtype_str(int t);
const char *result_str(int r);
const char *side_road(int n);

#endif /* CENTRAL_H */