#ifndef TIMING_H
#define TIMING_H

#include <stdint.h>

#ifndef TIME_SCALE
#define TIME_SCALE 5
#endif

typedef struct {
    uint32_t main_green_ms;
    uint32_t side_green_ms;
    uint32_t min_green_ms;
    uint32_t yellow_ms;
    uint32_t all_red_ms;
    uint32_t walk_ms;
    uint32_t ped_clear_ms;
    uint32_t extend_ms;
} timing_t;

void timing_load(int id, timing_t *t);

#endif
