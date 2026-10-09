#include "railway_controller.h"
#include "hardware_io.h"
#include "config.h"

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

static void cancel_gate_timeout(railway_controller_t *controller)
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
    cancel_gate_timeout(controller);

    controller->state = RAIL_FAULT;
    controller->fault_flags |= fault_flag;
    controller->gate_state = GATE_UNKNOWN;
    controller->warning_lights_on = 1;
    controller->train_signal = TRAIN_SIGNAL_RED;

    hw_set_warning_lights(1);
    hw_command_gate(GATE_UNKNOWN);
    hw_set_train_signal(TRAIN_SIGNAL_RED);

    /*
     * Small urgent event to both local intersection controllers.
     * Detailed fault information goes to Central as a normal message.
     */
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
        printf("[X1] TRAIN_APPROACH ignored: X1 is not CLEAR.\n");
        return;
    }

    printf("\n[X1] Train approach detected.\n");

    controller->state = RAIL_APPROACHING;
    controller->train_active = 1;
    controller->gate_state = GATE_LOWERING;
    controller->warning_lights_on = 1;
    controller->train_signal = TRAIN_SIGNAL_RED;

    hw_set_warning_lights(1);
    hw_command_gate(GATE_LOWERING);
    hw_set_train_signal(TRAIN_SIGNAL_RED);

    /*
     * Notify I1/I2 immediately so they can begin a safe R3 pre-emption
     * while the railway gate is lowering.
     */
    x1_send_railway_event(controller->comm, RAIL_EVENT_APPROACH);

    arm_gate_timeout(controller);
    report_status(controller);
    railway_print_state(controller);
}

void railway_gate_down_confirmed(railway_controller_t *controller)
{
    if (controller->state != RAIL_APPROACHING) {
        printf("[X1] GATE_DOWN ignored: not waiting for gate confirmation.\n");
        return;
    }

    printf("\n[X1] Gate DOWN confirmed.\n");

    cancel_gate_timeout(controller);

    controller->gate_state = GATE_DOWN;
    controller->state = RAIL_GATES_DOWN;
    controller->train_signal = TRAIN_SIGNAL_GREEN;

    hw_command_gate(GATE_DOWN);
    hw_set_train_signal(TRAIN_SIGNAL_GREEN);

    report_status(controller);
    railway_print_state(controller);
}

void railway_train_enter(railway_controller_t *controller)
{
    if (controller->state != RAIL_GATES_DOWN) {
        printf("[X1] TRAIN_ENTER rejected: gate has not been confirmed DOWN.\n");
        return;
    }

    printf("\n[X1] Train entered crossing.\n");

    controller->state = RAIL_CROSSING;

    x1_send_railway_event(controller->comm, RAIL_EVENT_CROSSING);

    report_status(controller);
    railway_print_state(controller);
}

void railway_train_clear(railway_controller_t *controller)
{
    if (controller->state == RAIL_FAULT) {
        /*
         * Train-clear does not automatically clear a latched fault.
         */
        printf("\n[X1] Train clear detected, but fault remains latched.\n");
        controller->train_active = 0;
        report_status(controller);
        railway_print_state(controller);
        return;
    }

    if (controller->state != RAIL_CROSSING &&
        controller->state != RAIL_GATES_DOWN) {
        printf("[X1] TRAIN_CLEAR ignored: no active crossing sequence.\n");
        return;
    }

    printf("\n[X1] Train cleared crossing.\n");

    controller->train_active = 0;
    controller->state = RAIL_CLEAR;
    controller->gate_state = GATE_UP;
    controller->warning_lights_on = 0;
    controller->train_signal = TRAIN_SIGNAL_RED;

    hw_command_gate(GATE_RAISING);
    hw_set_warning_lights(0);
    hw_command_gate(GATE_UP);
    hw_set_train_signal(TRAIN_SIGNAL_RED);

    x1_send_railway_event(controller->comm, RAIL_EVENT_CLEAR);

    report_status(controller);
    railway_print_state(controller);
}

void railway_gate_up_confirmed(railway_controller_t *controller)
{
    printf("\n[X1] Gate UP/healthy confirmed.\n");

    controller->gate_state = GATE_UP;
    hw_command_gate(GATE_UP);

    /*
     * In FAULT this does not automatically clear the fault.
     * An explicit clear-fault request is still required.
     */
    report_status(controller);
    railway_print_state(controller);
}

void railway_manual_gate_fault(railway_controller_t *controller)
{
    printf("\n[X1] Gate sensor/fault event detected.\n");
    enter_fault(controller, X1_FAULT_GATE_SENSOR);
}

void railway_gate_timeout(railway_controller_t *controller)
{
    if (controller->state != RAIL_APPROACHING) {
        return;
    }

    printf("\n[X1] Gate confirmation TIMEOUT.\n");
    enter_fault(controller, X1_FAULT_GATE_TIMEOUT);
}

int railway_clear_fault(railway_controller_t *controller)
{
    if (controller->state != RAIL_FAULT) {
        printf("[X1] CLEAR_FAULT rejected: X1 is not in FAULT.\n");
        return -1;
    }

    if (controller->train_active) {
        printf("[X1] CLEAR_FAULT rejected: train event is still active.\n");
        return -1;
    }

    if (controller->gate_state != GATE_UP) {
        printf("[X1] CLEAR_FAULT rejected: gate is not confirmed UP/healthy.\n");
        return -1;
    }

    printf("\n[X1] Fault manually cleared.\n");

    controller->fault_flags = X1_FAULT_NONE;
    controller->state = RAIL_CLEAR;
    controller->warning_lights_on = 0;
    controller->train_signal = TRAIN_SIGNAL_RED;

    hw_set_warning_lights(0);
    hw_set_train_signal(TRAIN_SIGNAL_RED);

    /*
     * I1/I2 may be holding R3 red because of the earlier fault.
     * CLEAR tells them that X1 is safe again.
     */
    x1_send_railway_event(controller->comm, RAIL_EVENT_CLEAR);

    report_status(controller);
    railway_print_state(controller);

    return 0;
}

void railway_print_state(const railway_controller_t *controller)
{
    printf("----------------------------------------\n");
    printf(" X1 railway state : %s\n", railway_state_name(controller->state));
    printf(" Gate state       : %s\n", gate_state_name(controller->gate_state));
    printf(" Warning lights   : %s\n",
           controller->warning_lights_on ? "ON" : "OFF");
    printf(" Train signal     : %s\n",
           controller->train_signal == TRAIN_SIGNAL_GREEN ?
           "GREEN" : "RED");
    printf(" Train active     : %s\n",
           controller->train_active ? "YES" : "NO");
    printf(" Fault flags      : 0x%08X\n",
           (unsigned)controller->fault_flags);
    printf("----------------------------------------\n");
}
