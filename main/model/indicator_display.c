#include "indicator_display.h"
#include "freertos/semphr.h"

#include "driver/ledc.h"
#include "esp_timer.h"
#include "nvs.h"

#define DISPLAY_CFG_STORAGE  "display"

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO          (45) // Define the output GPIO
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT // Set duty resolution to 13 bits
#define LEDC_FREQUENCY          (5000) // Frequency in Hertz. Set frequency at 5 kHz
#define LEDC_MAX_DUTY           ((1 << LEDC_DUTY_RES) - 1) // 8191 for 13-bit

static const char *TAG = "display";

/**
 * @brief Consolidated manager structure - single source of truth
 */
static struct {
    struct view_data_display cfg;
    esp_timer_handle_t sleep_timer;
    bool timer_running;
    bool display_on;
    bool init_done;
    SemaphoreHandle_t mutex;
} g_mgr;
/**
 * @brief DRY: Single function to set hardware brightness (13-bit PWM)
 * @param percent Brightness 1-99%
 */
static void __hw_set_brightness(uint8_t percent)
{
    if (percent > 99) {
        percent = 99;
    } else if (percent < 1) {
        percent = 1;
    }

    uint32_t duty = (LEDC_MAX_DUTY * percent) / 100;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
}

/**
 * @brief Safely stop the sleep timer if running
 */
static void __timer_stop_locked(void)
{
    if (g_mgr.timer_running) {
        ESP_ERROR_CHECK(esp_timer_stop(g_mgr.sleep_timer));
        g_mgr.timer_running = false;
    }
}

/**
 * @brief Restart sleep timer based on current config (call with mutex held)
 */
static void __timer_restart_locked(void)
{
    __timer_stop_locked();

    if (g_mgr.cfg.sleep_mode_en && g_mgr.cfg.sleep_mode_time_min > 0 && g_mgr.display_on) {
        uint64_t timeout_us = (uint64_t)g_mgr.cfg.sleep_mode_time_min * 60 * 1000000;
        ESP_ERROR_CHECK(esp_timer_start_once(g_mgr.sleep_timer, timeout_us));
        g_mgr.timer_running = true;
    }
}

/**
 * @brief Central function to control display power state
 * @param on true = turn on with configured brightness, false = turn off
 */
static void __display_set_state(bool on)
{
    xSemaphoreTake(g_mgr.mutex, portMAX_DELAY);
    
    if (on) {
        __hw_set_brightness(g_mgr.cfg.brightness);
        g_mgr.display_on = true;
        __timer_restart_locked();
    } else {
        ledc_stop(LEDC_MODE, LEDC_CHANNEL, 0);
        g_mgr.display_on = false;
        __timer_stop_locked();
    }
    
    xSemaphoreGive(g_mgr.mutex);
}

/**
 * @brief Initialize LEDC hardware for backlight control
 */
static void __hw_ledc_init(uint8_t initial_brightness)
{
    if (initial_brightness > 99) {
        initial_brightness = 99;
    } else if (initial_brightness < 1) {
        initial_brightness = 1;
    }

    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    uint32_t initial_duty = (LEDC_MAX_DUTY * initial_brightness) / 100;
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LEDC_OUTPUT_IO,
        .duty           = initial_duty,
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

/**
 * @brief Sleep timer callback - turn off display and notify view
 */
static void __sleep_mode_timer_callback(void* arg)
{
    ESP_LOGI(TAG, "Sleep mode triggered - turning off display");
    
    xSemaphoreTake(g_mgr.mutex, portMAX_DELAY);
    ledc_stop(LEDC_MODE, LEDC_CHANNEL, 0);
    g_mgr.display_on = false;
    g_mgr.timer_running = false;
    xSemaphoreGive(g_mgr.mutex);

    bool screen_state = false;
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SCREEN_CTRL, 
                      &screen_state, sizeof(screen_state), portMAX_DELAY);
}

/**
 * @brief Initialize sleep timer (does not start it)
 */
static void __sleep_timer_create(void)
{
    const esp_timer_create_args_t timer_args = {
        .callback = &__sleep_mode_timer_callback,
        .arg = NULL,
        .name = "display_sleep"
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &g_mgr.sleep_timer));
}


/**
 * @brief Save configuration to NVS (called outside critical section)
 */
static void __cfg_save_to_nvs(const struct view_data_display *p_cfg)
{
    esp_err_t ret = indicator_storage_write(DISPLAY_CFG_STORAGE, (void *)p_cfg, 
                                           sizeof(struct view_data_display));
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "Config write error: %d", ret);
    } else {
        ESP_LOGI(TAG, "Config saved: brightness=%d, sleep_mode=%d, time=%d min",
                 p_cfg->brightness, p_cfg->sleep_mode_en, p_cfg->sleep_mode_time_min);
    }
}

/**
 * @brief Restore configuration from NVS or use defaults
 */
