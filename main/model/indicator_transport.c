#include "indicator_transport.h"
#include "indicator_wifi.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include <string.h>
#include <time.h>

static const char *TAG = "transport";

// API endpoint
#define TRANSPORT_API_URL "http://transport.opendata.ch/v1/stationboard?station=" STATION_ID "&limit=20"

// Global transport data
static struct transport_data g_transport_data = {0};
static TimerHandle_t transport_timer = NULL;

// HTTP response buffer - Swiss API returns ~150KB!
#define MAX_HTTP_RECV_BUFFER (160 * 1024)  // 160KB
static char *http_recv_buffer = NULL;
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
        case HTTP_EVENT_ON_DATA:
            if (http_recv_len + evt->data_len < MAX_HTTP_RECV_BUFFER) {
                memcpy(http_recv_buffer + http_recv_len, evt->data, evt->data_len);
                http_recv_len += evt->data_len;
                http_recv_buffer[http_recv_len] = 0;
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        default:
            break;
    }
    return ESP_OK;
}

/**
 * @brief Parse departure time and calculate minutes until departure
 */
static int parse_departure_time(const char *time_str, time_t *departure_time)
{
    if (!time_str || !departure_time) return -1;
    
    // Parse ISO 8601 format: "2026-01-19T06:15:00+0100"
    struct tm tm_info = {0};
    int year, month, day, hour, min, sec;
    int tz_hour, tz_min;
    char tz_sign;
    
    // Parse with timezone
    if (sscanf(time_str, "%d-%d-%dT%d:%d:%d%c%02d%02d", 
               &year, &month, &day, &hour, &min, &sec, 
               &tz_sign, &tz_hour, &tz_min) != 9) {
        ESP_LOGW(TAG, "Failed to parse time: %s", time_str);
        return -1;
    }
    
    tm_info.tm_year = year - 1900;
    tm_info.tm_mon = month - 1;
    tm_info.tm_mday = day;
    tm_info.tm_hour = hour;
    tm_info.tm_min = min;
    tm_info.tm_sec = sec;
    tm_info.tm_isdst = -1;
    
    // mktime assumes local time, but we have CET time
    // Just use the time as-is since system should be set to Swiss timezone
    *departure_time = mktime(&tm_info);
    
    // Calculate minutes until departure
    time_t now;
    time(&now);
    int minutes = (int)difftime(*departure_time, now) / 60;
    
    return minutes < 0 ? 0 : minutes;
}

/**
 * @brief Parse JSON response and extract departures
 */
static esp_err_t parse_transport_data(const char *json_str)
{
    if (!json_str) return ESP_FAIL;
    
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return ESP_FAIL;
    }
    
    cJSON *stationboard = cJSON_GetObjectItem(root, "stationboard");
    if (!stationboard || !cJSON_IsArray(stationboard)) {
        ESP_LOGE(TAG, "No stationboard array in response");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    // Reset data
    memset(&g_transport_data, 0, sizeof(g_transport_data));
    int idx = 0;
    
    // Parse each departure
    cJSON *departure = NULL;
    cJSON_ArrayForEach(departure, stationboard) {
        if (idx >= MAX_DEPARTURES) break;
        
        // Get category (should be "B" for bus)
        cJSON *category = cJSON_GetObjectItem(departure, "category");
        if (!category || strcmp(cJSON_GetStringValue(category), "B") != 0) {
            continue;  // Skip non-bus entries
        }
        
        // Get line number
        cJSON *number = cJSON_GetObjectItem(departure, "number");
        if (!number) continue;
        const char *line = cJSON_GetStringValue(number);
        
        // Filter only lines 1 and 4
        if (strcmp(line, "1") != 0 && strcmp(line, "4") != 0) {
            continue;
        }
        
        // Get destination
        cJSON *to = cJSON_GetObjectItem(departure, "to");
        const char *destination = to ? cJSON_GetStringValue(to) : "Unknown";
        
        // Filter destinations - only specific directions
        if (strcmp(line, "4") == 0) {
            // Line 4: only Biberstein direction, not Suhr
            if (strstr(destination, "Biberstein") == NULL) {
                continue;
            }
        } else if (strcmp(line, "1") == 0) {
            // Line 1: only Küttigen direction, not Buchs
            if (strstr(destination, "ttigen") == NULL) {  // Küttigen or Kuttigen
                continue;
            }
        }
        
        // Get departure time
        cJSON *stop = cJSON_GetObjectItem(departure, "stop");
        cJSON *departure_time_json = stop ? cJSON_GetObjectItem(stop, "departure") : NULL;
        const char *time_str = departure_time_json ? cJSON_GetStringValue(departure_time_json) : NULL;
        
        if (!time_str) continue;
        
        // Parse time and calculate minutes
        time_t dep_time;
        int minutes = parse_departure_time(time_str, &dep_time);
        
        if (minutes < 0) continue;
        
        // Store departure info
        strncpy(g_transport_data.departures[idx].line, line, sizeof(g_transport_data.departures[idx].line) - 1);
        strncpy(g_transport_data.departures[idx].destination, destination, sizeof(g_transport_data.departures[idx].destination) - 1);
        g_transport_data.departures[idx].departure_time = dep_time;
        g_transport_data.departures[idx].minutes_until = minutes;
        g_transport_data.departures[idx].valid = true;
        
        ESP_LOGI(TAG, "Line %s to %s in %d min", line, destination, minutes);
        
        idx++;
    }
    
    g_transport_data.count = idx;
    time(&g_transport_data.last_update);
    
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "Parsed %d departures", idx);
    return idx > 0 ? ESP_OK : ESP_FAIL;
}

