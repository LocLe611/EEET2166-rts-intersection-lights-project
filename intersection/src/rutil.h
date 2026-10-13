/*
 * rtutil.h — real-time helpers: monotonic time, one-shot timers that deliver pulses,
 *            and creating threads with a fixed priority.
 */
#ifndef RTUTIL_H
#define RTUTIL_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>

/* ---- CLOCK_MONOTONIC time (never jumps when someone sets the date) ---- */
void    ts_now(struct timespec *ts);
void    ts_add_ms(struct timespec *ts, uint32_t ms);
int64_t ts_diff_ms(const struct timespec *a, const struct timespec *b);   /* a - b, in ms */

/* ---- one-shot POSIX timer that sends a pulse to a channel when it expires ---- */
typedef struct {
    timer_t         id;
    struct timespec deadline;   /* when the current arm is due */
    bool            armed;
} oneshot_t;

int  oneshot_init(oneshot_t *t, int coid, int prio, int code);
void oneshot_arm(oneshot_t *t, uint32_t ms);    /* (re)start: fires once after ms */
void oneshot_cancel(oneshot_t *t);
bool oneshot_is_due(oneshot_t *t);              /* call on the pulse: false = stale, ignore it */
void oneshot_destroy(oneshot_t *t);

/* ---- threads ---- */
int  spawn_thread(pthread_t *tid, void *(*fn)(void *), void *arg, int prio);   /* SCHED_FIFO */
void set_my_priority(int prio);

#endif
