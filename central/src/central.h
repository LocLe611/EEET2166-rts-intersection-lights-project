#ifndef CENTRAL_H
#define CENTRAL_H

#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include <sys/neutrino.h>
#include "config.h"
#include "message.h"

#define CENTRAL_LOG_PATH   "/tmp/central.log"
#define OFFLINE_TIMEOUT_MS (3 * STATUS_PERIOD_MS)
#define RECONNECT_MS       1500
#define TICK_MS            1000
#define SIM_MIN_PER_TICK   1
#define SIM_START_MIN      (6 * 60 + 50)

#define PRIO_HIGH   12
#define PRIO_NORMAL 10
#define PRIO_LOW     9

#define PC_TICK         (_PULSE_CODE_MINAVAIL + 0)
#define PC_DISPLAY_FEED (_PULSE_CODE_MINAVAIL + 1)

typedef enum { LINK_OFFLINE = 0, LINK_ONLINE } link_t;
typedef enum { CMDRES_NONE = 0, CMDRES_PENDING, CMDRES_ACK, CMDRES_REJECTED, CMDRES_RETRY } cmdres_t;

typedef struct {
    link_t link;
    uint64_t last_seen_ms;
    uint32_t last_rx_seq;
    uint32_t last_rx_session;
    uint32_t foreign_session;
    int have_data;

    intersection_status_msg_t ist;
    railway_status_msg_t rail;
    fault_status_msg_t fault;
    int have_fault;

    operation_mode_t desired_mode;
    override_t desired_override;
    int policy_valid;
    int sched_paused;

    uint32_t last_cmd_seq;
    uint16_t last_cmd_type;
    cmdres_t last_cmd_res;
} node_info_t;

#define NODE_SLOTS (NODE_DISPLAY + 1)

typedef struct {
    pthread_rwlock_t lock;
    node_info_t node[NODE_SLOTS];
    unsigned alarms;
    int sched_enabled;
    unsigned sim_min;
    int pattern;
    uint32_t session_id;
} central_t;

typedef struct {
    union {
        msg_header_t header;
        set_mode_msg_t set_mode;
        override_msg_t override;
        clear_fault_msg_t clear_fault;
    } m;
    size_t len;
} cmd_t;

#define CMDQ_LEN 16

typedef struct {
    pthread_mutex_t m;
    pthread_cond_t cv;
    cmd_t q[CMDQ_LEN];
    int head;
    int count;
    int resync;
} cmdq_t;

typedef union {
    msg_header_t header;
    struct _pulse pulse;
    intersection_status_msg_t istatus;
    railway_status_msg_t rail;
    fault_status_msg_t fault;
} central_rx_t;

extern central_t g_c;
extern cmdq_t g_cmdq[NODE_SLOTS];

uint64_t now_ms(void);
void abs_deadline(struct timespec *ts, unsigned ms);
void ctl_init(void);
void build_set_mode(cmd_t *c, operation_mode_t mode);
void build_override(cmd_t *c, override_t ovr);
void build_clear_fault(cmd_t *c);
int cmdq_push(node_id_t n, const cmd_t *c);
void cmdq_request_resync(node_id_t n);

void *rx_thread(void *arg);
void *link_thread(void *arg);
void *tick_thread(void *arg);
void *menu_thread(void *arg);
void *logger_thread(void *arg);
void *display_link_thread(void *arg);

void apply_pattern(int pat);
const char *pattern_str(int p);

#define LOG_LINE 160
int log_init(const char *path);
void log_msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

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

#endif
