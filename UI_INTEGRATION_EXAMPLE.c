/**
 * @file UI_INTEGRATION_EXAMPLE.c
 * @brief Example code for WiFi Multi-Network and System Info integration with LVGL UI
 * 
 * This file shows how to hook up the backend to the UI.
 * DO NOT compile this file directly - it's only an example/template!
 * 
 * Copy the relevant fragments to your indicator_view.c
 */

#include "lvgl.h"
#include "esp_event.h"
#include "view_data.h"

// ============================================================================
// EXAMPLE 1: WiFi Menu - Saved Networks List
// ============================================================================

static lv_obj_t *saved_networks_screen = NULL;
static lv_obj_t *saved_networks_list = NULL;

/**
 * @brief Create screen with list of saved WiFi networks
 */
static void create_saved_networks_screen(void)
{
    saved_networks_screen = lv_obj_create(NULL);
    
    // Title
    lv_obj_t *title = lv_label_create(saved_networks_screen);
    lv_label_set_text(title, "Saved Networks");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    
    // List of saved networks (LVGL list widget)
    saved_networks_list = lv_list_create(saved_networks_screen);
    lv_obj_set_size(saved_networks_list, 400, 350);
    lv_obj_align(saved_networks_list, LV_ALIGN_CENTER, 0, 0);
    
    // Bottom buttons
    lv_obj_t *btn_add = lv_btn_create(saved_networks_screen);
    lv_obj_add_event_cb(btn_add, on_add_network_btn_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_align(btn_add, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_t *label_add = lv_label_create(btn_add);
    lv_label_set_text(label_add, "+ Add Network");
    
    lv_obj_t *btn_back = lv_btn_create(saved_networks_screen);
    lv_obj_add_event_cb(btn_back, on_back_btn_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_t *label_back = lv_label_create(btn_back);
    lv_label_set_text(label_back, "< Back");
}

/**
 * @brief Callback: User opened saved networks screen
 */
static void on_saved_networks_btn_clicked(lv_event_t *e)
{
    // Show screen
    lv_scr_load(saved_networks_screen);
    
    // Clear old list
    lv_obj_clean(saved_networks_list);
    
    // Request current saved networks list from backend
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, 
                     VIEW_EVENT_WIFI_SAVED_LIST_REQ, NULL, 0, portMAX_DELAY);
}

/**
 * @brief Event handler: Backend sent list of saved networks
 */
static void handle_wifi_saved_list(struct view_data_wifi_saved_list *list)
{
    if (!list) return;
    
    ESP_LOGI("UI", "Received %d saved networks", list->count);
    
    // Clear current list
    lv_obj_clean(saved_networks_list);
    
    if (list->count == 0) {
        // No saved networks
        lv_obj_t *empty_label = lv_label_create(saved_networks_list);
        lv_label_set_text(empty_label, "No saved networks\nClick '+ Add Network' to add one");
        return;
    }
    
    // Add each saved network to list
    for (int i = 0; i < MAX_SAVED_NETWORKS; i++) {
        if (list->networks[i].valid) {
            char label_text[100];
            
            // Password icon
            const char *lock_icon = list->networks[i].have_password ? "🔒" : "🔓";
            
            snprintf(label_text, sizeof(label_text), "%s  %s", 
                    lock_icon, list->networks[i].ssid);
            
            // Add button to list
            lv_obj_t *btn = lv_list_add_btn(saved_networks_list, LV_SYMBOL_WIFI, label_text);
            
            // Store SSID as user_data (needed in callback)
            lv_obj_set_user_data(btn, strdup(list->networks[i].ssid));
            
            // Click handler: connect to this network
            lv_obj_add_event_cb(btn, on_saved_network_item_clicked, LV_EVENT_CLICKED, NULL);
            
            // "Delete" button (X)
            lv_obj_t *btn_delete = lv_btn_create(btn);
            lv_obj_set_size(btn_delete, 40, 40);
            lv_obj_align(btn_delete, LV_ALIGN_RIGHT_MID, 0, 0);
            lv_obj_set_user_data(btn_delete, strdup(list->networks[i].ssid));
            lv_obj_add_event_cb(btn_delete, on_delete_network_clicked, LV_EVENT_CLICKED, NULL);
            lv_obj_t *label_x = lv_label_create(btn_delete);
            lv_label_set_text(label_x, LV_SYMBOL_CLOSE);
        }
    }
}

/**
 * @brief Callback: User clicked on saved network (connect)
 */
static void on_saved_network_item_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const char *ssid = (const char *)lv_obj_get_user_data(btn);
    
    if (ssid && ssid[0]) {
        ESP_LOGI("UI", "Connecting to saved network: %s", ssid);
        
        // Send request to backend
        esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, 
                         VIEW_EVENT_WIFI_CONNECT_SAVED, 
                         (void *)ssid, strlen(ssid) + 1, portMAX_DELAY);
        
        // Show "Connecting..." animation
        // ... (your UI code) ...
    }
}

