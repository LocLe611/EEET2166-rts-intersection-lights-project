#include "hardware_io.h"
#include <stdio.h>

void hw_init(void)
{
    printf("[HW] X1 hardware simulation initialised.\n");
}

void hw_shutdown(void)
{
    printf("[HW] X1 hardware simulation stopped.\n");
}

void hw_set_warning_lights(int on)
{
    printf("[HW] Railway warning lights: %s\n", on ? "ON" : "OFF");
}

void hw_command_gate(gate_state_t state)
{
    const char *text = "UNKNOWN";

    switch (state) {
        case GATE_UP:       text = "UP"; break;
        case GATE_LOWERING: text = "LOWERING"; break;
        case GATE_DOWN:     text = "DOWN"; break;
        case GATE_RAISING:  text = "RAISING"; break;
        case GATE_UNKNOWN:  text = "UNKNOWN"; break;
    }

    printf("[HW] Boom gate state: %s\n", text);
}

void hw_set_train_signal(train_signal_t signal)
{
    printf("[HW] Train signal: %s\n",
           signal == TRAIN_SIGNAL_GREEN ? "GREEN" : "RED");
}
