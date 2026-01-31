#include "indicator_view.h"
#include "view_data.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_event.h"
#include "network_manager.h"
#include "transport_data.h"
#include "indicator_time.h"  // For time updates
#include "indicator_display.h"  // For display config
#include "sbb_clock.h"
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
static lv_obj_t *clock_screen = NULL;  /* first tab: SBB clock */
static sbb_clock_t clock_widget = NULL;
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
static void display_apply_btn_cb(lv_event_t *e);
static void time_update_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data);
static void view_event_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data);
static void tabview_event_cb(lv_event_t *e);
static void update_station_buttons_availability(void);
static void loading_arc_anim_cb(void *var, int32_t value);

typedef struct {
    const char *name;
    const char *id;
} station_t;

/* (bus/tram) – ID from transport.opendata.ch/v1/locations?query=Aarau */
static const station_t predefined_bus_stops[] = {
    {"Aarau, Gais", "8590142"},
    {"Aarau Bahnhof", "8502996"},
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

// Train details screen widgets
static lv_obj_t *train_details_screen = NULL;
static lv_obj_t *train_details_loading = NULL;
static lv_obj_t *train_details_view = NULL;
static lv_obj_t *train_details_list = NULL;
static lv_obj_t *train_details_title = NULL;
static lv_obj_t *train_details_cap1 = NULL;
static lv_obj_t *train_details_cap2 = NULL;
static lv_obj_t *train_details_close_btn = NULL;

// Bus details screen widgets
static lv_obj_t *bus_details_screen = NULL;
static lv_obj_t *bus_details_loading = NULL;
static lv_obj_t *bus_details_view = NULL;
static lv_obj_t *bus_details_list = NULL;
static lv_obj_t *bus_details_title = NULL;
static lv_obj_t *bus_details_close_btn = NULL;

/* – ID from transport.opendata.ch */
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

// Flag to prevent slider updates during user interaction
static bool display_settings_user_editing = false;

// Settings / Display screen widgets
static lv_obj_t *settings_main_cont = NULL;
static lv_obj_t *display_settings_cont = NULL;
static lv_obj_t *display_apply_btn = NULL;
// WiFi Screen widgets
static lv_obj_t *wifi_view_cont = NULL;
static lv_obj_t *wifi_netinfo_cont = NULL;  /* panel with IP, DNS, RSSI, etc. */
static lv_obj_t *wifi_list = NULL;
static lv_obj_t *wifi_password_view_cont = NULL;
static lv_obj_t *wifi_password_ta = NULL;
static lv_obj_t *wifi_password_ssid_label = NULL;  /* left panel SSID, updated when shown */
static lv_obj_t *wifi_keyboard = NULL;
static char current_wifi_ssid[32];

// WiFi Saved Networks widgets
static lv_obj_t *wifi_saved_cont = NULL;
static lv_obj_t *wifi_saved_list = NULL;

// WiFi Add Network widgets
static lv_obj_t *wifi_add_cont = NULL;
static lv_obj_t *wifi_add_ssid_ta = NULL;
static lv_obj_t *wifi_add_password_ta = NULL;
static lv_obj_t *wifi_add_password_checkbox = NULL;
static lv_obj_t *wifi_add_keyboard = NULL;

// System Info Screen widgets
static lv_obj_t *sysinfo_cont = NULL;
static lv_obj_t *sysinfo_chip_label = NULL;
static lv_obj_t *sysinfo_ram_label = NULL;
static lv_obj_t *sysinfo_ram_min_label = NULL;
static lv_obj_t *sysinfo_psram_label = NULL;
static lv_obj_t *sysinfo_uptime_label = NULL;
static lv_obj_t *sysinfo_versions_label = NULL;
static lv_obj_t *sysinfo_author_label = NULL;
static lv_obj_t *sysinfo_build_label = NULL;

// Forward declarations
static void update_bus_screen(const struct view_data_bus_countdown *data);
static void update_train_screen(const struct view_data_train_station *data);
static void update_train_details_screen(const struct view_data_train_details *data);
static void update_bus_details_screen(const struct view_data_bus_details *data);
static void update_settings_screen(const struct view_data_settings *data);
static void update_display_settings(const struct view_data_display *cfg);
static void update_wifi_list(const struct view_data_wifi_list *list);
static void update_wifi_network_info(void);
static void create_bus_screen(lv_obj_t *parent);
static void create_bus_details_screen(lv_obj_t *parent);
static void create_train_screen(lv_obj_t *parent);
static void create_train_details_screen(lv_obj_t *parent);
static void create_settings_screen(lv_obj_t *parent);
static void create_wifi_screen(lv_obj_t *parent);
static void create_wifi_password_screen(lv_obj_t *parent);
static void create_wifi_saved_screen(lv_obj_t *parent);
static void create_wifi_add_screen(lv_obj_t *parent);
static void update_wifi_saved_list(const struct view_data_wifi_saved_list *list);
static void create_sysinfo_screen(lv_obj_t *parent);
static void update_sysinfo_screen(const struct view_data_system_info *info);
static void refresh_btn_cb(lv_event_t *e);
static void bus_refresh_btn_cb(lv_event_t *e);
static void bus_back_btn_cb(lv_event_t *e);
static void bus_stop_select_cb(lv_event_t *e);
static void bus_list_item_cb(lv_event_t *e);
static void bus_list_item_delete_cb(lv_event_t *e);
static void bus_details_close_btn_cb(lv_event_t *e);
static void train_refresh_btn_cb(lv_event_t *e);
static void train_back_btn_cb(lv_event_t *e);
static void train_list_item_cb(lv_event_t *e);
static void train_list_item_delete_cb(lv_event_t *e);
static void details_close_btn_cb(lv_event_t *e);
static void station_select_cb(lv_event_t *e);
static void prev_btn_cb(lv_event_t *e);
static void next_btn_cb(lv_event_t *e);
static void brightness_slider_cb(lv_event_t *e);
static void sleep_slider_cb(lv_event_t *e);
static void wifi_btn_cb(lv_event_t *e);
static void display_btn_cb(lv_event_t *e);
static void display_back_btn_cb(lv_event_t *e);
static void sysinfo_btn_cb(lv_event_t *e);
static void sysinfo_back_btn_cb(lv_event_t *e);
static void wifi_back_btn_cb(lv_event_t *e);
static void wifi_scan_btn_cb(lv_event_t *e);
static void wifi_list_item_cb(lv_event_t *e);
static void wifi_password_back_btn_cb(lv_event_t *e);
static void wifi_connect_btn_cb(lv_event_t *e);
static void wifi_save_backup_btn_cb(lv_event_t *e);
static void wifi_keyboard_event_cb(lv_event_t *e);
static void wifi_saved_btn_cb(lv_event_t *e);
static void wifi_saved_back_btn_cb(lv_event_t *e);
static void wifi_saved_item_connect_cb(lv_event_t *e);
static void wifi_saved_item_delete_cb(lv_event_t *e);
static void wifi_add_btn_cb(lv_event_t *e);
static void wifi_add_back_btn_cb(lv_event_t *e);
static void wifi_add_save_btn_cb(lv_event_t *e);
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
    // 0 = Clock, 1 = Bus, 2 = Train, 3 = Settings
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
 * @brief Bus list item click callback
 */
static void bus_list_item_cb(lv_event_t *e)
{
    const char *journey_name = (const char *)lv_event_get_user_data(e);
    if (!journey_name) return;
    
    ESP_LOGI(TAG, "Requesting bus details for: %s", journey_name);
    
    // Show details screen (loading)
    lv_obj_clear_flag(bus_details_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(bus_details_loading, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(bus_details_view, LV_OBJ_FLAG_HIDDEN);
    
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_BUS_DETAILS_REQ, 
                     journey_name, strlen(journey_name) + 1, portMAX_DELAY);
}

/**
 * @brief Bus list item delete callback
 */
static void bus_list_item_delete_cb(lv_event_t *e)
{
    char *journey_name = (char *)lv_event_get_user_data(e);
    if (journey_name) {
        lv_mem_free(journey_name);
    }
}

/**
 * @brief Bus details close button callback
 */
static void bus_details_close_btn_cb(lv_event_t *e)
{
    // Hide details screen
    lv_obj_add_flag(bus_details_screen, LV_OBJ_FLAG_HIDDEN);
    
    // Clear data in model
    transport_data_clear_bus_details();
    
    // Clear list
    if (bus_details_list) {
        lv_obj_clean(bus_details_list);
    }
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
    lv_event_code_t code = lv_event_get_code(e);
    int32_t value = lv_slider_get_value(slider);
    
    // Set flag when user starts editing
    if (code == LV_EVENT_PRESSING || code == LV_EVENT_VALUE_CHANGED) {
        display_settings_user_editing = true;
    }
    
    char buf[32];
    snprintf(buf, sizeof(buf), "Brightness: %d%%", (int)value);
    lv_label_set_text(brightness_label, buf);
    
    // Immediately update display brightness (live preview) - but don't save
    int brightness = (int)value;
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_BRIGHTNESS_UPDATE,
                     &brightness, sizeof(brightness), portMAX_DELAY);
}

/**
 * @brief Sleep timeout slider callback - just updates label (apply saves config)
 */
static void sleep_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    int32_t value = lv_slider_get_value(slider);
    
    // Set flag when user starts editing
    if (code == LV_EVENT_PRESSING || code == LV_EVENT_VALUE_CHANGED) {
        display_settings_user_editing = true;
    }
    
    const char *text[] = {"Always On", "1 min", "5 min", "10 min", "30 min", "60 min"};
    int index = value / 20;  // 0-5
    if (index > 5) index = 5;
    
    char buf[32];
    snprintf(buf, sizeof(buf), "Timeout: %s", text[index]);
    lv_label_set_text(sleep_label, buf);
}

/**
 * @brief Apply button callback - saves display configuration
 */
