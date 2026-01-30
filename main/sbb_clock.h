/**
 * @file sbb_clock.h
 * @brief Swiss Railway (SBB) Clock widget for LVGL v8.
 * Implementuje logikę stop-to-go oraz fizykę bezwładności wskazówki minutowej.
 */

 #ifndef SBB_CLOCK_H
 #define SBB_CLOCK_H
 
 #include "lvgl.h"
 #include <stdbool.h>
 
 #ifdef __cplusplus
 extern "C" {
 #endif
 
 /** * Typ uchwytu dla zegara SBB (jest to lv_obj_t*).
  */
 typedef lv_obj_t * sbb_clock_t;
 
 /**
  * Tworzy widget zegara SBB.
  * @param parent Obiekt nadrzędny (np. lv_scr_act()).
  * @param size   Rozmiar zegara w pikselach (zegar jest kwadratowy).
  * @return       Wskaźnik do obiektu zegara lub NULL w przypadku błędu.
  */
 sbb_clock_t sbb_clock_create(lv_obj_t *parent, lv_coord_t size);
 
 /**
  * Informuje zegar o synchronizacji czasu (np. z NTP).
  * Po ustawieniu na 'true', zegar wykona płynną animację z godziny 12:00
  * do aktualnego czasu systemowego.
  * @param clock  Uchwyt zegara.
  * @param synced true jeśli czas jest zsynchronizowany.
  */
 void sbb_clock_set_time_synced(sbb_clock_t clock, bool synced);
 
 /**
  * Pobiera aktualne kąty wskazówek (użyteczne do debugowania).
  * @param clock     Uchwyt zegara.
  * @param hour_deg  Kąt wskazówki godzinowej (0-360).
  * @param min_deg   Kąt wskazówki minutowej (0-360).
  * @param sec_deg   Kąt wskazówki sekundowej (0-360, uwzględnia stop-to-go).
  */
 void sbb_clock_get_angles_deg(sbb_clock_t clock, float *hour_deg, float *min_deg, float *sec_deg);
 
 #ifdef __cplusplus
 }
 #endif
 
 #endif /* SBB_CLOCK_H */