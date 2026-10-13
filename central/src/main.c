/* main.c — central controller entry point. Run on rts-central:  ./central
 * Requires GNS:  gns -s  on rts-central,  gns -c  on every other node. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <sys/dispatch.h>
#include "central.h"

static void start(void *(*fn)(void *), void *arg, int prio, const char *name)
{
    pthread_attr_t a;
    struct sched_param sp = { .sched_priority = prio };
    pthread_attr_init(&a);
    pthread_attr_setinheritsched(&a, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&a, SCHED_RR);
    pthread_attr_setschedparam(&a, &sp);
    pthread_t t;
    int rc = pthread_create(&t, &a, fn, arg);
    if (rc != 0) {
        fprintf(stderr, "pthread_create %s: %s\n", name, strerror(rc));
        exit(1);
    }
    pthread_setname_np(t, name);
    pthread_attr_destroy(&a);
}

int main(void)
{
    ctl_init();
    if (log_init(CENTRAL_LOG_PATH) != 0)
        fprintf(stderr, "warning: cannot open %s, logging to screen only\n", CENTRAL_LOG_PATH);

    name_attach_t *att = name_attach(NULL, SVC_CENTRAL, NAME_FLAG_ATTACH_GLOBAL);
    if (!att) {
        fprintf(stderr, "name_attach(%s) failed: %s  (is gns running?)\n", SVC_CENTRAL, strerror(errno));
        return 1;
    }
    log_msg("central started, attached global name %s", SVC_CENTRAL);

    /* Priorities: inbound status + watchdog first, UI next, file I/O last.
     * The RX thread also inherits the sender's priority while serving a message. */
    start(logger_thread,       NULL,                      PRIO_LOW,    "logger");
    start(rx_thread,           att,                       PRIO_HIGH,   "rx_server");
    start(tick_thread,         NULL,                      PRIO_HIGH,   "tick");
    start(link_thread,         (void *)(intptr_t)NODE_I1, PRIO_NORMAL, "link_i1");
    start(link_thread,         (void *)(intptr_t)NODE_I2, PRIO_NORMAL, "link_i2");
    start(display_link_thread, NULL,                      PRIO_NORMAL, "link_display");
    start(dashboard_thread,    NULL,                      PRIO_NORMAL, "dashboard");

    menu_thread(NULL);          /* main thread becomes the menu; exits on 'q' */
    return 0;
}
