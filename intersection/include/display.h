/*
 * display.h — terminal "Light output" (report Fig 15): one line per status change
 */
#ifndef DISPLAY_H
#define DISPLAY_H

void *display_thread(void *arg);    /* arg = ctrl_t* (reads identity fields only) */

#endif
