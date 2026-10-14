#include <stdio.h>
#include "hardware_io.h"
#include "console.h"

void hw_init(void)
{
    printf("[X1][HW] Simulation ready.\n");
}

void hw_shutdown(void)
{
    printf("[X1][HW] Simulation stopped.\n");
}

void hw_set_warning_lights(int on)
{
    if (on)
        printf("[X1][HW] Warning lights: " UI_YELLOW "ON" UI_RESET "\n");
    else
        printf("[X1][HW] Warning lights: OFF\n");
}

void hw_command_gate(gate_state_t state)
{
    const char *text;

    switch (state) {
    case GATE_UP:       text = "UP"; break;
    case GATE_LOWERING: text = "LOWERING"; break;
    case GATE_DOWN:     text = "DOWN"; break;
    case GATE_RAISING:  text = "RAISING"; break;
    case GATE_UNKNOWN:  text = "UNKNOWN"; break;
    default:            text = "UNKNOWN"; break;
    }

    if (state == GATE_UNKNOWN)
        printf("[X1][HW] Boom gate: " UI_RED "%s" UI_RESET "\n", text);
    else
        printf("[X1][HW] Boom gate: %s\n", text);
}

void hw_set_train_signal(train_signal_t signal)
{
    if (signal == TRAIN_SIGNAL_GREEN)
        printf("[X1][HW] Train signal: " UI_GREEN "GREEN" UI_RESET "\n");
    else
        printf("[X1][HW] Train signal: " UI_RED "RED" UI_RESET "\n");
}
