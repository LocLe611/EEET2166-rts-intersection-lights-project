#ifndef CENTRAL_H
#define CENTRAL_H
/*
 * central.h — central controller internal API.
 *
 * Shared data and the primitive that protects each one (report table):
 *   g_c.node[], sim clock, schedule  -> pthread_rwlock_t g_c.lock
 *   change counter g_c.chg_seq       -> mutex g_c.chg_m + condvar g_c.chg_cv
 *   g_cmdq[n] (one per intersection) -> mutex + condvar inside cmdq_t
 *   log queue + recent-events ring   -> mutex + condvar in log.c
 *   stdout / prompt text             -> mutex in ui.c
 * Lock order rule: never hold two of these at once.
 */
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include "config.h"
#include "tl_types.h"
#include "tl_msg.h"

/* Our own pulse codes (rule 3: inside the user range) */
#define PC_TICK  (_PULSE_CODE_MINAVAIL + 0)

typedef enum { CMDRES_NONE = 0, CMDRES_PENDING, CMDRES_ACK, CMDRES_REJECTED, CMDRES_RETRY } cmdres_t;
typedef enum { PAT_NIGHT = 0, PAT_PEAK, PAT_OFFPEAK } pattern_t;

typedef struct {
    /* Link supervision (written by RX thread + watchdog) */
    link_t       link;
    uint64_t     last_seen_ms;
    uint32_t     last_rx_seq;
    int          have_data;
    msg_status_t st;                   /* last status from I1/I2 */
    msg_train_t  tr;                   /* last state from X1      */

    /* Operator / scheduler intent for I1/I2 (used for resync) */
    tl_mode_t    desired_mode;
    int          override_dir;         /* 0 = none, else MODE_OVERRIDE_NS / _EW */
    uint32_t     desired_timing[TIM_COUNT];
    int          sched_paused;         /* manual command pauses the schedule */

    /* Last command, for the dashboard "sent / acked / rejected" column */
    uint32_t     last_cmd_seq;
    uint16_t     last_cmd_sub;
    cmdres_t     last_cmd_res;
} node_info_t;

typedef struct {
    pthread_rwlock_t lock;             /* protects everything below except chg_* */
    node_info_t      node[NODE_COUNT];
    int              boom_alarm;       /* latched until operator acknowledges */
    int              sched_enabled;
    unsigned         sim_min;          /* minutes since midnight */
    int              pattern;          /* pattern_t, -1 = force re-apply */

    pthread_mutex_t  chg_m;            /* "something changed" notification */
    pthread_cond_t   chg_cv;
    unsigned         chg_seq;
} central_t;

#define CMDQ_LEN 16
typedef struct {
    pthread_mutex_t m;
    pthread_cond_t  cv;                /* CLOCK_MONOTONIC */
    msg_cmd_t       q[CMDQ_LEN];
    int             head, count;
    int             resync;            /* set by RX thread when the node (re)appears */
} cmdq_t;

extern central_t g_c;
extern cmdq_t    g_cmdq[NODE_COUNT];   /* only NODE_I1 / NODE_I2 used */

/* common.c */
uint64_t now_ms(void);
void     abs_deadline(struct timespec *ts, unsigned ms);
void     cond_init_mono(pthread_cond_t *cv);
void     ctl_init(void);
void     table_changed(void);
unsigned table_wait_change(unsigned last, unsigned timeout_ms);
uint32_t next_seq(void);
void     fill_cmd(msg_cmd_t *c, uint16_t sub, int32_t value, const uint32_t *timing);
int      cmdq_push(node_id_t n, uint16_t sub, int32_t value, const uint32_t *timing);
void     cmdq_request_resync(node_id_t n);

/* thread entry points */
void *rx_thread(void *arg);            /* arg = name_attach_t*           */
void *link_thread(void *arg);          /* arg = (void*)(intptr_t)node_id */
void *display_link_thread(void *arg);
void *tick_thread(void *arg);          /* watchdog + sim clock + scheduler */
void *dashboard_thread(void *arg);
void *menu_thread(void *arg);
void *logger_thread(void *arg);

/* tick.c (also used by the menu) */
void  apply_pattern(int pat, int force);
const char *pattern_str(int p);

/* log.c */
#define LOG_LINE   160
#define LOG_RECENT 6
int  log_init(const char *path);
void log_msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int  log_recent(char out[][LOG_LINE], int max);

/* ui.c — enum -> text */
const char *node_str(int n);
const char *colour_str(int c);
const char *ped_str(int p);
const char *phase_str(int p);
const char *mode_str(int m);
const char *cmd_str(int s);
const char *result_str(int r);
const char *train_str(int s);
void        ui_restore_terminal(void);

#endif /* CENTRAL_H */
