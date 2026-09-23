#include "connections_data.h"
#include "network_manager.h"
#include "esp_log.h"
#include "esp_event.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "connections";

// API endpoint base URL (same host as the stationboard endpoints)
#define CONNECTIONS_API_BASE "http://transport.opendata.ch/v1"

// From/To station selection (mutable, set via connections_data_set_from/to)
static char g_from_name[64] = "";
static char g_from_id[16] = "";
static char g_to_name[64] = "";
static char g_to_id[16] = "";

// Result storage
static struct view_data_connections g_connections_data = {0};

// Re-entrancy guards
static bool g_connections_fetch_in_progress = false;
static bool g_station_search_in_progress = false;

/**
 * @brief Simple URL encoder (spaces and any non-alphanumeric byte -> %XX)
 */
static void url_encode(const char *src, char *dst, size_t dst_len)
{
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
 * @brief Parse an ISO8601 timestamp (e.g. "2026-09-23T13:32:00+0200") into "HH:MM"
 */
static bool parse_iso8601_to_hhmm(const char *time_str, char *out, size_t out_len)
{
    if (!time_str || !out) return false;

    struct tm tm_info = {0};
    int year, month, day, hour, min, sec;
    int tz_hour, tz_min;
    char tz_sign;

    if (sscanf(time_str, "%d-%d-%dT%d:%d:%d%c%02d%02d",
               &year, &month, &day, &hour, &min, &sec,
               &tz_sign, &tz_hour, &tz_min) != 9) {
        ESP_LOGW(TAG, "Failed to parse time: %s", time_str);
        return false;
    }

    tm_info.tm_year = year - 1900;
    tm_info.tm_mon = month - 1;
    tm_info.tm_mday = day;
    tm_info.tm_hour = hour;
    tm_info.tm_min = min;
    tm_info.tm_sec = sec;
    tm_info.tm_isdst = -1;

    time_t t = mktime(&tm_info);
    struct tm local_tm;
    localtime_r(&t, &local_tm);
    snprintf(out, out_len, "%02d:%02d", local_tm.tm_hour, local_tm.tm_min);
    return true;
}

/**
 * @brief Parse the API's "DDdHH:MM:SS" duration string into "HH:MM"
 */
static void parse_duration_hhmm(const char *duration_str, char *out, size_t out_len)
{
    if (!out || out_len == 0) return;
    out[0] = '\0';

    if (!duration_str) return;

    const char *d_pos = strchr(duration_str, 'd');
    if (!d_pos) return;

    int hh = 0, mm = 0, ss = 0;
    if (sscanf(d_pos + 1, "%d:%d:%d", &hh, &mm, &ss) >= 2) {
        snprintf(out, out_len, "%02d:%02d", hh, mm);
    }
}

void connections_data_set_from(const char *name, const char *id)
{
    if (name) {
        strncpy(g_from_name, name, sizeof(g_from_name) - 1);
        g_from_name[sizeof(g_from_name) - 1] = '\0';
    }
    if (id) {
        strncpy(g_from_id, id, sizeof(g_from_id) - 1);
        g_from_id[sizeof(g_from_id) - 1] = '\0';
    }
    ESP_LOGI(TAG, "From set to: %s (%s)", g_from_name, g_from_id);
}

void connections_data_set_to(const char *name, const char *id)
{
    if (name) {
        strncpy(g_to_name, name, sizeof(g_to_name) - 1);
        g_to_name[sizeof(g_to_name) - 1] = '\0';
    }
    if (id) {
        strncpy(g_to_id, id, sizeof(g_to_id) - 1);
        g_to_id[sizeof(g_to_id) - 1] = '\0';
    }
    ESP_LOGI(TAG, "To set to: %s (%s)", g_to_name, g_to_id);
}

bool connections_data_ready(void)
{
    return g_from_id[0] != '\0' && g_to_id[0] != '\0';
}

/**
 * @brief Parse the /v1/locations response into a station search result set
 */
static esp_err_t parse_locations_json(const char *json_str, struct view_data_station_search *out)
{
    if (!json_str || !out) return ESP_FAIL;

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            ESP_LOGE(TAG, "JSON parse error before: %s", error_ptr);
        }
        return ESP_FAIL;
    }

    cJSON *stations = cJSON_GetObjectItem(root, "stations");
    if (!stations || !cJSON_IsArray(stations)) {
        ESP_LOGE(TAG, "No stations array in response");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    int idx = 0;
    cJSON *station = NULL;
    cJSON_ArrayForEach(station, stations) {
        if (idx >= MAX_STATION_RESULTS) break;

        cJSON *id = cJSON_GetObjectItem(station, "id");
        const char *id_str = (id && cJSON_IsString(id)) ? cJSON_GetStringValue(id) : NULL;
        if (!id_str || id_str[0] == '\0') {
            // Skip POIs / addresses without a real station id
            continue;
        }

        cJSON *name = cJSON_GetObjectItem(station, "name");
        const char *name_str = (name && cJSON_IsString(name)) ? cJSON_GetStringValue(name) : NULL;
        if (!name_str) continue;

        strncpy(out->results[idx].name, name_str, sizeof(out->results[idx].name) - 1);
        strncpy(out->results[idx].id, id_str, sizeof(out->results[idx].id) - 1);
        idx++;
    }

    out->count = idx;
    out->api_error = false;

    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * @brief Task to search stations by name
 */
static void fetch_station_search_task(void *arg)
{
    char *query = (char *)arg;
    struct view_data_station_search result = {0};

    ESP_LOGI(TAG, "Searching stations for: %s", query ? query : "");

    if (!network_manager_is_connected()) {
        ESP_LOGW(TAG, "WiFi not connected, cannot search stations");
        result.api_error = true;
        extern esp_event_loop_handle_t view_event_handle;
        esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_STATION_SEARCH_RESULTS,
                           &result, sizeof(result), portMAX_DELAY);
        free(query);
        g_station_search_in_progress = false;
        vTaskDelete(NULL);
        return;
    }

    char encoded_query[192];
    url_encode(query ? query : "", encoded_query, sizeof(encoded_query));

    char url[256];
    snprintf(url, sizeof(url), "%s/locations?query=%s", CONNECTIONS_API_BASE, encoded_query);
    ESP_LOGI(TAG, "Fetching from URL: %s", url);

    const size_t buffer_size = 32 * 1024;
    char *response_buffer = (char *)malloc(buffer_size);
    if (!response_buffer) {
        ESP_LOGE(TAG, "Failed to allocate response buffer");
        result.api_error = true;
        extern esp_event_loop_handle_t view_event_handle;
        esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_STATION_SEARCH_RESULTS,
                           &result, sizeof(result), portMAX_DELAY);
        free(query);
        g_station_search_in_progress = false;
        vTaskDelete(NULL);
        return;
    }

    size_t response_length = 0;
    esp_err_t err = network_manager_http_get(url, response_buffer, buffer_size, &response_length);

    if (err == ESP_OK && response_length > 0) {
        if (response_length < buffer_size) {
            response_buffer[response_length] = '\0';
        } else {
            response_buffer[buffer_size - 1] = '\0';
        }

        if (parse_locations_json(response_buffer, &result) != ESP_OK) {
            result.api_error = true;
            result.count = 0;
        }
    } else {
        ESP_LOGE(TAG, "Failed to fetch station search results: %s", esp_err_to_name(err));
        result.api_error = true;
    }

    extern esp_event_loop_handle_t view_event_handle;
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_STATION_SEARCH_RESULTS,
                       &result, sizeof(result), portMAX_DELAY);

    free(response_buffer);
    free(query);
    g_station_search_in_progress = false;
    vTaskDelete(NULL);
}

