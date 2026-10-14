#ifndef FSM_H
#define FSM_H

#include "intersection.h"

void fsm_start(ctrl_t *c);
void fsm_on_phase_timer(ctrl_t *c);
void fsm_rail_changed(ctrl_t *c);
void fsm_override_changed(ctrl_t *c);
void fsm_clear_fault(ctrl_t *c);
void fsm_refresh(ctrl_t *c);

#endif
