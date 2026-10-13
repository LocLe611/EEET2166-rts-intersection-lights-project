/* main.c — central controller (EEET2166 Group 11). Run on rts-central:  ./central
 *
 * Registers CENTRAL_SERVICE as a LOCAL name: I1, I2 and X1 reach it over Qnet at
 * QNET_SERVICE_PATH(CENTRAL_HOSTNAME, CENTRAL_SERVICE), so this node's hostname
 * must be CENTRAL_HOSTNAME ("rts-central"). No GNS needed.
 *
 * Threads: rx_server, tick (watchdog + schedule), link_i1, link_i2, link_x1,
 *          dashboard, logger, and the main thread as the operator menu. */
#include <errno.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dispatch.h>
#include "central.h"

static void start(void *(*fn)(void *), void *arg, int prio, const char *name)
{
    pthread_attr_t a;
    struct sched_param sp;
    memset(&sp, 0, sizeof sp);
    sp.sched_priority = prio;
    pthread_attr_init(&a);
    pthread_attr_setinheritsched(&a, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&a, SCHED_RR);
    pthread_attr_setschedparam(&a, &sp);

    pthread_t t;
    int rc = pthread_create(&t, &a, fn, arg);
    pthread_attr_destroy(&a);
    if (rc == EPERM) {                         /* not allowed to set priorities: run anyway */
        fprintf(stderr, "warning: no permission for priority %d (%s)\n", prio, name);
        rc = pthread_create(&t, NULL, fn, arg);
    }
    if (rc != 0) {
        fprintf(stderr, "pthread_create %s: %s\n", name, strerror(rc));
        exit(EXIT_FAILURE);
    }
}

int main(void)
{
    ctl_init();
    if (log_init(CENTRAL_LOG_PATH) != 0)
        fprintf(stderr, "warning: cannot open %s, events shown on screen only\n", CENTRAL_LOG_PATH);

    name_attach_t *att = name_attach(NULL, CENTRAL_SERVICE, 0);   /* local name, reached via Qnet */
    if (!att) {
        fprintf(stderr, "name_attach(%s) failed: %s (is central already running?)\n",
                CENTRAL_SERVICE, strerror(errno));
        return EXIT_FAILURE;
    }
    log_msg("central started: /dev/name/local/%s", CENTRAL_SERVICE);

    /* Priorities: incoming status + failure detection first, UI next, file I/O last.
     * The RX thread also runs at the sender's priority while serving its message. */
    start(logger_thread,    NULL,                      PRIO_LOW,    "logger");
    start(rx_thread,        att,                       PRIO_HIGH,   "rx_server");
    start(tick_thread,      NULL,                      PRIO_HIGH,   "tick");
    start(link_thread,      (void *)(intptr_t)NODE_I1, PRIO_NORMAL, "link_i1");
    start(link_thread,      (void *)(intptr_t)NODE_I2, PRIO_NORMAL, "link_i2");
    start(link_thread,      (void *)(intptr_t)NODE_X1, PRIO_NORMAL, "link_x1");
    start(dashboard_thread, NULL,                      PRIO_NORMAL, "dashboard");

    menu_thread(NULL);                         /* main thread becomes the menu; exits on 'q' */
    return EXIT_SUCCESS;
}