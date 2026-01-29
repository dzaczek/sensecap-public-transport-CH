#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include "esp_err.h"
#include "view_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize network manager (WiFi + HTTP client)
 * @return ESP_OK on success
 */
esp_err_t network_manager_init(void);

/**
 * @brief Check if WiFi is connected
 * @return true if connected, false otherwise
 */
bool network_manager_is_connected(void);

/**
 * @brief Get current IP address
 * @param ip_buffer Buffer to store IP address string (must be at least 16 bytes)
 * @return ESP_OK on success
 */
esp_err_t network_manager_get_ip(char *ip_buffer);

/**
 * @brief Perform HTTP GET request
 * @param url URL to fetch
 * @param response_buffer Buffer to store response
 * @param buffer_size Size of buffer
 * @param response_length Output: actual response length
 * @return ESP_OK on success
 */
esp_err_t network_manager_http_get(const char *url, char *response_buffer, 
                                    size_t buffer_size, size_t *response_length);

/**
 * @brief Connect to WiFi
 * @param ssid WiFi SSID
 * @param password WiFi password (can be NULL for open networks)
 * @return ESP_OK on success
 */
esp_err_t network_manager_wifi_connect(const char *ssid, const char *password);

/**
 * @brief Get WiFi status
 * @param status Output structure
 * @return ESP_OK on success
 */
esp_err_t network_manager_get_wifi_status(struct view_data_wifi_st *status);

/**
 * @brief Initialize ping capabilities
 */
esp_err_t network_manager_ping_init(void);

/**
 * @brief Ping a host to check connectivity
 * @param host Hostname or IP string
 * @return ESP_OK if reachable, ESP_FAIL otherwise
 */
esp_err_t network_manager_ping(const char *host);

/**
 * @brief Get full network info (IP, gateway, netmask, DNS, SSID, RSSI)
 * @param info Output structure to fill
 * @return ESP_OK on success
 */
esp_err_t network_manager_get_network_info(struct view_data_network_info *info);

#ifdef __cplusplus
}
#endif

#endif // NETWORK_MANAGER_H