/**
 * @brief Callback: User clicked "Delete" (X)
 */
static void on_delete_network_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const char *ssid = (const char *)lv_obj_get_user_data(btn);
    
    if (ssid && ssid[0]) {
        ESP_LOGI("UI", "Deleting network: %s", ssid);
        
        // Send request to backend
        esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, 
                         VIEW_EVENT_WIFI_DELETE_NETWORK, 
                         (void *)ssid, strlen(ssid) + 1, portMAX_DELAY);
        
        // Backend will automatically send updated list via VIEW_EVENT_WIFI_SAVED_LIST
    }
}

/**
 * @brief Callback: User clicked "+ Add Network"
 */
static void on_add_network_btn_clicked(lv_event_t *e)
{
    // Show form for entering SSID and password
    // ... (your UI code for form) ...
    
    // After entering and clicking "Save":
    const char *ssid = "UserEnteredSSID";      // From form
    const char *password = "UserEnteredPass";  // From form
    bool has_password = true;                  // Checkbox in UI
    
    struct view_data_wifi_config cfg = {0};
    strlcpy(cfg.ssid, ssid, sizeof(cfg.ssid));
    
    if (has_password && password && password[0]) {
        strlcpy((char *)cfg.password, password, sizeof(cfg.password));
        cfg.have_password = true;
    } else {
        cfg.have_password = false;
    }
    
    // Save to backend
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, 
                     VIEW_EVENT_WIFI_SAVE_NETWORK, &cfg, sizeof(cfg), portMAX_DELAY);
    
    // Backend will send updated list via VIEW_EVENT_WIFI_SAVED_LIST
}

// ============================================================================
// EXAMPLE 2: System Info Menu (Diagnostics)
// ============================================================================

static lv_obj_t *system_info_screen = NULL;
static lv_obj_t *label_chip = NULL;
static lv_obj_t *label_ram = NULL;
static lv_obj_t *label_ram_min = NULL;
static lv_obj_t *label_psram = NULL;
static lv_obj_t *label_uptime = NULL;
static lv_obj_t *label_versions = NULL;
static lv_obj_t *label_author = NULL;
static lv_obj_t *label_build = NULL;

/**
 * @brief Create System Info screen
 */
