/*
 * rtutil.c — real-time helpers (see rtutil.h)
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sched.h>
#include <sys/neutrino.h>

#include "rtutil.h"

#define TIMER_TOLERANCE_MS 2 /* see oneshot_is_due() */

/* ======================================== time ======================================== */

void ts_now(struct timespec *ts)
{
    clock_gettime(CLOCK_MONOTONIC, ts);
}

void ts_add_ms(struct timespec *ts, uint32_t ms)
{
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L)
    {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}

int64_t ts_diff_ms(const struct timespec *a, const struct timespec *b)
{
    return (int64_t)(a->tv_sec - b->tv_sec) * 1000 + (a->tv_nsec - b->tv_nsec) / 1000000;
}

/* =================================== one-shot timers =================================== */

int oneshot_init(oneshot_t *t, int coid, int prio, int code)
{
    struct sigevent ev;
    memset(&ev, 0, sizeof ev);
    SIGEV_PULSE_INIT(&ev, coid, prio, code, 0);
    t->armed = false;
    return timer_create(CLOCK_MONOTONIC, &ev, &t->id);
}

void oneshot_arm(oneshot_t *t, uint32_t ms)
{
    struct itimerspec its;
    memset(&its, 0, sizeof its); /* it_interval = 0  ->  one-shot */
    if (ms == 0)
        ms = 1; /* it_value = 0 would DISARM the timer */
    its.it_value.tv_sec = ms / 1000;
    its.it_value.tv_nsec = (long)(ms % 1000) * 1000000L;

    ts_now(&t->deadline);
    ts_add_ms(&t->deadline, ms);
    t->armed = true;
    if (timer_settime(t->id, 0, &its, NULL) == -1) /* relative; replaces any earlier arm */
        perror("timer_settime");
}

void oneshot_cancel(oneshot_t *t)
{
    struct itimerspec its;
    memset(&its, 0, sizeof its);
    timer_settime(t->id, 0, &its, NULL);
    t->armed = false;
}

/* Re-arming (or cancelling) a timer does NOT remove a pulse from an earlier arm that is
 * already waiting in the channel queue. So when a timer pulse arrives, only act on it if
 * the *current* arm is actually due. Timers never fire early, so a genuine expiry always
 * passes; a stale one is at least a whole phase too early and is ignored. */
bool oneshot_is_due(oneshot_t *t)
{
    struct timespec now;
    if (!t->armed)
        return false;
    ts_now(&now);
    if (ts_diff_ms(&t->deadline, &now) > TIMER_TOLERANCE_MS)
        return false; /* stale */
    t->armed = false;
    return true;
}

void oneshot_destroy(oneshot_t *t)
{
    timer_delete(t->id);
    t->armed = false;
}

/* ======================================= threads ======================================= */

int spawn_thread(pthread_t *tid, void *(*fn)(void *), void *arg, int prio)
{
    pthread_attr_t a;
    struct sched_param sp;
    int rc;

    pthread_attr_init(&a);
    pthread_attr_setinheritsched(&a, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&a, SCHED_FIFO);
    memset(&sp, 0, sizeof sp);
    sp.sched_priority = prio;
    pthread_attr_setschedparam(&a, &sp);
    rc = pthread_create(tid, &a, fn, arg);
    pthread_attr_destroy(&a);
    if (rc == EPERM)
    { /* not allowed to set priorities: run anyway */
        fprintf(stderr, "warning: no permission for priority %d, using default\n", prio);
        rc = pthread_create(tid, NULL, fn, arg);
    }
    return rc;
}

void set_my_priority(int prio)
{
    struct sched_param sp;
    memset(&sp, 0, sizeof sp);
    sp.sched_priority = prio;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0)
        fprintf(stderr, "warning: could not set priority %d\n", prio);
}
