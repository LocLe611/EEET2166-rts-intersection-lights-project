#include "sensor_input.h"
#include "x1_internal.h"

#include <sys/neutrino.h>
#include <ctype.h>
#include <stdio.h>

static void send_internal_pulse(int coid, int code)
{
    if (MsgSendPulse(coid, -1, code, 0) == -1) {
        perror("[INPUT] MsgSendPulse");
    }
}

void *sensor_input_thread(void *arg)
{
    sensor_thread_args_t *args;
    char line[64];
    char command;

    args = (sensor_thread_args_t *)arg;

    printf("\nX1 keyboard simulation commands:\n");
    printf("  A + Enter : train approaching\n");
    printf("  D + Enter : gate DOWN confirmed\n");
    printf("  T + Enter : train enters crossing\n");
    printf("  C + Enter : train cleared crossing\n");
    printf("  U + Enter : gate UP confirmed\n");
    printf("  F + Enter : gate fault\n");
    printf("  R + Enter : manual clear-fault request\n");
    printf("  Q + Enter : quit X1\n\n");

    while (fgets(line, sizeof(line), stdin) != NULL) {
        command = (char)toupper((unsigned char)line[0]);

        switch (command) {
            case 'A':
                send_internal_pulse(args->controller_coid,
                                    PULSE_X1_TRAIN_APPROACH);
                break;

            case 'D':
                send_internal_pulse(args->controller_coid,
                                    PULSE_X1_GATE_DOWN);
                break;

            case 'T':
                send_internal_pulse(args->controller_coid,
                                    PULSE_X1_TRAIN_ENTER);
                break;

            case 'C':
                send_internal_pulse(args->controller_coid,
                                    PULSE_X1_TRAIN_CLEAR);
                break;

            case 'U':
                send_internal_pulse(args->controller_coid,
                                    PULSE_X1_GATE_UP);
                break;

            case 'F':
                send_internal_pulse(args->controller_coid,
                                    PULSE_X1_MANUAL_FAULT);
                break;

            case 'R':
                send_internal_pulse(args->controller_coid,
                                    PULSE_X1_LOCAL_RESET);
                break;

            case 'Q':
                send_internal_pulse(args->controller_coid,
                                    PULSE_X1_QUIT);
                return NULL;

            case '\n':
            case '\0':
                break;

            default:
                printf("[INPUT] Unknown command '%c'.\n", command);
                break;
        }
    }

    send_internal_pulse(args->controller_coid, PULSE_X1_QUIT);
    return NULL;
}
