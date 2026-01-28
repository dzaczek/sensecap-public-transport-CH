#ifndef TRANSPORT_DATA_H
#define TRANSPORT_DATA_H

#include "esp_err.h"
#include "view_data.h"
#include "freertos/timers.h"
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// =================================================================================
// USER CONFIGURATION
// =================================================================================

// 1. Configure your Bus Stop
// Find your station ID at: http://transport.opendata.ch/examples/stationboard.html
// Or query: http://transport.opendata.ch/v1/locations?query=YOUR_CITY_NAME
#ifndef BUS_STOP_NAME
#define BUS_STOP_NAME "Zurich, Bahnhofplatz" // Example Name
#endif

#ifndef BUS_STOP_ID
#define BUS_STOP_ID "8503000" // Example ID for Zurich HB
#endif

// Second bus stop option (Optional)
#ifndef BUS_STOP_NAME_2
#define BUS_STOP_NAME_2 "Bern, Bahnhof"
#endif

#ifndef BUS_STOP_ID_2
#define BUS_STOP_ID_2 "8507000"
#endif

// 2. Configure your Train Station
#ifndef TRAIN_STATION_NAME
#define TRAIN_STATION_NAME "Zurich HB"
#endif

#ifndef TRAIN_STATION_ID
#define TRAIN_STATION_ID "8503000"
#endif

// 3. Filter Bus Lines
// Selected bus lines to display (comma-separated, e.g., "1,4,12")
// Leave empty or comment out logic in transport_data.c to show all
#ifndef SELECTED_BUS_LINES
#define SELECTED_BUS_LINES "31,32"
#endif

// 4. Refresh Intervals
// Refresh intervals (in minutes)
#define DAY_REFRESH_INTERVAL_MINUTES 5
#define NIGHT_REFRESH_INTERVAL_MINUTES 15

// Day mode hours (6:00 - 21:59)
#define DAY_START_HOUR 6
#define DAY_END_HOUR 21

// =================================================================================
// END CONFIGURATION
// =================================================================================

/**
 * @brief Set train station manually
 * @param name Station name
 * @param id Station ID
 */
void transport_data_set_train_station(const char *name, const char *id);

/**
 * @brief Set bus stop manually
 * @param name Stop name
 * @param id Stop ID
 */
void transport_data_set_bus_stop(const char *name, const char *id);

/**
 * @brief Initialize transport data module
 * @return ESP_OK on success
 */
esp_err_t transport_data_init(void);

/**
 * @brief Fetch bus data from API
 * @return ESP_OK on success
 */
esp_err_t transport_data_fetch_bus(void);

/**
 * @brief Fetch train data from API
 * @return ESP_OK on success
 */
esp_err_t transport_data_fetch_train(void);

/**
 * @brief Get bus countdown data for Screen A
 * @param data Output structure
 * @return ESP_OK on success
 */
esp_err_t transport_data_get_bus_countdown(struct view_data_bus_countdown *data);

/**
 * @brief Get train station data for Screen B
 * @param data Output structure
 * @return ESP_OK on success
 */
esp_err_t transport_data_get_train_station(struct view_data_train_station *data);

/**
 * @brief Check if current time is in day mode
 * @return true if day mode, false if night mode
 */
bool transport_data_is_day_mode(void);

/**
 * @brief Get current refresh interval based on time of day
 * @return Refresh interval in minutes
 */
int transport_data_get_refresh_interval(void);

/**
 * @brief Force immediate refresh (manual refresh)
 * @return ESP_OK on success
 */
esp_err_t transport_data_force_refresh(void);

/**
 * @brief Force refresh only bus data
 * @return ESP_OK on success
 */
esp_err_t transport_data_refresh_bus(void);

/**
 * @brief Force refresh only train data
 * @return ESP_OK on success
 */
esp_err_t transport_data_refresh_train(void);

/**
 * @brief Check if data is stale and needs refresh
 * @return true if refresh needed
 */
bool transport_data_needs_refresh(void);

/**
 * @brief Get refresh timer handle (for external control)
 * @return Timer handle or NULL
 */
TimerHandle_t transport_data_get_refresh_timer(void);

/**
 * @brief Get estimated time offset (Real Time - System Time)
 * @return Offset in seconds
 */
time_t transport_data_get_time_offset(void);

/**
 * @brief Notify transport data module about active screen change
 * @param screen_index 0=Bus, 1=Train, 2=Settings
 */
void transport_data_notify_screen_change(int screen_index);

#ifdef __cplusplus
}
#endif

#endif // TRANSPORT_DATA_H