void connections_data_search_stations(const char *query)
{
    if (!query) return;

    if (g_station_search_in_progress) {
        ESP_LOGW(TAG, "Station search already in progress, skipping");
        return;
    }

    char *query_copy = strdup(query);
    if (!query_copy) {
        ESP_LOGE(TAG, "Failed to allocate query copy");
        return;
    }

    g_station_search_in_progress = true;
    xTaskCreate(fetch_station_search_task, "station_search", 8192, query_copy, 5, NULL);
}

/**
 * @brief Fill one connection_section from a "sections[]" array entry
 */
static void parse_connection_section(cJSON *sec, struct connection_section *section)
{
    memset(section, 0, sizeof(*section));

    cJSON *walk = cJSON_GetObjectItem(sec, "walk");
    cJSON *journey = cJSON_GetObjectItem(sec, "journey");

    if (walk && !cJSON_IsNull(walk)) {
        strncpy(section->line, "Walk", sizeof(section->line) - 1);
    } else if (journey) {
        cJSON *cat = cJSON_GetObjectItem(journey, "category");
        cJSON *num = cJSON_GetObjectItem(journey, "number");
        const char *cat_str = (cat && cJSON_IsString(cat)) ? cJSON_GetStringValue(cat) : NULL;
        const char *num_str = (num && cJSON_IsString(num)) ? cJSON_GetStringValue(num) : NULL;

        if (cat_str && num_str) {
            snprintf(section->line, sizeof(section->line), "%s%s", cat_str, num_str);
        } else if (cat_str) {
            strncpy(section->line, cat_str, sizeof(section->line) - 1);
        } else if (num_str) {
            strncpy(section->line, num_str, sizeof(section->line) - 1);
        }
    }

    cJSON *departure = cJSON_GetObjectItem(sec, "departure");
    if (departure) {
        cJSON *station = cJSON_GetObjectItem(departure, "station");
        cJSON *name = station ? cJSON_GetObjectItem(station, "name") : NULL;
        if (name && cJSON_IsString(name)) {
            strncpy(section->from_name, cJSON_GetStringValue(name), sizeof(section->from_name) - 1);
        }

        cJSON *dep_time = cJSON_GetObjectItem(departure, "departure");
        if (dep_time && cJSON_IsString(dep_time)) {
            parse_iso8601_to_hhmm(cJSON_GetStringValue(dep_time), section->departure_time, sizeof(section->departure_time));
        }

        cJSON *platform = cJSON_GetObjectItem(departure, "platform");
        if (platform && cJSON_IsString(platform)) {
            strncpy(section->platform, cJSON_GetStringValue(platform), sizeof(section->platform) - 1);
        }
    }

    cJSON *arrival = cJSON_GetObjectItem(sec, "arrival");
    if (arrival) {
        cJSON *station = cJSON_GetObjectItem(arrival, "station");
        cJSON *name = station ? cJSON_GetObjectItem(station, "name") : NULL;
        if (name && cJSON_IsString(name)) {
            strncpy(section->to_name, cJSON_GetStringValue(name), sizeof(section->to_name) - 1);
        }

        cJSON *arr_time = cJSON_GetObjectItem(arrival, "arrival");
        if (arr_time && cJSON_IsString(arr_time)) {
            parse_iso8601_to_hhmm(cJSON_GetStringValue(arr_time), section->arrival_time, sizeof(section->arrival_time));
        }
    }
}

