#include "railway_controller.h"
#include "hardware_io.h"
#include "config.h"
#include "console.h"

#include <stdio.h>
#include <string.h>

static void arm_gate_timeout(railway_controller_t *controller)
{
    struct itimerspec spec;

    memset(&spec, 0, sizeof(spec));

    spec.it_value.tv_sec = X1_GATE_TIMEOUT_MS / 1000;
    spec.it_value.tv_nsec =
        (long)(X1_GATE_TIMEOUT_MS % 1000) * 1000000L;

    if (timer_settime(controller->gate_timer, 0, &spec, NULL) == -1) {
        perror("[X1] timer_settime");
    }
}

static void cancel_gate_timer(railway_controller_t *controller)
{
    struct itimerspec spec;

    memset(&spec, 0, sizeof(spec));

    if (timer_settime(controller->gate_timer, 0, &spec, NULL) == -1) {
        perror("[X1] cancel gate timer");
    }
}

static void report_status(railway_controller_t *controller)
{
    x1_send_railway_status(controller->comm,
                           controller->state,
                           controller->gate_state,
                           controller->warning_lights_on,
                           controller->train_signal,
                           controller->fault_flags);
}

static const char *railway_state_name(railway_state_t state)
{
    switch (state) {
        case RAIL_CLEAR:       return "CLEAR";
        case RAIL_APPROACHING: return "APPROACHING";
        case RAIL_GATES_DOWN:  return "GATES_DOWN";
        case RAIL_CROSSING:    return "CROSSING";
        case RAIL_FAULT:       return "FAULT";
        default:               return "UNKNOWN";
    }
}

static const char *gate_state_name(gate_state_t state)
{
    switch (state) {
        case GATE_UP:       return "UP";
        case GATE_LOWERING: return "LOWERING";
        case GATE_DOWN:     return "DOWN";
        case GATE_RAISING:  return "RAISING";
        case GATE_UNKNOWN:  return "UNKNOWN";
        default:            return "UNKNOWN";
    }
}

static void enter_fault(railway_controller_t *controller,
                        uint32_t fault_flag)
{
    cancel_gate_timer(controller);

    controller->state = RAIL_FAULT;
    controller->fault_flags |= fault_flag;
    controller->gate_state = GATE_UNKNOWN;
    controller->warning_lights_on = 1;
    controller->train_signal = TRAIN_SIGNAL_RED;

    hw_set_warning_lights(1);
    hw_command_gate(GATE_UNKNOWN);
    hw_set_train_signal(TRAIN_SIGNAL_RED);

    x1_send_railway_event(controller->comm, RAIL_EVENT_FAULT);

    x1_send_fault_status(controller->comm,
                         controller->fault_flags,
                         controller->state,
                         controller->gate_state);

    report_status(controller);
    railway_print_state(controller);
}

void railway_controller_init(railway_controller_t *controller,
                             x1_communication_t *comm,
                             timer_t gate_timer)
{
    if (controller == NULL) {
        return;
    }

    memset(controller, 0, sizeof(*controller));

    controller->state = RAIL_CLEAR;
    controller->gate_state = GATE_UP;
    controller->train_signal = TRAIN_SIGNAL_RED;
    controller->warning_lights_on = 0;
    controller->train_active = 0;
    controller->fault_flags = X1_FAULT_NONE;
    controller->gate_timer = gate_timer;
    controller->comm = comm;

    hw_set_warning_lights(0);
    hw_command_gate(GATE_UP);
    hw_set_train_signal(TRAIN_SIGNAL_RED);

    railway_print_state(controller);
    report_status(controller);
}

void railway_train_approach(railway_controller_t *controller)
{
    if (controller->state != RAIL_CLEAR) {
        printf("[X1] Approach ignored: crossing is not CLEAR.\n");
        return;
    }

    printf("\n[X1][EVENT] Train approach detected.\n");

    controller->state = RAIL_APPROACHING;
    controller->train_active = 1;
    controller->gate_state = GATE_LOWERING;
    controller->warning_lights_on = 1;
    controller->train_signal = TRAIN_SIGNAL_RED;

    hw_set_warning_lights(1);
    hw_command_gate(GATE_LOWERING);
    hw_set_train_signal(TRAIN_SIGNAL_RED);

    x1_send_railway_event(controller->comm, RAIL_EVENT_APPROACH);

    arm_gate_timeout(controller);

    report_status(controller);
    railway_print_state(controller);
}

void railway_gate_down_confirmed(railway_controller_t *controller)
{
    if (controller->state != RAIL_APPROACHING ||
        controller->gate_state != GATE_LOWERING) {
        printf("[X1] Gate confirmation ignored: not waiting for gate-down.\n");
        return;
    }

    cancel_gate_timer(controller);

    printf("\n[X1][EVENT] Gate DOWN confirmed; crossing secured.\n");

    controller->gate_state = GATE_DOWN;
    controller->state = RAIL_GATES_DOWN;
    controller->train_signal = TRAIN_SIGNAL_GREEN;

    hw_command_gate(GATE_DOWN);
    hw_set_train_signal(TRAIN_SIGNAL_GREEN);

    report_status(controller);
    railway_print_state(controller);

    controller->state = RAIL_CROSSING;

    printf("[X1][EVENT] Train is crossing.\n");

    x1_send_railway_event(controller->comm,
                          RAIL_EVENT_CROSSING);

    report_status(controller);
    railway_print_state(controller);
}

