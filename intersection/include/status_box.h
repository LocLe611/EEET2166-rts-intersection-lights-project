/*
 * status_box.h — hands the latest intersection status from the controller (main thread)
 *                to the display and uplink threads. One mutex + one condition variable.
 */
#ifndef STATUS_BOX_H
#define STATUS_BOX_H

#include <stdbool.h>
#include <stdint.h>
#include "message.h"

int  status_box_init(void);

/* Controller: store a new status and wake every waiting thread. */
void status_box_publish(const intersection_status_msg_t *s);

/* Display / uplink: block until the status changes after *seen, or timeout_ms passes
 * (0 = no timeout). Copies the status (and central link state) out; updates *seen.
 * Returns false when the program is shutting down. */
bool status_box_wait(uint32_t *seen, uint32_t timeout_ms,
                     intersection_status_msg_t *out, bool *central_up);

void status_box_set_central_up(bool up);   /* uplink tells the display */
void status_box_shutdown(void);            /* wake everyone and make them return false */

#endif
