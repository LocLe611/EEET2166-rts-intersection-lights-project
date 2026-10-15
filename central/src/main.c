#include <errno.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dispatch.h>
#include <unistd.h>
#include "central.h"
#include "console.h"

static int name_flags(void)
{
    return IPC_USE_GLOBAL_NAMES ? NAME_FLAG_ATTACH_GLOBAL : 0;
}

static void start(void *(*fn)(void *), void *arg, int prio, const char *name)
{
    pthread_attr_t a;
    struct sched_param sp;
    pthread_t t;
    int rc;

    memset(&sp, 0, sizeof sp);
    sp.sched_priority = prio;
    pthread_attr_init(&a);
    pthread_attr_setinheritsched(&a, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&a, SCHED_RR);
    pthread_attr_setschedparam(&a, &sp);

    rc = pthread_create(&t, &a, fn, arg);
    pthread_attr_destroy(&a);

    if (rc == EPERM)
        rc = pthread_create(&t, NULL, fn, arg);

    if (rc != 0) {
        fprintf(stderr, "pthread_create %s: %s\n", name, strerror(rc));
        exit(EXIT_FAILURE);
    }
}

int main(void)
{
    name_attach_t *att;
    char hostname[64] = "?";

    ctl_init();

    if (log_init(CENTRAL_LOG_PATH) != 0)
        fprintf(stderr, "warning: cannot open %s\n", CENTRAL_LOG_PATH);

    att = name_attach(NULL, CENTRAL_SERVICE, name_flags());
    if (!att) {
        fprintf(stderr, "name_attach(%s) failed: %s\n",
                CENTRAL_SERVICE, strerror(errno));
        return EXIT_FAILURE;
    }

    printf("%s\n", UI_RULE);
    gethostname(hostname, sizeof hostname - 1);
    hostname[sizeof hostname - 1] = '\0';
    printf(" EEET2166 | CENTRAL SUPERVISORY CONTROLLER\n");
    printf(" Host    : %s\n", hostname);
    printf(" Service : /dev/name/%s/%s\n",
           IPC_USE_GLOBAL_NAMES ? "global" : "local", CENTRAL_SERVICE);
    printf(" Role    : Supervisory modes, overrides and system monitoring\n");
    printf("%s\n", UI_RULE);

    log_msg("Central online");

    start(logger_thread, NULL, PRIO_LOW, "logger");
    start(rx_thread, att, PRIO_HIGH, "rx_server");
    start(tick_thread, NULL, PRIO_HIGH, "tick");
    start(link_thread, (void *)(intptr_t)NODE_I1, PRIO_NORMAL, "link_i1");
    start(link_thread, (void *)(intptr_t)NODE_I2, PRIO_NORMAL, "link_i2");
    start(link_thread, (void *)(intptr_t)NODE_X1, PRIO_NORMAL, "link_x1");
    start(display_link_thread, NULL, PRIO_LOW, "display_link");

    menu_thread(NULL);

    name_detach(att, 0);
    log_msg("Central stopped");
    return EXIT_SUCCESS;
}
