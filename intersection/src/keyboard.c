#include <ctype.h>
#include <stdio.h>
#include <sys/neutrino.h>
#include "keyboard.h"
#include "intersection.h"

static void print_help(const ctrl_t *c)
{
    printf("\n%s COMMANDS\n", c->name);
    printf("  p  Pedestrian request\n");
    printf("  v  Vehicle waiting on %s\n", c->side_road);
    printf("  e  Inject conflicting-green fault\n");
    printf("  h  Show commands\n");
    printf("  q  Quit\n");
}

static void send_key(int coid, int key)
{
    if (MsgSendPulse(coid, PRIO_CONTROLLER, PC_KEY, key) == -1)
        perror("MsgSendPulse");
}

void *keyboard_thread(void *arg)
{
    const ctrl_t *c = arg;
    int ch;

    print_help(c);

    while ((ch = getchar()) != EOF) {
        switch (ch) {
        case 'p':
            send_key(c->self_coid, KEY_PED);
            break;
        case 'v':
            send_key(c->self_coid, KEY_SIDE_VEHICLE);
            break;
        case 'e':
            send_key(c->self_coid, KEY_INJECT_FAULT);
            break;
        case 'h':
        case '?':
            print_help(c);
            break;
        case 'q':
            send_key(c->self_coid, KEY_QUIT);
            return NULL;
        default:
            if (!isspace((unsigned char)ch))
                printf("%s: unknown command '%c' (press h)\n", c->name, ch);
            break;
        }
    }

    send_key(c->self_coid, KEY_QUIT);
    return NULL;
}
