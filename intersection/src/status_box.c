/*
 * status_box.c — mutex + condition variable between the controller and display/uplink
 *
 * Why: the controller must never block on terminal output or the network, so it just drops
 * the newest status here and carries on. The other threads sleep on the condition variable
 * (no polling) and do their slow I/O AFTER unlocking.
 */
#include <errno.h>
#include <pthread.h>

#include "status_box.h"

#include "rutil.h"

static struct
{
    pthread_mutex_t mtx;
    pthread_cond_t changed; /* signalled whenever seq changes      */
    uint32_t seq;           /* bumped on every published status    */
    intersection_status_msg_t status;
    bool central_up;
    bool quit;
} box;

int status_box_init(void)
{
    pthread_condattr_t ca;
    pthread_condattr_init(&ca);
    pthread_condattr_setclock(&ca, CLOCK_MONOTONIC); /* timedwait immune to clock changes */
    if (pthread_cond_init(&box.changed, &ca) != 0)
        return -1;
    pthread_condattr_destroy(&ca);
    return pthread_mutex_init(&box.mtx, NULL); /* QNX: priority inheritance by default */
}

void status_box_publish(const intersection_status_msg_t *s)
{
    pthread_mutex_lock(&box.mtx);
    box.status = *s;
    box.seq++;
    pthread_cond_broadcast(&box.changed); /* broadcast: display AND uplink are waiting */
    pthread_mutex_unlock(&box.mtx);
}

bool status_box_wait(uint32_t *seen, uint32_t timeout_ms,
                     intersection_status_msg_t *out, bool *central_up)
{
    struct timespec until;
    bool running;

    pthread_mutex_lock(&box.mtx);
    if (timeout_ms)
    {
        ts_now(&until);
        ts_add_ms(&until, timeout_ms);
    }
    /* while, not if: wake-ups can be spurious, and the condition is what matters */
    while (box.seq == *seen && !box.quit)
    {
        if (timeout_ms == 0)
            pthread_cond_wait(&box.changed, &box.mtx);
        else if (pthread_cond_timedwait(&box.changed, &box.mtx, &until) == ETIMEDOUT)
            break;
    }
    running = !box.quit;
    *seen = box.seq;
    if (out)
        *out = box.status;
    if (central_up)
        *central_up = box.central_up;
    pthread_mutex_unlock(&box.mtx);
    return running;
}

void status_box_set_central_up(bool up)
{
    pthread_mutex_lock(&box.mtx);
    box.central_up = up;
    pthread_mutex_unlock(&box.mtx);
}

void status_box_shutdown(void)
{
    pthread_mutex_lock(&box.mtx);
    box.quit = true;
    pthread_cond_broadcast(&box.changed);
    pthread_mutex_unlock(&box.mtx);
}