/**
 * @brief Parse the /v1/connections response into g_connections_data
 */
static esp_err_t parse_connections_json(const char *json_str)
{
    if (!json_str) return ESP_FAIL;

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            ESP_LOGE(TAG, "JSON parse error before: %s", error_ptr);
        }
        return ESP_FAIL;
    }

    cJSON *connections = cJSON_GetObjectItem(root, "connections");
    if (!connections || !cJSON_IsArray(connections)) {
        ESP_LOGE(TAG, "No connections array in response");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    memset(&g_connections_data.connections, 0, sizeof(g_connections_data.connections));
    strncpy(g_connections_data.from_name, g_from_name, sizeof(g_connections_data.from_name) - 1);
    strncpy(g_connections_data.to_name, g_to_name, sizeof(g_connections_data.to_name) - 1);

    int idx = 0;
    cJSON *conn = NULL;
    cJSON_ArrayForEach(conn, connections) {
        if (idx >= MAX_CONNECTIONS) break;

        struct connection_view *cv = &g_connections_data.connections[idx];

        cJSON *from = cJSON_GetObjectItem(conn, "from");
        cJSON *to = cJSON_GetObjectItem(conn, "to");

        if (from) {
            cJSON *departure = cJSON_GetObjectItem(from, "departure");
            if (departure && cJSON_IsString(departure)) {
                parse_iso8601_to_hhmm(cJSON_GetStringValue(departure), cv->departure_time, sizeof(cv->departure_time));
            }

            cJSON *delay = cJSON_GetObjectItem(from, "delay");
            int delay_seconds = (delay && cJSON_IsNumber(delay)) ? (int)cJSON_GetNumberValue(delay) : 0;
            cv->departure_delay = delay_seconds / 60;
        }

        if (to) {
            cJSON *arrival = cJSON_GetObjectItem(to, "arrival");
            if (arrival && cJSON_IsString(arrival)) {
                parse_iso8601_to_hhmm(cJSON_GetStringValue(arrival), cv->arrival_time, sizeof(cv->arrival_time));
            }
        }

        cJSON *duration = cJSON_GetObjectItem(conn, "duration");
        if (duration && cJSON_IsString(duration)) {
            parse_duration_hhmm(cJSON_GetStringValue(duration), cv->duration, sizeof(cv->duration));
        }

        cJSON *transfers = cJSON_GetObjectItem(conn, "transfers");
        cv->transfers = (transfers && cJSON_IsNumber(transfers)) ? (int)cJSON_GetNumberValue(transfers) : 0;

        cJSON *sections = cJSON_GetObjectItem(conn, "sections");
        int sidx = 0;
        if (sections && cJSON_IsArray(sections)) {
            cJSON *sec = NULL;
            cJSON_ArrayForEach(sec, sections) {
                if (sidx >= MAX_CONNECTION_SECTIONS) break;
                parse_connection_section(sec, &cv->sections[sidx]);
                sidx++;
            }
        }
        cv->section_count = sidx;
        cv->valid = true;

        idx++;
    }

    g_connections_data.count = idx;
    time(&g_connections_data.update_time);
    g_connections_data.api_error = false;

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Parsed %d connections", idx);
    return idx > 0 ? ESP_OK : ESP_FAIL;
}

