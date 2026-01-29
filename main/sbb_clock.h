/**
 * @file sbb_clock.h
 * @brief Swiss Railway (SBB) Clock widget for LVGL – visual and stop-to-go logic.
 *
 * LVGL v8. First screen: all hands at 12 at start; when NTP syncs, hands animate
 * to current time over 10 seconds, then run with SBB stop-to-go second hand.
 */
#ifndef SBB_CLOCK_H
#define SBB_CLOCK_H

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque SBB clock widget handle (lv_obj_t *). */
typedef lv_obj_t * sbb_clock_t;

/**
 * Create SBB clock widget.
 * @param parent Parent object (e.g. tab content).
 * @param size   Width and height of the clock (square); dial fits inside.
 * @return       Clock container object, or NULL on failure.
 */
sbb_clock_t sbb_clock_create(lv_obj_t *parent, lv_coord_t size);

/**
 * Set whether time is synced (NTP). When switching to true, a one-shot
 * 10-second animation from 12:00 to current time is started.
 * @param clock   Clock widget from sbb_clock_create.
 * @param synced  true = time synced (start or continue real time + animation if needed).
 */
void sbb_clock_set_time_synced(sbb_clock_t clock, bool synced);

/**
 * Update clock to current system time. Call periodically (e.g. from
 * lv_timer every 20–30 ms) for smooth second hand. If animation is active,
 * this drives the 10s transition; otherwise applies SBB stop-to-go math.
 * @param clock Clock widget.
 */
void sbb_clock_update(sbb_clock_t clock);

/**
 * Get current hour/minute/second angles in degrees (0–360) for testing.
 * Second angle uses SBB logic (0 at top, freeze at 12).
 */
void sbb_clock_get_angles_deg(sbb_clock_t clock, float *hour_deg, float *min_deg, float *sec_deg);

#ifdef __cplusplus
}
#endif

#endif /* SBB_CLOCK_H */
