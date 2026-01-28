#include "indicator_view.h"
#include "view_data.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_event.h"
#include "network_manager.h"
#include "transport_data.h"
#include "indicator_time.h"  // For time updates
#include "config.h"
#include <string.h>
#include <time.h>

LV_FONT_DECLARE(arimo_14);
LV_FONT_DECLARE(arimo_16);
LV_FONT_DECLARE(arimo_20);
LV_FONT_DECLARE(arimo_24);


static const char *TAG = "view";

// Screen objects
static lv_obj_t *tabview = NULL;
static lv_obj_t *bus_screen = NULL;
static lv_obj_t *train_screen = NULL;
static lv_obj_t *settings_screen = NULL;

// Bus screen widgets
static lv_obj_t *bus_stop_label = NULL;
static lv_obj_t *bus_list = NULL;
static lv_obj_t *bus_refresh_btn = NULL;
static lv_obj_t *bus_back_btn = NULL;
static lv_obj_t *bus_prev_btn = NULL;
static lv_obj_t *bus_next_btn = NULL;
static lv_obj_t *bus_status_label = NULL;
static lv_obj_t *bus_time_label = NULL;  // Footer with current time
static lv_obj_t *bus_selection_cont = NULL;
static lv_obj_t *bus_view_cont = NULL;
static lv_obj_t *bus_loading_cont = NULL;

// Forward declarations
static void update_bus_screen(const struct view_data_bus_countdown *data);
static void update_train_screen(const struct view_data_train_station *data);
static void update_settings_screen(const struct view_data_settings *data);
static void create_bus_screen(lv_obj_t *parent);
static void create_train_screen(lv_obj_t *parent);
static void create_settings_screen(lv_obj_t *parent);
static void refresh_btn_cb(lv_event_t *e);
static void bus_refresh_btn_cb(lv_event_t *e);
static void bus_back_btn_cb(lv_event_t *e);
static void bus_stop_select_cb(lv_event_t *e);
static void train_refresh_btn_cb(lv_event_t *e);
static void train_back_btn_cb(lv_event_t *e);
static void station_select_cb(lv_event_t *e);
static void prev_btn_cb(lv_event_t *e);
static void next_btn_cb(lv_event_t *e);
static void brightness_slider_cb(lv_event_t *e);
static void sleep_slider_cb(lv_event_t *e);
static void time_update_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data);
static void view_event_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data);
static void tabview_event_cb(lv_event_t *e);

typedef struct {
    const char *name;
    const char *id;
} station_t;

static const station_t predefined_bus_stops[] = {
    {"Aarau, Gais", "8590142"},
    {"Aarau, Acheberstrasse", "8588428"}
};

// View state
static int bus_view_direction_index = 0; // Index in dynamic directions array

// Train screen widgets
static lv_obj_t *train_station_label = NULL;
static lv_obj_t *train_list = NULL;
static lv_obj_t *train_refresh_btn = NULL;
static lv_obj_t *train_back_btn = NULL;
static lv_obj_t *train_status_label = NULL;
static lv_obj_t *train_footer = NULL;
static lv_obj_t *station_selection_cont = NULL;
static lv_obj_t *train_view_cont = NULL;
static lv_obj_t *loading_cont = NULL;


static const station_t predefined_stations[] = {
    {"Aarau", "8502113"},
    {"Zürich HB", "8503000"},
    {"Bern", "8507000"},
    {"Brugg AG", "8500309"},
    {"Baden", "8503504"},
    {"Olten", "8500218"},
    {"Luzern", "8505000"}
};

// Settings screen widgets
static lv_obj_t *wifi_status_label = NULL;
static lv_obj_t *ip_label = NULL;
static lv_obj_t *api_status_label = NULL;
static lv_obj_t *brightness_slider = NULL;
static lv_obj_t *brightness_label = NULL;
static lv_obj_t *sleep_slider = NULL;
static lv_obj_t *sleep_label = NULL;

// WiFi Screen widgets
static lv_obj_t *settings_main_cont = NULL;
static lv_obj_t *wifi_view_cont = NULL;
static lv_obj_t *wifi_list = NULL;
static lv_obj_t *wifi_password_view_cont = NULL;
static lv_obj_t *wifi_password_ta = NULL;
static lv_obj_t *wifi_keyboard = NULL;
static char current_wifi_ssid[32];

// Forward declarations
static void update_bus_screen(const struct view_data_bus_countdown *data);
static void update_train_screen(const struct view_data_train_station *data);
static void update_settings_screen(const struct view_data_settings *data);
static void update_wifi_list(const struct view_data_wifi_list *list);
static void create_bus_screen(lv_obj_t *parent);
static void create_train_screen(lv_obj_t *parent);
static void create_settings_screen(lv_obj_t *parent);
static void create_wifi_screen(lv_obj_t *parent);
static void create_wifi_password_screen(lv_obj_t *parent);
static void refresh_btn_cb(lv_event_t *e);
static void bus_refresh_btn_cb(lv_event_t *e);
static void bus_back_btn_cb(lv_event_t *e);
static void bus_stop_select_cb(lv_event_t *e);
static void train_refresh_btn_cb(lv_event_t *e);
static void train_back_btn_cb(lv_event_t *e);
static void station_select_cb(lv_event_t *e);
static void prev_btn_cb(lv_event_t *e);
static void next_btn_cb(lv_event_t *e);
static void brightness_slider_cb(lv_event_t *e);
static void sleep_slider_cb(lv_event_t *e);
static void wifi_btn_cb(lv_event_t *e);
static void wifi_back_btn_cb(lv_event_t *e);
static void wifi_scan_btn_cb(lv_event_t *e);
static void wifi_list_item_cb(lv_event_t *e);
static void wifi_password_back_btn_cb(lv_event_t *e);
static void wifi_connect_btn_cb(lv_event_t *e);
static void wifi_keyboard_event_cb(lv_event_t *e);
static void time_update_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data);
static void view_event_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data);

/**
 * @brief Live update timer callback
 */
static void live_update_timer_cb(lv_timer_t *timer)
{
    time_t now;
    time(&now);
    
    // Apply offset if system time is out of sync
    now += transport_data_get_time_offset();

    // 1. Update Bus Data (Recalculate minutes)
    struct view_data_bus_countdown bus_data;
    if (transport_data_get_bus_countdown(&bus_data) == ESP_OK) {
        for (int i = 0; i < bus_data.count; i++) {
            if (bus_data.departures[i].valid && bus_data.departures[i].departure_timestamp > 0) {
                double diff = difftime(bus_data.departures[i].departure_timestamp, now);
                bus_data.departures[i].minutes_until = (int)(diff / 60);
            }
        }
        update_bus_screen(&bus_data);
    }

    // 2. Update Train Data (Prune passed trains)
    struct view_data_train_station train_data;
    if (transport_data_get_train_station(&train_data) == ESP_OK) {
        bool changed = false;
        for (int i = 0; i < train_data.count; i++) {
            if (train_data.departures[i].valid && train_data.departures[i].departure_timestamp > 0) {
                // Check if train has departed (> 1 minute ago to keep it briefly)
                double diff = difftime(train_data.departures[i].departure_timestamp, now);
                if (diff < -60) { // Departed more than 60 seconds ago
                    train_data.departures[i].valid = false;
                    changed = true;
                }
            }
        }
        // Always update to keep it in sync, or only if changed? 
        // Update always ensures filtering is applied even if we just fetched old data
        update_train_screen(&train_data);
    }
}

