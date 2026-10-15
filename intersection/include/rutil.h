#ifndef RTUTIL_H
#define RTUTIL_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>

void ts_now(struct timespec *ts);
void ts_add_ms(struct timespec *ts, uint32_t ms);
int64_t ts_diff_ms(const struct timespec *a, const struct timespec *b);

typedef struct {
    timer_t id;
    struct timespec deadline;
    bool armed;
} oneshot_t;

int oneshot_init(oneshot_t *t, int coid, int prio, int code);
void oneshot_arm(oneshot_t *t, uint32_t ms);
void oneshot_cancel(oneshot_t *t);
bool oneshot_is_due(oneshot_t *t);
void oneshot_destroy(oneshot_t *t);

int spawn_thread(pthread_t *tid, void *(*fn)(void *), void *arg, int prio);
void set_my_priority(int prio);

#endif
