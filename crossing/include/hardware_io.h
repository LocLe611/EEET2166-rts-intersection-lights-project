#ifndef HARDWARE_IO_H
#define HARDWARE_IO_H

#include "message.h"

void hw_init(void);
void hw_shutdown(void);
void hw_set_warning_lights(int on);
void hw_command_gate(gate_state_t state);
void hw_set_train_signal(train_signal_t signal);

#endif
