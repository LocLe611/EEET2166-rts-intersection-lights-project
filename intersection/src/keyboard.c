/*
 * keyboard.c — simulated sensors: each key press becomes a pulse or message to the
 * controller's own channel.
 *
 * Mode / override / clear keys use MsgSend — exactly what central will do — and the train
 * keys send the same PULSE_RAILWAY_EVENT X1 sends, so those paths are tested before the
 * other nodes exist.
 */
#include <stdio.h>
#include <string.h>
#include <sys/neutrino.h>

#include "keyboard.h"
#include "intersection.h"

static void print_help(const ctrl_t *c)
{
    printf("\n%s keys (press key, then Enter):\n"
           "  p  pedestrian button          v  vehicle waiting on %s\n"
           "  1  fixed-timing mode          2  sensor-driven mode\n"
           "  o  cycle override none/main/side   r  clear fault\n"
           "  t  (sim X1) train approaching  c  (sim X1) clear  g  (sim X1) railway fault\n"
           "  e  inject conflicting greens (tests the safety interlock)\n"
           "  h  help    q  quit\n\n",
           c->name, c->side_road);
}

/* Send a command to our own channel exactly as central would. */
static void send_cmd(int coid, uint16_t type, uint8_t arg, uint32_t seq)
{
    union
    {
        msg_header_t header;
        set_mode_msg_t set_mode;
        override_msg_t override;
    } m;
    msg_reply_t rep;
    size_t len = sizeof m.header;

    memset(&m, 0, sizeof m);
    m.header.type = type;
    m.header.subtype = SUBTYPE_NONE;
    m.header.sender = NODE_CENTRAL; /* the keyboard is standing in for central */
    m.header.sequence = seq;
    if (type == MSG_SET_MODE)
    {
        m.set_mode.mode = arg;
        len = sizeof m.set_mode;
    }
    if (type == MSG_OVERRIDE)
    {
        m.override.override = arg;
        len = sizeof m.override;
    }

    if (MsgSend(coid, &m, len, &rep, sizeof rep) == -1)
        perror("MsgSend");
    else if (rep.result != RESULT_OK)
        printf("  command rejected (result %d)\n", (int)rep.result);
    else if (type == MSG_SET_MODE)
        printf("  mode change accepted - applies at the next all-red\n");
}

static void send_rail(int coid, railway_event_t ev, uint16_t seq)
{
    MsgSendPulse(coid, RAILWAY_PULSE_PRIORITY, PULSE_RAILWAY_EVENT,
                 pack_railway_pulse(NODE_X1, ev, seq));
}

static void send_key(int coid, int key)
{
    MsgSendPulse(coid, PRIO_CONTROLLER, PC_KEY, key);
}

void *keyboard_thread(void *arg)
{
    const ctrl_t *c = arg;
    int coid = c->self_coid;
    uint8_t ovr = OVERRIDE_NONE;
    uint16_t rail_seq = 0;
    uint32_t cmd_seq = 0;
    int ch;

    print_help(c);
    while ((ch = getchar()) != EOF)
    {
        switch (ch)
        {
        case 'p':
            send_key(coid, KEY_PED);
            break;
        case 'v':
            send_key(coid, KEY_SIDE_VEHICLE);
            break;
        case 'e':
            send_key(coid, KEY_INJECT_FAULT);
            break;
        case 't':
            send_rail(coid, RAIL_EVENT_APPROACH, ++rail_seq);
            break;
        case 'c':
            send_rail(coid, RAIL_EVENT_CLEAR, ++rail_seq);
            break;
        case 'g':
            send_rail(coid, RAIL_EVENT_FAULT, ++rail_seq);
            break;
        case '1':
            send_cmd(coid, MSG_SET_MODE, MODE_FIXED, ++cmd_seq);
            break;
        case '2':
            send_cmd(coid, MSG_SET_MODE, MODE_SENSOR, ++cmd_seq);
            break;
        case 'o':
            ovr = (uint8_t)((ovr + 1) % 3);
            send_cmd(coid, MSG_OVERRIDE, ovr, ++cmd_seq);
            break;
        case 'r':
            send_cmd(coid, MSG_CLEAR_FAULT, 0, ++cmd_seq);
            break;
        case 'h':
        case '?':
            print_help(c);
            break;
        case 'q':
            send_key(coid, KEY_QUIT);
            return NULL;
        default:
            break; /* newline, spaces, ... */
        }
    }
    return NULL;
}
