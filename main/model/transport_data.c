#include "transport_data.h"
#include "network_manager.h"
#include "indicator_display.h"  // For display state check
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include <string.h>
#include <time.h>
#include <stdlib.h>

static const char *TAG = "transport_data";

// API endpoint base URL
#define TRANSPORT_API_BASE "http://transport.opendata.ch/v1"

// Global data storage
static struct view_data_bus_countdown g_bus_data = {0};
static struct view_data_train_station g_train_data = {0};
static struct view_data_train_details g_train_details = {0};
static struct view_data_bus_details g_bus_details = {0};

// Time synchronization offset (Real Time - System Time)
static time_t g_time_offset = 0;

// Refresh state
static time_t g_last_bus_refresh = 0;
static time_t g_last_train_refresh = 0;
static bool g_bus_refresh_in_progress = false;
static bool g_train_refresh_in_progress = false;
static bool g_details_refresh_in_progress = false;
static bool g_force_refresh = false;

// Selection state
static bool g_bus_stop_selected = false;
static bool g_train_station_selected = false;

// Active screen state (0=Bus, 1=Train, 2=Settings)
static int g_active_screen = 0;

// Train station config (mutable)
static char g_train_station_name[64] = TRAIN_STATION_NAME;
static char g_train_station_id[32] = TRAIN_STATION_ID;

// Bus stop config (mutable)
static char g_bus_stop_name[64] = BUS_STOP_NAME;
static char g_bus_stop_id[32] = BUS_STOP_ID;

void transport_data_notify_screen_change(int screen_index)
{
    g_active_screen = screen_index;
    ESP_LOGI(TAG, "Screen changed to: %d", screen_index);
    
    // Optional: Trigger immediate refresh if data is stale when switching screens
    // But basic requirement is just to fetch for active screen during timed refresh
}

void transport_data_set_bus_stop(const char *name, const char *id)
{
    if (name) strncpy(g_bus_stop_name, name, sizeof(g_bus_stop_name) - 1);
    if (id) strncpy(g_bus_stop_id, id, sizeof(g_bus_stop_id) - 1);
    
    g_bus_stop_selected = true; // Mark as selected

    // Invalidate current data
    g_bus_data.count = 0;
    strncpy(g_bus_data.stop_name, g_bus_stop_name, sizeof(g_bus_data.stop_name) - 1);
    g_bus_data.direction_count = 0;
    
    // Trigger refresh immediately
    transport_data_refresh_bus();
}

void transport_data_set_train_station(const char *name, const char *id)
{
    if (name) strncpy(g_train_station_name, name, sizeof(g_train_station_name) - 1);
    if (id) strncpy(g_train_station_id, id, sizeof(g_train_station_id) - 1);
    
    g_train_station_selected = true; // Mark as selected

    // Invalidate current data
    g_train_data.count = 0;
    strncpy(g_train_data.station_name, g_train_station_name, sizeof(g_train_data.station_name) - 1);
    
    // Trigger refresh immediately
    transport_data_refresh_train();
}

// Refresh configuration
static struct view_data_refresh_config g_refresh_config = {
    .day_refresh_minutes = DAY_REFRESH_INTERVAL_MINUTES,
    .night_refresh_minutes = NIGHT_REFRESH_INTERVAL_MINUTES,
    .day_start_hour = DAY_START_HOUR,
    .day_end_hour = DAY_END_HOUR,
};

// Timer handles
TimerHandle_t g_refresh_timer = NULL;  // Exposed for main.c
TimerHandle_t g_display_schedule_timer = NULL;

/**
 * @brief Check display schedule and keep screen on during active hours
 * Active: Mon-Fri, 06:15 - 07:15
 */
static void display_schedule_timer_cb(TimerHandle_t xTimer)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    // Check day of week (0=Sun, 1=Mon, ..., 5=Fri, 6=Sat)
    // We want Mon(1) to Fri(5)
    if (timeinfo.tm_wday >= 1 && timeinfo.tm_wday <= 5) {
        // Check time: 06:15 to 07:15
        // 06:15 to 06:59
        bool morning_slot = (timeinfo.tm_hour == 6 && timeinfo.tm_min >= 15);
        // 07:00 to 07:15
        bool end_slot = (timeinfo.tm_hour == 7 && timeinfo.tm_min <= 15);
        
        if (morning_slot || end_slot) {
            // Keep display on!
            // We assume indicator_display_on() is idempotent or cheap
            // And we reset the sleep timer
            indicator_display_on();
            indicator_display_sleep_restart();
            // ESP_LOGD(TAG, "Keeping display ON by schedule");
        }
    }
}

/**
 * @brief Parse ISO 8601 time string and calculate minutes until departure
 */
static int parse_departure_time(const char *time_str, time_t *departure_time)
{
    if (!time_str || !departure_time) return -1;
    
    // Parse ISO 8601 format: "2026-01-19T06:15:00+0100"
    struct tm tm_info = {0};
    int year, month, day, hour, min, sec;
    int tz_hour, tz_min;
    char tz_sign;
    
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
    
    *departure_time = mktime(&tm_info);
    
    time_t now;
    time(&now);
    
    // Check if system time is valid (e.g., > 2025-01-01)
    if (now < 1735689600) {
        ESP_LOGW(TAG, "System time invalid (now=%ld), cannot calculate minutes", (long)now);
        return -1; // Indicate invalid time
    }
    
    double diff = difftime(*departure_time, now);
    int minutes = (int)(diff / 60);
    
    // Debug log for first few calculations
    static int log_count = 0;
    if (log_count++ < 5) {
        ESP_LOGI(TAG, "Time calc: Dep=%ld, Now=%ld, Diff=%.0f, Min=%d", 
                 (long)*departure_time, (long)now, diff, minutes);
    }
    
    return minutes; // Allow negative minutes for "just left" or delay calc
}

/**
 * @brief Check if bus line is in selected list
 */
static bool is_selected_bus_line(const char *line)
{
    const char *selected = SELECTED_BUS_LINES;
    if (!selected || strlen(selected) == 0 || strcmp(selected, "*") == 0) return true;

    char line_copy[64];
    strncpy(line_copy, selected, sizeof(line_copy) - 1);
    
    char *token = strtok(line_copy, ",");
    while (token != NULL) {
        // Trim whitespace
        while (*token == ' ') token++;
        if (strcmp(token, line) == 0) {
            return true;
        }
        token = strtok(NULL, ",");
    }
    return false;
}

/**
 * @brief Helper to get or add direction index
 */
static int get_or_add_direction(const char *name, const char *id, char ids_store[][32])
{
    // Try to find by ID first if available
    if (id && id[0]) {
        for (int i = 0; i < g_bus_data.direction_count; i++) {
            if (strcmp(ids_store[i], id) == 0) {
                return i;
            }
        }
    } 
    // Fallback: match by name if ID missing (legacy/fallback case)
    else if (name && name[0]) {
        for (int i = 0; i < g_bus_data.direction_count; i++) {
            if (strcmp(g_bus_data.directions[i], name) == 0) {
                return i;
            }
        }
    }
    
    // Add new if space available
    if (g_bus_data.direction_count < MAX_DIRECTIONS) {
        strncpy(g_bus_data.directions[g_bus_data.direction_count], name ? name : "Unknown", sizeof(g_bus_data.directions[0]) - 1);
        
        if (id && id[0]) {
            strncpy(ids_store[g_bus_data.direction_count], id, 31);
            ids_store[g_bus_data.direction_count][31] = '\0';
        } else {
            ids_store[g_bus_data.direction_count][0] = '\0';
        }
        return g_bus_data.direction_count++;
    }
    
    return 0; // Fallback to first
}

