/*
 * uplink.h — "Communication" to central (report Fig 15): status messages + heartbeat
 */
#ifndef UPLINK_H
#define UPLINK_H

void *uplink_thread(void *arg);     /* arg = ctrl_t* (reads identity fields only) */

#endif
