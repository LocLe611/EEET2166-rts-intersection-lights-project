/*
 * timing.h — the intersection timing table (report §1.1: one table, scaled for the demo)
 */
#ifndef TIMING_H
#define TIMING_H

#include <stdint.h>

#ifndef TIME_SCALE
#define TIME_SCALE 5            /* real durations are divided by this; or CCFLAGS += -DTIME_SCALE=10 */
#endif

typedef struct {
    uint32_t main_green_ms;     /* R3 Glen Huntly Rd green (fixed-timing mode)            */
    uint32_t side_green_ms;     /* R1 Grange Rd (I1) / R2 Booran Rd (I2) green             */
    uint32_t min_green_ms;      /* a green is never cut shorter than this                  */
    uint32_t yellow_ms;         /* yellow clearance                                        */
    uint32_t all_red_ms;        /* all-red clearance                                       */
    uint32_t walk_ms;           /* steady WALK                                             */
    uint32_t ped_clear_ms;      /* flashing DON'T WALK = crossing length / walking speed   */
    uint32_t extend_ms;         /* a held green is re-checked this often                   */
} timing_t;

/* Fills *t with the timings for intersection id (1 or 2), already divided by TIME_SCALE. */
void timing_load(int id, timing_t *t);

#endif