/**
 * @brief Tabview change callback
 */
static void tabview_event_cb(lv_event_t *e)
{
    lv_obj_t *tv = lv_event_get_target(e);
    uint16_t id = lv_tabview_get_tab_act(tv);
    ESP_LOGI(TAG, "Tab changed to %d", id);
    
    // Notify transport model about active screen
    // 0 = Bus, 1 = Train, 2 = Settings
    transport_data_notify_screen_change(id);
}

/**
 * @brief Get line color based on line number (matching opendata.ch colors)
 */
static lv_color_t get_line_color(const char *line)
{
    if (strcmp(line, "1") == 0) {
        return lv_color_hex(0xFF0000);  // Red for line 1
    } else if (strcmp(line, "4") == 0) {
        return lv_color_hex(0xFF69B4);  // Pink/HotPink for line 4
    }
    return lv_color_hex(0x808080);  // Gray default
}

/**
 * @brief Bus prev direction button callback
 */
static void prev_btn_cb(lv_event_t *e)
{
    bus_view_direction_index--;
    
    // Trigger update with cached data
    struct view_data_bus_countdown data;
    transport_data_get_bus_countdown(&data);
    
    // Wrap around correctly
    if (data.direction_count > 0) {
        if (bus_view_direction_index < 0) {
            bus_view_direction_index = data.direction_count - 1;
        }
    } else {
        bus_view_direction_index = 0;
    }
    
    update_bus_screen(&data);
}

/**
 * @brief Bus next direction button callback
 */
static void next_btn_cb(lv_event_t *e)
{
    bus_view_direction_index++;
    
    // Trigger update with cached data
    struct view_data_bus_countdown data;
    transport_data_get_bus_countdown(&data);
    
    // Wrap around
    if (data.direction_count > 0) {
        bus_view_direction_index = bus_view_direction_index % data.direction_count;
    } else {
        bus_view_direction_index = 0;
    }
    
    update_bus_screen(&data);
}

/**
 * @brief Bus stop selection callback
 */