/**
 * @brief Fetch transport data from API
 */
esp_err_t transport_fetch_data(void)
{
    if (g_transport_data.update_in_progress) {
        ESP_LOGW(TAG, "Update already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Allocate buffer if not already done
    if (http_recv_buffer == NULL) {
        http_recv_buffer = (char*)malloc(MAX_HTTP_RECV_BUFFER);
        if (http_recv_buffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate HTTP buffer");
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGI(TAG, "Allocated %d KB for HTTP buffer", MAX_HTTP_RECV_BUFFER / 1024);
    }
    
    g_transport_data.update_in_progress = true;
    http_recv_len = 0;
    memset(http_recv_buffer, 0, MAX_HTTP_RECV_BUFFER);
    
    esp_http_client_config_t config = {
        .url = TRANSPORT_API_URL,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        g_transport_data.update_in_progress = false;
        return ESP_FAIL;
    }
    
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d",
                status, (int)esp_http_client_get_content_length(client));
        
        if (status == 200 && http_recv_len > 0) {
            err = parse_transport_data(http_recv_buffer);
            
            // Post events to view - both screens use same data
            if (err == ESP_OK) {
                extern esp_event_loop_handle_t view_event_handle;
                
                // Panel 1: Next departures
                struct view_data_transport_next next_data;
                if (transport_get_next_departures(&next_data) == ESP_OK) {
                    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_TRANSPORT_NEXT, 
                                     &next_data, sizeof(next_data), portMAX_DELAY);
                }
                
                // Panel 2: Timetable (uses same data!)
                struct view_data_transport_timetable timetable_data;
                if (transport_get_timetable(&timetable_data) == ESP_OK) {
                    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_TRANSPORT_TIMETABLE, 
                                     &timetable_data, sizeof(timetable_data), portMAX_DELAY);
                }
            }
        } else {
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }
    
    esp_http_client_cleanup(client);
    g_transport_data.update_in_progress = false;
    
    return err;
}

/**
 * @brief Task to fetch data (called from timer)
 */
static void transport_fetch_task(void *arg)
{
    ESP_LOGI(TAG, "Fetch task started");
    transport_fetch_data();
    vTaskDelete(NULL);
}

/**
 * @brief Timer callback - create task to fetch data
 */
static void transport_timer_callback(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG, "Timer triggered - creating fetch task");
    // Create task instead of calling directly to avoid stack overflow
    xTaskCreate(transport_fetch_task, "transport_fetch", 8192, NULL, 5, NULL);
}

/**
 * @brief Get next departures for Panel 1 - ALL departures as list
 */