/**
 * @brief Task to fetch connections between g_from_id and g_to_id
 */
static void fetch_connections_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Fetching connections: %s (%s) -> %s (%s)", g_from_name, g_from_id, g_to_name, g_to_id);

    if (!network_manager_is_connected()) {
        ESP_LOGW(TAG, "WiFi not connected, skipping connections fetch");
        g_connections_data.api_error = true;
        strncpy(g_connections_data.error_msg, "No network", sizeof(g_connections_data.error_msg) - 1);
        extern esp_event_loop_handle_t view_event_handle;
        esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_CONNECTIONS_UPDATE,
                           &g_connections_data, sizeof(g_connections_data), portMAX_DELAY);
        g_connections_fetch_in_progress = false;
        vTaskDelete(NULL);
        return;
    }

    char url[256];
    snprintf(url, sizeof(url), "%s/connections?from=%s&to=%s&limit=6",
             CONNECTIONS_API_BASE, g_from_id, g_to_id);
    ESP_LOGI(TAG, "Fetching from URL: %s", url);

    const size_t buffer_size = 400 * 1024;
    char *response_buffer = (char *)malloc(buffer_size);
    if (!response_buffer) {
        ESP_LOGE(TAG, "Failed to allocate response buffer");
        g_connections_data.api_error = true;
        strncpy(g_connections_data.error_msg, "Out of memory", sizeof(g_connections_data.error_msg) - 1);
        extern esp_event_loop_handle_t view_event_handle;
        esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_CONNECTIONS_UPDATE,
                           &g_connections_data, sizeof(g_connections_data), portMAX_DELAY);
        g_connections_fetch_in_progress = false;
        vTaskDelete(NULL);
        return;
    }

    size_t response_length = 0;
    esp_err_t err = network_manager_http_get(url, response_buffer, buffer_size, &response_length);

    if (err == ESP_OK && response_length > 0) {
        if (response_length < buffer_size) {
            response_buffer[response_length] = '\0';
        } else {
            response_buffer[buffer_size - 1] = '\0';
        }

        if (parse_connections_json(response_buffer) != ESP_OK) {
            g_connections_data.api_error = true;
            strncpy(g_connections_data.error_msg, "Parse error", sizeof(g_connections_data.error_msg) - 1);
        }
    } else {
        ESP_LOGE(TAG, "Failed to fetch connections: %s, len=%d", esp_err_to_name(err), (int)response_length);
        g_connections_data.api_error = true;
        strncpy(g_connections_data.error_msg, "API error", sizeof(g_connections_data.error_msg) - 1);
    }

    extern esp_event_loop_handle_t view_event_handle;
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_CONNECTIONS_UPDATE,
                       &g_connections_data, sizeof(g_connections_data), portMAX_DELAY);

    free(response_buffer);
    g_connections_fetch_in_progress = false;
    vTaskDelete(NULL);
}

void connections_data_fetch(void)
{
    if (!connections_data_ready()) {
        ESP_LOGW(TAG, "Cannot fetch connections, from/to station not set");
        return;
    }

    if (g_connections_fetch_in_progress) {
        ESP_LOGW(TAG, "Connections fetch already in progress, skipping");
        return;
    }

    g_connections_fetch_in_progress = true;
    xTaskCreate(fetch_connections_task, "fetch_conn", 8192, NULL, 5, NULL);
}

esp_err_t connections_data_init(void)
{
    ESP_LOGI(TAG, "Initializing connections data module");
    memset(&g_connections_data, 0, sizeof(g_connections_data));
    return ESP_OK;
}