static void bus_stop_select_cb(lv_event_t *e)
{
    station_t *stop = (station_t *)lv_event_get_user_data(e);
    if (!stop) return;
    
    // Set stop and refresh
    transport_data_set_bus_stop(stop->name, stop->id);
    
    // Update UI state
    lv_obj_add_flag(bus_selection_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(bus_loading_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(bus_view_cont, LV_OBJ_FLAG_HIDDEN);
    
    // Update label
    if (bus_stop_label) {
        lv_label_set_text(bus_stop_label, stop->name);
    }
}

/**
 * @brief Back to bus selection callback
 */
static void bus_back_btn_cb(lv_event_t *e)
{
    lv_obj_clear_flag(bus_selection_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(bus_loading_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(bus_view_cont, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Bus refresh button callback
 */
static void bus_refresh_btn_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "Manual bus refresh requested");
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_BUS_REFRESH, NULL, 0, portMAX_DELAY);
}

/**
 * @brief Station selection callback
 */
static void station_select_cb(lv_event_t *e)
{
    station_t *station = (station_t *)lv_event_get_user_data(e);
    if (!station) return;
    
    // Set station and refresh
    transport_data_set_train_station(station->name, station->id);
    
    // Update UI state
    lv_obj_add_flag(station_selection_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(loading_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(train_view_cont, LV_OBJ_FLAG_HIDDEN);
    
    // Update label
    if (train_station_label) {
        lv_label_set_text(train_station_label, station->name);
    }
}

/**
 * @brief Back to selection callback
 */
static void train_back_btn_cb(lv_event_t *e)
{
    lv_obj_clear_flag(station_selection_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(loading_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(train_view_cont, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Train refresh button callback
 */
static void train_refresh_btn_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "Manual train refresh requested");
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_TRAIN_REFRESH, NULL, 0, portMAX_DELAY);
}

/**
 * @brief Refresh button callback (legacy/global)
 */
static void refresh_btn_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "Manual refresh requested");
    transport_data_force_refresh();
}

/**
 * @brief Brightness slider callback
 */
static void brightness_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t value = lv_slider_get_value(slider);
    
    char buf[32];
    snprintf(buf, sizeof(buf), "Brightness: %d%%", value);
    lv_label_set_text(brightness_label, buf);
    
    // Update display brightness via event system
    uint8_t brightness = (uint8_t)value;
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_BRIGHTNESS_UPDATE,
                     &brightness, sizeof(brightness), portMAX_DELAY);
}

/**
 * @brief Sleep timeout slider callback
 */
static void sleep_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t value = lv_slider_get_value(slider);
    
    const char *text[] = {"Always On", "1 min", "5 min", "10 min", "30 min", "60 min"};
    int index = value / 20;  // 0-5
    if (index > 5) index = 5;
    
    char buf[32];
    snprintf(buf, sizeof(buf), "Timeout: %s", text[index]);
    lv_label_set_text(sleep_label, buf);
}

/**
 * @brief Update bus countdown screen
 */
static void update_bus_screen(const struct view_data_bus_countdown *data)
{
    if (!bus_screen || !data) return;
    
    lv_port_sem_take();
    
    // Hide loading, show view
    lv_obj_add_flag(bus_loading_cont, LV_OBJ_FLAG_HIDDEN);
    // Only show bus view if we are not in selection mode
    if (lv_obj_has_flag(bus_selection_cont, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(bus_view_cont, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Update status (only show on error)
    if (bus_status_label) {
        if (data->api_error) {
            lv_label_set_text(bus_status_label, data->error_msg);
            lv_obj_set_style_text_color(bus_status_label, lv_color_hex(0xFF0000), 0);
        } else {
            lv_label_set_text(bus_status_label, "");  // Hide when OK
        }
    }
    
    // Clear and update list with improved layout
    if (bus_list) {
        lv_obj_clean(bus_list);
        
        // Ensure index is valid
        if (data->direction_count > 0) {
            bus_view_direction_index = bus_view_direction_index % data->direction_count;
        } else {
            bus_view_direction_index = 0;
        }
        
        const char* dir_name = "No departures";
        if (data->direction_count > 0) {
            dir_name = data->directions[bus_view_direction_index];
        }
        
        // Add header label for direction
        lv_obj_t *header = lv_label_create(bus_list);
        lv_label_set_text(header, dir_name);
        lv_obj_set_style_text_font(header, &arimo_16, 0);
        lv_obj_set_style_text_color(header, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_pad_top(header, 5, 0);
        lv_obj_set_style_pad_bottom(header, 5, 0);
        
        for (int i = 0; i < data->count && i < MAX_DEPARTURES; i++) {
            if (!data->departures[i].valid) continue;
            
            // Filter by direction index
            if (data->direction_count > 0 && data->departures[i].direction_index != bus_view_direction_index) {
                continue;
            }
            
            // Create container for each departure item - simplified, no border
            lv_obj_t *item = lv_obj_create(bus_list);
            lv_obj_set_width(item, LV_PCT(100));
            lv_obj_set_height(item, 55);
            lv_obj_set_style_pad_all(item, 3, 0);
            lv_obj_set_style_pad_gap(item, 8, 0);
            lv_obj_set_style_bg_color(item, lv_color_hex(0x1A1A1A), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(item, 0, LV_PART_MAIN | LV_STATE_DEFAULT);  // No border
            lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
            
            // Line number with colored background
            lv_obj_t *line_container = lv_obj_create(item);
            lv_color_t line_color = get_line_color(data->departures[i].line);
            lv_obj_set_width(line_container, 55);
            lv_obj_set_height(line_container, 48);
            lv_obj_set_style_bg_color(line_container, line_color, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(line_container, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(line_container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(line_container, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_align(line_container, LV_ALIGN_LEFT_MID, 3, 0);
            lv_obj_clear_flag(line_container, LV_OBJ_FLAG_SCROLLABLE);
            
            lv_obj_t *line_label = lv_label_create(line_container);
            lv_label_set_text(line_label, data->departures[i].line);
            lv_obj_set_style_text_color(line_label, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(line_label, &arimo_24, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_center(line_label);
            
            // Destination/kierunek label (font 10) - between line and time
            lv_obj_t *destination_label = lv_label_create(item);
            lv_label_set_text(destination_label, data->departures[i].destination);
            lv_obj_set_style_text_color(destination_label, lv_color_hex(0xCCCCCC), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(destination_label, &arimo_14, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_width(destination_label, LV_PCT(50));
            lv_obj_align(destination_label, LV_ALIGN_LEFT_MID, 65, 0);
            
            // Minutes label
            lv_obj_t *minutes_label = lv_label_create(item);
            char minutes_text[64];
            
            if (data->departures[i].minutes_until == -1 && data->departures[i].valid) {
                 // Case where time is invalid/not synced
                 snprintf(minutes_text, sizeof(minutes_text), "--");
            } else if (data->departures[i].minutes_until <= 0) {
                snprintf(minutes_text, sizeof(minutes_text), "Now");
            } else if (data->departures[i].minutes_until > 60) {
                int hours = data->departures[i].minutes_until / 60;
                int mins = data->departures[i].minutes_until % 60;
                snprintf(minutes_text, sizeof(minutes_text), "%dh %02d'", hours, mins);
            } else {
                snprintf(minutes_text, sizeof(minutes_text), "%d min", data->departures[i].minutes_until);
            }
            
            // Append delay info if significant (> 1 min difference)
            if (abs(data->departures[i].delay_minutes) >= 1) {
                char delay_str[16];
                if (data->departures[i].delay_minutes > 0) {
                    snprintf(delay_str, sizeof(delay_str), " (+%d)", data->departures[i].delay_minutes);
                    strcat(minutes_text, delay_str);
                } else {
                    snprintf(delay_str, sizeof(delay_str), " (%d)", data->departures[i].delay_minutes);
                    strcat(minutes_text, delay_str);
                }
            }
            
            lv_label_set_text(minutes_label, minutes_text);
            lv_obj_set_style_text_font(minutes_label, &arimo_20, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_align(minutes_label, LV_ALIGN_RIGHT_MID, -5, 0);
            
            // Color logic for delay
            if (data->departures[i].delay_minutes >= 1) {
                // Late: Red
                lv_obj_set_style_text_color(minutes_label, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
            } else if (data->departures[i].delay_minutes <= -1) {
                // Early: Green
                lv_obj_set_style_text_color(minutes_label, lv_color_hex(0x00FF00), LV_PART_MAIN | LV_STATE_DEFAULT);
            } else {
                // On time: White
                lv_obj_set_style_text_color(minutes_label, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
            }
        }
    }
    
    // Update time in footer
    if (bus_time_label) {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%d.%m.%Y %H:%M", &timeinfo);
        lv_label_set_text(bus_time_label, time_str);
    }
    
    lv_port_sem_give();
}

/**
 * @brief Update train station screen
 */
static void update_train_screen(const struct view_data_train_station *data)
{
    if (!train_screen || !data) return;
    
    lv_port_sem_take();
    
    // Hide loading, show view
    lv_obj_add_flag(loading_cont, LV_OBJ_FLAG_HIDDEN);
    // Only show train view if we are not in selection mode
    if (lv_obj_has_flag(station_selection_cont, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(train_view_cont, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Update station name
    if (train_station_label) {
        lv_label_set_text(train_station_label, data->station_name[0] ? data->station_name : TRAIN_STATION_NAME);
    }
    
    // Update list
    if (train_list) {
        lv_obj_clean(train_list);
        
        for (int i = 0; i < data->count && i < MAX_DEPARTURES; i++) {
            if (!data->departures[i].valid) continue;
            
            // Row Item
            lv_obj_t *item = lv_obj_create(train_list);
            lv_obj_set_size(item, LV_PCT(100), 40);
            lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(item, 0, 0);
            lv_obj_set_style_border_side(item, LV_BORDER_SIDE_BOTTOM, 0);
            lv_obj_set_style_border_color(item, lv_color_hex(0x404040), 0);
            lv_obj_set_style_border_width(item, 1, 0); // Separator line
            lv_obj_set_style_pad_all(item, 0, 0);
            lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
            
            // 1. Train Badge (Line)
            lv_obj_t *badge = lv_obj_create(item);
            lv_obj_set_size(badge, 50, 30);
            lv_obj_align(badge, LV_ALIGN_LEFT_MID, 5, 0);
            lv_obj_set_style_border_width(badge, 0, 0);
            lv_obj_set_style_radius(badge, 4, 0);
            lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
            
            // Color logic:
            // S-Bahn (S): White bg, Black text (Requested: White background black letters)
            // RE, PE: White bg, Red text (Requested: White background red letters)
            // IC, ICN, VAE, EC: Red bg, White text (Requested: Red background White letters)
            
            bool is_sbahn = (strncmp(data->departures[i].line, "S", 1) == 0);
            bool is_re_pe = (strncmp(data->departures[i].line, "RE", 2) == 0 || 
                             strncmp(data->departures[i].line, "PE", 2) == 0);
            
            // Default assumes others are long distance (IC/EC etc) -> Red bg
            
            if (is_sbahn) {
                lv_obj_set_style_bg_color(badge, lv_color_white(), 0);
            } else if (is_re_pe) {
                lv_obj_set_style_bg_color(badge, lv_color_white(), 0);
            } else {
                // IC, ICN, VAE, EC etc -> Red
                lv_obj_set_style_bg_color(badge, lv_color_hex(0xEB0000), 0); // Red
            }
            
            lv_obj_t *lbl_line = lv_label_create(badge);
            lv_label_set_text(lbl_line, data->departures[i].line);
            lv_obj_center(lbl_line);
            
            if (is_sbahn) {
                 lv_obj_set_style_text_color(lbl_line, lv_color_black(), 0); // Black text
            } else if (is_re_pe) {
                 lv_obj_set_style_text_color(lbl_line, lv_color_hex(0xEB0000), 0); // Red text
            } else {
                 lv_obj_set_style_text_color(lbl_line, lv_color_white(), 0); // White text
            }
            lv_obj_set_style_text_font(lbl_line, &arimo_16, 0);
            
            // 1.5 Time
            lv_obj_t *lbl_time = lv_label_create(item);
            lv_label_set_text(lbl_time, data->departures[i].time_str);
            lv_obj_set_style_text_color(lbl_time, lv_color_white(), 0);
            lv_obj_set_style_text_font(lbl_time, &arimo_16, 0);
            lv_obj_align(lbl_time, LV_ALIGN_LEFT_MID, 65, 0);

            // 2. Destination & Via
            lv_obj_t *dest_cont = lv_obj_create(item);
            lv_obj_set_size(dest_cont, 220, 40);
            lv_obj_align(dest_cont, LV_ALIGN_LEFT_MID, 120, 0);
            lv_obj_set_style_bg_opa(dest_cont, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(dest_cont, 0, 0);
            lv_obj_set_style_pad_all(dest_cont, 0, 0);
            lv_obj_clear_flag(dest_cont, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t *lbl_dest = lv_label_create(dest_cont);
            lv_label_set_text(lbl_dest, data->departures[i].destination);
            lv_obj_set_style_text_color(lbl_dest, lv_color_white(), 0);
            lv_obj_set_style_text_font(lbl_dest, &arimo_16, 0);
            lv_obj_align(lbl_dest, LV_ALIGN_TOP_LEFT, 0, 2);
            lv_obj_set_width(lbl_dest, 220);
            lv_label_set_long_mode(lbl_dest, LV_LABEL_LONG_CLIP);
            
            if (data->departures[i].via[0]) {
                lv_obj_t *lbl_via = lv_label_create(dest_cont);
                lv_label_set_text(lbl_via, data->departures[i].via);
                lv_obj_set_style_text_color(lbl_via, lv_color_hex(0xAAAAAA), 0); // Grey
                lv_obj_set_style_text_font(lbl_via, &arimo_14, 0);
                lv_obj_align(lbl_via, LV_ALIGN_BOTTOM_LEFT, 0, -2);
                lv_obj_set_width(lbl_via, 220);
                lv_label_set_long_mode(lbl_via, LV_LABEL_LONG_SCROLL_CIRCULAR);
            }

            // 3. Platform
            lv_obj_t *lbl_plat = lv_label_create(item);
            lv_label_set_text(lbl_plat, data->departures[i].platform);
            lv_obj_set_style_text_color(lbl_plat, lv_color_white(), 0);
            lv_obj_align(lbl_plat, LV_ALIGN_RIGHT_MID, -80, 0);
            
            // 4. Info - Delay
            if (data->departures[i].delay_minutes > 0) {
                 lv_obj_t *lbl_info = lv_label_create(item);
                 char buf[32];
                 snprintf(buf, sizeof(buf), "approx. +%d'", data->departures[i].delay_minutes);
                 lv_label_set_text(lbl_info, buf);
                 lv_obj_set_style_text_color(lbl_info, lv_color_hex(0xFFD700), 0); // Yellow
                 lv_obj_align(lbl_info, LV_ALIGN_RIGHT_MID, -10, 0);
            }
        }
    }
    
    lv_port_sem_give();
}

/**
 * @brief Update settings screen
 */
static void update_settings_screen(const struct view_data_settings *data)
{
    if (!settings_screen || !data) return;
    
    lv_port_sem_take();
    
    // Update WiFi status
    if (wifi_status_label) {
        char wifi_status[64];
        if (data->wifi_status.is_connected) {
            snprintf(wifi_status, sizeof(wifi_status), "WiFi: %s", data->wifi_status.ssid);
        } else {
            strcpy(wifi_status, "WiFi: Not connected");
        }
        lv_label_set_text(wifi_status_label, wifi_status);
    }
    
    // Update IP address
    if (ip_label) {
        char ip_text[32];
        snprintf(ip_text, sizeof(ip_text), "IP: %s", data->ip_address);
        lv_label_set_text(ip_label, ip_text);
    }
    
    // Update API status
    if (api_status_label) {
        lv_label_set_text(api_status_label, data->api_status ? "API: OK" : "API: Error");
    }
    
    // Update brightness slider
    if (brightness_slider) {
        lv_slider_set_value(brightness_slider, data->brightness, LV_ANIM_OFF);
        char buf[32];
        snprintf(buf, sizeof(buf), "Brightness: %d%%", data->brightness);
        if (brightness_label) {
            lv_label_set_text(brightness_label, buf);
        }
    }
    
    lv_port_sem_give();
}

/**
 * @brief Create bus countdown screen
 */
static void create_bus_screen(lv_obj_t *parent)
{
    bus_screen = lv_obj_create(parent);
    lv_obj_set_size(bus_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(bus_screen, 0, 0);  // No padding
    lv_obj_set_style_border_width(bus_screen, 0, 0); // No border
    lv_obj_set_style_bg_color(bus_screen, lv_color_hex(0x1A1A1A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(bus_screen, LV_OBJ_FLAG_SCROLLABLE);
    
    // --- 1. Selection Screen Container ---
    bus_selection_cont = lv_obj_create(bus_screen);
    lv_obj_set_size(bus_selection_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(bus_selection_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bus_selection_cont, 0, 0);
    lv_obj_set_style_pad_all(bus_selection_cont, 10, 0);
    lv_obj_set_flex_flow(bus_selection_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(bus_selection_cont, 10, 0);
    
    lv_obj_t *sel_label = lv_label_create(bus_selection_cont);
    lv_label_set_text(sel_label, "Select stop:");
    lv_obj_set_style_text_font(sel_label, &arimo_20, 0);
    lv_obj_set_style_text_color(sel_label, lv_color_white(), 0);
    
    for (int i = 0; i < sizeof(predefined_bus_stops)/sizeof(predefined_bus_stops[0]); i++) {
        lv_obj_t *btn = lv_btn_create(bus_selection_cont);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_height(btn, 50);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x008000), 0); // Green
        lv_obj_add_event_cb(btn, bus_stop_select_cb, LV_EVENT_CLICKED, (void*)&predefined_bus_stops[i]);
        
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, predefined_bus_stops[i].name);
        lv_obj_set_style_text_font(lbl, &arimo_20, 0);
        lv_obj_center(lbl);
    }
    
    // --- 2. Loading Screen Container ---
    bus_loading_cont = lv_obj_create(bus_screen);
    lv_obj_set_size(bus_loading_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(bus_loading_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bus_loading_cont, 0, 0);
    lv_obj_add_flag(bus_loading_cont, LV_OBJ_FLAG_HIDDEN);
    
    lv_obj_t *loading_lbl = lv_label_create(bus_loading_cont);
    lv_label_set_text(loading_lbl, "Loading data...");
    lv_obj_set_style_text_font(loading_lbl, &arimo_20, 0);
    lv_obj_set_style_text_color(loading_lbl, lv_color_white(), 0);
    lv_obj_center(loading_lbl);
    
    // --- 3. Bus View Container (Existing View) ---
    bus_view_cont = lv_obj_create(bus_screen);
    lv_obj_set_size(bus_view_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(bus_view_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bus_view_cont, 0, 0);
    lv_obj_set_style_pad_all(bus_view_cont, 0, 0);
    lv_obj_add_flag(bus_view_cont, LV_OBJ_FLAG_HIDDEN);
    
    // Stop name as table header (no separate frame)
    bus_stop_label = lv_label_create(bus_view_cont);
    lv_label_set_text(bus_stop_label, BUS_STOP_NAME);
    lv_obj_set_style_text_font(bus_stop_label, &arimo_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bus_stop_label, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(bus_stop_label, LV_ALIGN_TOP_LEFT, 10, 5);
    
    // Refresh button (FAB) - moved to top right
    bus_refresh_btn = lv_btn_create(bus_view_cont);
    lv_obj_set_size(bus_refresh_btn, 45, 45);
    lv_obj_align(bus_refresh_btn, LV_ALIGN_TOP_RIGHT, -5, 5);
    lv_obj_add_event_cb(bus_refresh_btn, bus_refresh_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(bus_refresh_btn, lv_color_hex(0x008000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *refresh_label = lv_label_create(bus_refresh_btn);
    lv_label_set_text(refresh_label, "Ref");
    lv_obj_set_style_text_font(refresh_label, &arimo_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(refresh_label, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(refresh_label);
    
    // Next button - left of refresh
    bus_next_btn = lv_btn_create(bus_view_cont);
    lv_obj_set_size(bus_next_btn, 45, 45);
    lv_obj_align(bus_next_btn, LV_ALIGN_TOP_RIGHT, -55, 5); // 5px padding + 45px width + 5px gap
    lv_obj_add_event_cb(bus_next_btn, next_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(bus_next_btn, lv_color_hex(0x444444), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *next_label = lv_label_create(bus_next_btn);
    lv_label_set_text(next_label, "→"); 
    lv_obj_set_style_text_font(next_label, &arimo_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(next_label, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(next_label);

    // Prev button - left of next
    bus_prev_btn = lv_btn_create(bus_view_cont);
    lv_obj_set_size(bus_prev_btn, 45, 45);
    lv_obj_align(bus_prev_btn, LV_ALIGN_TOP_RIGHT, -105, 5); // 55 + 45 + 5
    lv_obj_add_event_cb(bus_prev_btn, prev_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(bus_prev_btn, lv_color_hex(0x444444), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *prev_label = lv_label_create(bus_prev_btn);
    lv_label_set_text(prev_label, "←"); 
    lv_obj_set_style_text_font(prev_label, &arimo_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(prev_label, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(prev_label);
    
    // Back button - left of prev
    bus_back_btn = lv_btn_create(bus_view_cont);
    lv_obj_set_size(bus_back_btn, 45, 45);
    lv_obj_align(bus_back_btn, LV_ALIGN_TOP_RIGHT, -155, 5); // 105 + 45 + 5
    lv_obj_add_event_cb(bus_back_btn, bus_back_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(bus_back_btn, lv_color_hex(0x444444), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *back_label = lv_label_create(bus_back_btn);
    lv_label_set_text(back_label, "x"); 
    lv_obj_set_style_text_font(back_label, &arimo_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(back_label, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(back_label);
    
    // Departure list - no border, simplified
    // Increase height to allow more items visible without scrolling issue
    bus_list = lv_obj_create(bus_view_cont);
    lv_obj_set_size(bus_list, LV_PCT(100), LV_PCT(85)); 
    lv_obj_align(bus_list, LV_ALIGN_TOP_MID, 0, 45); // Moved up slightly
    lv_obj_set_style_bg_color(bus_list, lv_color_hex(0x1A1A1A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bus_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(bus_list, 0, 0); // Remove padding to use full width
    lv_obj_set_style_pad_gap(bus_list, 3, 0);
    lv_obj_set_flex_flow(bus_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bus_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(bus_list, LV_OBJ_FLAG_SCROLLABLE); // Enable scrolling
    lv_obj_set_scrollbar_mode(bus_list, LV_SCROLLBAR_MODE_ACTIVE); // Always show scrollbar when scrolling
    
    // Footer with current time
    bus_time_label = lv_label_create(bus_view_cont);
    lv_obj_set_style_text_color(bus_time_label, lv_color_hex(0x808080), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bus_time_label, &arimo_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(bus_time_label, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_label_set_text(bus_time_label, "--:--");
    
    // Status label (hidden by default, shown only on error)
    bus_status_label = lv_label_create(bus_view_cont);
    lv_label_set_text(bus_status_label, "");
    lv_obj_set_style_text_color(bus_status_label, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(bus_status_label, LV_ALIGN_TOP_LEFT, 10, 30);
}

/**
 * @brief Create train station screen
 */
static void create_train_screen(lv_obj_t *parent)
{
    train_screen = lv_obj_create(parent);
    lv_obj_set_size(train_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(train_screen, 0, 0);
    lv_obj_set_style_border_width(train_screen, 0, 0); // No border
    lv_obj_set_style_bg_color(train_screen, lv_color_hex(0x0F163F), LV_PART_MAIN | LV_STATE_DEFAULT); // SBB Blue
    
    // --- 1. Selection Screen Container ---
    station_selection_cont = lv_obj_create(train_screen);
    lv_obj_set_size(station_selection_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(station_selection_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(station_selection_cont, 0, 0);
    lv_obj_set_style_pad_all(station_selection_cont, 10, 0);
    lv_obj_set_flex_flow(station_selection_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(station_selection_cont, 10, 0);
    
    lv_obj_t *sel_label = lv_label_create(station_selection_cont);
    lv_label_set_text(sel_label, "Select station:");
    lv_obj_set_style_text_font(sel_label, &arimo_20, 0);
    lv_obj_set_style_text_color(sel_label, lv_color_white(), 0);
    
    for (int i = 0; i < sizeof(predefined_stations)/sizeof(predefined_stations[0]); i++) {
        lv_obj_t *btn = lv_btn_create(station_selection_cont);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_height(btn, 50);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xEB0000), 0); // SBB Red
        lv_obj_add_event_cb(btn, station_select_cb, LV_EVENT_CLICKED, (void*)&predefined_stations[i]);
        
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, predefined_stations[i].name);
        lv_obj_set_style_text_font(lbl, &arimo_20, 0);
        lv_obj_center(lbl);
    }
    
    // --- 2. Loading Screen Container ---
    loading_cont = lv_obj_create(train_screen);
    lv_obj_set_size(loading_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(loading_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(loading_cont, 0, 0);
    lv_obj_add_flag(loading_cont, LV_OBJ_FLAG_HIDDEN);
    
    lv_obj_t *loading_lbl = lv_label_create(loading_cont);
    lv_label_set_text(loading_lbl, "Loading data...");
    lv_obj_set_style_text_font(loading_lbl, &arimo_20, 0);
    lv_obj_set_style_text_color(loading_lbl, lv_color_white(), 0);
    lv_obj_center(loading_lbl);
    
    // --- 3. Train View Container (Existing View) ---
    train_view_cont = lv_obj_create(train_screen);
    lv_obj_set_size(train_view_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(train_view_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(train_view_cont, 0, 0);
    lv_obj_set_style_pad_all(train_view_cont, 0, 0);
    lv_obj_add_flag(train_view_cont, LV_OBJ_FLAG_HIDDEN);

    // Station name label (Header)
    train_station_label = lv_label_create(train_view_cont);
    lv_label_set_text(train_station_label, TRAIN_STATION_NAME);
    lv_obj_set_style_text_font(train_station_label, &arimo_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(train_station_label, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(train_station_label, LV_ALIGN_TOP_LEFT, 10, 10);
    
    // Refresh button (FAB) - top right
    train_refresh_btn = lv_btn_create(train_view_cont);
    lv_obj_set_size(train_refresh_btn, 40, 40);
    lv_obj_align(train_refresh_btn, LV_ALIGN_TOP_RIGHT, -10, 5);
    lv_obj_add_event_cb(train_refresh_btn, train_refresh_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(train_refresh_btn, lv_color_hex(0xEB0000), LV_PART_MAIN | LV_STATE_DEFAULT); // SBB Red
    lv_obj_t *refresh_label = lv_label_create(train_refresh_btn);
    lv_label_set_text(refresh_label, "Ref");
    lv_obj_set_style_text_font(refresh_label, &arimo_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(refresh_label, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(refresh_label);
    
    // Back button - left of refresh
    train_back_btn = lv_btn_create(train_view_cont);
    lv_obj_set_size(train_back_btn, 40, 40);
    lv_obj_align(train_back_btn, LV_ALIGN_TOP_RIGHT, -60, 5);
    lv_obj_add_event_cb(train_back_btn, train_back_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(train_back_btn, lv_color_hex(0x444444), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *back_label = lv_label_create(train_back_btn);
    lv_label_set_text(back_label, "←");
    lv_obj_set_style_text_font(back_label, &arimo_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(back_label, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(back_label);
    
    // Column Headers: To | Platform | Info
    lv_obj_t *header_cont = lv_obj_create(train_view_cont);
    lv_obj_set_size(header_cont, LV_PCT(100), 30);
    lv_obj_align(header_cont, LV_ALIGN_TOP_MID, 0, 45);
    lv_obj_set_style_bg_opa(header_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header_cont, 0, 0);
    lv_obj_set_style_pad_all(header_cont, 0, 0);
    
    lv_obj_t *lbl_nach = lv_label_create(header_cont);
    lv_label_set_text(lbl_nach, "To");
    lv_obj_set_style_text_color(lbl_nach, lv_color_white(), 0);
    lv_obj_align(lbl_nach, LV_ALIGN_LEFT_MID, 120, 0); // Offset for train number and time
    
    lv_obj_t *lbl_gleis = lv_label_create(header_cont);
    lv_label_set_text(lbl_gleis, "Plat.");
    lv_obj_set_style_text_color(lbl_gleis, lv_color_white(), 0);
    lv_obj_align(lbl_gleis, LV_ALIGN_RIGHT_MID, -100, 0);
    
    lv_obj_t *lbl_hinweis = lv_label_create(header_cont);
    lv_label_set_text(lbl_hinweis, "Info");
    lv_obj_set_style_text_color(lbl_hinweis, lv_color_white(), 0);
    lv_obj_align(lbl_hinweis, LV_ALIGN_RIGHT_MID, -10, 0);

    // Train List Container
    train_list = lv_obj_create(train_view_cont);
    lv_obj_set_size(train_list, LV_PCT(100), LV_PCT(80)); // Use more vertical space since footer is gone
    lv_obj_align(train_list, LV_ALIGN_TOP_MID, 0, 75);
    lv_obj_set_style_bg_opa(train_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(train_list, 0, 0);
    lv_obj_set_style_pad_all(train_list, 0, 0);
    lv_obj_set_flex_flow(train_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(train_list, 2, 0);
}

/**
 * @brief WiFi Settings Button Callback
 */
static void wifi_btn_cb(lv_event_t *e)
{
    // Hide main settings, show WiFi view
    lv_obj_add_flag(settings_main_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(wifi_view_cont, LV_OBJ_FLAG_HIDDEN);
    
    // Request scan immediately
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_LIST_REQ, NULL, 0, portMAX_DELAY);
}

/**
 * @brief Back from WiFi list
 */
static void wifi_back_btn_cb(lv_event_t *e)
{
    lv_obj_add_flag(wifi_view_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(settings_main_cont, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief WiFi Scan/Refresh Button Callback
 */
static void wifi_scan_btn_cb(lv_event_t *e)
{
    lv_obj_clean(wifi_list);
    lv_obj_t *loading = lv_label_create(wifi_list);
    lv_label_set_text(loading, "Scanning...");
    lv_obj_set_style_text_color(loading, lv_color_white(), 0);
    
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_LIST_REQ, NULL, 0, portMAX_DELAY);
}

/**
 * @brief WiFi List Item Selection Callback
 */
static void wifi_list_item_cb(lv_event_t *e)
{
    struct view_data_wifi_item *item = (struct view_data_wifi_item *)lv_event_get_user_data(e);
    if (!item) return;
    
    // Save SSID
    strlcpy(current_wifi_ssid, item->ssid, sizeof(current_wifi_ssid));
    
    if (item->auth_mode) {
        // Needs password
        lv_textarea_set_text(wifi_password_ta, "");
        lv_obj_add_flag(wifi_view_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(wifi_password_view_cont, LV_OBJ_FLAG_HIDDEN);
    } else {
        // Open network, connect immediately
        struct view_data_wifi_config cfg = {0};
        strlcpy(cfg.ssid, current_wifi_ssid, sizeof(cfg.ssid));
        cfg.have_password = false;
        
        esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CONNECT, &cfg, sizeof(cfg), portMAX_DELAY);
        
        // Go back to main settings
        lv_obj_add_flag(wifi_view_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(settings_main_cont, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief Back from Password Screen
 */
static void wifi_password_back_btn_cb(lv_event_t *e)
{
    lv_obj_add_flag(wifi_password_view_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(wifi_view_cont, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Connect Button Callback
 */
static void wifi_connect_btn_cb(lv_event_t *e)
{
    struct view_data_wifi_config cfg = {0};
    strlcpy(cfg.ssid, current_wifi_ssid, sizeof(cfg.ssid));
    
    const char *password = lv_textarea_get_text(wifi_password_ta);
    if (strlen(password) > 0) {
        strlcpy((char*)cfg.password, password, sizeof(cfg.password));
        cfg.have_password = true;
    } else {
        cfg.have_password = false;
    }
    
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CONNECT, &cfg, sizeof(cfg), portMAX_DELAY);
    
    // Go back to main settings
    lv_obj_add_flag(wifi_password_view_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(settings_main_cont, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Keyboard Event Callback
 */
static void wifi_keyboard_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        if(code == LV_EVENT_READY) {
            wifi_connect_btn_cb(NULL);
        } else {
            wifi_password_back_btn_cb(NULL);
        }
    }
}

/**
 * @brief Update WiFi List
 */
static void update_wifi_list(const struct view_data_wifi_list *list)
{
    if (!wifi_list || !list) return;
    
    lv_port_sem_take();
    lv_obj_clean(wifi_list);
    
    for (int i = 0; i < list->cnt; i++) {
        lv_obj_t *btn = lv_btn_create(wifi_list);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_height(btn, 50);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x333333), 0);
        
        // We need to copy the item data because the event data is temporary
        struct view_data_wifi_item *item_copy = (struct view_data_wifi_item *)lv_mem_alloc(sizeof(struct view_data_wifi_item));
        if (item_copy) {
            memcpy(item_copy, &list->aps[i], sizeof(struct view_data_wifi_item));
            lv_obj_add_event_cb(btn, wifi_list_item_cb, LV_EVENT_CLICKED, item_copy);
        }
        
        lv_obj_t *lbl_ssid = lv_label_create(btn);
        lv_label_set_text(lbl_ssid, list->aps[i].ssid);
        lv_obj_align(lbl_ssid, LV_ALIGN_LEFT_MID, 10, 0);
        lv_obj_set_style_text_font(lbl_ssid, &arimo_20, 0);
        
        lv_obj_t *lbl_rssi = lv_label_create(btn);
        lv_label_set_text_fmt(lbl_rssi, "%d dBm %s", list->aps[i].rssi, list->aps[i].auth_mode ? "Lock" : "");
        lv_obj_align(lbl_rssi, LV_ALIGN_RIGHT_MID, -10, 0);
        lv_obj_set_style_text_font(lbl_rssi, &arimo_14, 0);
    }
    lv_port_sem_give();
}

/**
 * @brief Create WiFi Screen
 */
static void create_wifi_screen(lv_obj_t *parent)
{
    wifi_view_cont = lv_obj_create(parent);
    lv_obj_set_size(wifi_view_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(wifi_view_cont, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(wifi_view_cont, 0, 0);
    lv_obj_set_style_pad_all(wifi_view_cont, 0, 0);
    lv_obj_add_flag(wifi_view_cont, LV_OBJ_FLAG_HIDDEN);
    
    // Header
    lv_obj_t *header = lv_obj_create(wifi_view_cont);
    lv_obj_set_size(header, LV_PCT(100), 50);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x202020), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    
    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 40, 40);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_add_event_cb(back_btn, wifi_back_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, "<");
    lv_obj_center(back_lbl);
    
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "WiFi Networks");
    lv_obj_center(title);
    
    lv_obj_t *scan_btn = lv_btn_create(header);
    lv_obj_set_size(scan_btn, 40, 40);
    lv_obj_align(scan_btn, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_add_event_cb(scan_btn, wifi_scan_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *scan_lbl = lv_label_create(scan_btn);
    lv_label_set_text(scan_lbl, "R");
    lv_obj_center(scan_lbl);
    
    // List
    wifi_list = lv_obj_create(wifi_view_cont);
    lv_obj_set_size(wifi_list, LV_PCT(100), LV_PCT(100));
    lv_obj_set_y(wifi_list, 50);
    lv_obj_set_height(wifi_list, lv_pct(100)); // minus header? lvgl handles overflow or we use flex
    lv_obj_set_style_bg_color(wifi_list, lv_color_black(), 0);
    lv_obj_set_style_border_width(wifi_list, 0, 0);
    lv_obj_set_flex_flow(wifi_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(wifi_list, 5, 0);
}

/**
 * @brief Create WiFi Password Screen
 */
static void create_wifi_password_screen(lv_obj_t *parent)
{
    wifi_password_view_cont = lv_obj_create(parent);
    lv_obj_set_size(wifi_password_view_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(wifi_password_view_cont, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(wifi_password_view_cont, 0, 0);
    lv_obj_add_flag(wifi_password_view_cont, LV_OBJ_FLAG_HIDDEN);
    
    // Title
    lv_obj_t *lbl = lv_label_create(wifi_password_view_cont);
    lv_label_set_text(lbl, "Enter Password:");
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 10);
    
    // Text area
    wifi_password_ta = lv_textarea_create(wifi_password_view_cont);
    lv_textarea_set_one_line(wifi_password_ta, true);
    lv_textarea_set_password_mode(wifi_password_ta, true);
    lv_obj_set_width(wifi_password_ta, LV_PCT(90));
    lv_obj_align(wifi_password_ta, LV_ALIGN_TOP_MID, 0, 40);
    
    // Connect Button
    lv_obj_t *btn = lv_btn_create(wifi_password_view_cont);
    lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -10, 80);
    lv_obj_add_event_cb(btn, wifi_connect_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "Connect");
    lv_obj_center(btn_lbl);
    
    // Cancel Button
    lv_obj_t *cancel_btn = lv_btn_create(wifi_password_view_cont);
    lv_obj_align(cancel_btn, LV_ALIGN_TOP_LEFT, 10, 80);
    lv_obj_add_event_cb(cancel_btn, wifi_password_back_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_center(cancel_lbl);

    // Keyboard
    wifi_keyboard = lv_keyboard_create(wifi_password_view_cont);
    lv_keyboard_set_textarea(wifi_keyboard, wifi_password_ta);
    lv_obj_add_event_cb(wifi_keyboard, wifi_keyboard_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

/**
 * @brief Create settings screen
 */
static void create_settings_screen(lv_obj_t *parent)
{
    settings_screen = lv_obj_create(parent);
    lv_obj_set_size(settings_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(settings_screen, 0, 0);
    lv_obj_set_style_bg_color(settings_screen, lv_color_black(), 0);

    // Main Settings Container
    settings_main_cont = lv_obj_create(settings_screen);
    lv_obj_set_size(settings_main_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(settings_main_cont, 10, 0);
    lv_obj_set_style_bg_opa(settings_main_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(settings_main_cont, 0, 0);

    // Create WiFi Screens (Hidden)
    create_wifi_screen(settings_screen);
    create_wifi_password_screen(settings_screen);
    
    // WiFi status
    wifi_status_label = lv_label_create(settings_main_cont);
    lv_label_set_text(wifi_status_label, "WiFi: Not connected");
    lv_obj_align(wifi_status_label, LV_ALIGN_TOP_LEFT, 10, 10);
    
    // IP address
    ip_label = lv_label_create(settings_main_cont);
    lv_label_set_text(ip_label, "IP: Not available");
    lv_obj_align(ip_label, LV_ALIGN_TOP_LEFT, 10, 40);
    
    // API status
    api_status_label = lv_label_create(settings_main_cont);
    lv_label_set_text(api_status_label, "API: Unknown");
    lv_obj_align(api_status_label, LV_ALIGN_TOP_LEFT, 10, 70);

    // WiFi Config Button
    lv_obj_t *btn_wifi = lv_btn_create(settings_main_cont);
    lv_obj_set_size(btn_wifi, 100, 40);
    lv_obj_align(btn_wifi, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_add_event_cb(btn_wifi, wifi_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_wifi = lv_label_create(btn_wifi);
    lv_label_set_text(lbl_wifi, "WiFi");
    lv_obj_center(lbl_wifi);
    
    // Brightness slider
    brightness_label = lv_label_create(settings_main_cont);
    lv_label_set_text(brightness_label, "Brightness: 50%");
    lv_obj_align(brightness_label, LV_ALIGN_TOP_LEFT, 10, 110);
    
    brightness_slider = lv_slider_create(settings_main_cont);
    lv_obj_set_size(brightness_slider, LV_PCT(80), 20);
    lv_obj_align(brightness_slider, LV_ALIGN_TOP_LEFT, 10, 140);
    lv_slider_set_range(brightness_slider, 0, 100);
    lv_slider_set_value(brightness_slider, 50, LV_ANIM_OFF);
    lv_obj_add_event_cb(brightness_slider, brightness_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    
    // Sleep timeout slider
    sleep_label = lv_label_create(settings_main_cont);
    lv_label_set_text(sleep_label, "Timeout: Always On");
    lv_obj_align(sleep_label, LV_ALIGN_TOP_LEFT, 10, 180);
    
    sleep_slider = lv_slider_create(settings_main_cont);
    lv_obj_set_size(sleep_slider, LV_PCT(80), 20);
    lv_obj_align(sleep_slider, LV_ALIGN_TOP_LEFT, 10, 210);
    lv_slider_set_range(sleep_slider, 0, 100);
    lv_slider_set_value(sleep_slider, 0, LV_ANIM_OFF);
    lv_obj_add_event_cb(sleep_slider, sleep_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

/**
 * @brief Time update handler for footer
 */
static void time_update_handler(void* handler_args, esp_event_base_t base, 
                                int32_t id, void* event_data)
{
    if (id == VIEW_EVENT_TIME && bus_time_label) {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%d.%m.%Y %H:%M", &timeinfo);
        lv_label_set_text(bus_time_label, time_str);
    }
}

/**
 * @brief View event handler
 */
static void view_event_handler(void* handler_args, esp_event_base_t base, 
                               int32_t id, void* event_data)
{
    switch (id) {
        case VIEW_EVENT_BUS_COUNTDOWN_UPDATE: {
            const struct view_data_bus_countdown *data = 
                (const struct view_data_bus_countdown *)event_data;
            update_bus_screen(data);
            break;
        }
        case VIEW_EVENT_TRAIN_STATION_UPDATE: {
            const struct view_data_train_station *data = 
                (const struct view_data_train_station *)event_data;
            update_train_screen(data);
            break;
        }
        case VIEW_EVENT_SETTINGS_UPDATE: {
            const struct view_data_settings *data = 
                (const struct view_data_settings *)event_data;
            update_settings_screen(data);
            break;
        }
        case VIEW_EVENT_TRANSPORT_REFRESH: {
            transport_data_force_refresh();
            break;
        }
        case VIEW_EVENT_BUS_REFRESH: {
            transport_data_refresh_bus();
            break;
        }
        case VIEW_EVENT_TRAIN_REFRESH: {
            transport_data_refresh_train();
            break;
        }
        case VIEW_EVENT_WIFI_LIST: {
            const struct view_data_wifi_list *list = (const struct view_data_wifi_list *)event_data;
            update_wifi_list(list);
            break;
        }
        case VIEW_EVENT_WIFI_CONNECT_RET: {
             // struct view_data_wifi_connet_ret_msg *msg = (struct view_data_wifi_connet_ret_msg *)event_data;
             // TODO: Show result to user
             break;
        }
        default:
            break;
    }
}

int indicator_view_init(void)
{
    ESP_LOGI(TAG, "Initializing view...");
    
    // Create tabview for navigation - hidden top bar (full screen)
    tabview = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, 0);
    lv_obj_add_event_cb(tabview, tabview_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Add full blue border
    lv_obj_set_style_border_width(tabview, 5, LV_PART_MAIN);
    lv_obj_set_style_border_color(tabview, lv_color_hex(0x0000FF), LV_PART_MAIN);
    lv_obj_set_style_border_side(tabview, LV_BORDER_SIDE_FULL, LV_PART_MAIN);
    
    // Remove default background and outline (focus ring)
    lv_obj_set_style_bg_color(tabview, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_outline_width(tabview, 0, LV_PART_MAIN | LV_STATE_ANY);
    lv_obj_clear_flag(tabview, LV_OBJ_FLAG_SCROLLABLE);

    // Get content object and remove padding/borders
    lv_obj_t * content = lv_tabview_get_content(tabview);
    if (content) {
        lv_obj_set_style_pad_all(content, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
    }
    
    // Create tabs
    lv_obj_t *bus_tab = lv_tabview_add_tab(tabview, "Bus");
    lv_obj_t *train_tab = lv_tabview_add_tab(tabview, "Train");
    lv_obj_t *settings_tab = lv_tabview_add_tab(tabview, "Settings");
    
    // Remove padding from tabs and ensure no focus ring
    lv_obj_set_style_pad_all(bus_tab, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(bus_tab, 0, LV_PART_MAIN | LV_STATE_ANY);
    
    lv_obj_set_style_pad_all(train_tab, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(train_tab, 0, LV_PART_MAIN | LV_STATE_ANY);
    
    lv_obj_set_style_pad_all(settings_tab, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(settings_tab, 0, LV_PART_MAIN | LV_STATE_ANY);
    
    // Create screens
    create_bus_screen(bus_tab);
    create_train_screen(train_tab);
    create_settings_screen(settings_tab);
    
    // Register event handler
    esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE,
                                            VIEW_EVENT_BUS_COUNTDOWN_UPDATE,
                                            view_event_handler, NULL, NULL);
    esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE,
                                            VIEW_EVENT_TRAIN_STATION_UPDATE,
                                            view_event_handler, NULL, NULL);
    esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE,
                                            VIEW_EVENT_SETTINGS_UPDATE,
                                            view_event_handler, NULL, NULL);
    esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE,
                                            VIEW_EVENT_TRANSPORT_REFRESH,
                                            view_event_handler, NULL, NULL);
    esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE,
                                            VIEW_EVENT_BUS_REFRESH,
                                            view_event_handler, NULL, NULL);
    esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE,
                                            VIEW_EVENT_TRAIN_REFRESH,
                                            view_event_handler, NULL, NULL);
    esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE,
                                            VIEW_EVENT_WIFI_LIST,
                                            view_event_handler, NULL, NULL);
    esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE,
                                            VIEW_EVENT_WIFI_CONNECT_RET,
                                            view_event_handler, NULL, NULL);
    // Register time event handler for footer updates
    esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE,
                                            VIEW_EVENT_TIME,
                                            time_update_handler, NULL, NULL);
                                            
    // Create timer for live updates (every 5 seconds)
    lv_timer_create(live_update_timer_cb, 5000, NULL);
    
    ESP_LOGI(TAG, "View initialized");
    return 0;
}
