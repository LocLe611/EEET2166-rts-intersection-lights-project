/*
 * events.h — turns what MsgReceive delivers (pulses, messages) into state-machine inputs.
 *            Called ONLY from the main thread.
 */
#ifndef EVENTS_H
#define EVENTS_H

#include "intersection.h"

/* A pulse: timer expiry, simulated key, railway event from X1, or a kernel pulse. */
void events_handle_pulse(ctrl_t *c, const struct _pulse *p);

/* A message from central (len = bytes received). Returns the result_code_t to reply with,
 * or -1 if it isn't a message this node understands. */
int32_t events_handle_message(ctrl_t *c, const void *msg, int len);

#endif
