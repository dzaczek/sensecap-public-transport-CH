/**
 * LVGL configuration override for this project.
 * Ensures rotation/transform (clock hands, dial marks) work.
 *
 * KROK 1: Włącz LV_DRAW_COMPLEX (tu już ustawione na 1).
 * Jeśli wskazówki nadal się nie obracają, skopiuj ten plik do
 * managed_components/lv_conf.h (obok folderu lvgl), potem:
 *   idf.py fullclean
 *   idf.py build
 *
 * KROK 2: Zalecane LV_MEM_SIZE min. 32–64 KB w konfiguracji LVGL (menuconfig).
 */
#ifndef LV_CONF_H
#define LV_CONF_H

/* Enable complex draw engine (required for rotation, skewing, etc.) */
#define LV_DRAW_COMPLEX 1

#endif /* LV_CONF_H */
