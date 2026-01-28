#ifndef INDICATOR_TRANSPORT_H
#define INDICATOR_TRANSPORT_H

#include <time.h>
#include "esp_err.h"
#include "view_data.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_DEPARTURES 10
/* Legacy: ten moduł (indicator_transport.c) jest wyłączony z builda – używany jest transport_data.c.
 * Lista przystanków/stacji jest w indicator_view.c: predefined_bus_stops, predefined_stations. */
#define STATION_ID "8590142"  // fallback tylko gdyby ten moduł był w buildzie

/**
 * @brief Single bus departure information
 */
struct transport_departure {
    char line[8];              // Bus line number: "1", "4"
    char destination[64];      // Final destination
    time_t departure_time;     // Unix timestamp
    int minutes_until;         // Minutes until departure
    bool valid;                // Is this entry valid
};

/**
 * @brief Transport data for all departures
 */
struct transport_data {
    struct transport_departure departures[MAX_DEPARTURES];
    int count;                 // Number of valid departures
    time_t last_update;        // Last successful update time
    bool update_in_progress;   // Update currently running
};

/**
 * @brief Initialize transport module
 * @return ESP_OK on success
 */
int indicator_transport_init(void);

/**
 * @brief Fetch transport data from API
 * @return ESP_OK on success
 */
esp_err_t transport_fetch_data(void);

/**
 * @brief Get next departures for Panel 1
 * @param data Output structure
 * @return ESP_OK on success
 */
esp_err_t transport_get_next_departures(struct view_data_transport_next *data);

/**
 * @brief Get timetable for Panel 2
 * @param data Output structure
 * @return ESP_OK on success
 */
esp_err_t transport_get_timetable(struct view_data_transport_timetable *data);

#ifdef __cplusplus
}
#endif

#endif // INDICATOR_TRANSPORT_H
