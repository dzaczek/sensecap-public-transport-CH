/**
 * @file UI_INTEGRATION_EXAMPLE.c
 * @brief Przykładowy kod integracji WiFi Multi-Network i System Info z LVGL UI
 * 
 * Ten plik pokazuje jak podpiąć backend pod UI.
 * NIE kompiluj tego pliku bezpośrednio - to tylko przykład/szablon!
 * 
 * Skopiuj odpowiednie fragmenty do swojego indicator_view.c
 */

#include "lvgl.h"
#include "esp_event.h"
#include "view_data.h"

// ============================================================================
// PRZYKŁAD 1: Menu WiFi - Lista zapisanych sieci
// ============================================================================

static lv_obj_t *saved_networks_screen = NULL;
static lv_obj_t *saved_networks_list = NULL;

/**
 * @brief Stwórz ekran z listą zapisanych sieci WiFi
 */
static void create_saved_networks_screen(void)
{
    saved_networks_screen = lv_obj_create(NULL);
    
    // Tytuł
    lv_obj_t *title = lv_label_create(saved_networks_screen);
    lv_label_set_text(title, "Saved Networks");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    
    // Lista zapisanych sieci (LVGL list widget)
    saved_networks_list = lv_list_create(saved_networks_screen);
    lv_obj_set_size(saved_networks_list, 400, 350);
    lv_obj_align(saved_networks_list, LV_ALIGN_CENTER, 0, 0);
    
    // Przyciski na dole
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
 * @brief Callback: Użytkownik otworzył ekran zapisanych sieci
 */
static void on_saved_networks_btn_clicked(lv_event_t *e)
{
    // Pokaż ekran
    lv_scr_load(saved_networks_screen);
    
    // Wyczyść starą listę
    lv_obj_clean(saved_networks_list);
    
    // Poproś backend o aktualną listę zapisanych sieci
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, 
                     VIEW_EVENT_WIFI_SAVED_LIST_REQ, NULL, 0, portMAX_DELAY);
}

/**
 * @brief Event handler: Backend przysłał listę zapisanych sieci
 */
static void handle_wifi_saved_list(struct view_data_wifi_saved_list *list)
{
    if (!list) return;
    
    ESP_LOGI("UI", "Received %d saved networks", list->count);
    
    // Wyczyść aktualną listę
    lv_obj_clean(saved_networks_list);
    
    if (list->count == 0) {
        // Brak zapisanych sieci
        lv_obj_t *empty_label = lv_label_create(saved_networks_list);
        lv_label_set_text(empty_label, "No saved networks\nClick '+ Add Network' to add one");
        return;
    }
    
    // Dodaj każdą zapisaną sieć do listy
    for (int i = 0; i < MAX_SAVED_NETWORKS; i++) {
        if (list->networks[i].valid) {
            char label_text[100];
            
            // Ikona hasła
            const char *lock_icon = list->networks[i].have_password ? "🔒" : "🔓";
            
            snprintf(label_text, sizeof(label_text), "%s  %s", 
                    lock_icon, list->networks[i].ssid);
            
            // Dodaj przycisk do listy
            lv_obj_t *btn = lv_list_add_btn(saved_networks_list, LV_SYMBOL_WIFI, label_text);
            
            // Zapisz SSID jako user_data (potrzebne w callbacku)
            lv_obj_set_user_data(btn, strdup(list->networks[i].ssid));
            
            // Handler kliknięcia: połącz z tą siecią
            lv_obj_add_event_cb(btn, on_saved_network_item_clicked, LV_EVENT_CLICKED, NULL);
            
            // Przycisk "Delete" (X)
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
 * @brief Callback: Użytkownik kliknął na zapisaną sieć (połącz)
 */
static void on_saved_network_item_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const char *ssid = (const char *)lv_obj_get_user_data(btn);
    
    if (ssid && ssid[0]) {
        ESP_LOGI("UI", "Connecting to saved network: %s", ssid);
        
        // Wyślij request do backendu
        esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, 
                         VIEW_EVENT_WIFI_CONNECT_SAVED, 
                         (void *)ssid, strlen(ssid) + 1, portMAX_DELAY);
        
        // Pokaż animację "Connecting..."
        // ... (twój kod UI) ...
    }
}

/**
 * @brief Callback: Użytkownik kliknął "Delete" (X)
 */
static void on_delete_network_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const char *ssid = (const char *)lv_obj_get_user_data(btn);
    
    if (ssid && ssid[0]) {
        ESP_LOGI("UI", "Deleting network: %s", ssid);
        
        // Wyślij request do backendu
        esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, 
                         VIEW_EVENT_WIFI_DELETE_NETWORK, 
                         (void *)ssid, strlen(ssid) + 1, portMAX_DELAY);
        
        // Backend automatycznie wyśle zaktualizowaną listę przez VIEW_EVENT_WIFI_SAVED_LIST
    }
}

