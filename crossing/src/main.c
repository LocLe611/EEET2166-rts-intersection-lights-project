#include "message.h"
#include "config.h"
#include "x1_internal.h"
#include "receive_union.h"
#include "communication.h"
#include "railway_controller.h"
#include "sensor_input.h"
#include "hardware_io.h"

#include <sys/dispatch.h>
#include <sys/iofunc.h>
#include <sys/neutrino.h>
#include <sys/netmgr.h>

#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static timer_t create_gate_timer(int self_coid)
{
    struct sigevent event;
    timer_t timer_id;

    SIGEV_PULSE_INIT(&event,
                     self_coid,
                     SIGEV_PULSE_PRIO_INHERIT,
                     PULSE_X1_GATE_COMPLETE,
                     0);

    if (timer_create(CLOCK_MONOTONIC, &event, &timer_id) == -1) {
        perror("timer_create");
        exit(EXIT_FAILURE);
    }

    return timer_id;
}

static void handle_kernel_pulse(const struct _pulse *pulse)
{
    switch (pulse->code) {
        case _PULSE_CODE_DISCONNECT:
            ConnectDetach(pulse->scoid);
            break;

        case _PULSE_CODE_UNBLOCK:
            break;

        case _PULSE_CODE_COIDDEATH:
            break;

        default:
            break;
    }
}

int main(void)
{
    name_attach_t *attach;
    int self_coid;
    timer_t gate_timer;
    pthread_t sensor_thread;
    sensor_thread_args_t sensor_args;

    x1_communication_t communication;
    railway_controller_t controller;

    int pthread_rc;
    int running;

    printf("========================================\n");
    printf(" Node 3 - X1 Railway Crossing Controller\n");
    printf(" Service: %s\n", X1_SERVICE);
    printf("========================================\n");

    /*
     * Local service for standalone testing.
     * Change 0 back to NAME_FLAG_ATTACH_GLOBAL when integrating
     * with the other QNX nodes using GNS.
     */
    attach = name_attach(NULL, X1_SERVICE, 0 /* NAME_FLAG_ATTACH_GLOBAL */);

    if (attach == NULL) {
        perror("name_attach");
        return EXIT_FAILURE;
    }

    /*
     * Create a connection back to X1's own channel.
     * The keyboard input thread and gate timer use this
     * to send internal pulses.
     */
    self_coid = ConnectAttach(ND_LOCAL_NODE,
                              0,
                              attach->chid,
                              _NTO_SIDE_CHANNEL,
                              0);

    if (self_coid == -1) {
        perror("ConnectAttach(self)");
        name_detach(attach, 0);
        return EXIT_FAILURE;
    }

    gate_timer = create_gate_timer(self_coid);

    hw_init();
    x1_communication_init(&communication);

    railway_controller_init(&controller,
                            &communication,
                            gate_timer);

    sensor_args.controller_coid = self_coid;

    pthread_rc = pthread_create(&sensor_thread,
                                NULL,
                                sensor_input_thread,
                                &sensor_args);

    if (pthread_rc != 0) {
        errno = pthread_rc;
        perror("pthread_create");

        x1_communication_close(&communication);
        timer_delete(gate_timer);
        ConnectDetach(self_coid);
        name_detach(attach, 0);
        hw_shutdown();

        return EXIT_FAILURE;
    }

    running = 1;

    while (running) {
        x1_receive_t receive_buffer;
        int rcvid;

        memset(&receive_buffer, 0, sizeof(receive_buffer));

        rcvid = MsgReceive(attach->chid,
                           &receive_buffer,
                           sizeof(receive_buffer),
                           NULL);

        if (rcvid == -1) {
            if (errno == EINTR) {
                continue;
            }

            perror("MsgReceive");
            break;
        }

        if (rcvid == 0) {
            int code;

            code = receive_buffer.pulse.code;

            if (code < _PULSE_CODE_MINAVAIL) {
                handle_kernel_pulse(&receive_buffer.pulse);
                continue;
            }

            switch (code) {
                case PULSE_X1_TRAIN_APPROACH:
                    railway_train_approach(&controller);
                    break;

                case PULSE_X1_TRAIN_CLEAR:
                    railway_train_clear(&controller);
                    break;

                case PULSE_X1_MANUAL_FAULT:
                    railway_manual_gate_fault(&controller);
                    break;

                case PULSE_X1_LOCAL_RESET:
                    railway_clear_fault(&controller);
                    break;

                case PULSE_X1_GATE_COMPLETE:
                    railway_gate_lowering_complete(&controller);
                    break;

                case PULSE_X1_QUIT:
                    running = 0;
                    break;

                default:
                    printf("[X1] Unknown pulse code %d.\n", code);
                    break;
            }

            continue;
        }

        /*
         * name_open() causes the server to receive
         * an _IO_CONNECT message.
         */
        if (receive_buffer.header.type == _IO_CONNECT) {
            MsgReply(rcvid, EOK, NULL, 0);
            continue;
        }

        if (receive_buffer.header.type >= _IO_BASE &&
            receive_buffer.header.type <= _IO_MAX) {
            MsgError(rcvid, ENOSYS);
            continue;
        }

        switch (receive_buffer.header.type) {
            case MSG_CLEAR_FAULT: {
                msg_reply_t reply;
                int clear_result;

                clear_result = railway_clear_fault(&controller);

                reply.result =
                    clear_result == 0 ? RESULT_OK : RESULT_REJECTED;

                reply.sequence =
                    receive_buffer.clear_fault.header.sequence;

                MsgReply(rcvid,
                         EOK,
                         &reply,
                         sizeof(reply));
                break;
            }

            default:
                MsgError(rcvid, ENOSYS);
                break;
        }
    }

    pthread_join(sensor_thread, NULL);

    x1_communication_close(&communication);

    timer_delete(gate_timer);
    ConnectDetach(self_coid);
    name_detach(attach, 0);

    hw_shutdown();

    printf("[X1] Shutdown complete.\n");

    return EXIT_SUCCESS;
}
