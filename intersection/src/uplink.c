
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/neutrino.h>
#include <sys/dispatch.h>

#include "uplink.h"
#include "intersection.h"
#include "status_box.h"

static int send_with_timeout(int coid, const void *msg, size_t len, void *reply, size_t rlen)
{
    uint64_t timeout_ns = (uint64_t)IPC_SEND_TIMEOUT_MS * 1000000ULL;
    TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_SEND | _NTO_TIMEOUT_REPLY, NULL, &timeout_ns, NULL);
    return (int)MsgSend(coid, msg, len, reply, rlen);
}

static void set_link(const ctrl_t *c, int *link, int now_up, const char *why)
{
    if (*link == now_up)
        return;
    *link = now_up;
    if (now_up)
        printf("[%s] Central link ONLINE (%s).\n", c->name, c->central_path);
    else
        printf("[%s] Central link OFFLINE (%s); local control continues.\n",
               c->name, why);
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
    int link = -1;

    while (status_box_wait(&seen, STATUS_PERIOD_MS, &s, NULL))
    {

        if (coid == -1)
        {
            coid = name_open(c->central_path,
                             IPC_USE_GLOBAL_NAMES ? NAME_FLAG_ATTACH_GLOBAL : 0);
            if (coid == -1)
            {
                set_link(c, &link, 0, strerror(errno));
                continue;
            }
        }

        s.header.type = (s.main_light != last_main || s.side_light != last_side ||
                         s.ped_light != last_ped)
                            ? MSG_LIGHT_STATUS
                            : MSG_INTERSECTION_STATUS;
        s.header.subtype = SUBTYPE_STATUS_UPDATE;
        s.header.sender = (uint8_t)c->node;
        s.header.session = c->session_id;
        s.header.sequence = ++tx_seq;

        if (send_with_timeout(coid, &s, sizeof s, &rep, sizeof rep) == -1)
        {
            set_link(c, &link, 0, strerror(errno));
            name_close(coid);
            coid = -1;
            last_main = last_side = last_ped = 0xFF;
            continue;
        }
        if (rep.result != RESULT_OK || rep.sequence != s.header.sequence)
        {
            set_link(c, &link, 0, "status rejected");
            name_close(coid);
            coid = -1;
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