/**
 * @brief Callback: Użytkownik kliknął "+ Add Network"
 */
static void on_add_network_btn_clicked(lv_event_t *e)
{
    // Pokaż formularz do wpisania SSID i hasła
    // ... (twój kod UI dla formularza) ...
    
    // Po wpisaniu i kliknięciu "Save":
    const char *ssid = "UserEnteredSSID";      // Z formularza
    const char *password = "UserEnteredPass";  // Z formularza
    bool has_password = true;                  // Checkbox w UI
    
    struct view_data_wifi_config cfg = {0};
    strlcpy(cfg.ssid, ssid, sizeof(cfg.ssid));
    
    if (has_password && password && password[0]) {
        strlcpy((char *)cfg.password, password, sizeof(cfg.password));
        cfg.have_password = true;
    } else {
        cfg.have_password = false;
    }
    
    // Zapisz do backendu
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, 
                     VIEW_EVENT_WIFI_SAVE_NETWORK, &cfg, sizeof(cfg), portMAX_DELAY);
    
    // Backend wyśle zaktualizowaną listę przez VIEW_EVENT_WIFI_SAVED_LIST
}

// ============================================================================
// PRZYKŁAD 2: Menu System Info (Diagnostyka)
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
 * @brief Stwórz ekran System Info
 */
static void create_system_info_screen(void)
{
    system_info_screen = lv_obj_create(NULL);
    
    // Tytuł
    lv_obj_t *title = lv_label_create(system_info_screen);
    lv_label_set_text(title, "System Information");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    
    // Kontener na dane (scrollable)
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
    
    // Przycisk Back
    lv_obj_t *btn_back = lv_btn_create(system_info_screen);
    lv_obj_add_event_cb(btn_back, on_back_btn_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_t *label_back = lv_label_create(btn_back);
    lv_label_set_text(label_back, "< Back to Settings");
}

/**
 * @brief Event handler: Backend przysłał system info (co 5 sekund)
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
// GŁÓWNY EVENT HANDLER (dodaj do indicator_view.c)
// ============================================================================

/**
 * @brief Główny handler eventów z backendu
 * Dodaj te case'y do swojego istniejącego view_event_handler w indicator_view.c
 */
static void view_event_handler(void* handler_args, esp_event_base_t base, 
                               int32_t id, void* event_data)
{
    switch (id) {
        // ... twoje istniejące case'y ...
        
        case VIEW_EVENT_WIFI_SAVED_LIST: {
            // Backend przysłał listę zapisanych sieci
            struct view_data_wifi_saved_list *list = 
                (struct view_data_wifi_saved_list *)event_data;
            handle_wifi_saved_list(list);
            break;
        }
        
        case VIEW_EVENT_SYSTEM_INFO_UPDATE: {
            // Backend przysłał aktualizację system info (co 5 sekund)
            struct view_data_system_info *info = 
                (struct view_data_system_info *)event_data;
            handle_system_info_update(info);
            break;
        }
        
        // ... reszta twoich case'ów ...
    }
}

// ============================================================================
// INICJALIZACJA (dodaj do indicator_view_init)
// ============================================================================

void indicator_view_init(void)
{
    // ... twój istniejący kod inicjalizacji ...
    
    // Stwórz nowe ekrany
    create_saved_networks_screen();
    create_system_info_screen();
    
    // Zarejestruj event handler
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
    
    // ... reszta twojej inicjalizacji ...
}

// ============================================================================
// UWAGI KOŃCOWE
// ============================================================================

/*
 * Ten kod to przykład/template. Musisz go dostosować do swojego stylu UI.
 * 
 * KLUCZOWE PUNKTY:
 * 
 * 1. Backend AUTOMATYCZNIE wysyła:
 *    - VIEW_EVENT_SYSTEM_INFO_UPDATE co 5 sekund (zawsze)
 *    - VIEW_EVENT_WIFI_SAVED_LIST po każdym REQUEST lub zmianie listy
 * 
 * 2. UI musi TYLKO:
 *    - Wysłać VIEW_EVENT_WIFI_SAVED_LIST_REQ aby dostać listę
 *    - Odbierać VIEW_EVENT_WIFI_SAVED_LIST i wyświetlić dane
 *    - Odbierać VIEW_EVENT_SYSTEM_INFO_UPDATE i aktualizować labele
 * 
 * 3. Auto-save działa automatycznie:
 *    - Po każdym udanym połączeniu WiFi, sieć jest automatycznie zapisywana
 *    - Nie musisz nic robić w UI
 * 
 * 4. User_data:
 *    - Używam lv_obj_set_user_data() do przechowywania SSID przy przyciskach
 *    - PAMIĘTAJ o free() jeśli używasz strdup() (lub użyj static buffer)
 * 
 * 5. LVGL Mutex:
 *    - Zawsze używaj lv_port_sem_take/give gdy modyfikujesz UI z callbacku
 */