/**
 * @brief Compare function for qsort
 */
static int compare_bus_departures(const void *a, const void *b)
{
    const struct bus_departure_view *dep_a = (const struct bus_departure_view *)a;
    const struct bus_departure_view *dep_b = (const struct bus_departure_view *)b;
    
    // Valid departures come first
    if (dep_a->valid && !dep_b->valid) return -1;
    if (!dep_a->valid && dep_b->valid) return 1;
    if (!dep_a->valid && !dep_b->valid) return 0;
    
    // Sort by timestamp
    if (dep_a->departure_timestamp < dep_b->departure_timestamp) return -1;
    if (dep_a->departure_timestamp > dep_b->departure_timestamp) return 1;
    
    return 0;
}

/**
 * @brief Parse bus stationboard JSON
 */
static esp_err_t parse_bus_json(const char *json_str)
{
    if (!json_str) return ESP_FAIL;
    
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            ESP_LOGE(TAG, "JSON parse error before: %s", error_ptr);
        }
        ESP_LOGE(TAG, "Failed to parse JSON. First 200 chars: %.200s", json_str ? json_str : "NULL");
        return ESP_FAIL;
    }
    
    cJSON *stationboard = cJSON_GetObjectItem(root, "stationboard");
    if (!stationboard || !cJSON_IsArray(stationboard)) {
        ESP_LOGE(TAG, "No stationboard array in response");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    // Reset data
    memset(&g_bus_data, 0, sizeof(g_bus_data));
    strncpy(g_bus_data.stop_name, BUS_STOP_NAME, sizeof(g_bus_data.stop_name) - 1);
    g_bus_data.direction_count = 0;
    int idx = 0;
    
    // Define storage for direction IDs locally for this parse session
    char direction_ids[MAX_DIRECTIONS][32];
    memset(direction_ids, 0, sizeof(direction_ids));
    
    cJSON *departure = NULL;
    cJSON_ArrayForEach(departure, stationboard) {
        if (idx >= MAX_DEPARTURES) break;
        
        // Filter by category (B = Bus, T = Tram)
        cJSON *category = cJSON_GetObjectItem(departure, "category");
        const char *cat_str = category ? cJSON_GetStringValue(category) : "";
        
        // Get line number
        cJSON *number = cJSON_GetObjectItem(departure, "number");
        const char *line = number ? cJSON_GetStringValue(number) : "Unknown";

        // Get destination for logging
        cJSON *to = cJSON_GetObjectItem(departure, "to");
        const char *destination = to ? cJSON_GetStringValue(to) : "Unknown";

        ESP_LOGI(TAG, "Seen departure: Cat='%s', Line='%s', To='%s'", cat_str, line, destination);

        if (!category || (strcmp(cat_str, "B") != 0 && strcmp(cat_str, "T") != 0)) {
            continue;
        }
        
        if (!number) continue;
        
        // Filter selected lines only
        if (!is_selected_bus_line(line)) {
            ESP_LOGW(TAG, "Skipping line '%s' (not in selected list: %s)", line, SELECTED_BUS_LINES);
            continue;
        }
        
        // Determine direction based on passList (find first valid next stop)
        char direction_name[64] = "Unknown";
        char direction_id[32] = ""; 
        bool direction_found = false;
        
        cJSON *passList = cJSON_GetObjectItem(departure, "passList");
        if (passList && cJSON_IsArray(passList)) {
            int list_size = cJSON_GetArraySize(passList);
            
            // Use passList[1] as requested for direction grouping
            // We assume passList[0] is current station and passList[1] is the next stop (direction)
            if (list_size > 1) {
                 cJSON *next_item = cJSON_GetArrayItem(passList, 1);
                 cJSON *next_station = cJSON_GetObjectItem(next_item, "station");
                 cJSON *next_name = next_station ? cJSON_GetObjectItem(next_station, "name") : NULL;
                 cJSON *next_id = next_station ? cJSON_GetObjectItem(next_station, "id") : NULL;
                 
                 if (next_name && cJSON_GetStringValue(next_name)) {
                     snprintf(direction_name, sizeof(direction_name), "Direction: %s", cJSON_GetStringValue(next_name));
                     if (next_id && cJSON_GetStringValue(next_id)) {
                         strncpy(direction_id, cJSON_GetStringValue(next_id), sizeof(direction_id) - 1);
                     }
                     direction_found = true;
                 }
            }
        }
        
        // Fallback: If "next stop" is not found in the intermediate list
        if (!direction_found) {
             // Instead of guessing the city, we simply use the final station (destination).
             // This works in every city in the world.
             // If destination is "Warszawa Centralna", direction becomes "Direction: Warszawa Centralna"
             snprintf(direction_name, sizeof(direction_name), "Direction: %s", destination);
             
             // Optional: If the name is very long, truncate it (simple UI protection)
             // if (strlen(direction_name) > 20) direction_name[20] = '\0';
        }

        // Get or add direction index
        int dir_idx = get_or_add_direction(direction_name, direction_id, direction_ids);
        g_bus_data.departures[idx].direction_index = dir_idx; // Assign the direction index!

        // Get departure time
        cJSON *stop = cJSON_GetObjectItem(departure, "stop");
        cJSON *departure_time_json = stop ? cJSON_GetObjectItem(stop, "departure") : NULL;
        const char *time_str = departure_time_json ? cJSON_GetStringValue(departure_time_json) : NULL;
        
        // Get journey name (unique ID)
        cJSON *name_json = cJSON_GetObjectItem(departure, "name");
        const char *journey_name = name_json ? cJSON_GetStringValue(name_json) : "";

        if (!time_str) continue;
        
        time_t dep_time;
        int minutes = parse_departure_time(time_str, &dep_time);
        if (minutes < 0) continue;
        
        // Store departure
        strncpy(g_bus_data.departures[idx].line, line, sizeof(g_bus_data.departures[idx].line) - 1);
        strncpy(g_bus_data.departures[idx].destination, destination, 
                sizeof(g_bus_data.departures[idx].destination) - 1);
        strncpy(g_bus_data.departures[idx].journey_name, journey_name,
                sizeof(g_bus_data.departures[idx].journey_name) - 1);
        
        struct tm timeinfo;
        localtime_r(&dep_time, &timeinfo);
        snprintf(g_bus_data.departures[idx].time_str, sizeof(g_bus_data.departures[idx].time_str),
                 "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        
        g_bus_data.departures[idx].departure_timestamp = dep_time;
        g_bus_data.departures[idx].minutes_until = minutes;
        g_bus_data.departures[idx].valid = true;
        
        idx++;
    }
    
    // Sort departures by time
    if (idx > 1) {
        qsort(g_bus_data.departures, idx, sizeof(struct bus_departure_view), compare_bus_departures);
    }
    
    // Debug logging for direction grouping
    int dir_counts[MAX_DIRECTIONS] = {0};
    for (int i = 0; i < idx; i++) {
        int dir = g_bus_data.departures[i].direction_index;
        if (dir >= 0 && dir < MAX_DIRECTIONS) {
            dir_counts[dir]++;
        }
    }
    
    ESP_LOGI(TAG, "Parsed %d bus departures. Directions found: %d", idx, g_bus_data.direction_count);
    for (int i = 0; i < g_bus_data.direction_count; i++) {
        ESP_LOGI(TAG, "  Dir %d (%s): %d departures", i, g_bus_data.directions[i], dir_counts[i]);
    }
    
    g_bus_data.count = idx;
    time(&g_bus_data.update_time);
    g_bus_data.api_error = false;
    
    cJSON_Delete(root);
    ESP_LOGI(TAG, "Parsed %d bus departures", idx);
    return idx > 0 ? ESP_OK : ESP_FAIL;
}

/**
 * @brief Parse train stationboard JSON
 */
static esp_err_t parse_train_json(const char *json_str)
{
    if (!json_str) return ESP_FAIL;
    
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            ESP_LOGE(TAG, "JSON parse error before: %s", error_ptr);
        }
        ESP_LOGE(TAG, "Failed to parse JSON. First 200 chars: %.200s", json_str ? json_str : "NULL");
        return ESP_FAIL;
    }
    
    cJSON *stationboard = cJSON_GetObjectItem(root, "stationboard");
    if (!stationboard || !cJSON_IsArray(stationboard)) {
        ESP_LOGE(TAG, "No stationboard array in response");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    // Reset data
    memset(&g_train_data, 0, sizeof(g_train_data));
    strncpy(g_train_data.station_name, TRAIN_STATION_NAME, sizeof(g_train_data.station_name) - 1);
    int idx = 0;
    
    cJSON *departure = NULL;
    cJSON_ArrayForEach(departure, stationboard) {
        if (idx >= MAX_DEPARTURES) break;
        
        // Get category (IC, IR, RE, S, etc.)
        cJSON *category = cJSON_GetObjectItem(departure, "category");
        const char *category_str = category ? cJSON_GetStringValue(category) : "";
        
        // Get line number (e.g., "S12", "IC3")
        cJSON *number = cJSON_GetObjectItem(departure, "number");
        const char *number_str = number ? cJSON_GetStringValue(number) : "";
        
        // Combine category and number (e.g. S + 14 = S14)
        char line[16];
        if (category_str && number_str) {
            snprintf(line, sizeof(line), "%s%s", category_str, number_str);
        } else if (category_str) {
            strncpy(line, category_str, sizeof(line) - 1);
        } else {
            strncpy(line, number_str, sizeof(line) - 1);
        }
        
        // Get destination
        cJSON *to = cJSON_GetObjectItem(departure, "to");
        const char *destination = (to && cJSON_GetStringValue(to)) ? cJSON_GetStringValue(to) : "Unknown";

        // Get intermediate stations (via)
        char via_str[128] = "";
        cJSON *passList = cJSON_GetObjectItem(departure, "passList");
        if (passList && cJSON_IsArray(passList)) {
            int list_size = cJSON_GetArraySize(passList);
            int added_count = 0;
            for (int i = 0; i < list_size && added_count < 3; i++) {
                cJSON *item = cJSON_GetArrayItem(passList, i);
                cJSON *station = cJSON_GetObjectItem(item, "station");
                cJSON *name = station ? cJSON_GetObjectItem(station, "name") : NULL;
                cJSON *id = station ? cJSON_GetObjectItem(station, "id") : NULL;
                
                // Skip current station
                if (id && cJSON_GetStringValue(id) && strcmp(cJSON_GetStringValue(id), TRAIN_STATION_ID) == 0) continue;
                if (name && cJSON_GetStringValue(name) && strstr(cJSON_GetStringValue(name), TRAIN_STATION_NAME) != NULL) continue;
                
                // Skip final destination (already shown)
                if (name && cJSON_GetStringValue(name) && strcmp(cJSON_GetStringValue(name), destination) == 0) continue;

                if (name && cJSON_GetStringValue(name)) {
                    if (added_count > 0) strcat(via_str, " - ");
                    
                    // Simple logic to shorten names if needed, or just take first 2-3 stops
                    strncat(via_str, cJSON_GetStringValue(name), sizeof(via_str) - strlen(via_str) - 1);
                    added_count++;
                }
            }
        }
        
        // Get departure time
        cJSON *stop = cJSON_GetObjectItem(departure, "stop");
        cJSON *departure_time_json = stop ? cJSON_GetObjectItem(stop, "departure") : NULL;
        const char *time_str = departure_time_json ? cJSON_GetStringValue(departure_time_json) : NULL;

        // Get journey name (unique ID)
        cJSON *name_json = cJSON_GetObjectItem(departure, "name");
        const char *journey_name = name_json ? cJSON_GetStringValue(name_json) : "";

        // Get platform
        cJSON *platform_json = stop ? cJSON_GetObjectItem(stop, "platform") : NULL;
        const char *platform = platform_json ? cJSON_GetStringValue(platform_json) : "";
        
        if (!time_str) continue;
        
        time_t dep_time;
        int minutes = parse_departure_time(time_str, &dep_time);
        
        // If minutes < -10, assume train has left long ago or error
        if (minutes < -10) continue;
        
        // Get delay (in seconds, convert to minutes)
        cJSON *delay = cJSON_GetObjectItem(stop, "delay");
        int delay_seconds = delay ? (int)cJSON_GetNumberValue(delay) : 0;
        int delay_minutes = delay_seconds / 60;  // Convert to minutes
        
        // Store departure
        strncpy(g_train_data.departures[idx].line, line, sizeof(g_train_data.departures[idx].line) - 1);
        strncpy(g_train_data.departures[idx].destination, destination,
                sizeof(g_train_data.departures[idx].destination) - 1);
        strncpy(g_train_data.departures[idx].via, via_str,
                sizeof(g_train_data.departures[idx].via) - 1);
        strncpy(g_train_data.departures[idx].journey_name, journey_name,
                sizeof(g_train_data.departures[idx].journey_name) - 1);
        strncpy(g_train_data.departures[idx].platform, platform,
                sizeof(g_train_data.departures[idx].platform) - 1);
        
        struct tm timeinfo;
        localtime_r(&dep_time, &timeinfo);
        snprintf(g_train_data.departures[idx].time_str, sizeof(g_train_data.departures[idx].time_str),
                 "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        
        g_train_data.departures[idx].departure_timestamp = dep_time;
        g_train_data.departures[idx].delay_minutes = delay_minutes;
        g_train_data.departures[idx].minutes_until = minutes;
        g_train_data.departures[idx].valid = true;
        
        idx++;
    }
    
    g_train_data.count = idx;
    time(&g_train_data.update_time);
    g_train_data.api_error = false;
    
    // Update time offset based on first departure if available
    if (idx > 0 && g_train_data.departures[0].valid) {
        time_t sys_now;
        time(&sys_now);
        // Assuming first departure is "now" or close to it. 
        // Ideally we'd use 'timestamp' from API header but we don't have it.
        // We use the first departure time as a reference point.
        // If system time is 23:59 and departure is 07:56, offset is ~+8h.
        // Only update if difference is significant (> 5 mins) to avoid jitter
        double diff = difftime(g_train_data.departures[0].departure_timestamp, sys_now);
        if (abs(diff) > 300) {
            g_time_offset = (time_t)diff;
            ESP_LOGW(TAG, "System time out of sync. Applied offset: %ld seconds", (long)g_time_offset);
        } else {
            g_time_offset = 0;
        }
    }
    
    cJSON_Delete(root);
    ESP_LOGI(TAG, "Parsed %d train departures", idx);
    return idx > 0 ? ESP_OK : ESP_FAIL;
}

/**
 * @brief Parse journey details JSON (from connections endpoint)
 */
static esp_err_t parse_journey_json(const char *json_str)
{
    if (!json_str) return ESP_FAIL;
    
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse details JSON");
        return ESP_FAIL;
    }
    
    // Clear data first
    memset(&g_train_details, 0, sizeof(g_train_details));
    
    // Check connections array
    cJSON *connections = cJSON_GetObjectItem(root, "connections");
    if (!connections || !cJSON_IsArray(connections)) {
        ESP_LOGE(TAG, "No connections array in response");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    // Get first connection
    cJSON *connection = cJSON_GetArrayItem(connections, 0);
    if (!connection) {
        ESP_LOGE(TAG, "No connection found");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    // Get sections (we want the journey section)
    cJSON *sections = cJSON_GetObjectItem(connection, "sections");
    if (!sections || !cJSON_IsArray(sections)) {
        ESP_LOGE(TAG, "No sections in connection");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    // Find the first section with a journey
    cJSON *journey = NULL;
    cJSON *section = NULL;
    cJSON_ArrayForEach(section, sections) {
        journey = cJSON_GetObjectItem(section, "journey");
        if (journey) break;
    }
    
    if (!journey) {
        ESP_LOGE(TAG, "No journey found in sections");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    // Name
    cJSON *name = cJSON_GetObjectItem(journey, "name");
    if (name) strncpy(g_train_details.name, cJSON_GetStringValue(name), sizeof(g_train_details.name)-1);
    
    // Operator
    cJSON *operator = cJSON_GetObjectItem(journey, "operator");
    if (operator) strncpy(g_train_details.operator, cJSON_GetStringValue(operator), sizeof(g_train_details.operator)-1);
    
    // Stops (passList)
    cJSON *passList = cJSON_GetObjectItem(journey, "passList");
    if (passList && cJSON_IsArray(passList)) {
        int idx = 0;
        cJSON *stop = NULL;
        cJSON_ArrayForEach(stop, passList) {
            if (idx >= 30) break;
            
            // Station name
            cJSON *station = cJSON_GetObjectItem(stop, "station");
            cJSON *st_name = station ? cJSON_GetObjectItem(station, "name") : NULL;
            if (st_name) strncpy(g_train_details.stops[idx].name, cJSON_GetStringValue(st_name), sizeof(g_train_details.stops[idx].name)-1);
            
            // Arrival
            cJSON *arr = cJSON_GetObjectItem(stop, "arrival");
            if (arr && cJSON_GetStringValue(arr)) {
                time_t t;
                parse_departure_time(cJSON_GetStringValue(arr), &t);
                struct tm tm;
                localtime_r(&t, &tm);
                snprintf(g_train_details.stops[idx].arrival, 16, "%02d:%02d", tm.tm_hour, tm.tm_min);
            }
            
            // Departure
            cJSON *dep = cJSON_GetObjectItem(stop, "departure");
            if (dep && cJSON_GetStringValue(dep)) {
                time_t t;
                parse_departure_time(cJSON_GetStringValue(dep), &t);
                struct tm tm;
                localtime_r(&t, &tm);
                snprintf(g_train_details.stops[idx].departure, 16, "%02d:%02d", tm.tm_hour, tm.tm_min);
            }
            
            // Delay
            cJSON *delay = cJSON_GetObjectItem(stop, "delay");
            if (delay && cJSON_IsNumber(delay)) {
                g_train_details.stops[idx].delay = (int)cJSON_GetNumberValue(delay);
            }
            
            idx++;
        }
        g_train_details.stop_count = idx;
    }
    
    // 1. Try to get capacity from the main connection object
    cJSON *cap1 = cJSON_GetObjectItem(connection, "capacity1st");
    cJSON *cap2 = cJSON_GetObjectItem(connection, "capacity2nd");
    
    if (cap1) {
        int c = 0;
        if (cJSON_IsNumber(cap1)) c = (int)cJSON_GetNumberValue(cap1);
        else if (cJSON_IsString(cap1)) c = atoi(cJSON_GetStringValue(cap1));
        
        if (c==1) strcpy(g_train_details.capacity_1st, "Low");
        else if (c==2) strcpy(g_train_details.capacity_1st, "Med");
        else if (c==3) strcpy(g_train_details.capacity_1st, "High");
    }
    
    if (cap2) {
        int c = 0;
        if (cJSON_IsNumber(cap2)) c = (int)cJSON_GetNumberValue(cap2);
        else if (cJSON_IsString(cap2)) c = atoi(cJSON_GetStringValue(cap2));
        
        if (c==1) strcpy(g_train_details.capacity_2nd, "Low");
        else if (c==2) strcpy(g_train_details.capacity_2nd, "Med");
        else if (c==3) strcpy(g_train_details.capacity_2nd, "High");
    }

    // 2. If not found, check stops in passList (fallback)
    if ((!g_train_details.capacity_1st[0] || !g_train_details.capacity_2nd[0]) && 
        passList && cJSON_IsArray(passList)) {
        cJSON *stop = NULL;
        cJSON_ArrayForEach(stop, passList) {
             cJSON *prognosis = cJSON_GetObjectItem(stop, "prognosis");
             cJSON *cap1 = prognosis ? cJSON_GetObjectItem(prognosis, "capacity1st") : cJSON_GetObjectItem(stop, "capacity1st");
             cJSON *cap2 = prognosis ? cJSON_GetObjectItem(prognosis, "capacity2nd") : cJSON_GetObjectItem(stop, "capacity2nd");
             
             if (cap1) {
                 int c = 0;
                 if (cJSON_IsNumber(cap1)) c = (int)cJSON_GetNumberValue(cap1);
                 else if (cJSON_IsString(cap1)) c = atoi(cJSON_GetStringValue(cap1));
                 
                 if (c==1) strcpy(g_train_details.capacity_1st, "Low");
                 else if (c==2) strcpy(g_train_details.capacity_1st, "Medium");
                 else if (c==3) strcpy(g_train_details.capacity_1st, "High");
             }
             
             if (cap2) {
                 int c = 0;
                 if (cJSON_IsNumber(cap2)) c = (int)cJSON_GetNumberValue(cap2);
                 else if (cJSON_IsString(cap2)) c = atoi(cJSON_GetStringValue(cap2));
                 
                 if (c==1) strcpy(g_train_details.capacity_2nd, "Low");
                 else if (c==2) strcpy(g_train_details.capacity_2nd, "Medium");
                 else if (c==3) strcpy(g_train_details.capacity_2nd, "High");
             }
             
             if (g_train_details.capacity_1st[0] || g_train_details.capacity_2nd[0]) break; 
        }
    }
    
    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * @brief Simple URL encoder
 */
static void url_encode(const char *src, char *dst, size_t dst_len) {
    static const char *hex = "0123456789ABCDEF";
    size_t pos = 0;
    while (*src && pos < dst_len - 1) {
        if ((*src >= 'a' && *src <= 'z') ||
            (*src >= 'A' && *src <= 'Z') ||
            (*src >= '0' && *src <= '9') ||
            *src == '-' || *src == '_' || *src == '.' || *src == '~') {
            dst[pos++] = *src;
        } else {
            if (pos + 3 >= dst_len) break;
            dst[pos++] = '%';
            dst[pos++] = hex[(*src >> 4) & 0x0F];
            dst[pos++] = hex[*src & 0x0F];
        }
        src++;
    }
    dst[pos] = '\0';
}

/**
 * @brief Task to fetch journey details using connections search
 */
static void fetch_details_task(void *arg)
{
    char *journey_name = (char *)arg;
    ESP_LOGI(TAG, "Fetching details for: %s", journey_name);
    
    // Find the train in our current list to get 'to' and 'departure' time
    char destination[64] = {0};
    time_t departure_time = 0;
    
    for (int i = 0; i < g_train_data.count; i++) {
        if (strcmp(g_train_data.departures[i].journey_name, journey_name) == 0) {
            strncpy(destination, g_train_data.departures[i].destination, sizeof(destination)-1);
            departure_time = g_train_data.departures[i].departure_timestamp;
            break;
        }
    }
    
    if (!destination[0] || departure_time == 0) {
        ESP_LOGE(TAG, "Could not find train '%s' in current list", journey_name);
        g_train_details.error = true;
        strcpy(g_train_details.error_msg, "Train not found");
        free(journey_name);
        g_details_refresh_in_progress = false;
        vTaskDelete(NULL);
        return;
    }
    
    // Encode destination
    char encoded_dest[256];
    url_encode(destination, encoded_dest, sizeof(encoded_dest));
    
    // Encode from station
    char encoded_from[256];
    url_encode(g_train_station_name, encoded_from, sizeof(encoded_from));
    
    // Format date and time
    struct tm tm_info;
    localtime_r(&departure_time, &tm_info);
    char date_str[16]; // YYYY-MM-DD
    char time_str[16]; // HH:MM
    strftime(date_str, sizeof(date_str), "%Y-%m-%d", &tm_info);
    strftime(time_str, sizeof(time_str), "%H:%M", &tm_info);
    
    char url[512];
    // Use connections endpoint to find the exact train run
    snprintf(url, sizeof(url), "%s/connections?from=%s&to=%s&date=%s&time=%s&limit=1", 
             TRANSPORT_API_BASE, encoded_from, encoded_dest, date_str, time_str);
    
    ESP_LOGI(TAG, "URL: %s", url);
    
    char *response_buffer = (char*)malloc(100 * 1024); // 100KB buffer
    if (!response_buffer) {
        free(journey_name);
        g_details_refresh_in_progress = false;
        vTaskDelete(NULL);
        return;
    }
    
    size_t len = 0;
    esp_err_t err = network_manager_http_get(url, response_buffer, 100 * 1024, &len);
    
    if (err == ESP_OK && len > 0) {
        if (len < 100 * 1024) response_buffer[len] = '\0';
        
        if (parse_journey_json(response_buffer) == ESP_OK) {
             g_train_details.error = false;
        } else {
             g_train_details.error = true;
             strcpy(g_train_details.error_msg, "Parse Error");
        }
    } else {
        g_train_details.error = true;
        strcpy(g_train_details.error_msg, "Network Error");
    }
    
    g_train_details.loading = false;
    
    // Post event
    extern esp_event_loop_handle_t view_event_handle;
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_TRAIN_DETAILS_UPDATE,
                     &g_train_details, sizeof(g_train_details), portMAX_DELAY);
    
    free(response_buffer);
    free(journey_name);
    g_details_refresh_in_progress = false;
    vTaskDelete(NULL);
}

/**
 * @brief Parse journey details JSON (for bus)
 */
static esp_err_t parse_bus_journey_json(const char *json_str)
{
    if (!json_str) return ESP_FAIL;
    
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse bus details JSON");
        return ESP_FAIL;
    }
    
    // Clear data first
    memset(&g_bus_details, 0, sizeof(g_bus_details));
    
    // Check connections array
    cJSON *connections = cJSON_GetObjectItem(root, "connections");
    if (!connections || !cJSON_IsArray(connections)) {
        ESP_LOGE(TAG, "No connections array in response");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    // Get first connection
    cJSON *connection = cJSON_GetArrayItem(connections, 0);
    if (!connection) {
        ESP_LOGE(TAG, "No connection found");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    // Get sections
    cJSON *sections = cJSON_GetObjectItem(connection, "sections");
    if (!sections || !cJSON_IsArray(sections)) {
        ESP_LOGE(TAG, "No sections in connection");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    // Find the first section with a journey
    cJSON *journey = NULL;
    cJSON *section = NULL;
    cJSON_ArrayForEach(section, sections) {
        journey = cJSON_GetObjectItem(section, "journey");
        if (journey) break;
    }
    
    if (!journey) {
        ESP_LOGE(TAG, "No journey found in sections");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    // Name
    cJSON *name = cJSON_GetObjectItem(journey, "name");
    if (name) strncpy(g_bus_details.name, cJSON_GetStringValue(name), sizeof(g_bus_details.name)-1);
    
    // Operator
    cJSON *operator = cJSON_GetObjectItem(journey, "operator");
    if (operator) strncpy(g_bus_details.operator, cJSON_GetStringValue(operator), sizeof(g_bus_details.operator)-1);
    
    // Stops (passList)
    cJSON *passList = cJSON_GetObjectItem(journey, "passList");
    if (passList && cJSON_IsArray(passList)) {
        int idx = 0;
        cJSON *stop = NULL;
        cJSON_ArrayForEach(stop, passList) {
            if (idx >= 30) break;
            
            cJSON *station = cJSON_GetObjectItem(stop, "station");
            cJSON *st_name = station ? cJSON_GetObjectItem(station, "name") : NULL;
            if (st_name) strncpy(g_bus_details.stops[idx].name, cJSON_GetStringValue(st_name), sizeof(g_bus_details.stops[idx].name)-1);
            
            cJSON *arr = cJSON_GetObjectItem(stop, "arrival");
            if (arr && cJSON_GetStringValue(arr)) {
                time_t t;
                parse_departure_time(cJSON_GetStringValue(arr), &t);
                struct tm tm;
                localtime_r(&t, &tm);
                snprintf(g_bus_details.stops[idx].arrival, 16, "%02d:%02d", tm.tm_hour, tm.tm_min);
            }
            
            cJSON *dep = cJSON_GetObjectItem(stop, "departure");
            if (dep && cJSON_GetStringValue(dep)) {
                time_t t;
                parse_departure_time(cJSON_GetStringValue(dep), &t);
                struct tm tm;
                localtime_r(&t, &tm);
                snprintf(g_bus_details.stops[idx].departure, 16, "%02d:%02d", tm.tm_hour, tm.tm_min);
            }
            
            cJSON *delay = cJSON_GetObjectItem(stop, "delay");
            if (delay && cJSON_IsNumber(delay)) {
                g_bus_details.stops[idx].delay = (int)cJSON_GetNumberValue(delay);
            }
            
            idx++;
        }
        g_bus_details.stop_count = idx;
    }
    
    // 1. Try to get capacity from the main connection object
    cJSON *cap1 = cJSON_GetObjectItem(connection, "capacity1st");
    cJSON *cap2 = cJSON_GetObjectItem(connection, "capacity2nd");
    
    if (cap1) {
        int c = 0;
        if (cJSON_IsNumber(cap1)) c = (int)cJSON_GetNumberValue(cap1);
        else if (cJSON_IsString(cap1)) c = atoi(cJSON_GetStringValue(cap1));
        
        if (c==1) strcpy(g_bus_details.capacity_1st, "Low");
        else if (c==2) strcpy(g_bus_details.capacity_1st, "Med");
        else if (c==3) strcpy(g_bus_details.capacity_1st, "High");
    }
    
    if (cap2) {
        int c = 0;
        if (cJSON_IsNumber(cap2)) c = (int)cJSON_GetNumberValue(cap2);
        else if (cJSON_IsString(cap2)) c = atoi(cJSON_GetStringValue(cap2));
        
        if (c==1) strcpy(g_bus_details.capacity_2nd, "Low");
        else if (c==2) strcpy(g_bus_details.capacity_2nd, "Med");
        else if (c==3) strcpy(g_bus_details.capacity_2nd, "High");
    }

    // 2. Capacity usually null for buses but we check anyway in passList
    if ((!g_bus_details.capacity_1st[0] || !g_bus_details.capacity_2nd[0]) &&
        passList && cJSON_IsArray(passList)) {
        cJSON *stop = NULL;
        cJSON_ArrayForEach(stop, passList) {
             cJSON *prognosis = cJSON_GetObjectItem(stop, "prognosis");
             cJSON *cap1 = prognosis ? cJSON_GetObjectItem(prognosis, "capacity1st") : cJSON_GetObjectItem(stop, "capacity1st");
             cJSON *cap2 = prognosis ? cJSON_GetObjectItem(prognosis, "capacity2nd") : cJSON_GetObjectItem(stop, "capacity2nd");
             
             if (cap1) {
                 int c = 0;
                 if (cJSON_IsNumber(cap1)) c = (int)cJSON_GetNumberValue(cap1);
                 else if (cJSON_IsString(cap1)) c = atoi(cJSON_GetStringValue(cap1));
                 
                 if (c==1) strcpy(g_bus_details.capacity_1st, "Low");
                 else if (c==2) strcpy(g_bus_details.capacity_1st, "Medium");
                 else if (c==3) strcpy(g_bus_details.capacity_1st, "High");
             }
             
             if (cap2) {
                 int c = 0;
                 if (cJSON_IsNumber(cap2)) c = (int)cJSON_GetNumberValue(cap2);
                 else if (cJSON_IsString(cap2)) c = atoi(cJSON_GetStringValue(cap2));
                 
                 if (c==1) strcpy(g_bus_details.capacity_2nd, "Low");
                 else if (c==2) strcpy(g_bus_details.capacity_2nd, "Medium");
                 else if (c==3) strcpy(g_bus_details.capacity_2nd, "High");
             }
             
             if (g_bus_details.capacity_1st[0] || g_bus_details.capacity_2nd[0]) break; 
        }
    }
    
    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * @brief Task to fetch bus details
 */
static void fetch_bus_details_task(void *arg)
{
    char *journey_name = (char *)arg;
    ESP_LOGI(TAG, "Fetching bus details for: %s", journey_name);
    
    // Find the bus in our current list to get 'to' and 'departure' time
    char destination[64] = {0};
    time_t departure_time = 0;
    
    for (int i = 0; i < g_bus_data.count; i++) {
        if (strcmp(g_bus_data.departures[i].journey_name, journey_name) == 0) {
            strncpy(destination, g_bus_data.departures[i].destination, sizeof(destination)-1);
            departure_time = g_bus_data.departures[i].departure_timestamp;
            break;
        }
    }
    
    if (!destination[0] || departure_time == 0) {
        ESP_LOGE(TAG, "Could not find bus '%s' in current list", journey_name);
        g_bus_details.error = true;
        strcpy(g_bus_details.error_msg, "Bus not found");
        free(journey_name);
        g_details_refresh_in_progress = false;
        vTaskDelete(NULL);
        return;
    }
    
    // Encode destination
    char encoded_dest[256];
    url_encode(destination, encoded_dest, sizeof(encoded_dest));
    
    // Encode from station
    char encoded_from[256];
    url_encode(g_bus_stop_name, encoded_from, sizeof(encoded_from));
    
    // Format date and time
    struct tm tm_info;
    localtime_r(&departure_time, &tm_info);
    char date_str[16]; // YYYY-MM-DD
    char time_str[16]; // HH:MM
    strftime(date_str, sizeof(date_str), "%Y-%m-%d", &tm_info);
    strftime(time_str, sizeof(time_str), "%H:%M", &tm_info);
    
    char url[512];
    snprintf(url, sizeof(url), "%s/connections?from=%s&to=%s&date=%s&time=%s&limit=1", 
             TRANSPORT_API_BASE, encoded_from, encoded_dest, date_str, time_str);
    
    ESP_LOGI(TAG, "URL: %s", url);
    
    char *response_buffer = (char*)malloc(100 * 1024);
    if (!response_buffer) {
        free(journey_name);
        g_details_refresh_in_progress = false;
        vTaskDelete(NULL);
        return;
    }
    
    size_t len = 0;
    esp_err_t err = network_manager_http_get(url, response_buffer, 100 * 1024, &len);
    
    if (err == ESP_OK && len > 0) {
        if (len < 100 * 1024) response_buffer[len] = '\0';
        
        if (parse_bus_journey_json(response_buffer) == ESP_OK) {
             g_bus_details.error = false;
        } else {
             g_bus_details.error = true;
             strcpy(g_bus_details.error_msg, "Parse Error");
        }
    } else {
        g_bus_details.error = true;
        strcpy(g_bus_details.error_msg, "Network Error");
    }
    
    g_bus_details.loading = false;
    
    extern esp_event_loop_handle_t view_event_handle;
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_BUS_DETAILS_UPDATE,
                     &g_bus_details, sizeof(g_bus_details), portMAX_DELAY);
    
    free(response_buffer);
    free(journey_name);
    g_details_refresh_in_progress = false;
    vTaskDelete(NULL);
}

/**
 * @brief Task to fetch bus data
 */
static void fetch_bus_task(void *arg)
{
    ESP_LOGI(TAG, "Fetching bus data...");
    
    // Check if display is on - don't fetch if screen is off
    if (!indicator_display_st_get()) {
        ESP_LOGI(TAG, "Display is off, skipping bus data fetch");
        vTaskDelete(NULL);
        return;
    }
    
    // Check if WiFi is connected
    if (!network_manager_is_connected()) {
        ESP_LOGW(TAG, "WiFi not connected, skipping bus data fetch");
        vTaskDelete(NULL);
        return;
    }

    // Check if station is selected
    if (!g_bus_stop_selected) {
        ESP_LOGD(TAG, "No bus stop selected, skipping fetch");
        vTaskDelete(NULL);
        return;
    }
    
    // Wait for time sync if needed (up to 10 seconds)
    time_t now;
    time(&now);
    int retries = 0;
    while (now < 1735689600 && retries < 10) { // 2025-01-01
        ESP_LOGI(TAG, "Waiting for time sync... (%d/10)", retries + 1);
        vTaskDelay(pdMS_TO_TICKS(1000));
        time(&now);
        retries++;
    }
    
    if (now < 1735689600) {
         ESP_LOGW(TAG, "Time not synced yet, fetching anyway but times might be wrong");
    }
    
    // Ping check to ensure connectivity
    if (network_manager_ping("8.8.8.8") != ESP_OK) {
        ESP_LOGW(TAG, "Ping failed, network might be unstable");
    }
    
    if (g_bus_refresh_in_progress) {
        ESP_LOGW(TAG, "Bus refresh already in progress");
        vTaskDelete(NULL);
        return;
    }
    
    g_bus_refresh_in_progress = true;
    
    char url[256];
    snprintf(url, sizeof(url), "%s/stationboard?station=%s&limit=20", 
             TRANSPORT_API_BASE, g_bus_stop_id);
    
    ESP_LOGI(TAG, "Fetching from URL: %s", url);
    
    char *response_buffer = (char*)malloc(400 * 1024);  // 400KB buffer
    if (!response_buffer) {
        ESP_LOGE(TAG, "Failed to allocate response buffer");
        g_bus_refresh_in_progress = false;
        vTaskDelete(NULL);
        return;
    }
    
    size_t response_length = 0;
    esp_err_t err = network_manager_http_get(url, response_buffer, 400 * 1024, &response_length);
    
    if (err == ESP_OK && response_length > 0) {
        ESP_LOGI(TAG, "Received %d bytes, parsing JSON...", response_length);
        // Ensure null termination
        if (response_length < 400 * 1024) {
            response_buffer[response_length] = '\0';
        } else {
            ESP_LOGW(TAG, "Response buffer full, may be truncated");
            response_buffer[400 * 1024 - 1] = '\0';
        }
        
        // Check if JSON looks complete (ends with } or ])
        size_t len = strlen(response_buffer);
        if (len > 0) {
            char last_char = response_buffer[len-1];
            // Skip trailing whitespace
            while (len > 0 && (last_char == ' ' || last_char == '\n' || last_char == '\r' || last_char == '\t')) {
                response_buffer[len-1] = '\0';
                len--;
                if (len > 0) last_char = response_buffer[len-1];
            }
            
            if (len > 0 && last_char != '}' && last_char != ']') {
                ESP_LOGW(TAG, "JSON may be incomplete, last char: '%c' (0x%02x), len: %d", last_char, last_char, len);
                // Print end of buffer
                int start = len > 50 ? len - 50 : 0;
                ESP_LOGW(TAG, "End of buffer: %s", response_buffer + start);
            }
        }
        
        ESP_LOGI(TAG, "Free heap before parse: %d, Free PSRAM: %d", 
                 esp_get_free_heap_size(), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
                 
        err = parse_bus_json(response_buffer);
        if (err == ESP_OK) {
            time(&g_last_bus_refresh);
            
            // Post event to update UI
            extern esp_event_loop_handle_t view_event_handle;
            esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_BUS_COUNTDOWN_UPDATE,
                             &g_bus_data, sizeof(g_bus_data), portMAX_DELAY);
        } else {
            g_bus_data.api_error = true;
            strncpy(g_bus_data.error_msg, "Parse error", sizeof(g_bus_data.error_msg) - 1);
            // Post event to update UI with error
            extern esp_event_loop_handle_t view_event_handle;
            esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_BUS_COUNTDOWN_UPDATE,
                             &g_bus_data, sizeof(g_bus_data), portMAX_DELAY);
        }
    } else {
        g_bus_data.api_error = true;
        strncpy(g_bus_data.error_msg, "API error", sizeof(g_bus_data.error_msg) - 1);
        ESP_LOGE(TAG, "Failed to fetch bus data: %s, len=%d", esp_err_to_name(err), response_length);
        // Post event to update UI with error
        extern esp_event_loop_handle_t view_event_handle;
        esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_BUS_COUNTDOWN_UPDATE,
                         &g_bus_data, sizeof(g_bus_data), portMAX_DELAY);
    }
    
    free(response_buffer);
    g_bus_refresh_in_progress = false;
    vTaskDelete(NULL);
}

/**
 * @brief Task to fetch train data
 */
static void fetch_train_task(void *arg)
{
    ESP_LOGI(TAG, "Fetching train data...");
    
    // Check if display is on - don't fetch if screen is off
    if (!indicator_display_st_get()) {
        ESP_LOGI(TAG, "Display is off, skipping train data fetch");
        vTaskDelete(NULL);
        return;
    }
    
    // Check if WiFi is connected
    if (!network_manager_is_connected()) {
        ESP_LOGW(TAG, "WiFi not connected, skipping train data fetch");
        vTaskDelete(NULL);
        return;
    }

    // Check if station is selected
    if (!g_train_station_selected) {
        ESP_LOGD(TAG, "No train station selected, skipping fetch");
        vTaskDelete(NULL);
        return;
    }
    
    if (g_train_refresh_in_progress) {
        ESP_LOGW(TAG, "Train refresh already in progress");
        vTaskDelete(NULL);
        return;
    }
    
    g_train_refresh_in_progress = true;
    
    char url[256];
    snprintf(url, sizeof(url), "%s/stationboard?station=%s&limit=20", 
             TRANSPORT_API_BASE, g_train_station_id);
    
    ESP_LOGI(TAG, "Fetching from URL: %s", url);
    
    char *response_buffer = (char*)malloc(400 * 1024);  // 400KB buffer
    if (!response_buffer) {
        ESP_LOGE(TAG, "Failed to allocate response buffer");
        g_train_refresh_in_progress = false;
        vTaskDelete(NULL);
        return;
    }
    
    size_t response_length = 0;
    esp_err_t err = network_manager_http_get(url, response_buffer, 400 * 1024, &response_length);
    
    if (err == ESP_OK && response_length > 0) {
        ESP_LOGI(TAG, "Received %d bytes, parsing JSON...", response_length);
        // Ensure null termination
        if (response_length < 400 * 1024) {
            response_buffer[response_length] = '\0';
        } else {
            ESP_LOGW(TAG, "Response buffer full, may be truncated");
            response_buffer[400 * 1024 - 1] = '\0';
        }
        
        // Check if JSON looks complete (ends with } or ])
        size_t len = strlen(response_buffer);
        if (len > 0) {
            char last_char = response_buffer[len-1];
            // Skip trailing whitespace
            while (len > 0 && (last_char == ' ' || last_char == '\n' || last_char == '\r' || last_char == '\t')) {
                response_buffer[len-1] = '\0';
                len--;
                if (len > 0) last_char = response_buffer[len-1];
            }
            
            if (len > 0 && last_char != '}' && last_char != ']') {
                ESP_LOGW(TAG, "JSON may be incomplete, last char: '%c' (0x%02x), len: %d", last_char, last_char, len);
                // Print end of buffer
                int start = len > 50 ? len - 50 : 0;
                ESP_LOGW(TAG, "End of buffer: %s", response_buffer + start);
            }
        }
        
        ESP_LOGI(TAG, "Free heap before parse: %d, Free PSRAM: %d", 
                 esp_get_free_heap_size(), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        
        err = parse_train_json(response_buffer);
        if (err == ESP_OK) {
            time(&g_last_train_refresh);
            
            // Post event to update UI
            extern esp_event_loop_handle_t view_event_handle;
            esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_TRAIN_STATION_UPDATE,
                             &g_train_data, sizeof(g_train_data), portMAX_DELAY);
        } else {
            g_train_data.api_error = true;
            strncpy(g_train_data.error_msg, "Parse error", sizeof(g_train_data.error_msg) - 1);
            // Post event to update UI with error
            extern esp_event_loop_handle_t view_event_handle;
            esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_TRAIN_STATION_UPDATE,
                             &g_train_data, sizeof(g_train_data), portMAX_DELAY);
        }
    } else {
        g_train_data.api_error = true;
        strncpy(g_train_data.error_msg, "API error", sizeof(g_train_data.error_msg) - 1);
        ESP_LOGE(TAG, "Failed to fetch train data: %s", esp_err_to_name(err));
        // Post event to update UI with error
        extern esp_event_loop_handle_t view_event_handle;
        esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_TRAIN_STATION_UPDATE,
                         &g_train_data, sizeof(g_train_data), portMAX_DELAY);
    }
    
    free(response_buffer);
    g_train_refresh_in_progress = false;
    vTaskDelete(NULL);
}

/**
 * @brief Timer callback for smart refresh - dynamically adjusts interval
 */
static void refresh_timer_callback(TimerHandle_t xTimer)
{
    /* Keep callback minimal: run in Tmr Svc task with limited stack. */
    int interval_min = transport_data_get_refresh_interval();
    xTimerChangePeriod(xTimer, pdMS_TO_TICKS(interval_min * 60 * 1000), 0);

    if (g_active_screen == 0) {
        xTaskCreate(fetch_bus_task, "fetch_bus", 8192, NULL, 5, NULL);
    } else if (g_active_screen == 1) {
        xTaskCreate(fetch_train_task, "fetch_train", 8192, NULL, 5, NULL);
    }
    /* Settings screen: skip refresh (no log here to save stack) */
}

esp_err_t transport_data_init(void)
{
    ESP_LOGI(TAG, "Initializing transport data module");
    ESP_LOGI(TAG, "Bus stop: %s (%s)", BUS_STOP_NAME, BUS_STOP_ID);
    ESP_LOGI(TAG, "Train station: %s (%s)", TRAIN_STATION_NAME, TRAIN_STATION_ID);
    ESP_LOGI(TAG, "Selected bus lines: %s", SELECTED_BUS_LINES);
    
    // Initialize data structures
    strncpy(g_bus_data.stop_name, g_bus_stop_name, sizeof(g_bus_data.stop_name) - 1);
    strncpy(g_train_data.station_name, g_train_station_name, sizeof(g_train_data.station_name) - 1);
    
    // Create refresh timer (will be started after initial fetch)
    g_refresh_timer = xTimerCreate("transport_refresh",
                                   pdMS_TO_TICKS(transport_data_get_refresh_interval() * 60 * 1000),
                                   pdTRUE,
                                   NULL,
                                   refresh_timer_callback);
    
    if (g_refresh_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create refresh timer");
        return ESP_FAIL;
    }
    
    // Create display schedule timer (check every 30 seconds)
    g_display_schedule_timer = xTimerCreate("display_schedule",
                                   pdMS_TO_TICKS(30 * 1000),
                                   pdTRUE,
                                   NULL,
                                   display_schedule_timer_cb);
                                   
    if (g_display_schedule_timer) {
        xTimerStart(g_display_schedule_timer, 0);
    }
    
    ESP_LOGI(TAG, "Transport data module initialized");
    return ESP_OK;
}

esp_err_t transport_data_fetch_bus(void)
{
    xTaskCreate(fetch_bus_task, "fetch_bus", 8192, NULL, 5, NULL);
    return ESP_OK;
}

esp_err_t transport_data_fetch_train(void)
{
    xTaskCreate(fetch_train_task, "fetch_train", 8192, NULL, 5, NULL);
    return ESP_OK;
}

esp_err_t transport_data_get_bus_countdown(struct view_data_bus_countdown *data)
{
    if (!data) return ESP_ERR_INVALID_ARG;
    memcpy(data, &g_bus_data, sizeof(*data));
    return ESP_OK;
}

esp_err_t transport_data_get_train_station(struct view_data_train_station *data)
{
    if (!data) return ESP_ERR_INVALID_ARG;
    memcpy(data, &g_train_data, sizeof(*data));
    return ESP_OK;
}

esp_err_t transport_data_fetch_train_details(const char *journey_name)
{
    if (!journey_name) return ESP_ERR_INVALID_ARG;
    if (g_details_refresh_in_progress) return ESP_ERR_INVALID_STATE;
    
    g_details_refresh_in_progress = true;
    
    // Copy name to heap to pass to task
    char *name_copy = strdup(journey_name);
    if (!name_copy) {
        g_details_refresh_in_progress = false;
        return ESP_ERR_NO_MEM;
    }
    
    // Set loading state
    g_train_details.loading = true;
    g_train_details.error = false;
    
    xTaskCreate(fetch_details_task, "fetch_details", 8192, name_copy, 5, NULL);
    return ESP_OK;
}

esp_err_t transport_data_fetch_bus_details(const char *journey_name)
{
    if (!journey_name) return ESP_ERR_INVALID_ARG;
    if (g_details_refresh_in_progress) return ESP_ERR_INVALID_STATE;
    
    g_details_refresh_in_progress = true;
    
    char *name_copy = strdup(journey_name);
    if (!name_copy) {
        g_details_refresh_in_progress = false;
        return ESP_ERR_NO_MEM;
    }
    
    g_bus_details.loading = true;
    g_bus_details.error = false;
    
    xTaskCreate(fetch_bus_details_task, "fetch_bus_det", 8192, name_copy, 5, NULL);
    return ESP_OK;
}

esp_err_t transport_data_get_bus_details(struct view_data_bus_details *data)
{
    if (!data) return ESP_ERR_INVALID_ARG;
    memcpy(data, &g_bus_details, sizeof(*data));
    return ESP_OK;
}

void transport_data_clear_bus_details(void)
{
    memset(&g_bus_details, 0, sizeof(g_bus_details));
}

esp_err_t transport_data_get_train_details(struct view_data_train_details *data)
{
    if (!data) return ESP_ERR_INVALID_ARG;
    memcpy(data, &g_train_details, sizeof(*data));
    return ESP_OK;
}

void transport_data_clear_train_details(void)
{
    memset(&g_train_details, 0, sizeof(g_train_details));
}

bool transport_data_is_day_mode(void)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    int hour = timeinfo.tm_hour;
    return (hour >= g_refresh_config.day_start_hour && hour <= g_refresh_config.day_end_hour);
}

int transport_data_get_refresh_interval(void)
{
    return transport_data_is_day_mode() ? 
           g_refresh_config.day_refresh_minutes : 
           g_refresh_config.night_refresh_minutes;
}

time_t transport_data_get_time_offset(void)
{
    return g_time_offset;
}

esp_err_t transport_data_force_refresh(void)
{
    ESP_LOGI(TAG, "Force refresh requested");
    g_force_refresh = true;
    
    // Only refresh active screen
    if (g_active_screen == 0) {
        xTaskCreate(fetch_bus_task, "bus_fetch_task", 8192, NULL, 5, NULL);
    } else if (g_active_screen == 1) {
        xTaskCreate(fetch_train_task, "train_fetch_task", 8192, NULL, 5, NULL);
    } else {
        // Fallback: refresh all if unknown
        xTaskCreate(fetch_bus_task, "bus_fetch_task", 8192, NULL, 5, NULL);
        xTaskCreate(fetch_train_task, "train_fetch_task", 8192, NULL, 5, NULL);
    }
    
    return ESP_OK;
}

esp_err_t transport_data_refresh_bus(void)
{
    ESP_LOGI(TAG, "Bus refresh requested");
    xTaskCreate(fetch_bus_task, "bus_fetch_task", 8192, NULL, 5, NULL);
    return ESP_OK;
}

esp_err_t transport_data_refresh_train(void)
{
    ESP_LOGI(TAG, "Train refresh requested");
    xTaskCreate(fetch_train_task, "train_fetch_task", 8192, NULL, 5, NULL);
    return ESP_OK;
}

bool transport_data_needs_refresh(void)
{
    if (g_force_refresh) {
        g_force_refresh = false;
        return true;
    }
    
    time_t now;
    time(&now);
    int interval_seconds = transport_data_get_refresh_interval() * 60;
    
    bool bus_stale = (now - g_last_bus_refresh) >= interval_seconds;
    bool train_stale = (now - g_last_train_refresh) >= interval_seconds;
    
    return bus_stale || train_stale;
}

TimerHandle_t transport_data_get_refresh_timer(void)
{
    return g_refresh_timer;
}
