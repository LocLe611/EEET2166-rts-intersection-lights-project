#ifndef SENSOR_INPUT_H
#define SENSOR_INPUT_H

typedef struct {
    int controller_coid;
} sensor_thread_args_t;

void *sensor_input_thread(void *arg);

#endif
