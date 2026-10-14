
#include <errno.h>
#include <pthread.h>
#include <string.h>

#include "status_box.h"

#include "rutil.h"

static void unlock_mutex(void *arg)
{
    pthread_mutex_unlock((pthread_mutex_t *)arg);
}

static struct
{
    pthread_mutex_t mtx;
    pthread_cond_t changed;
    uint32_t seq;
    intersection_status_msg_t status;
    bool central_up;
    bool quit;
} box;

int status_box_init(void)
{
    pthread_condattr_t ca;

    memset(&box, 0, sizeof box);
    if (pthread_mutex_init(&box.mtx, NULL) != 0)
        return -1;

    pthread_condattr_init(&ca);
    pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
    if (pthread_cond_init(&box.changed, &ca) != 0) {
        pthread_condattr_destroy(&ca);
        pthread_mutex_destroy(&box.mtx);
        return -1;
    }

    pthread_condattr_destroy(&ca);
    return 0;
}

void status_box_publish(const intersection_status_msg_t *s)
{
    pthread_mutex_lock(&box.mtx);
    box.status = *s;
    box.seq++;
    pthread_cond_broadcast(&box.changed);
    pthread_mutex_unlock(&box.mtx);
}

bool status_box_wait(uint32_t *seen, uint32_t timeout_ms,
                     intersection_status_msg_t *out, bool *central_up)
{
    struct timespec until;
    bool running;

    pthread_mutex_lock(&box.mtx);
    pthread_cleanup_push(unlock_mutex, &box.mtx);

    if (timeout_ms) {
        ts_now(&until);
        ts_add_ms(&until, timeout_ms);
    }

    while (box.seq == *seen && !box.quit) {
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

    pthread_cleanup_pop(1);
    return running;
}

void status_box_set_central_up(bool up)
{
    pthread_mutex_lock(&box.mtx);
    if (box.central_up != up) {
        box.central_up = up;
        box.seq++;
        pthread_cond_broadcast(&box.changed);
    }
    pthread_mutex_unlock(&box.mtx);
}

void status_box_shutdown(void)
{
    pthread_mutex_lock(&box.mtx);
    box.quit = true;
    pthread_cond_broadcast(&box.changed);
    pthread_mutex_unlock(&box.mtx);
}
