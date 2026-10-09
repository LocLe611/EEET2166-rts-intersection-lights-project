/*
 * uplink.c — sends the intersection status to central.
 *
 * Network calls can stall, so they live in this thread, and every MsgSend is bounded by
 * IPC_SEND_TIMEOUT_MS with TimerTimeout (report §2.1). If central is slow or down, the lights
 * and the display carry on; this thread just retries name_open every STATUS_PERIOD_MS.
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/neutrino.h>
#include <sys/dispatch.h>

#include "uplink.h"
#include "intersection.h"
#include "status_box.h"

/* MsgSend with a deadline. Returns -1 (errno ETIMEDOUT) if central doesn't receive AND
 * reply within IPC_SEND_TIMEOUT_MS. */
static int send_with_timeout(int coid, const void *msg, size_t len, void *reply, size_t rlen)
{
    uint64_t timeout_ns = (uint64_t)IPC_SEND_TIMEOUT_MS * 1000000ULL;
    /* TimerTimeout applies to the very next kernel call only — nothing in between! */
    TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_SEND | _NTO_TIMEOUT_REPLY, NULL, &timeout_ns, NULL);
    return (int)MsgSend(coid, msg, len, reply, rlen);
}

/* Log and show the central link state only when it changes (no spam while central is down). */
static void set_link(const ctrl_t *c, int *link, int now_up, const char *why)
{
    if (*link == now_up)
        return;
    *link = now_up;
    if (now_up)
        printf("%s uplink: central up (%s)\n", c->name, c->central_path);
    else
        printf("%s uplink: central down (%s) - lights keep running, retrying every %d ms\n",
               c->name, why, STATUS_PERIOD_MS);
    status_box_set_central_up(now_up != 0);
}

void *uplink_thread(void *arg)
{
    const ctrl_t *c = arg;
    intersection_status_msg_t s;
    msg_reply_t rep;
    uint32_t seen = 0, tx_seq = 0;
    uint8_t last_main = 0xFF, last_side = 0xFF, last_ped = 0xFF;
    int coid = -1;
    int link = -1; /* -1 unknown, 0 down, 1 up */

    /* Wakes when the status changes, or every STATUS_PERIOD_MS anyway (= heartbeat) */
    while (status_box_wait(&seen, STATUS_PERIOD_MS, &s, NULL))
    {

        if (coid == -1)
        { /* (re)connect: start-up order doesn't matter */
            coid = name_open(c->central_path, 0);
            if (coid == -1)
            {
                set_link(c, &link, 0, strerror(errno));
                continue;
            }
        }

        /* MSG_LIGHT_STATUS if the lamps changed since the last message, otherwise
         * MSG_INTERSECTION_STATUS (mode / railway / link change, or the periodic resend). */
        s.header.type = (s.main_light != last_main || s.side_light != last_side ||
                         s.ped_light != last_ped)
                            ? MSG_LIGHT_STATUS
                            : MSG_INTERSECTION_STATUS;
        s.header.subtype = SUBTYPE_STATUS_UPDATE;
        s.header.sender = (uint8_t)c->node;
        s.header.sequence = ++tx_seq;

        if (send_with_timeout(coid, &s, sizeof s, &rep, sizeof rep) == -1)
        {
            set_link(c, &link, 0, strerror(errno));
            name_close(coid);
            coid = -1;
            last_main = last_side = last_ped = 0xFF; /* next send counts as a light change */
            continue;
        }
        set_link(c, &link, 1, NULL);
        last_main = s.main_light;
        last_side = s.side_light;
        last_ped = s.ped_light;
    }

    if (coid != -1)
        name_close(coid);
    return NULL;
}
