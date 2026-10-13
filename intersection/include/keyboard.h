/*
 * keyboard.h — simulated sensors ("Vehicle sensor" + "Pedestrian input", report Fig 15)
 */
#ifndef KEYBOARD_H
#define KEYBOARD_H

void *keyboard_thread(void *arg);   /* arg = ctrl_t* (reads identity fields only) */

#endif
