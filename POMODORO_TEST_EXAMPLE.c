/**
 * @file POMODORO_TEST_EXAMPLE.c
 * @brief Standalone test example for Pomodoro Timer View
 * 
 * HOW TO USE:
 * -----------
 * 1. Replace content of main/main.c with this file (make backup first!)
 * 2. Build and flash: idf.py build flash monitor
 * 3. Touch the hourglass on screen to flip and start timer
 * 
 * OR integrate into existing tabview (see POMODORO_INTEGRATION.md)
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bsp_board.h"
#include "lv_port.h"
#include "lvgl.h"
#include "indicator_pomodoro.h"

static const char *TAG = "pomodoro_test";

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Pomodoro Timer - Standalone Test");
    ESP_LOGI(TAG, "========================================");
    
    // Initialize board (display, touch, etc.)
    ESP_LOGI(TAG, "Initializing board...");
    ESP_ERROR_CHECK(bsp_board_init());
    
    // Initialize LVGL port
    ESP_LOGI(TAG, "Initializing LVGL...");
    lv_port_init();
    
    ESP_LOGI(TAG, "Heap before Pomodoro init: %d bytes", esp_get_free_heap_size());
    
    // Get active screen
    lv_obj_t *screen = lv_scr_act();
    
    // Initialize Pomodoro Timer View
    ESP_LOGI(TAG, "Creating Pomodoro Timer...");
    lv_obj_t *pomodoro = lv_indicator_pomodoro_init(screen);
    
    if (pomodoro == NULL) {
        ESP_LOGE(TAG, "Failed to create Pomodoro timer!");
        return;
    }
    
    ESP_LOGI(TAG, "Heap after Pomodoro init: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Pomodoro Timer Ready!");
    ESP_LOGI(TAG, "- Tap hourglass to flip & start timer");
    ESP_LOGI(TAG, "- 25-minute session will begin");
    ESP_LOGI(TAG, "- Sand will flow through hourglass");
    ESP_LOGI(TAG, "========================================");
    
    // Main loop - just keep LVGL running
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // Optional: Log status every 10 seconds
        static int log_counter = 0;
        if (++log_counter >= 10) {
            log_counter = 0;
            
            bool running = lv_indicator_pomodoro_is_running();
            int remaining = lv_indicator_pomodoro_get_remaining_seconds();
            int minutes = remaining / 60;
            int seconds = remaining % 60;
            
            ESP_LOGI(TAG, "Status: %s | Time: %02d:%02d | Free heap: %d bytes",
                     running ? "RUNNING" : "PAUSED",
                     minutes, seconds,
                     esp_get_free_heap_size());
        }
    }
}

/**
 * ALTERNATIVE: Integration with TabView
 * --------------------------------------
 * If you want to add Pomodoro to existing multi-screen app:
 */
#if 0  // Set to 1 to enable this code

#include "indicator_view.h"

void add_pomodoro_to_tabview(void)
{
    // Assuming you have a tabview object
    extern lv_obj_t *tabview;  // Your existing tabview
    
    // Add new tab
    lv_obj_t *tab_pomodoro = lv_tabview_add_tab(tabview, LV_SYMBOL_LOOP " Timer");
    
    // Initialize Pomodoro in this tab
    lv_indicator_pomodoro_init(tab_pomodoro);
    
    ESP_LOGI(TAG, "Pomodoro tab added to tabview");
}

#endif

/**
 * EXPECTED OUTPUT
 * ---------------
 * When running, you should see:
 * 
 * Visual:
 * - Hourglass shape made of blue glass outline
 * - Golden sand particles in top chamber
 * - Time display showing "25:00"
 * - Status text "Tap to Start"
 * - Back button (top-left)
 * 
 * After tapping hourglass:
 * - Hourglass flips (sand moves to other chamber)
 * - Timer counts down: 24:59, 24:58...
 * - Sand particles flow through narrow neck
 * - Sand accumulates in bottom chamber
 * - Status changes to "Focus Time"
 * 
 * After 25 minutes:
 * - Timer shows "00:00"
 * - Status: "Session Complete!"
 * - Sand stops flowing
 * 
 * Performance:
 * - Physics runs on Core 1 @ 25 FPS
 * - Rendering runs on Core 0 @ 20 FPS
 * - Should be smooth on ESP32-S3
 * 
 * Memory:
 * - Canvas buffer: ~168 KB (240x280x2 bytes)
 * - Sand grids: ~16 KB (120x140x2 grids)
 * - Total: ~200 KB (uses PSRAM for canvas)
 */

/**
 * TROUBLESHOOTING
 * ---------------
 * 
 * 1. Screen is black:
 *    - Check display initialization in bsp_board_init()
 *    - Verify LVGL port is running (lv_port_init())
 *    - Check brightness settings
 * 
 * 2. Touch doesn't work:
 *    - Verify touch controller initialization
 *    - Check if LV_INDEV_TYPE_POINTER is configured
 *    - Add debug logs in canvas_event_cb()
 * 
 * 3. Sand doesn't fall:
 *    - Check if physics task started (see Core 1 in logs)
 *    - Verify is_running flag is true after tap
 *    - Check grid_mutex isn't deadlocked
 * 
 * 4. Performance issues / lag:
 *    - Increase PHYSICS_UPDATE_MS (40→50ms)
 *    - Increase RENDER_UPDATE_MS (50→66ms)
 *    - Increase SAND_PARTICLE_SIZE (2→3 or 4)
 *    - Reduce max_sand_particles in init_sand_grid()
 * 
 * 5. Crash / ESP_ERR_NO_MEM:
 *    - Check PSRAM is enabled in sdkconfig
 *    - Verify heap_caps_malloc() succeeds
 *    - Monitor free heap with esp_get_free_heap_size()
 * 
 * 6. Canvas doesn't render:
 *    - Check canvas buffer allocation
 *    - Verify LV_COLOR_DEPTH matches (RGB565 = 16-bit)
 *    - Ensure LV_CANVAS_BUF_SIZE_TRUE_COLOR macro is correct
 */
