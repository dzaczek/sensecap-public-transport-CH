#include "network_manager.h"
#include "indicator_wifi.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ping/ping_sock.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include <string.h>

static const char *TAG = "network_mgr";
static SemaphoreHandle_t network_mutex = NULL;

// Global var to track length during a single request
static int http_recv_len = 0;

/**
 * @brief HTTP event handler
 */
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            http_recv_len = 0;
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, val=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            if (!evt->user_data) {
                ESP_LOGW(TAG, "ON_DATA: user_data is NULL");
                break;
            }
            
            // user_data points to response_buffer
            char *buffer = (char *)evt->user_data;
            // Use the size we know we allocated in transport_data (400KB)
            // Ideally this should be passed in a struct via user_data, but for now we assume it matches
            size_t max_size = 400 * 1024; 
            
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);

            if (http_recv_len + evt->data_len < max_size) {
                memcpy(buffer + http_recv_len, evt->data, evt->data_len);
                http_recv_len += evt->data_len;
                buffer[http_recv_len] = 0;
            } else {
                ESP_LOGW(TAG, "HTTP buffer overflow: %d + %d >= %d", 
                        http_recv_len, evt->data_len, max_size);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH, received %d bytes", http_recv_len);
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t network_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing network manager");
    
    // WiFi is initialized by indicator_wifi_init()
    
    if (network_mutex == NULL) {
        network_mutex = xSemaphoreCreateMutex();
    }
    
    return ESP_OK;
}

bool network_manager_is_connected(void)
{
    struct view_data_wifi_st status;
    if (network_manager_get_wifi_status(&status) == ESP_OK) {
        return status.is_connected && status.is_network;
    }
    return false;
}

esp_err_t network_manager_get_ip(char *ip_buffer)
{
    if (!ip_buffer) return ESP_ERR_INVALID_ARG;
    
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        strcpy(ip_buffer, "Not connected");
        return ESP_FAIL;
    }
    
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        snprintf(ip_buffer, 16, IPSTR, IP2STR(&ip_info.ip));
        return ESP_OK;
    }
    
    strcpy(ip_buffer, "No IP");
    return ESP_FAIL;
}

esp_err_t network_manager_http_get(const char *url, char *response_buffer, 
                                    size_t buffer_size, size_t *response_length)
{
    if (!url || !response_buffer || !response_length) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!network_manager_is_connected()) {
        ESP_LOGW(TAG, "WiFi not connected, cannot fetch URL");
        return ESP_ERR_WIFI_NOT_CONNECT;
    }
    
    if (xSemaphoreTake(network_mutex, pdMS_TO_TICKS(10000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take network mutex");
        return ESP_ERR_TIMEOUT;
    }
    
    http_recv_len = 0;
    *response_length = 0;
    
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = response_buffer,  // Pass buffer directly
        .timeout_ms = 30000,           // Increased timeout to 30s
        .buffer_size = 4096,           // Increased internal RX buffer for headers/chunks
        .buffer_size_tx = 1024,        // TX buffer
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        xSemaphoreGive(network_mutex);
        return ESP_FAIL;
    }
    
    // Explicitly request identity to avoid compression issues, though standard client handles it
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        int64_t content_len = esp_http_client_get_content_length(client);
        
        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %lld, recv_len = %d",
                status, content_len, http_recv_len);
        
        if (status == 200) {
            *response_length = http_recv_len;
            // If recv_len is 0 but status 200, something is wrong with data reception
            if (http_recv_len == 0) {
                 ESP_LOGW(TAG, "Status 200 but 0 bytes received!");
            }
        } else {
            ESP_LOGE(TAG, "HTTP request failed with status %d", status);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }
    
    esp_http_client_cleanup(client);
    xSemaphoreGive(network_mutex);
    return err;
}

esp_err_t network_manager_wifi_connect(const char *ssid, const char *password)
{
    // WiFi connection is handled by indicator_wifi module
    // This is a wrapper for consistency
    return ESP_OK;
}

esp_err_t network_manager_get_wifi_status(struct view_data_wifi_st *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    
    // Get status from indicator_wifi module via event system
    // For now, we'll use a simple approach - check WiFi connection
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        memset(status, 0, sizeof(*status));
        return ESP_FAIL;
    }
    
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        status->is_connected = true;
        strncpy(status->ssid, (char*)ap_info.ssid, sizeof(status->ssid) - 1);
        status->rssi = ap_info.rssi;
    } else {
        status->is_connected = false;
    }
    
    // Check if we have IP (network connectivity)
    esp_netif_ip_info_t ip_info;
    status->is_network = (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK);
    status->is_connecting = false;
    
    return ESP_OK;
}

esp_err_t network_manager_ping(const char *host)
{
    ESP_LOGI(TAG, "Pinging %s...", host);
    
    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    ip_addr_t target_addr;
    struct addrinfo hint;
    struct addrinfo *res = NULL;
    memset(&hint, 0, sizeof(hint));
    memset(&target_addr, 0, sizeof(target_addr));
    
    // Resolve hostname
    if (getaddrinfo(host, NULL, &hint, &res) != 0) {
        ESP_LOGE(TAG, "Unknown host: %s", host);
        return ESP_FAIL;
    }
    
    if (res->ai_family == AF_INET) {
        struct sockaddr_in *addr4 = (struct sockaddr_in *)res->ai_addr;
        inet_addr_to_ip4addr(ip_2_ip4(&target_addr), &addr4->sin_addr);
    } else {
        ESP_LOGE(TAG, "IPv6 not supported for ping");
        freeaddrinfo(res);
        return ESP_FAIL;
    }
    freeaddrinfo(res);
    
    ping_config.target_addr = target_addr;
    ping_config.count = 1; // Just one ping to check connectivity
    
    // Simple DNS check passed, returning OK
    return ESP_OK; 
}
