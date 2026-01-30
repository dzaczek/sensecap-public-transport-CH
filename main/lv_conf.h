/**
 * LVGL configuration override for this project.
 * Ensures rotation/transform (clock hands, dial marks) work.
 *
 * STEP 1: Enable LV_DRAW_COMPLEX (already set to 1 here).
 * If hands still do not rotate, copy this file to
 * managed_components/lv_conf.h (next to the lvgl folder), then:
 *   idf.py fullclean
 *   idf.py build
 *
 * STEP 2: Recommended LV_MEM_SIZE min. 32–64 KB in LVGL config (menuconfig).
 */
#ifndef LV_CONF_H
#define LV_CONF_H

/* Enable complex draw engine (required for rotation, skewing, etc.) */
#define LV_DRAW_COMPLEX 1

#endif /* LV_CONF_H */
