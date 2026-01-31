#ifndef INDICATOR_WIFI_H
#define INDICATOR_WIFI_H

#include "config.h"
#include "view_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize WiFi module with multi-network support
 * @return 0 on success, negative on error
 */
int indicator_wifi_init(void);

/**
 * @brief Get current WiFi status (master source of truth)
 * 
 * This is the single source of truth for WiFi status.
 * Other modules (like network_manager) should use this instead of
 * querying ESP-IDF APIs directly.
 * 
 * @param status Output structure to fill with current WiFi status
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if status is NULL
 */
esp_err_t indicator_wifi_get_status(struct view_data_wifi_st *status);

/**
 * @brief Request list of saved WiFi networks
 * Response will be sent via VIEW_EVENT_WIFI_SAVED_LIST event
 * 
 * Usage from UI:
 *   esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, 
 *                    VIEW_EVENT_WIFI_SAVED_LIST_REQ, NULL, 0, portMAX_DELAY);
 */

/**
 * @brief Save a WiFi network to the saved networks list
 * 
 * Usage from UI:
 *   struct view_data_wifi_config cfg = {
 *       .ssid = "MyNetwork",
 *       .password = "MyPassword",
 *       .have_password = true
 *   };
 *   esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, 
 *                    VIEW_EVENT_WIFI_SAVE_NETWORK, &cfg, sizeof(cfg), portMAX_DELAY);
 */

/**
 * @brief Delete a WiFi network from saved list by SSID
 * 
 * Usage from UI:
 *   char ssid[] = "NetworkToDelete";
 *   esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, 
 *                    VIEW_EVENT_WIFI_DELETE_NETWORK, ssid, strlen(ssid) + 1, portMAX_DELAY);
 */

/**
 * @brief Connect to a saved WiFi network by SSID
 * 
 * Usage from UI:
 *   char ssid[] = "SavedNetwork";
 *   esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, 
 *                    VIEW_EVENT_WIFI_CONNECT_SAVED, ssid, strlen(ssid) + 1, portMAX_DELAY);
 */

#ifdef __cplusplus
}
#endif

#endif
