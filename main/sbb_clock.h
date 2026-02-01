/**
 * @file sbb_clock.h
 * @brief Swiss Railway (SBB) Clock widget for LVGL v8.
 * Implements stop-to-go logic and minute hand inertia physics.
 */

#ifndef SBB_CLOCK_H
#define SBB_CLOCK_H

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Handle type for SBB clock (it is lv_obj_t*).
 */
typedef lv_obj_t * sbb_clock_t;

/**
 * Creates SBB clock widget.
 * @param parent Parent object (e.g. lv_scr_act()).
 * @param size   Clock size in pixels (clock is square).
 * @return       Pointer to clock object or NULL in case of error.
 */
sbb_clock_t sbb_clock_create(lv_obj_t *parent, lv_coord_t size);

/**
 * Informs the clock about time synchronization (e.g. from NTP).
 * When set to 'true', the clock will perform a smooth animation from 12:00
 * to the current system time.
 * @param clock  Clock handle.
 * @param synced true if time is synchronized.
 */
void sbb_clock_set_time_synced(sbb_clock_t clock, bool synced);

/**
 * Gets current hand angles (useful for debugging).
 * @param clock     Clock handle.
 * @param hour_deg  Hour hand angle (0-360).
 * @param min_deg   Minute hand angle (0-360).
 * @param sec_deg   Second hand angle (0-360, accounts for stop-to-go).
 */
void sbb_clock_get_angles_deg(sbb_clock_t clock, float *hour_deg, float *min_deg, float *sec_deg);

 #ifdef __cplusplus
 }
 #endif
 
 #endif /* SBB_CLOCK_H */