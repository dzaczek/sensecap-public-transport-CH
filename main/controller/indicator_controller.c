#include "indicator_controller.h"
#include "view_data.h"
#include "esp_log.h"
#include "esp_event.h"

static const char *TAG = "controller";

/**
 * @brief Controller event handler
 */
static void controller_event_handler(void* handler_args, esp_event_base_t base, 
                                    int32_t id, void* event_data)
{
    switch (id) {
        case VIEW_EVENT_TRANSPORT_REFRESH:
            ESP_LOGI(TAG, "Manual refresh requested");
            // Refresh is handled by transport_data module
            break;
        default:
            break;
    }
}

int indicator_controller_init(void)
{
    ESP_LOGI(TAG, "Initializing controller...");
    
    // Register event handlers
    esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE,
                                            VIEW_EVENT_TRANSPORT_REFRESH,
                                            controller_event_handler, NULL, NULL);
    
    ESP_LOGI(TAG, "Controller initialized");
    return 0;
}