static void create_system_info_screen(void)
{
    system_info_screen = lv_obj_create(NULL);
    
    // Title
    lv_obj_t *title = lv_label_create(system_info_screen);
    lv_label_set_text(title, "System Information");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    
    // Container for data (scrollable)
    lv_obj_t *cont = lv_obj_create(system_info_screen);
    lv_obj_set_size(cont, 440, 380);
    lv_obj_align(cont, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    
    // === Hardware ===
    lv_obj_t *header_hw = lv_label_create(cont);
    lv_label_set_text(header_hw, "Hardware:");
    lv_obj_set_style_text_font(header_hw, &lv_font_montserrat_16, 0);
    
    label_chip = lv_label_create(cont);
    lv_label_set_text(label_chip, "Chip: Loading...");
    
    // === Memory ===
    lv_obj_t *header_mem = lv_label_create(cont);
    lv_label_set_text(header_mem, "\nMemory:");
    lv_obj_set_style_text_font(header_mem, &lv_font_montserrat_16, 0);
    
    label_ram = lv_label_create(cont);
    lv_label_set_text(label_ram, "RAM: Loading...");
    
    label_ram_min = lv_label_create(cont);
    lv_label_set_text(label_ram_min, "Min Free: Loading...");
    
    label_psram = lv_label_create(cont);
    lv_label_set_text(label_psram, "PSRAM: Loading...");
    
    // === System ===
    lv_obj_t *header_sys = lv_label_create(cont);
    lv_label_set_text(header_sys, "\nSystem:");
    lv_obj_set_style_text_font(header_sys, &lv_font_montserrat_16, 0);
    
    label_uptime = lv_label_create(cont);
    lv_label_set_text(label_uptime, "Uptime: Loading...");
    
    label_versions = lv_label_create(cont);
    lv_label_set_text(label_versions, "Versions: Loading...");
    
    // === About ===
    lv_obj_t *header_about = lv_label_create(cont);
    lv_label_set_text(header_about, "\nAbout:");
    lv_obj_set_style_text_font(header_about, &lv_font_montserrat_16, 0);
    
    label_author = lv_label_create(cont);
    lv_label_set_text(label_author, "Author: Loading...");
    
    label_build = lv_label_create(cont);
    lv_label_set_text(label_build, "Built: Loading...");
    
    // Back button
    lv_obj_t *btn_back = lv_btn_create(system_info_screen);
    lv_obj_add_event_cb(btn_back, on_back_btn_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_t *label_back = lv_label_create(btn_back);
    lv_label_set_text(label_back, "< Back to Settings");
}

/**
 * @brief Event handler: Backend sent system info (every 5 seconds)
 */
static void handle_system_info_update(struct view_data_system_info *info)
{
    if (!info) return;
    
    char buf[128];
    
    // === Hardware ===
    snprintf(buf, sizeof(buf), "Chip: %s (%u cores @ %lu MHz)", 
            info->chip_model, info->cpu_cores, info->cpu_freq_mhz);
    lv_label_set_text(label_chip, buf);
    
    // === Memory ===
    snprintf(buf, sizeof(buf), "RAM: %lu KB free / %lu KB total", 
            info->heap_free / 1024, info->heap_total / 1024);
    lv_label_set_text(label_ram, buf);
    
    snprintf(buf, sizeof(buf), "Min Free: %lu KB (lowest ever)", 
            info->heap_min_free / 1024);
    lv_label_set_text(label_ram_min, buf);
    
    if (info->psram_total > 0) {
        snprintf(buf, sizeof(buf), "PSRAM: %lu MB free / %lu MB total", 
                info->psram_free / (1024*1024), info->psram_total / (1024*1024));
    } else {
        snprintf(buf, sizeof(buf), "PSRAM: Not available");
    }
    lv_label_set_text(label_psram, buf);
    
    // === System ===
    uint32_t days = info->uptime_seconds / 86400;
    uint32_t hours = (info->uptime_seconds % 86400) / 3600;
    uint32_t mins = (info->uptime_seconds % 3600) / 60;
    
    if (days > 0) {
        snprintf(buf, sizeof(buf), "Uptime: %lu days %luh %lum", days, hours, mins);
    } else {
        snprintf(buf, sizeof(buf), "Uptime: %luh %lum", hours, mins);
    }
    lv_label_set_text(label_uptime, buf);
    
    snprintf(buf, sizeof(buf), "App: %s | IDF: %s", 
            info->app_version, info->idf_version);
    lv_label_set_text(label_versions, buf);
    
    // === About ===
    snprintf(buf, sizeof(buf), "Author: %s", info->author);
    lv_label_set_text(label_author, buf);
    
    snprintf(buf, sizeof(buf), "Built: %s at %s", 
            info->compile_date, info->compile_time);
    lv_label_set_text(label_build, buf);
}

// ============================================================================
// MAIN EVENT HANDLER (add to indicator_view.c)
// ============================================================================

/**
 * @brief Main event handler from backend
 * Add these case's to your existing view_event_handler in indicator_view.c
 */
static void view_event_handler(void* handler_args, esp_event_base_t base, 
                               int32_t id, void* event_data)
{
    switch (id) {
        // ... your existing case's ...
        
        case VIEW_EVENT_WIFI_SAVED_LIST: {
            // Backend sent list of saved networks
            struct view_data_wifi_saved_list *list = 
                (struct view_data_wifi_saved_list *)event_data;
            handle_wifi_saved_list(list);
            break;
        }
        
        case VIEW_EVENT_SYSTEM_INFO_UPDATE: {
            // Backend sent system info update (every 5 seconds)
            struct view_data_system_info *info = 
                (struct view_data_system_info *)event_data;
            handle_system_info_update(info);
            break;
        }
        
        // ... rest of your case's ...
    }
}

// ============================================================================
// INITIALIZATION (add to indicator_view_init)
// ============================================================================

void indicator_view_init(void)
{
    // ... your existing initialization code ...
    
    // Create new screens
    create_saved_networks_screen();
    create_system_info_screen();
    
    // Register event handler
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, 
        VIEW_EVENT_BASE, 
        VIEW_EVENT_WIFI_SAVED_LIST, 
        view_event_handler, 
        NULL, 
        NULL
    ));
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, 
        VIEW_EVENT_BASE, 
        VIEW_EVENT_SYSTEM_INFO_UPDATE, 
        view_event_handler, 
        NULL, 
        NULL
    ));
    
    // ... rest of your initialization ...
}

// ============================================================================
// FINAL NOTES
// ============================================================================

/*
 * This code is an example/template. You need to adapt it to your UI style.
 * 
 * KEY POINTS:
 * 
 * 1. Backend AUTOMATICALLY sends:
 *    - VIEW_EVENT_SYSTEM_INFO_UPDATE every 5 seconds (always)
 *    - VIEW_EVENT_WIFI_SAVED_LIST after each REQUEST or list change
 * 
 * 2. UI only needs to:
 *    - Send VIEW_EVENT_WIFI_SAVED_LIST_REQ to get the list
 *    - Receive VIEW_EVENT_WIFI_SAVED_LIST and display data
 *    - Receive VIEW_EVENT_SYSTEM_INFO_UPDATE and update labels
 * 
 * 3. Auto-save works automatically:
 *    - After each successful WiFi connection, network is automatically saved
 *    - You don't need to do anything in UI
 * 
 * 4. User_data:
 *    - Use lv_obj_set_user_data() to store SSID at buttons
 *    - REMEMBER to free() if you use strdup() (or use static buffer)
 * 
 * 5. LVGL Mutex:
 *    - Always use lv_port_sem_take/give when modifying UI from callback
 */
