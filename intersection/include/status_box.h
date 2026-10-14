#ifndef STATUS_BOX_H
#define STATUS_BOX_H

#include <stdbool.h>
#include <stdint.h>
#include "message.h"

int status_box_init(void);
void status_box_publish(const intersection_status_msg_t *s);
bool status_box_wait(uint32_t *seen, uint32_t timeout_ms,
                     intersection_status_msg_t *out, bool *central_up);
void status_box_set_central_up(bool up);
void status_box_shutdown(void);

#endif