void railway_gate_timeout(railway_controller_t *controller)
{
    if (controller->state != RAIL_APPROACHING ||
        controller->gate_state != GATE_LOWERING) {
        return;
    }

    printf(UI_RED "\n[X1][FAULT] Gate-down timeout after %d ms.\n" UI_RESET,
           X1_GATE_TIMEOUT_MS);

    enter_fault(controller, X1_FAULT_GATE_TIMEOUT);
}

void railway_train_clear(railway_controller_t *controller)
{
    if (controller->state == RAIL_FAULT) {
        
        printf("\n[X1][EVENT] Train cleared; fault remains latched.\n");

        controller->train_active = 0;

        report_status(controller);
        railway_print_state(controller);
        return;
    }

    if (controller->state != RAIL_CROSSING &&
        controller->state != RAIL_GATES_DOWN) {
        printf("[X1] Clear ignored: no active crossing sequence.\n");
        return;
    }

    printf("\n[X1][EVENT] Train cleared crossing.\n");

    controller->train_active = 0;
    controller->train_signal = TRAIN_SIGNAL_RED;
    controller->gate_state = GATE_RAISING;

    hw_set_train_signal(TRAIN_SIGNAL_RED);
    hw_command_gate(GATE_RAISING);

    controller->gate_state = GATE_UP;
    controller->warning_lights_on = 0;
    controller->state = RAIL_CLEAR;

    hw_command_gate(GATE_UP);
    hw_set_warning_lights(0);

    x1_send_railway_event(controller->comm,
                          RAIL_EVENT_CLEAR);

    report_status(controller);
    railway_print_state(controller);
}

void railway_manual_gate_fault(railway_controller_t *controller)
{
    printf(UI_RED "\n[X1][FAULT] Gate/sensor fault injected.\n" UI_RESET);

    enter_fault(controller,
                X1_FAULT_GATE_SENSOR);
}

int railway_clear_fault(railway_controller_t *controller)
{
    if (controller->state != RAIL_FAULT) {
        printf("[X1] Clear fault rejected: X1 is not in FAULT.\n");
        return -1;
    }

    if (controller->train_active) {
        printf("[X1] Clear fault rejected: train is still active.\n");
        return -1;
    }

    printf("\n[X1][EVENT] Fault cleared; returning gate UP.\n");

    controller->gate_state = GATE_RAISING;
    hw_command_gate(GATE_RAISING);

    controller->fault_flags = X1_FAULT_NONE;
    controller->state = RAIL_CLEAR;
    controller->gate_state = GATE_UP;
    controller->warning_lights_on = 0;
    controller->train_signal = TRAIN_SIGNAL_RED;

    hw_command_gate(GATE_UP);
    hw_set_warning_lights(0);
    hw_set_train_signal(TRAIN_SIGNAL_RED);

    x1_send_railway_event(controller->comm,
                          RAIL_EVENT_CLEAR);

    report_status(controller);
    railway_print_state(controller);

    return 0;
}

void railway_send_heartbeat(railway_controller_t *controller)
{
    railway_event_t event;

    if (controller == NULL || controller->comm == NULL) {
        return;
    }

    switch (controller->state) {
        case RAIL_CLEAR:
            event = RAIL_EVENT_CLEAR;
            break;
        case RAIL_APPROACHING:
            event = RAIL_EVENT_APPROACH;
            break;
        case RAIL_GATES_DOWN:
        case RAIL_CROSSING:
            event = RAIL_EVENT_CROSSING;
            break;
        case RAIL_FAULT:
        default:
            event = RAIL_EVENT_FAULT;
            break;
    }

    x1_send_heartbeat(controller->comm,
                      event,
                      controller->state,
                      controller->gate_state,
                      controller->warning_lights_on,
                      controller->train_signal,
                      controller->fault_flags);
}

void railway_print_state(const railway_controller_t *controller)
{
    printf("[X1][STATUS] Railway=%s | Gate=%s | Warning=",
           railway_state_name(controller->state),
           gate_state_name(controller->gate_state));

    if (controller->warning_lights_on)
        printf(UI_YELLOW "ON" UI_RESET);
    else
        printf("OFF");

    printf(" | Train=");
    if (controller->train_signal == TRAIN_SIGNAL_GREEN)
        printf(UI_GREEN "GREEN" UI_RESET);
    else
        printf(UI_RED "RED" UI_RESET);

    if (controller->fault_flags)
        printf(UI_RED " | Fault=0x%08X" UI_RESET,
               (unsigned)controller->fault_flags);
    else
        printf(" | Fault=none");

    printf("\n");
}