static void __cfg_restore_from_nvs(void)
{
    struct view_data_display cfg;
    size_t len = sizeof(cfg);
    
    esp_err_t ret = indicator_storage_read(DISPLAY_CFG_STORAGE, (void *)&cfg, &len);
    
    if (ret == ESP_OK && len == sizeof(cfg)) {
        ESP_LOGI(TAG, "Config restored from NVS");
    } else {
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "Config not found, using defaults");
        } else {
            ESP_LOGI(TAG, "Config read error: %d, using defaults", ret);
        }
        
        // Default configuration
        cfg.brightness = 80;
        cfg.sleep_mode_en = false;
        cfg.sleep_mode_time_min = 0;
    }
    
    // Store in manager (short critical section)
    xSemaphoreTake(g_mgr.mutex, portMAX_DELAY);
    memcpy(&g_mgr.cfg, &cfg, sizeof(cfg));
    xSemaphoreGive(g_mgr.mutex);
}

/**
 * @brief Event handler for view events
 */
static void __view_event_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data)
{
    switch (id) {
        case VIEW_EVENT_BRIGHTNESS_UPDATE: {
            // Live preview - update brightness only, don't save
            int *p_brightness = (int *)event_data;
            ESP_LOGI(TAG, "Brightness update (preview): %d", *p_brightness);
            
            xSemaphoreTake(g_mgr.mutex, portMAX_DELAY);
            g_mgr.cfg.brightness = *p_brightness;
            __hw_set_brightness(*p_brightness);
            xSemaphoreGive(g_mgr.mutex);
            break;
        }
        
        case VIEW_EVENT_DISPLAY_CFG_APPLY: {
            // Apply full configuration: save to NVS and restart timer
            struct view_data_display *p_cfg = (struct view_data_display *)event_data;
            ESP_LOGI(TAG, "Applying config: brightness=%d, sleep_mode=%d, time=%d min",
                     p_cfg->brightness, p_cfg->sleep_mode_en, p_cfg->sleep_mode_time_min);
            
            // Update in-memory config and hardware (critical section)
            xSemaphoreTake(g_mgr.mutex, portMAX_DELAY);
            memcpy(&g_mgr.cfg, p_cfg, sizeof(struct view_data_display));
            __hw_set_brightness(p_cfg->brightness);
            __timer_restart_locked();
            xSemaphoreGive(g_mgr.mutex);
            
            // Save to NVS (outside critical section)
            __cfg_save_to_nvs(p_cfg);
            break;
        }
        
        default:
            break;
    }
}
/**
 * @brief Initialize display manager module
 */
int indicator_display_init(void)
{
    // Create single mutex for entire manager
    g_mgr.mutex = xSemaphoreCreateMutex();
    if (!g_mgr.mutex) {
        ESP_LOGI(TAG, "Failed to create mutex");
        return -1;
    }

    // Restore configuration from NVS
    __cfg_restore_from_nvs();

    // Initialize LEDC hardware
    xSemaphoreTake(g_mgr.mutex, portMAX_DELAY);
    uint8_t initial_brightness = g_mgr.cfg.brightness;
    xSemaphoreGive(g_mgr.mutex);
    
    __hw_ledc_init(initial_brightness);
    
    // Set initial state
    xSemaphoreTake(g_mgr.mutex, portMAX_DELAY);
    g_mgr.display_on = true;
    xSemaphoreGive(g_mgr.mutex);

    // Create sleep timer
    __sleep_timer_create();
    
    // Start timer if sleep mode is enabled
    xSemaphoreTake(g_mgr.mutex, portMAX_DELAY);
    struct view_data_display cfg_copy = g_mgr.cfg;
    __timer_restart_locked();
    xSemaphoreGive(g_mgr.mutex);

    // Notify view of current configuration (outside critical section)
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_DISPLAY_CFG, 
                      &cfg_copy, sizeof(cfg_copy), portMAX_DELAY);

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_BRIGHTNESS_UPDATE, 
                                                            __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_DISPLAY_CFG_APPLY, 
                                                            __view_event_handler, NULL, NULL));

    xSemaphoreTake(g_mgr.mutex, portMAX_DELAY);
    g_mgr.init_done = true;
    xSemaphoreGive(g_mgr.mutex);
    
    ESP_LOGI(TAG, "Display manager initialized");
    return 0;
}

/**
 * @brief Restart sleep timer with current configuration
 */
int indicator_display_sleep_restart(void)
{
    xSemaphoreTake(g_mgr.mutex, portMAX_DELAY);
    if (!g_mgr.init_done) {
        xSemaphoreGive(g_mgr.mutex);
        return 0;
    }
    __timer_restart_locked();
    xSemaphoreGive(g_mgr.mutex);
    return 0;
}

/**
 * @brief Get current display state
 */
bool indicator_display_st_get(void)
{
    xSemaphoreTake(g_mgr.mutex, portMAX_DELAY);
    bool state = g_mgr.display_on;
    xSemaphoreGive(g_mgr.mutex);
    return state;
}

/**
 * @brief Turn on display and restart sleep timer
 */
int indicator_display_on(void)
{
    __display_set_state(true);
    return 0;
}

/**
 * @brief Turn off display and stop sleep timer
 */
int indicator_display_off(void)
{
    __display_set_state(false);
    return 0;
}

/**
 * @brief Get current display configuration
 */
void indicator_display_cfg_get(struct view_data_display *p_cfg)
{
    if (p_cfg) {
        xSemaphoreTake(g_mgr.mutex, portMAX_DELAY);
        memcpy(p_cfg, &g_mgr.cfg, sizeof(struct view_data_display));
        xSemaphoreGive(g_mgr.mutex);
    }
}
