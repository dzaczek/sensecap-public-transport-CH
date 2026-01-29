#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bsp_board.h"
#include "lv_port.h"
#include "esp_event.h"
#include "esp_event_base.h"

#include "indicator_model.h"
#include "indicator_view.h"
#include "transport_data.h"
#include "network_manager.h"
#include "indicator_display.h"
#include "view_data.h"

static const char *TAG = "app_main";

#define VERSION   "v1.0.0"

#define SENSECAP  "\n\
   _____                      _________    ____         \n\
  / ___/___  ____  ________  / ____/   |  / __ \\       \n\
  \\__ \\/ _ \\/ __ \\/ ___/ _ \\/ /   / /| | / /_/ /   \n\
 ___/ /  __/ / / (__  )  __/ /___/ ___ |/ ____/         \n\
/____/\\___/_/ /_/____/\\___/\\____/_/  |_/_/           \n\
--------------------------------------------------------\n\
 Version: %s %s %s\n\
--------------------------------------------------------\n\
"

ESP_EVENT_DEFINE_BASE(VIEW_EVENT_BASE);
esp_event_loop_handle_t view_event_handle;

/**
 * @brief Task to periodically update settings screen
 */
static void settings_update_task(void *arg)
{
    struct view_data_settings settings = {0};
    
    while (1) {
        // Get WiFi status
        network_manager_get_wifi_status(&settings.wifi_status);
        
        // Get IP address
        network_manager_get_ip(settings.ip_address);
        
        // Check API status (check if we can reach the API)
        settings.api_status = network_manager_is_connected();
        
        // Get brightness (from display config if available)
        settings.brightness = 50;  // Default, TODO: Load from storage
        
        // Get sleep timeout (from storage if needed)
        settings.sleep_timeout_min = 0;  // TODO: Load from storage
        
        // Post update event
        esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SETTINGS_UPDATE,
                         &settings, sizeof(settings), portMAX_DELAY);
        
        vTaskDelay(pdMS_TO_TICKS(5000));  // Update every 5 seconds
    }
}

/**
 * @brief Task for initial data fetch after WiFi connects
 */
static void initial_fetch_task(void *arg)
{
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    
    // Wait for WiFi connection (up to 60 seconds to handle slow or unstable connections)
    int retries = 0;
    while (!network_manager_is_connected() && retries < 60) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        retries++;
        if (retries % 10 == 0) {
            ESP_LOGI(TAG, "Still waiting for WiFi... (%d/60)", retries);
        }
    }
    
    if (!network_manager_is_connected()) {
        ESP_LOGW(TAG, "WiFi connection timeout after 60 seconds");
        ESP_LOGW(TAG, "Application will continue with limited functionality");
        ESP_LOGW(TAG, "Data refresh will start automatically when WiFi connects");
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "WiFi connected after %d seconds", retries);

    ESP_LOGI(TAG, "WiFi connected, waiting for time sync...");
    
    // Wait for time sync (up to 120 seconds to handle unstable WiFi)
    retries = 0;
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    while (timeinfo.tm_year < (2020 - 1900) && retries < 120) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        time(&now);
        localtime_r(&now, &timeinfo);
        retries++;
        
        // Check if we're still connected to WiFi
        if (!network_manager_is_connected() && retries % 10 == 0) {
            ESP_LOGW(TAG, "WiFi disconnected during time sync, waiting for reconnection...");
        }
        
        if (retries % 10 == 0) {
            ESP_LOGI(TAG, "Waiting for time sync... (%d/120)", retries);
        }
    }
    
    if (timeinfo.tm_year < (2020 - 1900)) {
        ESP_LOGW(TAG, "Time sync timeout after %d seconds - continuing with unsynced time", retries);
        ESP_LOGW(TAG, "Application will retry NTP sync when WiFi connection stabilizes");
    } else {
        ESP_LOGI(TAG, "Time synced successfully after %d seconds: %s", retries, asctime(&timeinfo));
    }

    ESP_LOGI(TAG, "Checking internet connectivity (ping 1.1.1.1)...");
    if (network_manager_ping("1.1.1.1") == ESP_OK) {
        ESP_LOGI(TAG, "Internet access confirmed");
        
        // Start refresh timer (it will check if station is selected before fetching)
        TimerHandle_t refresh_timer = transport_data_get_refresh_timer();
        if (refresh_timer) {
            int interval = transport_data_get_refresh_interval();
            xTimerChangePeriod(refresh_timer, pdMS_TO_TICKS(interval * 60 * 1000), 0);
            xTimerStart(refresh_timer, 0);
            ESP_LOGI(TAG, "Started refresh timer with %d minute interval", interval);
        }
    } else {
        ESP_LOGE(TAG, "Ping failed, no internet access?");
    }
    
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI("", SENSECAP, VERSION, __DATE__, __TIME__);

    ESP_ERROR_CHECK(bsp_board_init());
    lv_port_init();

    esp_event_loop_args_t view_event_task_args = {
        .queue_size = 20,
        .task_name = "view_event_task",
        .task_priority = uxTaskPriorityGet(NULL),
        .task_stack_size = 10240,
        .task_core_id = tskNO_AFFINITY
    };

    ESP_ERROR_CHECK(esp_event_loop_create(&view_event_task_args, &view_event_handle));

    lv_port_sem_take();
    indicator_view_init();
    lv_port_sem_give();

    indicator_model_init();
    /* Trigger initial settings/button state now that model is inited */
    {
        struct view_data_settings settings = {0};
        network_manager_get_wifi_status(&settings.wifi_status);
        network_manager_get_ip(settings.ip_address);
        settings.api_status = network_manager_is_connected();
        settings.brightness = 50;
        settings.sleep_timeout_min = 0;
        esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SETTINGS_UPDATE,
                          &settings, sizeof(settings), portMAX_DELAY);
    }
    // Initialize controller
    extern int indicator_controller_init(void);
    indicator_controller_init();
    
    // Create task for initial data fetch
    xTaskCreate(initial_fetch_task, "initial_fetch", 8192, NULL, 5, NULL);
    
    // Create task for settings updates
    xTaskCreate(settings_update_task, "settings_update", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Application started");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