esp_err_t transport_get_next_departures(struct view_data_transport_next *data)
{
    if (!data) return ESP_ERR_INVALID_ARG;
    
    memset(data, 0, sizeof(*data));
    data->update_time = g_transport_data.last_update;
    
    // Copy ALL valid departures with formatted times
    int idx = 0;
    for (int i = 0; i < g_transport_data.count && idx < MAX_DEPARTURES; i++) {
        if (g_transport_data.departures[i].valid) {
            // Copy line and destination
            strncpy(data->departures[idx].line, g_transport_data.departures[i].line, 
                    sizeof(data->departures[idx].line) - 1);
            strncpy(data->departures[idx].destination, g_transport_data.departures[i].destination, 
                    sizeof(data->departures[idx].destination) - 1);
            
            // Copy minutes
            data->departures[idx].minutes_until = g_transport_data.departures[i].minutes_until;
            
            // Format time string as "HH:MM"
            struct tm timeinfo;
            localtime_r(&g_transport_data.departures[i].departure_time, &timeinfo);
            snprintf(data->departures[idx].time_str, sizeof(data->departures[idx].time_str), 
                     "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
            
            idx++;
        }
    }
    
    data->count = idx;
    ESP_LOGI(TAG, "Panel 1: Returning %d departures", idx);
    return idx > 0 ? ESP_OK : ESP_FAIL;
}

/**
 * @brief Get timetable for Panel 2
 */
esp_err_t transport_get_timetable(struct view_data_transport_timetable *data)
{
    if (!data) return ESP_ERR_INVALID_ARG;
    
    memset(data, 0, sizeof(*data));
    data->update_time = g_transport_data.last_update;
    
    // Copy all valid departures and format time strings
    int idx = 0;
    for (int i = 0; i < g_transport_data.count && idx < MAX_DEPARTURES; i++) {
        if (g_transport_data.departures[i].valid) {
            // Copy line and destination
            strncpy(data->departures[idx].line, g_transport_data.departures[i].line, 
                    sizeof(data->departures[idx].line) - 1);
            strncpy(data->departures[idx].destination, g_transport_data.departures[i].destination, 
                    sizeof(data->departures[idx].destination) - 1);
            
            // Copy minutes
            data->departures[idx].minutes_until = g_transport_data.departures[i].minutes_until;
            
            // Format time string as "HH:MM"
            struct tm timeinfo;
            localtime_r(&g_transport_data.departures[i].departure_time, &timeinfo);
            snprintf(data->departures[idx].time_str, sizeof(data->departures[idx].time_str), 
                     "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
            
            idx++;
        }
    }
    
    data->count = idx;
    return idx > 0 ? ESP_OK : ESP_FAIL;
}

/**
 * @brief Task to do initial fetch after delay
 */
static void transport_initial_fetch_task(void *arg)
{
    ESP_LOGI(TAG, "Waiting 10 seconds for WiFi...");
    vTaskDelay(pdMS_TO_TICKS(10000));  // Wait 10 seconds for WiFi
    
    ESP_LOGI(TAG, "Starting initial transport data fetch");
    transport_fetch_data();
    
    vTaskDelete(NULL);
}

/**
 * @brief Initialize transport module
 */
int indicator_transport_init(void)
{
    ESP_LOGI(TAG, "============ Initializing transport module ============");
    ESP_LOGI(TAG, "Station ID: %s", STATION_ID);
    ESP_LOGI(TAG, "API URL: %s", TRANSPORT_API_URL);
    
    // Create timer for periodic updates (5 minutes to avoid API limit)
    transport_timer = xTimerCreate("transport_timer",
                                   pdMS_TO_TICKS(300000),  // 5 minutes (300 seconds)
                                   pdTRUE,                  // Auto-reload
                                   NULL,
                                   transport_timer_callback);
    
    if (transport_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create timer");
        return ESP_FAIL;
    }
    
    // Start timer
    if (xTimerStart(transport_timer, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start timer");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Timer started successfully");
    
    // Create task for initial fetch (delayed to allow WiFi to connect)
    // Large stack needed for HTTP client + JSON parsing of 160KB response
    xTaskCreate(transport_initial_fetch_task, "transport_init", 8192, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "Transport module initialized successfully");
    return ESP_OK;
}
