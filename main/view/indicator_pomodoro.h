#ifndef INDICATOR_POMODORO_H
#define INDICATOR_POMODORO_H

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize Pomodoro timer view with falling sand hourglass visualization
 * 
 * Creates a canvas-based cellular automata sand simulation.
 * Physics calculations run on Core 1 (FreeRTOS task).
 * Rendering happens on Core 0 (LVGL timer).
 * 
 * @param parent Parent LVGL object (screen/tabview)
 * @return lv_obj_t* Created Pomodoro screen object
 */
lv_obj_t* lv_indicator_pomodoro_init(lv_obj_t *parent);

/**
 * @brief Destroy Pomodoro view and cleanup resources
 */
void lv_indicator_pomodoro_deinit(void);

/**
 * @brief Get current timer state
 * @return true if timer is running, false if paused/stopped
 */
bool lv_indicator_pomodoro_is_running(void);

/**
 * @brief Get remaining time in seconds
 * @return Remaining time in seconds
 */
int lv_indicator_pomodoro_get_remaining_seconds(void);

#ifdef __cplusplus
}
#endif

#endif // INDICATOR_POMODORO_H