static void display_apply_btn_cb(lv_event_t *e)
{
    if (!brightness_slider || !sleep_slider) return;
    
    struct view_data_display cfg;
    
    // Get brightness value
    cfg.brightness = (int)lv_slider_get_value(brightness_slider);
    
    // Get sleep timeout value
    int32_t sleep_value = lv_slider_get_value(sleep_slider);
    int index = sleep_value / 20;  // 0-5
    if (index > 5) index = 5;
    
    // Map index to minutes: 0=off, 1=1min, 2=5min, 3=10min, 4=30min, 5=60min
    const int timeout_minutes[] = {0, 1, 5, 10, 30, 60};
    cfg.sleep_mode_time_min = timeout_minutes[index];
    cfg.sleep_mode_en = (cfg.sleep_mode_time_min > 0);
    
    ESP_LOGI(TAG, "Applying display config: brightness=%d, timeout=%d min, enabled=%d", 
             cfg.brightness, cfg.sleep_mode_time_min, cfg.sleep_mode_en);
    
    // Clear editing flag - allow updates now
    display_settings_user_editing = false;
    
    // Send event to save and apply configuration
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_DISPLAY_CFG_APPLY,
                     &cfg, sizeof(cfg), portMAX_DELAY);
    
    // Visual feedback
    lv_obj_t *apply_btn = lv_event_get_target(e);
    if (apply_btn) {
        lv_obj_t *lbl = lv_obj_get_child(apply_btn, 0);
        if (lbl) {
            const char *old_text = lv_label_get_text(lbl);
            lv_label_set_text(lbl, "Saved!");
            // Note: Text will be reset on next entry to display settings or by timer
        }
    }
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
            lv_obj_t *item = lv_btn_create(bus_list); // Changed to btn
            lv_obj_set_width(item, LV_PCT(100));
            lv_obj_set_height(item, 55);
            lv_obj_set_style_pad_all(item, 3, 0);
            lv_obj_set_style_pad_gap(item, 8, 0);
            lv_obj_set_style_bg_color(item, lv_color_hex(0x1A1A1A), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(item, 0, LV_PART_MAIN | LV_STATE_DEFAULT);  // No border
            lv_obj_set_style_shadow_width(item, 0, 0); // No shadow for button
            lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
            
            // Add click event
            char *j_name = lv_mem_alloc(strlen(data->departures[i].journey_name) + 1);
            if (j_name) {
                strcpy(j_name, data->departures[i].journey_name);
                lv_obj_add_event_cb(item, bus_list_item_cb, LV_EVENT_CLICKED, j_name);
                lv_obj_add_event_cb(item, bus_list_item_delete_cb, LV_EVENT_DELETE, j_name);
            }
            
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
 * @brief Update bus details screen
 */
static void update_bus_details_screen(const struct view_data_bus_details *data)
{
    if (!bus_details_screen || !data) return;
    
    lv_port_sem_take();
    
    lv_obj_add_flag(bus_details_loading, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(bus_details_view, LV_OBJ_FLAG_HIDDEN);
    
    // Title
    char title[64];
    snprintf(title, sizeof(title), "%s - %s", data->name, data->operator);
    lv_label_set_text(bus_details_title, title);
    
    // List
    lv_obj_clean(bus_details_list);
    
    for (int i = 0; i < data->stop_count; i++) {
        lv_obj_t *item = lv_obj_create(bus_details_list);
        lv_obj_set_size(item, LV_PCT(100), 30);
        lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_pad_all(item, 0, 0);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        
        // Time
        lv_obj_t *lbl_time = lv_label_create(item);
        if (data->stops[i].departure[0]) {
            lv_label_set_text(lbl_time, data->stops[i].departure);
        } else {
            lv_label_set_text(lbl_time, data->stops[i].arrival); // Last stop
        }
        lv_obj_set_style_text_color(lbl_time, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl_time, &arimo_16, 0);
        lv_obj_align(lbl_time, LV_ALIGN_LEFT_MID, 5, 0);
        
        // Name
        lv_obj_t *lbl_name = lv_label_create(item);
        lv_label_set_text(lbl_name, data->stops[i].name);
        lv_obj_set_style_text_color(lbl_name, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl_name, &arimo_16, 0);
        lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, 60, 0);
        
        // Delay (if any)
        if (data->stops[i].delay > 0) {
            lv_obj_t *lbl_delay = lv_label_create(item);
            lv_label_set_text_fmt(lbl_delay, "+%d'", data->stops[i].delay);
            lv_obj_set_style_text_color(lbl_delay, lv_color_hex(0xFFD700), 0);
            lv_obj_align(lbl_delay, LV_ALIGN_RIGHT_MID, -5, 0);
        }
    }
    
    lv_port_sem_give();
}

/**
 * @brief Create bus details screen
 */
static void create_bus_details_screen(lv_obj_t *parent)
{
    // It's a modal over the parent (tab)
    bus_details_screen = lv_obj_create(parent);
    lv_obj_set_size(bus_details_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(bus_details_screen, lv_color_hex(0x101010), 0); 
    lv_obj_set_style_bg_opa(bus_details_screen, LV_OPA_COVER, 0); 
    lv_obj_set_style_border_width(bus_details_screen, 2, 0);
    lv_obj_set_style_border_color(bus_details_screen, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_pad_all(bus_details_screen, 0, 0);
    lv_obj_add_flag(bus_details_screen, LV_OBJ_FLAG_HIDDEN); 
    
    // Header
    lv_obj_t *header = lv_obj_create(bus_details_screen);
    lv_obj_set_size(header, LV_PCT(100), 60); 
    lv_obj_set_style_bg_color(header, lv_color_hex(0x303030), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    
    bus_details_close_btn = lv_btn_create(header);
    lv_obj_set_size(bus_details_close_btn, 50, 50); 
    lv_obj_align(bus_details_close_btn, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_set_style_bg_color(bus_details_close_btn, lv_color_hex(0xFF0000), 0); 
    lv_obj_add_event_cb(bus_details_close_btn, bus_details_close_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_x = lv_label_create(bus_details_close_btn);
    lv_label_set_text(lbl_x, LV_SYMBOL_CLOSE);
    lv_obj_center(lbl_x);
    
    bus_details_title = lv_label_create(header);
    lv_label_set_text(bus_details_title, "Details");
    lv_obj_set_style_text_font(bus_details_title, &arimo_20, 0);
    lv_obj_set_width(bus_details_title, LV_PCT(75)); 
    lv_label_set_long_mode(bus_details_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(bus_details_title, LV_ALIGN_LEFT_MID, 10, 0);
    
    // Loading
    bus_details_loading = lv_obj_create(bus_details_screen);
    lv_obj_set_size(bus_details_loading, LV_PCT(100), LV_PCT(80));
    lv_obj_set_y(bus_details_loading, 60);
    lv_obj_set_style_bg_opa(bus_details_loading, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bus_details_loading, 0, 0);
    
    lv_obj_t *load_lbl = lv_label_create(bus_details_loading);
    lv_label_set_text(load_lbl, "Loading details...");
    lv_obj_set_style_text_font(load_lbl, &arimo_20, 0);
    lv_obj_center(load_lbl);
    
    // Content View
    bus_details_view = lv_obj_create(bus_details_screen);
    lv_obj_set_size(bus_details_view, LV_PCT(100), LV_PCT(85));
    lv_obj_set_y(bus_details_view, 60);
    lv_obj_set_style_bg_opa(bus_details_view, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bus_details_view, 0, 0);
    lv_obj_set_style_pad_all(bus_details_view, 0, 0);
    lv_obj_add_flag(bus_details_view, LV_OBJ_FLAG_HIDDEN);
    
    // List of stops
    bus_details_list = lv_obj_create(bus_details_view);
    lv_obj_set_size(bus_details_list, LV_PCT(100), LV_PCT(100)); // Full height
    lv_obj_align(bus_details_list, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(bus_details_list, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(bus_details_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(bus_details_list, 2, 0);
}

/**
 * @brief Train list item click callback
 */
static void train_list_item_cb(lv_event_t *e)
{
    const char *journey_name = (const char *)lv_event_get_user_data(e);
    if (!journey_name) return;
    
    ESP_LOGI(TAG, "Requesting details for: %s", journey_name);
    
    // Show details screen (loading)
    lv_obj_clear_flag(train_details_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(train_details_loading, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(train_details_view, LV_OBJ_FLAG_HIDDEN);
    
    // Request details fetch
    // Note: We need to pass a copy because event data is temporary, but here we pass char* to the event
    // The event handler will treat it as void* data.
    // However, event_post copies data by value if size > 0.
    // So we pass the pointer content (the string) in the buffer.
    
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_TRAIN_DETAILS_REQ, 
                     journey_name, strlen(journey_name) + 1, portMAX_DELAY);
}

/**
 * @brief Train list item delete callback to free memory
 */
static void train_list_item_delete_cb(lv_event_t *e)
{
    char *journey_name = (char *)lv_event_get_user_data(e);
    if (journey_name) {
        lv_mem_free(journey_name);
    }
}

/**
 * @brief Details close button callback
 */
static void details_close_btn_cb(lv_event_t *e)
{
    // Hide details screen
    lv_obj_add_flag(train_details_screen, LV_OBJ_FLAG_HIDDEN);
    
    // Clear data in model
    transport_data_clear_train_details();
    
    // Clear list
    if (train_details_list) {
        lv_obj_clean(train_details_list);
    }
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
            
            // Row Item - Clickable!
            lv_obj_t *item = lv_btn_create(train_list); // Changed from obj to btn
            lv_obj_set_size(item, LV_PCT(100), 40);
            lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(item, 0, 0);
            lv_obj_set_style_border_side(item, LV_BORDER_SIDE_BOTTOM, 0);
            lv_obj_set_style_border_color(item, lv_color_hex(0x404040), 0);
            lv_obj_set_style_border_width(item, 1, 0); // Separator line
            lv_obj_set_style_pad_all(item, 0, 0);
            lv_obj_set_style_shadow_width(item, 0, 0); // No shadow for button
            lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
            
            // Allocate memory for journey name to pass as user data
            char *j_name = lv_mem_alloc(strlen(data->departures[i].journey_name) + 1);
            if (j_name) {
                strcpy(j_name, data->departures[i].journey_name);
                lv_obj_add_event_cb(item, train_list_item_cb, LV_EVENT_CLICKED, j_name);
                lv_obj_add_event_cb(item, train_list_item_delete_cb, LV_EVENT_DELETE, j_name);
            }
            
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
    
    // Update sleep timeout slider
    if (sleep_slider && data->sleep_timeout_min >= 0) {
        // Map minutes to slider value: 0=0, 1=20, 5=40, 10=60, 30=80, 60=100
        int slider_val = 0;
        if (data->sleep_timeout_min == 0) slider_val = 0;
        else if (data->sleep_timeout_min == 1) slider_val = 20;
        else if (data->sleep_timeout_min == 5) slider_val = 40;
        else if (data->sleep_timeout_min == 10) slider_val = 60;
        else if (data->sleep_timeout_min == 30) slider_val = 80;
        else if (data->sleep_timeout_min >= 60) slider_val = 100;
        else slider_val = 0; // default to always on for unknown values
        
        lv_slider_set_value(sleep_slider, slider_val, LV_ANIM_OFF);
        
        const char *text[] = {"Always On", "1 min", "5 min", "10 min", "30 min", "60 min"};
        int index = slider_val / 20;
        if (index > 5) index = 5;
        char buf[32];
        snprintf(buf, sizeof(buf), "Timeout: %s", text[index]);
        if (sleep_label) {
            lv_label_set_text(sleep_label, buf);
        }
    }
    
    lv_port_sem_give();
}

/**
 * @brief Update display settings from config
 */
static void update_display_settings(const struct view_data_display *cfg)
{
    if (!cfg) return;
    
    // Don't update sliders if user is currently editing them
    if (display_settings_user_editing) {
        ESP_LOGD(TAG, "Skipping display settings update - user is editing");
        return;
    }
    
    lv_port_sem_take();
    
    // Update brightness slider
    if (brightness_slider) {
        lv_slider_set_value(brightness_slider, cfg->brightness, LV_ANIM_OFF);
        char buf[32];
        snprintf(buf, sizeof(buf), "Brightness: %d%%", cfg->brightness);
        if (brightness_label) {
            lv_label_set_text(brightness_label, buf);
        }
    }
    
    // Update sleep timeout slider
    if (sleep_slider) {
        // Map minutes to slider value
        int slider_val = 0;
        if (cfg->sleep_mode_time_min == 0) slider_val = 0;
        else if (cfg->sleep_mode_time_min == 1) slider_val = 20;
        else if (cfg->sleep_mode_time_min == 5) slider_val = 40;
        else if (cfg->sleep_mode_time_min == 10) slider_val = 60;
        else if (cfg->sleep_mode_time_min == 30) slider_val = 80;
        else if (cfg->sleep_mode_time_min >= 60) slider_val = 100;
        
        lv_slider_set_value(sleep_slider, slider_val, LV_ANIM_OFF);
        
        const char *text[] = {"Always On", "1 min", "5 min", "10 min", "30 min", "60 min"};
        int index = slider_val / 20;
        if (index > 5) index = 5;
        char buf[32];
        snprintf(buf, sizeof(buf), "Timeout: %s", text[index]);
        if (sleep_label) {
            lv_label_set_text(sleep_label, buf);
        }
    }
    
    lv_port_sem_give();
}

/**
 * @brief Animation callback for arc-based loading spinner (rotation 0..360).
 */
static void loading_arc_anim_cb(void *var, int32_t value)
{
    lv_arc_set_rotation((lv_obj_t *)var, (uint16_t)value);
}

/**
 * @brief Update bus/train station selection buttons: disabled (gray) until network + time sync.
 */
static void update_station_buttons_availability(void)
{
    bool ready = network_manager_is_connected() && indicator_time_is_synced();

    if (bus_selection_cont) {
        uint32_t n = lv_obj_get_child_cnt(bus_selection_cont);
        for (uint32_t i = 1; i < n; i++) {
            lv_obj_t *btn = lv_obj_get_child(bus_selection_cont, i);
            if (ready) {
                lv_obj_clear_state(btn, LV_STATE_DISABLED);
            } else {
                lv_obj_add_state(btn, LV_STATE_DISABLED);
            }
        }
    }
    if (station_selection_cont) {
        uint32_t n = lv_obj_get_child_cnt(station_selection_cont);
        for (uint32_t i = 1; i < n; i++) {
            lv_obj_t *btn = lv_obj_get_child(station_selection_cont, i);
            if (ready) {
                lv_obj_clear_state(btn, LV_STATE_DISABLED);
            } else {
                lv_obj_add_state(btn, LV_STATE_DISABLED);
            }
        }
    }
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
    lv_obj_set_scrollbar_mode(bus_selection_cont, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *sel_label = lv_label_create(bus_selection_cont);
    lv_label_set_text(sel_label, "Select stop:");
    lv_obj_set_style_text_font(sel_label, &arimo_20, 0);
    lv_obj_set_style_text_color(sel_label, lv_color_white(), 0);
    
    for (int i = 0; i < sizeof(predefined_bus_stops)/sizeof(predefined_bus_stops[0]); i++) {
        lv_obj_t *btn = lv_btn_create(bus_selection_cont);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_height(btn, 50);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x008000), 0); // Green
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x505050), LV_PART_MAIN | LV_STATE_DISABLED);
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
#if LV_USE_SPINNER
    lv_obj_t *bus_spinner = lv_spinner_create(bus_loading_cont, 1000, 60);
    lv_obj_set_size(bus_spinner, 80, 80);
    lv_obj_align(bus_spinner, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_style_arc_color(bus_spinner, lv_color_white(), LV_PART_INDICATOR);
#else
    lv_obj_t *bus_arc = lv_arc_create(bus_loading_cont);
    lv_obj_set_size(bus_arc, 80, 80);
    lv_obj_align(bus_arc, LV_ALIGN_CENTER, 0, -30);
    lv_arc_set_range(bus_arc, 0, 360);
    lv_arc_set_bg_angles(bus_arc, 0, 360);
    lv_arc_set_angles(bus_arc, 0, 90);
    lv_obj_set_style_arc_color(bus_arc, lv_color_hex(0x404040), LV_PART_MAIN);
    lv_obj_set_style_arc_color(bus_arc, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(bus_arc, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(bus_arc, 6, LV_PART_INDICATOR);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, bus_arc);
    lv_anim_set_exec_cb(&a, loading_arc_anim_cb);
    lv_anim_set_values(&a, 0, 360);
    lv_anim_set_time(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
#endif
    lv_obj_t *loading_lbl = lv_label_create(bus_loading_cont);
    lv_label_set_text(loading_lbl, "Loading data...");
    lv_obj_set_style_text_font(loading_lbl, &arimo_20, 0);
    lv_obj_set_style_text_color(loading_lbl, lv_color_white(), 0);
    lv_obj_align(loading_lbl, LV_ALIGN_CENTER, 0, 30);
    
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
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x505050), LV_PART_MAIN | LV_STATE_DISABLED);
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
#if LV_USE_SPINNER
    lv_obj_t *train_spinner = lv_spinner_create(loading_cont, 1000, 60);
    lv_obj_set_size(train_spinner, 80, 80);
    lv_obj_align(train_spinner, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_style_arc_color(train_spinner, lv_color_white(), LV_PART_INDICATOR);
#else
    lv_obj_t *train_arc = lv_arc_create(loading_cont);
    lv_obj_set_size(train_arc, 80, 80);
    lv_obj_align(train_arc, LV_ALIGN_CENTER, 0, -30);
    lv_arc_set_range(train_arc, 0, 360);
    lv_arc_set_bg_angles(train_arc, 0, 360);
    lv_arc_set_angles(train_arc, 0, 90);
    lv_obj_set_style_arc_color(train_arc, lv_color_hex(0x404040), LV_PART_MAIN);
    lv_obj_set_style_arc_color(train_arc, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(train_arc, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(train_arc, 6, LV_PART_INDICATOR);
    lv_anim_t a2;
    lv_anim_init(&a2);
    lv_anim_set_var(&a2, train_arc);
    lv_anim_set_exec_cb(&a2, loading_arc_anim_cb);
    lv_anim_set_values(&a2, 0, 360);
    lv_anim_set_time(&a2, 1000);
    lv_anim_set_repeat_count(&a2, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a2);
#endif
    lv_obj_t *loading_lbl = lv_label_create(loading_cont);
    lv_label_set_text(loading_lbl, "Loading data...");
    lv_obj_set_style_text_font(loading_lbl, &arimo_20, 0);
    lv_obj_set_style_text_color(loading_lbl, lv_color_white(), 0);
    lv_obj_align(loading_lbl, LV_ALIGN_CENTER, 0, 30);
    
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
    update_wifi_network_info();
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
 * @brief Open Display settings (hide main, show display submenu)
 */
static void display_btn_cb(lv_event_t *e)
{
    // Clear editing flag to allow loading current config
    display_settings_user_editing = false;
    
    lv_obj_add_flag(settings_main_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(display_settings_cont, LV_OBJ_FLAG_HIDDEN);
    
    // Reset apply button text
    if (display_apply_btn) {
        lv_obj_t *lbl = lv_obj_get_child(display_apply_btn, 0);
        if (lbl) {
            lv_label_set_text(lbl, "Apply & Save");
        }
    }
    
    // Load current display config and update sliders
    struct view_data_display cfg;
    indicator_display_cfg_get(&cfg);
    update_display_settings(&cfg);
    
    ESP_LOGI(TAG, "Display settings opened - loaded config: brightness=%d, timeout=%d min", 
             cfg.brightness, cfg.sleep_mode_time_min);
}

/**
 * @brief Back from Display settings to main Settings
 */
static void display_back_btn_cb(lv_event_t *e)
{
    // Clear editing flag when leaving
    display_settings_user_editing = false;
    
    lv_obj_add_flag(display_settings_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(settings_main_cont, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief System Info Button Callback
 */
static void sysinfo_btn_cb(lv_event_t *e)
{
    lv_obj_add_flag(settings_main_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(sysinfo_cont, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief System Info Back Button Callback
 */
static void sysinfo_back_btn_cb(lv_event_t *e)
{
    lv_obj_add_flag(sysinfo_cont, LV_OBJ_FLAG_HIDDEN);
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
        if (wifi_password_ssid_label) {
            lv_label_set_text(wifi_password_ssid_label, current_wifi_ssid[0] ? current_wifi_ssid : "—");
        }
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
 * @brief Save current SSID + password as backup network (fallback after 2 min)
 */
static void wifi_save_backup_btn_cb(lv_event_t *e)
{
    struct view_data_wifi_config cfg = {0};
    strlcpy(cfg.ssid, current_wifi_ssid, sizeof(cfg.ssid));
    const char *password = lv_textarea_get_text(wifi_password_ta);
    if (password && strlen(password) > 0) {
        strlcpy((char *)cfg.password, password, sizeof(cfg.password));
        cfg.have_password = true;
    } else {
        cfg.have_password = false;
    }
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_SET_BACKUP, &cfg, sizeof(cfg), portMAX_DELAY);
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
 * @brief Saved Networks Button Callback
 */
static void wifi_saved_btn_cb(lv_event_t *e)
{
    // Hide WiFi list, show saved networks
    lv_obj_add_flag(wifi_view_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(wifi_saved_cont, LV_OBJ_FLAG_HIDDEN);
    
    // Request list from backend
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, 
                     VIEW_EVENT_WIFI_SAVED_LIST_REQ, NULL, 0, portMAX_DELAY);
}

/**
 * @brief Saved Networks Back Button Callback
 */
static void wifi_saved_back_btn_cb(lv_event_t *e)
{
    lv_obj_add_flag(wifi_saved_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(wifi_view_cont, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Saved Network Item Connect Callback
 */
static void wifi_saved_item_connect_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const char *ssid = (const char *)lv_obj_get_user_data(btn);
    
    if (ssid && ssid[0]) {
        ESP_LOGI(TAG, "Connecting to saved network: %s", ssid);
        esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, 
                         VIEW_EVENT_WIFI_CONNECT_SAVED, 
                         (void *)ssid, strlen(ssid) + 1, portMAX_DELAY);
        
        // Go back to WiFi screen
        lv_obj_add_flag(wifi_saved_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(wifi_view_cont, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief Saved Network Item Delete Callback
 */
static void wifi_saved_item_delete_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const char *ssid = (const char *)lv_obj_get_user_data(btn);
    
    if (ssid && ssid[0]) {
        ESP_LOGI(TAG, "Deleting network: %s", ssid);
        esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, 
                         VIEW_EVENT_WIFI_DELETE_NETWORK, 
                         (void *)ssid, strlen(ssid) + 1, portMAX_DELAY);
        
        // Backend will send updated list
    }
}

/**
 * @brief Add Network Button Callback
 */
static void wifi_add_btn_cb(lv_event_t *e)
{
    // Clear form
    lv_textarea_set_text(wifi_add_ssid_ta, "");
    lv_textarea_set_text(wifi_add_password_ta, "");
    lv_obj_clear_state(wifi_add_password_checkbox, LV_STATE_CHECKED);
    
    // Hide saved networks, show add form
    lv_obj_add_flag(wifi_saved_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(wifi_add_cont, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Add Network Back Button Callback
 */
static void wifi_add_back_btn_cb(lv_event_t *e)
{
    lv_obj_add_flag(wifi_add_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(wifi_saved_cont, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Add Network Save Button Callback
 */
static void wifi_add_save_btn_cb(lv_event_t *e)
{
    const char *ssid = lv_textarea_get_text(wifi_add_ssid_ta);
    const char *password = lv_textarea_get_text(wifi_add_password_ta);
    bool has_password = lv_obj_has_state(wifi_add_password_checkbox, LV_STATE_CHECKED);
    
    if (!ssid || ssid[0] == '\0') {
        ESP_LOGW(TAG, "SSID is empty");
        return;
    }
    
    struct view_data_wifi_config cfg = {0};
    strlcpy(cfg.ssid, ssid, sizeof(cfg.ssid));
    
    if (has_password && password && password[0]) {
        strlcpy((char *)cfg.password, password, sizeof(cfg.password));
        cfg.have_password = true;
    } else {
        cfg.have_password = false;
    }
    
    ESP_LOGI(TAG, "Saving network: %s", cfg.ssid);
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, 
                     VIEW_EVENT_WIFI_SAVE_NETWORK, &cfg, sizeof(cfg), portMAX_DELAY);
    
    // Go back to saved networks (backend will send updated list)
    lv_obj_add_flag(wifi_add_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(wifi_saved_cont, LV_OBJ_FLAG_HIDDEN);
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
 * @brief Refresh WiFi network info panel (IP, DNS, gateway, RSSI, etc.)
 */
static void update_wifi_network_info(void)
{
    if (!wifi_netinfo_cont) return;
    struct view_data_network_info info;
    if (network_manager_get_network_info(&info) != ESP_OK) {
        info.connected = false;
        strcpy(info.ip, "-");
        strcpy(info.gateway, "-");
        strcpy(info.netmask, "-");
        strcpy(info.dns_primary, "-");
        strcpy(info.dns_secondary, "-");
        info.ssid[0] = '\0';
        info.rssi = 0;
    }
    lv_port_sem_take();
    uint32_t n = lv_obj_get_child_cnt(wifi_netinfo_cont);
    if (n >= 1) {
        lv_obj_t *lbl = lv_obj_get_child(wifi_netinfo_cont, 0);
        lv_label_set_text_fmt(lbl, "Status: %s", info.connected ? "Connected" : "Not connected");
    }
    if (n >= 2) {
        lv_obj_t *lbl = lv_obj_get_child(wifi_netinfo_cont, 1);
        lv_label_set_text_fmt(lbl, "SSID: %s", info.ssid[0] ? info.ssid : "-");
    }
    if (n >= 3) {
        lv_obj_t *lbl = lv_obj_get_child(wifi_netinfo_cont, 2);
        lv_label_set_text_fmt(lbl, "IP: %s", info.ip);
    }
    if (n >= 4) {
        lv_obj_t *lbl = lv_obj_get_child(wifi_netinfo_cont, 3);
        lv_label_set_text_fmt(lbl, "Gateway: %s", info.gateway);
    }
    if (n >= 5) {
        lv_obj_t *lbl = lv_obj_get_child(wifi_netinfo_cont, 4);
        lv_label_set_text_fmt(lbl, "Netmask: %s", info.netmask);
    }
    if (n >= 6) {
        lv_obj_t *lbl = lv_obj_get_child(wifi_netinfo_cont, 5);
        lv_label_set_text_fmt(lbl, "DNS: %s / %s", info.dns_primary, info.dns_secondary);
    }
    if (n >= 7) {
        lv_obj_t *lbl = lv_obj_get_child(wifi_netinfo_cont, 6);
        lv_label_set_text_fmt(lbl, "Signal: %d dBm", info.rssi);
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
    lv_label_set_text(title, "WiFi");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 50, 15);
    
    // Saved Networks button
    lv_obj_t *saved_btn = lv_btn_create(header);
    lv_obj_set_size(saved_btn, 60, 40);
    lv_obj_align(saved_btn, LV_ALIGN_RIGHT_MID, -70, 0);
    lv_obj_add_event_cb(saved_btn, wifi_saved_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *saved_lbl = lv_label_create(saved_btn);
    lv_label_set_text(saved_lbl, "Saved");
    lv_obj_set_style_text_font(saved_lbl, &arimo_14, 0);
    lv_obj_center(saved_lbl);
    
    lv_obj_t *scan_btn = lv_btn_create(header);
    lv_obj_set_size(scan_btn, 60, 40);
    lv_obj_align(scan_btn, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_add_event_cb(scan_btn, wifi_scan_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *scan_lbl = lv_label_create(scan_btn);
    lv_label_set_text(scan_lbl, "Scan");
    lv_obj_set_style_text_font(scan_lbl, &arimo_14, 0);
    lv_obj_center(scan_lbl);
    
    // Network info panel (scrollable, under header)
    wifi_netinfo_cont = lv_obj_create(wifi_view_cont);
    lv_obj_set_size(wifi_netinfo_cont, LV_PCT(100), 165);
    lv_obj_set_y(wifi_netinfo_cont, 50);
    lv_obj_set_style_bg_color(wifi_netinfo_cont, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(wifi_netinfo_cont, 0, 0);
    lv_obj_set_style_pad_all(wifi_netinfo_cont, 8, 0);
    lv_obj_set_flex_flow(wifi_netinfo_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(wifi_netinfo_cont, 2, 0);
    lv_obj_set_style_text_color(wifi_netinfo_cont, lv_color_white(), 0);
    for (int i = 0; i < 7; i++) {
        lv_obj_t *lbl = lv_label_create(wifi_netinfo_cont);
        lv_label_set_text(lbl, "-");
        lv_obj_set_style_text_font(lbl, &arimo_14, 0);
    }
    /* Do not update info here (model not inited yet). Will update on open/event. */
    
    // List of networks (below info panel)
    wifi_list = lv_obj_create(wifi_view_cont);
    lv_obj_set_size(wifi_list, LV_PCT(100), LV_PCT(100));
    lv_obj_set_y(wifi_list, 215);
    lv_obj_set_height(wifi_list, lv_pct(100));
    lv_obj_set_style_bg_color(wifi_list, lv_color_black(), 0);
    lv_obj_set_style_border_width(wifi_list, 0, 0);
    lv_obj_set_flex_flow(wifi_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(wifi_list, 5, 0);
}

/**
 * @brief Create WiFi Password Screen – right-side password panel for readability
 */
static void create_wifi_password_screen(lv_obj_t *parent)
{
    wifi_password_view_cont = lv_obj_create(parent);
    lv_obj_set_size(wifi_password_view_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(wifi_password_view_cont, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(wifi_password_view_cont, 0, 0);
    lv_obj_add_flag(wifi_password_view_cont, LV_OBJ_FLAG_HIDDEN);
    
    // Left side: SSID / instruction (50% width)
    lv_obj_t *left_panel = lv_obj_create(wifi_password_view_cont);
    lv_obj_set_size(left_panel, LV_PCT(50), LV_PCT(100));
    lv_obj_set_style_bg_opa(left_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left_panel, 0, 0);
    lv_obj_align(left_panel, LV_ALIGN_LEFT_MID, 0, 0);
    
    wifi_password_ssid_label = lv_label_create(left_panel);
    lv_label_set_text(wifi_password_ssid_label, "—");
    lv_obj_set_style_text_font(wifi_password_ssid_label, &arimo_20, 0);
    lv_obj_set_style_text_color(wifi_password_ssid_label, lv_color_white(), 0);
    lv_obj_align(wifi_password_ssid_label, LV_ALIGN_TOP_MID, 0, 30);
    
    lv_obj_t *hint_lbl = lv_label_create(left_panel);
    lv_label_set_text(hint_lbl, "Enter password on the right");
    lv_obj_set_style_text_font(hint_lbl, &arimo_16, 0);
    lv_obj_set_style_text_color(hint_lbl, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(hint_lbl, LV_ALIGN_TOP_MID, 0, 80);
    
    // Right side: password panel (50% width) – more readable
    lv_obj_t *right_panel = lv_obj_create(wifi_password_view_cont);
    lv_obj_set_size(right_panel, LV_PCT(50), LV_PCT(100));
    lv_obj_set_style_bg_color(right_panel, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(right_panel, 0, 0);
    lv_obj_set_style_pad_all(right_panel, 12, 0);
    lv_obj_align(right_panel, LV_ALIGN_RIGHT_MID, 0, 0);
    
    lv_obj_t *pass_title = lv_label_create(right_panel);
    lv_label_set_text(pass_title, "Password");
    lv_obj_set_style_text_font(pass_title, &arimo_20, 0);
    lv_obj_set_style_text_color(pass_title, lv_color_white(), 0);
    lv_obj_align(pass_title, LV_ALIGN_TOP_LEFT, 0, 10);
    
    wifi_password_ta = lv_textarea_create(right_panel);
    lv_textarea_set_one_line(wifi_password_ta, true);
    lv_textarea_set_password_mode(wifi_password_ta, true);
    lv_obj_set_size(wifi_password_ta, LV_PCT(100), 60);
    lv_obj_align(wifi_password_ta, LV_ALIGN_TOP_LEFT, 0, 45);
    lv_obj_set_style_text_font(wifi_password_ta, &arimo_20, 0);
    
    lv_obj_t *btn = lv_btn_create(right_panel);
    lv_obj_set_size(btn, LV_PCT(100), 48);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 0, 120);
    lv_obj_add_event_cb(btn, wifi_connect_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "Connect");
    lv_obj_set_style_text_font(btn_lbl, &arimo_20, 0);
    lv_obj_center(btn_lbl);
    
    lv_obj_t *cancel_btn = lv_btn_create(right_panel);
    lv_obj_set_size(cancel_btn, LV_PCT(100), 48);
    lv_obj_align(cancel_btn, LV_ALIGN_TOP_LEFT, 0, 178);
    lv_obj_add_event_cb(cancel_btn, wifi_password_back_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_set_style_text_font(cancel_lbl, &arimo_20, 0);
    lv_obj_center(cancel_lbl);

    lv_obj_t *backup_btn = lv_btn_create(right_panel);
    lv_obj_set_size(backup_btn, LV_PCT(100), 44);
    lv_obj_align(backup_btn, LV_ALIGN_TOP_LEFT, 0, 234);
    lv_obj_add_event_cb(backup_btn, wifi_save_backup_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *backup_lbl = lv_label_create(backup_btn);
    lv_label_set_text(backup_lbl, "Save as backup");
    lv_obj_set_style_text_font(backup_lbl, &arimo_16, 0);
    lv_obj_center(backup_lbl);

    wifi_keyboard = lv_keyboard_create(wifi_password_view_cont);
    lv_keyboard_set_textarea(wifi_keyboard, wifi_password_ta);
    lv_obj_set_size(wifi_keyboard, LV_PCT(100), LV_PCT(40));
    lv_obj_align(wifi_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(wifi_keyboard, wifi_keyboard_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

/**
 * @brief Create WiFi Saved Networks screen
 */
static void create_wifi_saved_screen(lv_obj_t *parent)
{
    wifi_saved_cont = lv_obj_create(parent);
    lv_obj_set_size(wifi_saved_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(wifi_saved_cont, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(wifi_saved_cont, 0, 0);
    lv_obj_set_style_pad_all(wifi_saved_cont, 10, 0);
    lv_obj_add_flag(wifi_saved_cont, LV_OBJ_FLAG_HIDDEN);

    // Header
    lv_obj_t *header = lv_obj_create(wifi_saved_cont);
    lv_obj_set_size(header, LV_PCT(100), 50);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x202020), 0);
    lv_obj_set_style_border_width(header, 0, 0);

    // Back button
    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 60, 40);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_add_event_cb(back_btn, wifi_saved_back_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, "<");
    lv_obj_set_style_text_font(back_lbl, &arimo_20, 0);
    lv_obj_center(back_lbl);

    // Title
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Saved Networks");
    lv_obj_set_style_text_font(title, &arimo_20, 0);
    lv_obj_center(title);

    // Add button (+)
    lv_obj_t *add_btn = lv_btn_create(header);
    lv_obj_set_size(add_btn, 60, 40);
    lv_obj_align(add_btn, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_add_event_cb(add_btn, wifi_add_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(add_btn, lv_color_hex(0x00AA00), 0);
    lv_obj_t *add_lbl = lv_label_create(add_btn);
    lv_label_set_text(add_lbl, "+");
    lv_obj_set_style_text_font(add_lbl, &arimo_24, 0);
    lv_obj_center(add_lbl);

    // List container
    wifi_saved_list = lv_obj_create(wifi_saved_cont);
    lv_obj_set_size(wifi_saved_list, LV_PCT(100), LV_PCT(85));
    lv_obj_align(wifi_saved_list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(wifi_saved_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_saved_list, 0, 0);
    lv_obj_set_flex_flow(wifi_saved_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(wifi_saved_list, 5, 0);
}

/**
 * @brief Create WiFi Add Network screen
 */
static void create_wifi_add_screen(lv_obj_t *parent)
{
    wifi_add_cont = lv_obj_create(parent);
    lv_obj_set_size(wifi_add_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(wifi_add_cont, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(wifi_add_cont, 0, 0);
    lv_obj_set_style_pad_all(wifi_add_cont, 0, 0);
    lv_obj_add_flag(wifi_add_cont, LV_OBJ_FLAG_HIDDEN);

    // Top panel - form (60% height)
    lv_obj_t *top_panel = lv_obj_create(wifi_add_cont);
    lv_obj_set_size(top_panel, LV_PCT(100), LV_PCT(60));
    lv_obj_align(top_panel, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(top_panel, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(top_panel, 0, 0);
    lv_obj_set_style_pad_all(top_panel, 15, 0);

    // Title
    lv_obj_t *title = lv_label_create(top_panel);
    lv_label_set_text(title, "Add Network");
    lv_obj_set_style_text_font(title, &arimo_24, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

    // Back button
    lv_obj_t *back_btn = lv_btn_create(top_panel);
    lv_obj_set_size(back_btn, 60, 40);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 5, 5);
    lv_obj_add_event_cb(back_btn, wifi_add_back_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, "<");
    lv_obj_set_style_text_font(back_lbl, &arimo_20, 0);
    lv_obj_center(back_lbl);

    // SSID label
    lv_obj_t *ssid_lbl = lv_label_create(top_panel);
    lv_label_set_text(ssid_lbl, "SSID:");
    lv_obj_set_style_text_font(ssid_lbl, &arimo_16, 0);
    lv_obj_set_style_text_color(ssid_lbl, lv_color_white(), 0);
    lv_obj_align(ssid_lbl, LV_ALIGN_TOP_LEFT, 15, 55);

    // SSID input
    wifi_add_ssid_ta = lv_textarea_create(top_panel);
    lv_textarea_set_one_line(wifi_add_ssid_ta, true);
    lv_obj_set_size(wifi_add_ssid_ta, LV_PCT(90), 55);
    lv_obj_align(wifi_add_ssid_ta, LV_ALIGN_TOP_MID, 0, 85);
    lv_obj_set_style_text_font(wifi_add_ssid_ta, &arimo_20, 0);

    // Password checkbox
    wifi_add_password_checkbox = lv_checkbox_create(top_panel);
    lv_checkbox_set_text(wifi_add_password_checkbox, "Has Password");
    lv_obj_set_style_text_font(wifi_add_password_checkbox, &arimo_16, 0);
    lv_obj_align(wifi_add_password_checkbox, LV_ALIGN_TOP_LEFT, 15, 155);

    // Password label
    lv_obj_t *pass_lbl = lv_label_create(top_panel);
    lv_label_set_text(pass_lbl, "Password:");
    lv_obj_set_style_text_font(pass_lbl, &arimo_16, 0);
    lv_obj_set_style_text_color(pass_lbl, lv_color_white(), 0);
    lv_obj_align(pass_lbl, LV_ALIGN_TOP_LEFT, 15, 195);

    // Password input
    wifi_add_password_ta = lv_textarea_create(top_panel);
    lv_textarea_set_one_line(wifi_add_password_ta, true);
    lv_textarea_set_password_mode(wifi_add_password_ta, true);
    lv_obj_set_size(wifi_add_password_ta, LV_PCT(90), 55);
    lv_obj_align(wifi_add_password_ta, LV_ALIGN_TOP_MID, 0, 225);
    lv_obj_set_style_text_font(wifi_add_password_ta, &arimo_20, 0);

    // Save button (right side)
    lv_obj_t *save_btn = lv_btn_create(top_panel);
    lv_obj_set_size(save_btn, 120, 50);
    lv_obj_align(save_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_set_style_bg_color(save_btn, lv_color_hex(0x00AA00), 0);
    lv_obj_add_event_cb(save_btn, wifi_add_save_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *save_lbl = lv_label_create(save_btn);
    lv_label_set_text(save_lbl, "Save");
    lv_obj_set_style_text_font(save_lbl, &arimo_20, 0);
    lv_obj_center(save_lbl);

    // Bottom panel - keyboard (40% height, full width)
    wifi_add_keyboard = lv_keyboard_create(wifi_add_cont);
    lv_obj_set_size(wifi_add_keyboard, LV_PCT(100), LV_PCT(40));
    lv_obj_align(wifi_add_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(wifi_add_keyboard, wifi_add_ssid_ta);
}

/**
 * @brief Update WiFi Saved Networks list
 */
static void update_wifi_saved_list(const struct view_data_wifi_saved_list *list)
{
    if (!list) return;

    lv_port_sem_take();

    // Clear existing items
    lv_obj_clean(wifi_saved_list);

    if (list->count == 0) {
        lv_obj_t *empty_lbl = lv_label_create(wifi_saved_list);
        lv_label_set_text(empty_lbl, "No saved networks\n\nClick '+' to add one");
        lv_obj_set_style_text_font(empty_lbl, &arimo_20, 0);
        lv_obj_set_style_text_color(empty_lbl, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_text_align(empty_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_port_sem_give();
        return;
    }

    // Get current WiFi status to highlight connected network
    struct view_data_wifi_st wifi_st;
    network_manager_get_wifi_status(&wifi_st);

    // Add each saved network
    for (int i = 0; i < MAX_SAVED_NETWORKS; i++) {
        if (!list->networks[i].valid) continue;

        // Check if this is the currently connected network
        bool is_connected = wifi_st.is_connected && 
                           (strcmp(wifi_st.ssid, list->networks[i].ssid) == 0);

        lv_obj_t *item = lv_obj_create(wifi_saved_list);
        lv_obj_set_size(item, LV_PCT(100), 70);
        
        // Highlight connected network with different color
        if (is_connected) {
            lv_obj_set_style_bg_color(item, lv_color_hex(0x004400), 0);  // Green background
            lv_obj_set_style_border_color(item, lv_color_hex(0x00FF00), 0);
            lv_obj_set_style_border_width(item, 2, 0);
        } else {
            lv_obj_set_style_bg_color(item, lv_color_hex(0x2a2a2a), 0);
            lv_obj_set_style_border_width(item, 1, 0);
            lv_obj_set_style_border_color(item, lv_color_hex(0x555555), 0);
        }
        lv_obj_set_style_pad_all(item, 5, 0);

        // SSID label with connection status
        lv_obj_t *ssid_lbl = lv_label_create(item);
        char buf[80];
        const char *lock_icon = list->networks[i].have_password ? "🔒" : "🔓";
        const char *conn_icon = is_connected ? "✓ " : "";
        snprintf(buf, sizeof(buf), "%s%s %s", conn_icon, lock_icon, list->networks[i].ssid);
        lv_label_set_text(ssid_lbl, buf);
        lv_obj_set_style_text_font(ssid_lbl, &arimo_16, 0);
        lv_obj_set_style_text_color(ssid_lbl, lv_color_white(), 0);
        lv_obj_align(ssid_lbl, LV_ALIGN_TOP_LEFT, 10, 5);

        // Status label (if connected)
        if (is_connected) {
            lv_obj_t *status_lbl = lv_label_create(item);
            lv_label_set_text(status_lbl, "Connected");
            lv_obj_set_style_text_font(status_lbl, &arimo_14, 0);
            lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x00FF00), 0);
            lv_obj_align(status_lbl, LV_ALIGN_BOTTOM_LEFT, 35, -5);
        }

        // Connect button (disabled if already connected)
        lv_obj_t *conn_btn = lv_btn_create(item);
        lv_obj_set_size(conn_btn, 85, 45);
        lv_obj_align(conn_btn, LV_ALIGN_RIGHT_MID, -65, 0);
        
        if (is_connected) {
            lv_obj_set_style_bg_color(conn_btn, lv_color_hex(0x555555), 0);  // Gray if connected
            lv_obj_add_state(conn_btn, LV_STATE_DISABLED);
        } else {
            lv_obj_set_style_bg_color(conn_btn, lv_color_hex(0x0066CC), 0);
            lv_obj_set_user_data(conn_btn, strdup(list->networks[i].ssid));
            lv_obj_add_event_cb(conn_btn, wifi_saved_item_connect_cb, LV_EVENT_CLICKED, NULL);
        }
        
        lv_obj_t *conn_lbl = lv_label_create(conn_btn);
        lv_label_set_text(conn_lbl, "Connect");
        lv_obj_set_style_text_font(conn_lbl, &arimo_14, 0);
        lv_obj_center(conn_lbl);

        // Delete button
        lv_obj_t *del_btn = lv_btn_create(item);
        lv_obj_set_size(del_btn, 50, 45);
        lv_obj_align(del_btn, LV_ALIGN_RIGHT_MID, -5, 0);
        lv_obj_set_style_bg_color(del_btn, lv_color_hex(0xCC0000), 0);
        lv_obj_set_user_data(del_btn, strdup(list->networks[i].ssid));
        lv_obj_add_event_cb(del_btn, wifi_saved_item_delete_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *del_lbl = lv_label_create(del_btn);
        lv_label_set_text(del_lbl, LV_SYMBOL_CLOSE);
        lv_obj_set_style_text_font(del_lbl, &arimo_20, 0);
        lv_obj_center(del_lbl);
    }

    lv_port_sem_give();
}

/**
 * @brief Create System Info screen
 */
static void create_sysinfo_screen(lv_obj_t *parent)
{
    sysinfo_cont = lv_obj_create(parent);
    lv_obj_set_size(sysinfo_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(sysinfo_cont, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(sysinfo_cont, 0, 0);
    lv_obj_set_style_pad_all(sysinfo_cont, 10, 0);
    lv_obj_add_flag(sysinfo_cont, LV_OBJ_FLAG_HIDDEN);

    // Back button
    lv_obj_t *back_btn = lv_btn_create(sysinfo_cont);
    lv_obj_set_size(back_btn, 60, 40);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_event_cb(back_btn, sysinfo_back_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, "<");
    lv_obj_set_style_text_font(back_lbl, &arimo_20, 0);
    lv_obj_center(back_lbl);

    // Title
    lv_obj_t *title = lv_label_create(sysinfo_cont);
    lv_label_set_text(title, "System Information");
    lv_obj_set_style_text_font(title, &arimo_24, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

    // Scrollable container for labels
    lv_obj_t *scroll_cont = lv_obj_create(sysinfo_cont);
    lv_obj_set_size(scroll_cont, LV_PCT(100), LV_PCT(85));
    lv_obj_align(scroll_cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(scroll_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scroll_cont, 0, 0);
    lv_obj_set_flex_flow(scroll_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scroll_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // Hardware section
    lv_obj_t *hw_header = lv_label_create(scroll_cont);
    lv_label_set_text(hw_header, "Hardware:");
    lv_obj_set_style_text_font(hw_header, &arimo_20, 0);
    lv_obj_set_style_text_color(hw_header, lv_color_hex(0x00FF00), 0);

    sysinfo_chip_label = lv_label_create(scroll_cont);
    lv_label_set_text(sysinfo_chip_label, "Chip: Loading...");
    lv_obj_set_style_text_font(sysinfo_chip_label, &arimo_16, 0);
    lv_obj_set_style_text_color(sysinfo_chip_label, lv_color_white(), 0);

    // Memory section
    lv_obj_t *mem_header = lv_label_create(scroll_cont);
    lv_label_set_text(mem_header, "\nMemory:");
    lv_obj_set_style_text_font(mem_header, &arimo_20, 0);
    lv_obj_set_style_text_color(mem_header, lv_color_hex(0x00FF00), 0);

    sysinfo_ram_label = lv_label_create(scroll_cont);
    lv_label_set_text(sysinfo_ram_label, "RAM: Loading...");
    lv_obj_set_style_text_font(sysinfo_ram_label, &arimo_16, 0);
    lv_obj_set_style_text_color(sysinfo_ram_label, lv_color_white(), 0);

    sysinfo_ram_min_label = lv_label_create(scroll_cont);
    lv_label_set_text(sysinfo_ram_min_label, "Min Free: Loading...");
    lv_obj_set_style_text_font(sysinfo_ram_min_label, &arimo_16, 0);
    lv_obj_set_style_text_color(sysinfo_ram_min_label, lv_color_white(), 0);

    sysinfo_psram_label = lv_label_create(scroll_cont);
    lv_label_set_text(sysinfo_psram_label, "PSRAM: Loading...");
    lv_obj_set_style_text_font(sysinfo_psram_label, &arimo_16, 0);
    lv_obj_set_style_text_color(sysinfo_psram_label, lv_color_white(), 0);

    // System section
    lv_obj_t *sys_header = lv_label_create(scroll_cont);
    lv_label_set_text(sys_header, "\nSystem:");
    lv_obj_set_style_text_font(sys_header, &arimo_20, 0);
    lv_obj_set_style_text_color(sys_header, lv_color_hex(0x00FF00), 0);

    sysinfo_uptime_label = lv_label_create(scroll_cont);
    lv_label_set_text(sysinfo_uptime_label, "Uptime: Loading...");
    lv_obj_set_style_text_font(sysinfo_uptime_label, &arimo_16, 0);
    lv_obj_set_style_text_color(sysinfo_uptime_label, lv_color_white(), 0);

    sysinfo_versions_label = lv_label_create(scroll_cont);
    lv_label_set_text(sysinfo_versions_label, "Versions: Loading...");
    lv_obj_set_style_text_font(sysinfo_versions_label, &arimo_14, 0);
    lv_obj_set_style_text_color(sysinfo_versions_label, lv_color_white(), 0);

    // About section
    lv_obj_t *about_header = lv_label_create(scroll_cont);
    lv_label_set_text(about_header, "\nAbout:");
    lv_obj_set_style_text_font(about_header, &arimo_20, 0);
    lv_obj_set_style_text_color(about_header, lv_color_hex(0x00FF00), 0);

    sysinfo_author_label = lv_label_create(scroll_cont);
    lv_label_set_text(sysinfo_author_label, "Author: Loading...");
    lv_obj_set_style_text_font(sysinfo_author_label, &arimo_16, 0);
    lv_obj_set_style_text_color(sysinfo_author_label, lv_color_white(), 0);

    sysinfo_build_label = lv_label_create(scroll_cont);
    lv_label_set_text(sysinfo_build_label, "Built: Loading...");
    lv_obj_set_style_text_font(sysinfo_build_label, &arimo_14, 0);
    lv_obj_set_style_text_color(sysinfo_build_label, lv_color_white(), 0);
}

/**
 * @brief Update System Info screen with new data
 */
static void update_sysinfo_screen(const struct view_data_system_info *info)
{
    if (!info) return;

    lv_port_sem_take();

    // Hardware
    char buf[128];
    snprintf(buf, sizeof(buf), "Chip: %s (%u cores @ %lu MHz)", 
            info->chip_model, info->cpu_cores, info->cpu_freq_mhz);
    lv_label_set_text(sysinfo_chip_label, buf);

    // Memory
    snprintf(buf, sizeof(buf), "RAM: %lu KB free / %lu KB total", 
            info->heap_free / 1024, info->heap_total / 1024);
    lv_label_set_text(sysinfo_ram_label, buf);

    snprintf(buf, sizeof(buf), "Min Free: %lu KB", info->heap_min_free / 1024);
    lv_label_set_text(sysinfo_ram_min_label, buf);

    if (info->psram_total > 0) {
        snprintf(buf, sizeof(buf), "PSRAM: %lu MB free / %lu MB total", 
                info->psram_free / (1024*1024), info->psram_total / (1024*1024));
    } else {
        snprintf(buf, sizeof(buf), "PSRAM: Not available");
    }
    lv_label_set_text(sysinfo_psram_label, buf);

    // System
    uint32_t days = info->uptime_seconds / 86400;
    uint32_t hours = (info->uptime_seconds % 86400) / 3600;
    uint32_t mins = (info->uptime_seconds % 3600) / 60;

    if (days > 0) {
        snprintf(buf, sizeof(buf), "Uptime: %lud %luh %lum", days, hours, mins);
    } else {
        snprintf(buf, sizeof(buf), "Uptime: %luh %lum", hours, mins);
    }
    lv_label_set_text(sysinfo_uptime_label, buf);

    snprintf(buf, sizeof(buf), "App: %s | IDF: %s", 
            info->app_version, info->idf_version);
    lv_label_set_text(sysinfo_versions_label, buf);

    // About
    snprintf(buf, sizeof(buf), "Author: %s", info->author);
    lv_label_set_text(sysinfo_author_label, buf);

    snprintf(buf, sizeof(buf), "Built: %s %s", 
            info->compile_date, info->compile_time);
    lv_label_set_text(sysinfo_build_label, buf);

    lv_port_sem_give();
}

/**
 * @brief Update train details screen
 */
static void update_train_details_screen(const struct view_data_train_details *data)
{
    if (!train_details_screen || !data) return;
    
    lv_port_sem_take();
    
    lv_obj_add_flag(train_details_loading, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(train_details_view, LV_OBJ_FLAG_HIDDEN);
    
    // Title
    char title[64];
    snprintf(title, sizeof(title), "%s - %s", data->name, data->operator);
    lv_label_set_text(train_details_title, title);
    
    // Capacity
    char cap[64];
    snprintf(cap, sizeof(cap), "1st: %s  2nd: %s", 
             data->capacity_1st[0] ? data->capacity_1st : "-",
             data->capacity_2nd[0] ? data->capacity_2nd : "-");
    lv_label_set_text(train_details_cap1, cap);
    
    // List
    lv_obj_clean(train_details_list);
    
    for (int i = 0; i < data->stop_count; i++) {
        lv_obj_t *item = lv_obj_create(train_details_list);
        lv_obj_set_size(item, LV_PCT(100), 30);
        lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_pad_all(item, 0, 0);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        
        // Time
        lv_obj_t *lbl_time = lv_label_create(item);
        if (data->stops[i].departure[0]) {
            lv_label_set_text(lbl_time, data->stops[i].departure);
        } else {
            lv_label_set_text(lbl_time, data->stops[i].arrival); // Last stop
        }
        lv_obj_set_style_text_color(lbl_time, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl_time, &arimo_16, 0);
        lv_obj_align(lbl_time, LV_ALIGN_LEFT_MID, 5, 0);
        
        // Name
        lv_obj_t *lbl_name = lv_label_create(item);
        lv_label_set_text(lbl_name, data->stops[i].name);
        lv_obj_set_style_text_color(lbl_name, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl_name, &arimo_16, 0);
        lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, 60, 0);
        
        // Delay (if any)
        if (data->stops[i].delay > 0) {
            lv_obj_t *lbl_delay = lv_label_create(item);
            lv_label_set_text_fmt(lbl_delay, "+%d'", data->stops[i].delay);
            lv_obj_set_style_text_color(lbl_delay, lv_color_hex(0xFFD700), 0);
            lv_obj_align(lbl_delay, LV_ALIGN_RIGHT_MID, -5, 0);
        }
    }
    
    lv_port_sem_give();
}

/**
 * @brief Create train details screen
 */
static void create_train_details_screen(lv_obj_t *parent)
{
    // It's a modal over the parent (tab)
    train_details_screen = lv_obj_create(parent);
    lv_obj_set_size(train_details_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(train_details_screen, lv_color_hex(0x101010), 0); // Slightly lighter black
    lv_obj_set_style_bg_opa(train_details_screen, LV_OPA_COVER, 0); // Opaque
    lv_obj_set_style_border_width(train_details_screen, 2, 0);
    lv_obj_set_style_border_color(train_details_screen, lv_color_hex(0xFFFFFF), 0); // White border to make it pop
    lv_obj_set_style_pad_all(train_details_screen, 0, 0);
    lv_obj_add_flag(train_details_screen, LV_OBJ_FLAG_HIDDEN); // Hidden by default
    
    // Header
    lv_obj_t *header = lv_obj_create(train_details_screen);
    lv_obj_set_size(header, LV_PCT(100), 60); // Taller header
    lv_obj_set_style_bg_color(header, lv_color_hex(0x303030), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    
    train_details_close_btn = lv_btn_create(header);
    lv_obj_set_size(train_details_close_btn, 50, 50); // Bigger close button
    lv_obj_align(train_details_close_btn, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_set_style_bg_color(train_details_close_btn, lv_color_hex(0xFF0000), 0); // Red button
    lv_obj_add_event_cb(train_details_close_btn, details_close_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_x = lv_label_create(train_details_close_btn);
    lv_label_set_text(lbl_x, LV_SYMBOL_CLOSE);
    lv_obj_center(lbl_x);
    
    train_details_title = lv_label_create(header);
    lv_label_set_text(train_details_title, "Details");
    lv_obj_set_style_text_font(train_details_title, &arimo_20, 0);
    lv_obj_set_width(train_details_title, LV_PCT(75)); // Limit width to avoid overlap with close button
    lv_label_set_long_mode(train_details_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(train_details_title, LV_ALIGN_LEFT_MID, 10, 0);
    
    // Loading
    train_details_loading = lv_obj_create(train_details_screen);
    lv_obj_set_size(train_details_loading, LV_PCT(100), LV_PCT(80));
    lv_obj_set_y(train_details_loading, 60);
    lv_obj_set_style_bg_opa(train_details_loading, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(train_details_loading, 0, 0);
    
    lv_obj_t *load_lbl = lv_label_create(train_details_loading);
    lv_label_set_text(load_lbl, "Loading details...");
    lv_obj_set_style_text_font(load_lbl, &arimo_20, 0);
    lv_obj_center(load_lbl);
    
    // Content View
    train_details_view = lv_obj_create(train_details_screen);
    lv_obj_set_size(train_details_view, LV_PCT(100), LV_PCT(85));
    lv_obj_set_y(train_details_view, 60);
    lv_obj_set_style_bg_opa(train_details_view, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(train_details_view, 0, 0);
    lv_obj_set_style_pad_all(train_details_view, 0, 0);
    lv_obj_add_flag(train_details_view, LV_OBJ_FLAG_HIDDEN);
    
    // Capacity info
    train_details_cap1 = lv_label_create(train_details_view);
    lv_label_set_text(train_details_cap1, "Capacity: -");
    lv_obj_set_style_text_font(train_details_cap1, &arimo_16, 0);
    lv_obj_set_style_text_color(train_details_cap1, lv_color_hex(0xCCCCCC), 0);
    lv_obj_align(train_details_cap1, LV_ALIGN_TOP_LEFT, 10, 5);
    
    // List of stops
    train_details_list = lv_obj_create(train_details_view);
    lv_obj_set_size(train_details_list, LV_PCT(100), LV_PCT(85));
    lv_obj_align(train_details_list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(train_details_list, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(train_details_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(train_details_list, 2, 0);
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
    create_wifi_saved_screen(settings_screen);
    create_wifi_add_screen(settings_screen);
    
    // Create System Info Screen (Hidden)
    create_sysinfo_screen(settings_screen);
    
    // Main menu: three buttons
    lv_obj_t *btn_wifi = lv_btn_create(settings_main_cont);
    lv_obj_set_size(btn_wifi, LV_PCT(90), 55);
    lv_obj_align(btn_wifi, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_add_event_cb(btn_wifi, wifi_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_wifi = lv_label_create(btn_wifi);
    lv_label_set_text(lbl_wifi, "WiFi");
    lv_obj_set_style_text_font(lbl_wifi, &arimo_20, 0);
    lv_obj_center(lbl_wifi);

    lv_obj_t *btn_display = lv_btn_create(settings_main_cont);
    lv_obj_set_size(btn_display, LV_PCT(90), 55);
    lv_obj_align(btn_display, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_add_event_cb(btn_display, display_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_display = lv_label_create(btn_display);
    lv_label_set_text(lbl_display, "Display");
    lv_obj_set_style_text_font(lbl_display, &arimo_20, 0);
    lv_obj_center(lbl_display);

    // System Info button
    lv_obj_t *btn_sysinfo = lv_btn_create(settings_main_cont);
    lv_obj_set_size(btn_sysinfo, LV_PCT(90), 55);
    lv_obj_align(btn_sysinfo, LV_ALIGN_TOP_MID, 0, 170);
    lv_obj_add_event_cb(btn_sysinfo, sysinfo_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_sysinfo = lv_label_create(btn_sysinfo);
    lv_label_set_text(lbl_sysinfo, "System Info");
    lv_obj_set_style_text_font(lbl_sysinfo, &arimo_20, 0);
    lv_obj_center(lbl_sysinfo);

    // Display settings submenu (hidden by default)
    display_settings_cont = lv_obj_create(settings_screen);
    lv_obj_set_size(display_settings_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(display_settings_cont, 10, 0);
    lv_obj_set_style_bg_opa(display_settings_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(display_settings_cont, 0, 0);
    lv_obj_add_flag(display_settings_cont, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *display_back_btn = lv_btn_create(display_settings_cont);
    lv_obj_set_size(display_back_btn, 60, 40);
    lv_obj_align(display_back_btn, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_add_event_cb(display_back_btn, display_back_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *display_back_lbl = lv_label_create(display_back_btn);
    lv_label_set_text(display_back_lbl, "<");
    lv_obj_center(display_back_lbl);

    lv_obj_t *display_title = lv_label_create(display_settings_cont);
    lv_label_set_text(display_title, "Display");
    lv_obj_set_style_text_font(display_title, &arimo_20, 0);
    lv_obj_set_style_text_color(display_title, lv_color_white(), 0);
    lv_obj_align(display_title, LV_ALIGN_TOP_MID, 0, 15);

    brightness_label = lv_label_create(display_settings_cont);
    lv_label_set_text(brightness_label, "Brightness: 50%");
    lv_obj_align(brightness_label, LV_ALIGN_TOP_LEFT, 10, 70);
    lv_obj_set_style_text_color(brightness_label, lv_color_white(), 0);

    brightness_slider = lv_slider_create(display_settings_cont);
    lv_obj_set_size(brightness_slider, LV_PCT(80), 20);
    lv_obj_align(brightness_slider, LV_ALIGN_TOP_LEFT, 10, 100);
    lv_slider_set_range(brightness_slider, 1, 100);  // 1-100 range to avoid black screen at 0
    lv_slider_set_value(brightness_slider, 50, LV_ANIM_OFF);
    lv_obj_add_event_cb(brightness_slider, brightness_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    sleep_label = lv_label_create(display_settings_cont);
    lv_label_set_text(sleep_label, "Timeout: Always On");
    lv_obj_align(sleep_label, LV_ALIGN_TOP_LEFT, 10, 150);
    lv_obj_set_style_text_color(sleep_label, lv_color_white(), 0);

    sleep_slider = lv_slider_create(display_settings_cont);
    lv_obj_set_size(sleep_slider, LV_PCT(80), 20);
    lv_obj_align(sleep_slider, LV_ALIGN_TOP_LEFT, 10, 180);
    lv_slider_set_range(sleep_slider, 0, 100);
    lv_slider_set_value(sleep_slider, 0, LV_ANIM_OFF);
    lv_obj_add_event_cb(sleep_slider, sleep_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Apply button to save configuration
    display_apply_btn = lv_btn_create(display_settings_cont);
    lv_obj_set_size(display_apply_btn, LV_PCT(80), 50);
    lv_obj_align(display_apply_btn, LV_ALIGN_TOP_LEFT, 10, 230);
    lv_obj_set_style_bg_color(display_apply_btn, lv_color_hex(0x008000), 0); // Green
    lv_obj_add_event_cb(display_apply_btn, display_apply_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *apply_lbl = lv_label_create(display_apply_btn);
    lv_label_set_text(apply_lbl, "Apply & Save");
    lv_obj_set_style_text_font(apply_lbl, &arimo_20, 0);
    lv_obj_center(apply_lbl);
}

/**
 * @brief Time update handler for footer and SBB clock (NTP synced -> 10s animation)
 */
static void time_update_handler(void* handler_args, esp_event_base_t base, 
                                int32_t id, void* event_data)
{
    if (id != VIEW_EVENT_TIME) return;
    if (clock_widget) {
        sbb_clock_set_time_synced(clock_widget, indicator_time_is_synced());
    }
    if (bus_time_label) {
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
        case VIEW_EVENT_TRAIN_DETAILS_UPDATE: {
            const struct view_data_train_details *data = 
                (const struct view_data_train_details *)event_data;
            update_train_details_screen(data);
            break;
        }
        case VIEW_EVENT_TRAIN_DETAILS_REQ: {
            // event_data is the journey name string (char*)
            const char *journey_name = (const char*)event_data;
            transport_data_fetch_train_details(journey_name);
            break;
        }
        case VIEW_EVENT_BUS_DETAILS_UPDATE: {
            const struct view_data_bus_details *data = 
                (const struct view_data_bus_details *)event_data;
            update_bus_details_screen(data);
            break;
        }
        case VIEW_EVENT_BUS_DETAILS_REQ: {
            const char *journey_name = (const char*)event_data;
            transport_data_fetch_bus_details(journey_name);
            break;
        }
        case VIEW_EVENT_SETTINGS_UPDATE: {
            const struct view_data_settings *data = 
                (const struct view_data_settings *)event_data;
            update_settings_screen(data);
            update_station_buttons_availability();
            break;
        }
        case VIEW_EVENT_WIFI_ST: {
            update_station_buttons_availability();
            update_wifi_network_info();
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
        case VIEW_EVENT_WIFI_SAVED_LIST: {
            const struct view_data_wifi_saved_list *list = (const struct view_data_wifi_saved_list *)event_data;
            update_wifi_saved_list(list);
            break;
        }
        case VIEW_EVENT_WIFI_CONNECT_RET: {
             // struct view_data_wifi_connet_ret_msg *msg = (struct view_data_wifi_connet_ret_msg *)event_data;
             // TODO: Show result to user
             break;
        }
        case VIEW_EVENT_DISPLAY_CFG: {
            const struct view_data_display *cfg = (const struct view_data_display *)event_data;
            update_display_settings(cfg);
            break;
        }
        case VIEW_EVENT_SYSTEM_INFO_UPDATE: {
            const struct view_data_system_info *info = (const struct view_data_system_info *)event_data;
            update_sysinfo_screen(info);
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
    
    // Create tabs – Clock first, then Bus, Train, Settings
    lv_obj_t *clock_tab = lv_tabview_add_tab(tabview, "Clock");
    lv_obj_t *bus_tab = lv_tabview_add_tab(tabview, "Bus");
    lv_obj_t *train_tab = lv_tabview_add_tab(tabview, "Train");
    lv_obj_t *settings_tab = lv_tabview_add_tab(tabview, "Settings");
    
    // Remove padding from tabs and ensure no focus ring
    lv_obj_set_style_pad_all(clock_tab, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(clock_tab, 0, LV_PART_MAIN | LV_STATE_ANY);
    
    lv_obj_set_style_pad_all(bus_tab, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(bus_tab, 0, LV_PART_MAIN | LV_STATE_ANY);
    
    lv_obj_set_style_pad_all(train_tab, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(train_tab, 0, LV_PART_MAIN | LV_STATE_ANY);
    
    lv_obj_set_style_pad_all(settings_tab, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(settings_tab, 0, LV_PART_MAIN | LV_STATE_ANY);
    
    // Create clock screen (first tab) – SBB clock, all hands at 12 until NTP sync
    clock_screen = lv_obj_create(clock_tab);
    lv_obj_set_size(clock_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(clock_screen, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(clock_screen, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(clock_screen, lv_color_hex(0x1A1A1A), LV_PART_MAIN);
    lv_obj_clear_flag(clock_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_coord_t w = lv_disp_get_hor_res(NULL);
    lv_coord_t h = lv_disp_get_ver_res(NULL);
    lv_coord_t clock_size = (w < h) ? w : h;
    clock_size = (clock_size * 90) / 100;
    if (clock_size < 80) clock_size = 80;
    clock_widget = sbb_clock_create(clock_screen, clock_size);
    if (clock_widget) {
        lv_obj_center(clock_widget);
        sbb_clock_set_time_synced(clock_widget, indicator_time_is_synced());
    }
    
    // Create screens
    create_bus_screen(bus_tab);
    create_bus_details_screen(bus_tab);
    create_train_screen(train_tab);
    // Create details screen as child of train tab, so it overlays the train screen
    create_train_details_screen(train_tab);
    create_settings_screen(settings_tab);
    /* Do not call update_station_buttons_availability() here – model (time, network) is not inited yet.
     * It will run on first VIEW_EVENT_SETTINGS_UPDATE or VIEW_EVENT_WIFI_ST after model init. */
    
    // Register event handler
    esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE,
                                            VIEW_EVENT_BUS_COUNTDOWN_UPDATE,
                                            view_event_handler, NULL, NULL);
    esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE,
                                            VIEW_EVENT_TRAIN_STATION_UPDATE,
                                            view_event_handler, NULL, NULL);
    esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE,
                                            VIEW_EVENT_TRAIN_DETAILS_UPDATE,
                                            view_event_handler, NULL, NULL);
    esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE,
                                            VIEW_EVENT_TRAIN_DETAILS_REQ,
                                            view_event_handler, NULL, NULL);
    esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE,
                                            VIEW_EVENT_BUS_DETAILS_UPDATE,
                                            view_event_handler, NULL, NULL);
    esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE,
                                            VIEW_EVENT_BUS_DETAILS_REQ,
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
                                            VIEW_EVENT_WIFI_ST,
                                            view_event_handler, NULL, NULL);
    esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE,
                                            VIEW_EVENT_WIFI_LIST,
                                            view_event_handler, NULL, NULL);
    esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE,
                                            VIEW_EVENT_WIFI_SAVED_LIST,
                                            view_event_handler, NULL, NULL);
    esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE,
                                            VIEW_EVENT_WIFI_CONNECT_RET,
                                            view_event_handler, NULL, NULL);
    esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE,
                                            VIEW_EVENT_DISPLAY_CFG,
                                            view_event_handler, NULL, NULL);
    esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE,
                                            VIEW_EVENT_SYSTEM_INFO_UPDATE,
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
