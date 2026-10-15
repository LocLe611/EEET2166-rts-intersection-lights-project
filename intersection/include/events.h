
#ifndef EVENTS_H
#define EVENTS_H

#include "intersection.h"


void events_handle_pulse(ctrl_t *c, const struct _pulse *p);

int32_t events_handle_message(ctrl_t *c, const void *msg, int len);

#endif
