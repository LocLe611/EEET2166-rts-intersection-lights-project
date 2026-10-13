/*
 * fsm.h — the traffic-light state machine (report Fig 5 cycle + Fig 8 pedestrian phase).
 *         Called ONLY from the main thread.
 */
#ifndef FSM_H
#define FSM_H

#include "intersection.h"

void fsm_start(ctrl_t *c);              /* power-up: everything red, inputs at safe values   */
void fsm_on_phase_timer(ctrl_t *c);     /* the phase timer really expired                    */
void fsm_rail_changed(ctrl_t *c);       /* railway state or X1 link changed -> preemption    */
void fsm_override_changed(ctrl_t *c);   /* c->override was just changed by central           */
void fsm_clear_fault(ctrl_t *c);        /* operator cleared the fault                        */
void fsm_refresh(ctrl_t *c);            /* some input changed: re-check outputs and publish  */

#endif
