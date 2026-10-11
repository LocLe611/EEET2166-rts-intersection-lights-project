#ifndef RAILWAY_CONTROLLER_H
#define RAILWAY_CONTROLLER_H

#include <stdint.h>
#include <time.h>
#include "message.h"
#include "communication.h"

typedef struct {
    railway_state_t state;
    gate_state_t gate_state;
    train_signal_t train_signal;
    int warning_lights_on;
    int train_active;
    uint32_t fault_flags;
    timer_t gate_timer;
    x1_communication_t *comm;
} railway_controller_t;

void railway_controller_init(railway_controller_t *controller,
                             x1_communication_t *comm,
                             timer_t gate_timer);

void railway_train_approach(railway_controller_t *controller);
void railway_gate_lowering_complete(railway_controller_t *controller);
void railway_train_clear(railway_controller_t *controller);

void railway_manual_gate_fault(railway_controller_t *controller);
int railway_clear_fault(railway_controller_t *controller);

void railway_print_state(const railway_controller_t *controller);

#endif
