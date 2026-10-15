#include <ctype.h>
#include <stdio.h>
#include <sys/neutrino.h>
#include "sensor_input.h"
#include "x1_internal.h"

static void send_internal_pulse(int coid, int code)
{
    if (MsgSendPulse(coid, -1, code, 0) == -1)
        perror("MsgSendPulse");
}

static void print_help(void)
{
    printf("\nX1 COMMANDS\n");
    printf("  A  Train approaching\n");
    printf("  D  Gate-down sensor confirmed\n");
    printf("  C  Train cleared crossing\n");
    printf("  F  Inject gate/sensor fault\n");
    printf("  R  Clear latched fault\n");
    printf("  H  Show commands\n");
    printf("  Q  Quit\n");
}

void *sensor_input_thread(void *arg)
{
    sensor_thread_args_t *args = arg;
    char line[64];
    char command;

    print_help();

    while (fgets(line, sizeof line, stdin) != NULL) {
        command = (char)toupper((unsigned char)line[0]);

        switch (command) {
        case 'A':
            send_internal_pulse(args->controller_coid, PULSE_X1_TRAIN_APPROACH);
            break;
        case 'D':
            send_internal_pulse(args->controller_coid, PULSE_X1_GATE_DOWN_CONFIRMED);
            break;
        case 'C':
            send_internal_pulse(args->controller_coid, PULSE_X1_TRAIN_CLEAR);
            break;
        case 'F':
            send_internal_pulse(args->controller_coid, PULSE_X1_MANUAL_FAULT);
            break;
        case 'R':
            send_internal_pulse(args->controller_coid, PULSE_X1_LOCAL_RESET);
            break;
        case 'H':
        case '?':
            print_help();
            break;
        case 'Q':
            send_internal_pulse(args->controller_coid, PULSE_X1_QUIT);
            return NULL;
        case '\n':
        case '\0':
            break;
        default:
            printf("[X1] Unknown command '%c' (press H).\n", command);
            break;
        }
    }

    send_internal_pulse(args->controller_coid, PULSE_X1_QUIT);
    return NULL;
}
