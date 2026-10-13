#ifndef COMMUNICATION_H
#define COMMUNICATION_H

#include <stdint.h>
#include "message.h"

typedef struct {
    int i1_coid;
    int i2_coid;
    int central_coid;
    uint16_t pulse_sequence;
    uint32_t message_sequence;
} x1_communication_t;

void x1_communication_init(x1_communication_t *comm);
void x1_communication_close(x1_communication_t *comm);

int x1_send_railway_event(x1_communication_t *comm,
                          railway_event_t event);

int x1_send_railway_status(x1_communication_t *comm,
                           railway_state_t railway_state,
                           gate_state_t gate_state,
                           int warning_lights_on,
                           train_signal_t train_signal,
                           uint32_t fault_flags);

int x1_send_fault_status(x1_communication_t *comm,
                         uint32_t fault_flags,
                         railway_state_t railway_state,
                         gate_state_t gate_state);

#endif
