#include "indicator_model.h"
#include "indicator_storage.h"
#include "indicator_wifi.h"
#include "indicator_display.h"
#include "indicator_time.h"
#include "indicator_btn.h"
#include "network_manager.h"
#include "transport_data.h"
#include "esp_log.h"

static const char *TAG = "model";

int indicator_model_init(void)
{
    ESP_LOGI(TAG, "Initializing model components...");
    
    indicator_storage_init();
    indicator_wifi_init();
    indicator_time_init();
    indicator_display_init();
    indicator_btn_init();
    
    // Initialize network manager (must be after WiFi)
    network_manager_init();
    
    // Initialize transport data module with smart refresh
    transport_data_init();
    
    ESP_LOGI(TAG, "Model initialization complete");
    return 0;
}
