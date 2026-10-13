/*
 * timing.c — EDIT TIMINGS HERE.
 *
 * Real-world durations in milliseconds. PLACEHOLDERS: replace them with the values your
 * report derived from Google Earth (road widths, 1.2 m/s walking speed, yellow for 60 km/h).
 */
#include "timing.h"

static const timing_t TIMING_I1 = {
    .main_green_ms = 30000,
    .side_green_ms = 20000, /* R1 Grange Rd */
    .min_green_ms = 8000,
    .yellow_ms = 4000,
    .all_red_ms = 2000,
    .walk_ms = 6000,
    .ped_clear_ms = 15000,
    .extend_ms = 3000,
};

static const timing_t TIMING_I2 = {
    .main_green_ms = 30000,
    .side_green_ms = 15000, /* R2 Booran Rd: moderate traffic (spec), so a shorter green */
    .min_green_ms = 8000,
    .yellow_ms = 4000,
    .all_red_ms = 2000,
    .walk_ms = 6000,
    .ped_clear_ms = 15000,
    .extend_ms = 3000,
};

static uint32_t scaled(uint32_t ms)
{
    uint32_t s = ms / TIME_SCALE;
    return s ? s : 1;
}

void timing_load(int id, timing_t *t)
{
    *t = (id == 2) ? TIMING_I2 : TIMING_I1;

    t->main_green_ms = scaled(t->main_green_ms);
    t->side_green_ms = scaled(t->side_green_ms);
    t->min_green_ms = scaled(t->min_green_ms);
    t->yellow_ms = scaled(t->yellow_ms);
    t->all_red_ms = scaled(t->all_red_ms);
    t->walk_ms = scaled(t->walk_ms);
    t->ped_clear_ms = scaled(t->ped_clear_ms);
    t->extend_ms = scaled(t->extend_ms);
}
