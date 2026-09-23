#ifndef CONNECTIONS_DATA_H
#define CONNECTIONS_DATA_H

#include "esp_err.h"
#include "view_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize connections (journey planner) data module
 * @return ESP_OK on success
 */
esp_err_t connections_data_init(void);

/**
 * @brief Search stations by name (From/To picker)
 * Fires GET /v1/locations and posts VIEW_EVENT_STATION_SEARCH_RESULTS
 * @param query Free-text search query
 */
void connections_data_search_stations(const char *query);

/**
 * @brief Set the "from" station for the next connections fetch
 * @param name Station name
 * @param id Station ID
 */
void connections_data_set_from(const char *name, const char *id);

/**
 * @brief Set the "to" station for the next connections fetch
 * @param name Station name
 * @param id Station ID
 */
void connections_data_set_to(const char *name, const char *id);

/**
 * @brief Fetch connections between the currently set from/to stations
 * Fires GET /v1/connections and posts VIEW_EVENT_CONNECTIONS_UPDATE
 */
void connections_data_fetch(void);

/**
 * @brief Check whether both from and to stations are set
 * @return true once both are set (e.g. to enable the "Search" button)
 */
bool connections_data_ready(void);

#ifdef __cplusplus
}
#endif

#endif // CONNECTIONS_DATA_H
