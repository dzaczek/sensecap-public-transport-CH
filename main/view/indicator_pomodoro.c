/**
 * @file indicator_pomodoro.c
 * @brief Pomodoro Timer with Falling Sand Hourglass Visualization
 * 
 * Features:
 * - Cellular automata sand simulation on lv_canvas
 * - Physics calculations on FreeRTOS Core 1
 * - LVGL rendering on Core 0
 * - Touch-based hourglass flip
 * - 25-minute Pomodoro timer
 */

#include "indicator_pomodoro.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

static const char *TAG = "pomodoro";

// Configuration
#define POMODORO_DURATION_SEC    (25 * 60)  // 25 minutes = 1500 seconds
#define CANVAS_WIDTH             240        // Canvas width in pixels
#define CANVAS_HEIGHT            280        // Canvas height in pixels
#define SAND_PARTICLE_SIZE       2          // Pixel size of each sand grain
#define PHYSICS_UPDATE_MS        40         // Physics update interval (25 FPS)
#define RENDER_UPDATE_MS         50         // Render update interval (20 FPS)
#define HOURGLASS_NECK_WIDTH     3          // Narrowest point (very narrow for slow flow)
#define HOURGLASS_NECK_Y_TOP     135        // Y position of top neck opening
#define HOURGLASS_NECK_Y_BOTTOM  145        // Y position of bottom neck opening

// NEW CONCEPT: Slower flow, longer fall time for smoothness
// 1 grain every 2 seconds = 0.5 grains/sec
// Fall time per grain: ~1.8 seconds (ensures smooth continuous movement)
#define SAND_GRAINS_PER_SECOND   0.5f       // 1 grain every 2 seconds
#define TOTAL_SAND_GRAINS        750        // 1500s × 0.5 grain/s = 750 grains
#define GRAIN_FALL_TIME_SEC      1.8f       // Time for 1 grain to fall through hourglass

// Sand simulation grid (reduced resolution for performance)
#define GRID_WIDTH               (CANVAS_WIDTH / SAND_PARTICLE_SIZE)
#define GRID_HEIGHT              (CANVAS_HEIGHT / SAND_PARTICLE_SIZE)

// Sand particle types
typedef enum {
    CELL_EMPTY = 0,
    CELL_SAND,
    CELL_GLASS,
} cell_type_t;

// Hourglass state
typedef struct {
    lv_obj_t *screen;
    lv_obj_t *canvas;
    lv_obj_t *time_label;
    lv_obj_t *status_label;
    lv_obj_t *back_btn;
    
    // Canvas buffer (LVGL requires LV_COLOR_DEPTH/8 bytes per pixel)
    lv_color_t *canvas_buf;
    
    // Sand simulation grid
    uint8_t *grid;              // Current state
    uint8_t *grid_next;         // Next state (double buffer)
    
    // Physics task
    TaskHandle_t physics_task;
    SemaphoreHandle_t grid_mutex;
    
    // Timer
    lv_timer_t *render_timer;
    esp_timer_handle_t pomodoro_timer;
    int remaining_seconds;
    bool is_running;
    bool is_flipped;            // false = sand falls down, true = sand falls up
    
    // Flow rate limiter (bottleneck control) - STRICT budget system
    float grain_budget;                // Accumulated grain budget (fractional grains allowed)
    int total_grains_fallen;           // Total grains that have fallen (for debugging)
    
    // Touch state
    bool canvas_pressed;
} pomodoro_state_t;

static pomodoro_state_t *g_state = NULL;

// Forward declarations
static void create_pomodoro_screen(lv_obj_t *parent);
static void init_sand_grid(void);
static void update_physics(void);
static void render_canvas(void);
static void physics_task_func(void *arg);
static void render_timer_cb(lv_timer_t *timer);
static void pomodoro_timer_cb(void *arg);
static void canvas_event_cb(lv_event_t *e);
static void back_btn_cb(lv_event_t *e);
static void flip_hourglass(void);
static void start_timer(void);
static void stop_timer(void);
static void reset_timer(void);
static bool is_inside_hourglass(int x, int y);
static bool is_hourglass_neck(int x, int y);

// Color definitions
#define COLOR_BACKGROUND  lv_color_make(245, 240, 230)  // Light beige
#define COLOR_GLASS       lv_color_make(100, 150, 200)  // Light blue glass
#define COLOR_SAND        lv_color_make(200, 160, 100)  // Golden sand
#define COLOR_FRAME       lv_color_make(80, 60, 40)     // Dark brown frame

/**
 * @brief Check if point is inside hourglass shape
 */
static bool is_inside_hourglass(int x, int y) {
    int center_x = GRID_WIDTH / 2;
    int center_y = GRID_HEIGHT / 2;
    
    // Top chamber (inverted triangle)
    if (y < center_y) {
        int max_width = GRID_WIDTH * 0.6;  // 60% of width at top
        int y_from_top = y;
        int y_range = center_y;
        int width_at_y = HOURGLASS_NECK_WIDTH + 
                        (max_width - HOURGLASS_NECK_WIDTH) * (y_range - y_from_top) / y_range;
        int x_min = center_x - width_at_y / 2;
        int x_max = center_x + width_at_y / 2;
        return (x >= x_min && x <= x_max);
    }
    // Bottom chamber (triangle)
    else {
        int max_width = GRID_WIDTH * 0.6;  // 60% of width at bottom
        int y_from_center = y - center_y;
        int y_range = GRID_HEIGHT - center_y;
        int width_at_y = HOURGLASS_NECK_WIDTH + 
                        (max_width - HOURGLASS_NECK_WIDTH) * y_from_center / y_range;
        int x_min = center_x - width_at_y / 2;
        int x_max = center_x + width_at_y / 2;
        return (x >= x_min && x <= x_max);
    }
}

/**
 * @brief Check if point is in the narrow neck opening
 */
static bool is_hourglass_neck(int x, int y) {
    int center_x = GRID_WIDTH / 2;
    int neck_y_grid_top = HOURGLASS_NECK_Y_TOP / SAND_PARTICLE_SIZE;
    int neck_y_grid_bottom = HOURGLASS_NECK_Y_BOTTOM / SAND_PARTICLE_SIZE;
    
    // Check if in neck Y range
    if (y >= neck_y_grid_top && y <= neck_y_grid_bottom) {
        int x_min = center_x - HOURGLASS_NECK_WIDTH / (2 * SAND_PARTICLE_SIZE);
        int x_max = center_x + HOURGLASS_NECK_WIDTH / (2 * SAND_PARTICLE_SIZE);
        return (x >= x_min && x <= x_max);
    }
    return false;
}

/**
 * @brief Initialize sand grid with hourglass shape and sand in top chamber
 */
static void init_sand_grid(void) {
    if (!g_state) return;
    
    memset(g_state->grid, CELL_EMPTY, GRID_WIDTH * GRID_HEIGHT);
    memset(g_state->grid_next, CELL_EMPTY, GRID_WIDTH * GRID_HEIGHT);
    
    // Draw hourglass glass boundaries
    for (int y = 0; y < GRID_HEIGHT; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            if (!is_inside_hourglass(x, y)) {
                // Outside hourglass = glass wall
                int idx = y * GRID_WIDTH + x;
                
                // Only draw glass if it's near the hourglass boundary
                // Check if any neighbor is inside hourglass
                bool near_boundary = false;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        int nx = x + dx;
                        int ny = y + dy;
                        if (nx >= 0 && nx < GRID_WIDTH && ny >= 0 && ny < GRID_HEIGHT) {
                            if (is_inside_hourglass(nx, ny)) {
                                near_boundary = true;
                                break;
                            }
                        }
                    }
                    if (near_boundary) break;
                }
                
                if (near_boundary) {
                    g_state->grid[idx] = CELL_GLASS;
                }
            }
        }
    }
    
    // Fill top chamber with EXACT number of sand grains
    // NEW CONCEPT: 1 grain every 2 seconds = 0.5 grains/sec
    // Each grain takes ~1.8s to fall → continuous smooth movement
    int sand_particles = 0;
    int target_grains = TOTAL_SAND_GRAINS;  // 750 grains for 25 minutes
    
    ESP_LOGI(TAG, "Target sand grains: %d (%.1f grains/sec × %d seconds, fall time: %.1fs)", 
             target_grains, SAND_GRAINS_PER_SECOND, POMODORO_DURATION_SEC, GRAIN_FALL_TIME_SEC);
    
    // Fill from bottom-up in top chamber for stable pile
    for (int y = GRID_HEIGHT / 2 - 5; y >= 5; y--) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            if (sand_particles >= target_grains) break;
            
            if (is_inside_hourglass(x, y)) {
                int idx = y * GRID_WIDTH + x;
                if (g_state->grid[idx] == CELL_EMPTY) {
                    g_state->grid[idx] = CELL_SAND;
                    sand_particles++;
                }
            }
        }
        if (sand_particles >= target_grains) break;
    }
    
    ESP_LOGI(TAG, "Sand grid initialized with %d particles (target: %d)", 
             sand_particles, target_grains);
}

/**
 * @brief Update sand physics using cellular automata
 * Runs on Core 1 in dedicated FreeRTOS task
 */
static void update_physics(void) {
    if (!g_state || !g_state->is_running) return;
    
    if (xSemaphoreTake(g_state->grid_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return;
    }
    
    // STRICT GLOBAL Flow Rate Budget System
    // At 25 FPS: 0.5 grains/second = 0.5/25 = 0.02 grains per frame
    const float grains_per_frame = SAND_GRAINS_PER_SECOND / 25.0f;
    
    // Add budget for this frame
    g_state->grain_budget += grains_per_frame;
    
    // Calculate how many grains can MOVE this frame (global limit, not just neck!)
    int allowed_movements_this_frame = (int)g_state->grain_budget;
    
    // Copy current to next
    memcpy(g_state->grid_next, g_state->grid, GRID_WIDTH * GRID_HEIGHT);
    
    int gravity_dir = g_state->is_flipped ? -1 : 1;  // -1 = up, 1 = down
    
    // Define the gate line (middle of the hourglass)
    int gate_y = GRID_HEIGHT / 2;
    
    // Track grains that actually pass through the gate this frame
    int grains_passed_this_frame = 0;
    
    // Process each sand particle (scan in gravity direction)
    int start_y = gravity_dir > 0 ? (GRID_HEIGHT - 2) : 1;
    int end_y = gravity_dir > 0 ? 0 : (GRID_HEIGHT - 1);
    int step_y = -gravity_dir;
    
    for (int y = start_y; y != end_y; y += step_y) {
        // Randomize x-order to avoid patterns
        int x_order[GRID_WIDTH];
        for (int i = 0; i < GRID_WIDTH; i++) x_order[i] = i;
        
        // Simple shuffle
        for (int i = 0; i < GRID_WIDTH - 1; i++) {
            int j = i + rand() % (GRID_WIDTH - i);
            int temp = x_order[i];
            x_order[i] = x_order[j];
            x_order[j] = temp;
        }
        
        for (int xi = 0; xi < GRID_WIDTH; xi++) {
            int x = x_order[xi];
            int idx = y * GRID_WIDTH + x;
            
            if (g_state->grid[idx] != CELL_SAND) continue;
            
            // Calculate new Y position
            int ny = y + gravity_dir;
            
            // Check bounds
            if (ny < 0 || ny >= GRID_HEIGHT) continue;

            // Check if grain is trying to cross the gate line
            bool crossing_gate = false;
            if (gravity_dir > 0) { // Falling down
                if (y < gate_y && ny >= gate_y) crossing_gate = true;
            } else { // Falling up
                if (y >= gate_y && ny < gate_y) crossing_gate = true;
            }
            
            // STRICT Rate limiter: Check budget ONLY for gate crossing
            if (crossing_gate) {
                if (grains_passed_this_frame >= allowed_movements_this_frame) {
                    // Budget exhausted! This grain waits at the gate.
                    continue; 
                }
            }
            
            // Try move down/up
            int idx_down = ny * GRID_WIDTH + x;
            if (g_state->grid_next[idx_down] == CELL_EMPTY) {
                g_state->grid_next[idx] = CELL_EMPTY;
                g_state->grid_next[idx_down] = CELL_SAND;
                
                // Deduct from budget ONLY if passed through gate
                if (crossing_gate) {
                    grains_passed_this_frame++;
                }
                continue;
            }
            
            // Try diagonal (randomly left or right first)
            int dx = (rand() % 2) ? 1 : -1;
            for (int i = 0; i < 2; i++) {
                int nx = x + dx;
                if (nx >= 0 && nx < GRID_WIDTH) {
                    int idx_diag = ny * GRID_WIDTH + nx;
                    if (g_state->grid_next[idx_diag] == CELL_EMPTY) {
                        g_state->grid_next[idx] = CELL_EMPTY;
                        g_state->grid_next[idx_diag] = CELL_SAND;
                        
                        // Deduct from budget ONLY if passed through gate
                        if (crossing_gate) {
                            grains_passed_this_frame++;
                        }
                        break;
                    }
                }
                dx = -dx;  // Try other direction
            }
        }
    }
    
    // Deduct used budget (STRICT accounting)
    g_state->grain_budget -= grains_passed_this_frame;
    
    // Track total grains that passed neck
    g_state->total_grains_fallen += grains_passed_this_frame;
    
    // Swap buffers
    uint8_t *temp = g_state->grid;
    g_state->grid = g_state->grid_next;
    g_state->grid_next = temp;
    
    xSemaphoreGive(g_state->grid_mutex);
}

/**
 * @brief Render sand grid to LVGL canvas
 * Runs on Core 0 in LVGL timer
 */
static void render_canvas(void) {
    if (!g_state || !g_state->canvas) return;
    
    if (xSemaphoreTake(g_state->grid_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return;
    }
    
    // Clear canvas
    lv_canvas_fill_bg(g_state->canvas, COLOR_BACKGROUND, LV_OPA_COVER);
    
    // Draw sand particles and glass
    for (int y = 0; y < GRID_HEIGHT; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            int idx = y * GRID_WIDTH + x;
            cell_type_t cell = g_state->grid[idx];
            
            if (cell == CELL_SAND) {
                lv_canvas_set_px(g_state->canvas, 
                                x * SAND_PARTICLE_SIZE, 
                                y * SAND_PARTICLE_SIZE, 
                                COLOR_SAND);
                if (SAND_PARTICLE_SIZE > 1) {
                    lv_canvas_set_px(g_state->canvas, 
                                    x * SAND_PARTICLE_SIZE + 1, 
                                    y * SAND_PARTICLE_SIZE, 
                                    COLOR_SAND);
                    lv_canvas_set_px(g_state->canvas, 
                                    x * SAND_PARTICLE_SIZE, 
                                    y * SAND_PARTICLE_SIZE + 1, 
                                    COLOR_SAND);
                    lv_canvas_set_px(g_state->canvas, 
                                    x * SAND_PARTICLE_SIZE + 1, 
                                    y * SAND_PARTICLE_SIZE + 1, 
                                    COLOR_SAND);
                }
            } else if (cell == CELL_GLASS) {
                lv_canvas_set_px(g_state->canvas, 
                                x * SAND_PARTICLE_SIZE, 
                                y * SAND_PARTICLE_SIZE, 
                                COLOR_GLASS);
            }
        }
    }
    
    xSemaphoreGive(g_state->grid_mutex);
}

/**
 * @brief Physics task running on Core 1
 */
static void physics_task_func(void *arg) {
    ESP_LOGI(TAG, "Physics task started on core %d", xPortGetCoreID());
    
    // Log initial stack usage
    UBaseType_t stack_hwm = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "Physics task stack high water mark: %d bytes free", stack_hwm * sizeof(StackType_t));
    
    const TickType_t delay = pdMS_TO_TICKS(PHYSICS_UPDATE_MS);
    uint32_t log_counter = 0;
    int last_total_fallen = 0;
    
    ESP_LOGI(TAG, "STRICT Flow Control: %.3f grains/frame = %.1f grains/sec @ 25 FPS (1 grain per %.1f sec)", 
             SAND_GRAINS_PER_SECOND / 25.0f, 
             SAND_GRAINS_PER_SECOND,
             1.0f / SAND_GRAINS_PER_SECOND);
    
    while (g_state && g_state->physics_task) {
        if (g_state->is_running) {
            update_physics();  // Full speed physics, STRICT grain budget
            
            // Log actual flow rate every 10 seconds
            log_counter++;
            if (log_counter >= 250) {  // 250 frames @ 25 FPS = 10 seconds
                int grains_in_10sec = g_state->total_grains_fallen - last_total_fallen;
                float actual_rate = grains_in_10sec / 10.0f;
                
                ESP_LOGI(TAG, "Flow: %.1f grains/sec (target: %d) | Total: %d | Budget: %.2f", 
                         actual_rate,
                         SAND_GRAINS_PER_SECOND,
                         g_state->total_grains_fallen,
                         g_state->grain_budget);
                
                last_total_fallen = g_state->total_grains_fallen;
                log_counter = 0;
            }
        }
        vTaskDelay(delay);
    }
    
    ESP_LOGI(TAG, "Physics task ended");
    vTaskDelete(NULL);
}

/**
 * @brief LVGL render timer callback (Core 0)
 */
static void render_timer_cb(lv_timer_t *timer) {
    if (!g_state) return;
    
    render_canvas();
    
    // Update time display
    if (g_state->time_label) {
        int minutes = g_state->remaining_seconds / 60;
        int seconds = g_state->remaining_seconds % 60;
        lv_label_set_text_fmt(g_state->time_label, "%02d:%02d", minutes, seconds);
    }
    
    // Update status
    if (g_state->status_label) {
        if (g_state->is_running) {
            lv_label_set_text(g_state->status_label, "Focus Time");
        } else if (g_state->remaining_seconds == 0) {
            lv_label_set_text(g_state->status_label, "Session Complete!");
        } else {
            lv_label_set_text(g_state->status_label, "Paused - Tap to Start");
        }
    }
}

/**
 * @brief Pomodoro timer callback (decrements seconds)
 */
static void pomodoro_timer_cb(void *arg) {
    if (!g_state || !g_state->is_running) return;
    
    g_state->remaining_seconds--;
    
    if (g_state->remaining_seconds <= 0) {
        g_state->remaining_seconds = 0;
        stop_timer();
        ESP_LOGI(TAG, "Pomodoro session complete!");
        // TODO: Add notification/alarm sound
    }
}

/**
 * @brief Canvas touch event handler - flip hourglass
 */
static void canvas_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_PRESSED) {
        g_state->canvas_pressed = true;
    } else if (code == LV_EVENT_RELEASED && g_state->canvas_pressed) {
        g_state->canvas_pressed = false;
        flip_hourglass();
    }
}

/**
 * @brief Back button callback
 */
static void back_btn_cb(lv_event_t *e) {
    ESP_LOGI(TAG, "Back button pressed");
    lv_indicator_pomodoro_deinit();
    // TODO: Return to main menu (depends on your navigation system)
}

/**
 * @brief Flip hourglass and reset timer
 */
static void flip_hourglass(void) {
    ESP_LOGI(TAG, "Flipping hourglass...");
    
    if (xSemaphoreTake(g_state->grid_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    
    // Flip gravity
    g_state->is_flipped = !g_state->is_flipped;
    
    // Vertically flip the entire grid
    for (int y = 0; y < GRID_HEIGHT / 2; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            int idx_top = y * GRID_WIDTH + x;
            int idx_bottom = (GRID_HEIGHT - 1 - y) * GRID_WIDTH + x;
            
            uint8_t temp = g_state->grid[idx_top];
            g_state->grid[idx_top] = g_state->grid[idx_bottom];
            g_state->grid[idx_bottom] = temp;
        }
    }
    
    xSemaphoreGive(g_state->grid_mutex);
    
    // Reset STRICT flow rate limiter (budget system)
    g_state->grain_budget = 0.0f;
    g_state->total_grains_fallen = 0;
    
    // Reset and start timer
    reset_timer();
    start_timer();
}

/**
 * @brief Start Pomodoro timer
 */
static void start_timer(void) {
    if (!g_state) return;
    
    g_state->is_running = true;
    
    // Start ESP timer (1 second periodic)
    if (!g_state->pomodoro_timer) {
        esp_timer_create_args_t timer_args = {
            .callback = pomodoro_timer_cb,
            .name = "pomodoro_timer"
        };
        esp_timer_create(&timer_args, &g_state->pomodoro_timer);
    }
    
    esp_timer_start_periodic(g_state->pomodoro_timer, 1000000);  // 1 second
    ESP_LOGI(TAG, "Timer started");
}

/**
 * @brief Stop Pomodoro timer
 */
static void stop_timer(void) {
    if (!g_state) return;
    
    g_state->is_running = false;
    
    if (g_state->pomodoro_timer) {
        esp_timer_stop(g_state->pomodoro_timer);
    }
    
    ESP_LOGI(TAG, "Timer stopped");
}

/**
 * @brief Reset Pomodoro timer to 25 minutes
 */
static void reset_timer(void) {
    if (!g_state) return;
    
    g_state->remaining_seconds = POMODORO_DURATION_SEC;
    ESP_LOGI(TAG, "Timer reset to %d seconds", POMODORO_DURATION_SEC);
}

/**
 * @brief Create Pomodoro screen UI
 */
static void create_pomodoro_screen(lv_obj_t *parent) {
    // Main screen container
    g_state->screen = lv_obj_create(parent);
    lv_obj_set_size(g_state->screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_state->screen, lv_color_make(30, 30, 35), 0);
    lv_obj_clear_flag(g_state->screen, LV_OBJ_FLAG_SCROLLABLE);
    
    // Back button
    g_state->back_btn = lv_btn_create(g_state->screen);
    lv_obj_set_size(g_state->back_btn, 80, 35);
    lv_obj_align(g_state->back_btn, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_add_event_cb(g_state->back_btn, back_btn_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_label = lv_label_create(g_state->back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_label);
    
    // Status label
    g_state->status_label = lv_label_create(g_state->screen);
    lv_obj_align(g_state->status_label, LV_ALIGN_TOP_MID, 0, 15);
    lv_label_set_text(g_state->status_label, "Tap to Start");
    lv_obj_set_style_text_color(g_state->status_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(g_state->status_label, &lv_font_montserrat_16, 0);
    
    // Time display
    g_state->time_label = lv_label_create(g_state->screen);
    lv_obj_align(g_state->time_label, LV_ALIGN_TOP_MID, 0, 40);
    lv_label_set_text(g_state->time_label, "25:00");
    lv_obj_set_style_text_color(g_state->time_label, lv_color_make(255, 200, 100), 0);
    lv_obj_set_style_text_font(g_state->time_label, &lv_font_montserrat_28, 0);
    
    // Canvas for sand simulation
    g_state->canvas = lv_canvas_create(g_state->screen);
    lv_obj_align(g_state->canvas, LV_ALIGN_CENTER, 0, 10);
    
    // Allocate canvas buffer
    size_t buf_size = LV_CANVAS_BUF_SIZE_TRUE_COLOR(CANVAS_WIDTH, CANVAS_HEIGHT);
    g_state->canvas_buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    if (!g_state->canvas_buf) {
        ESP_LOGE(TAG, "Failed to allocate canvas buffer!");
        return;
    }
    
    lv_canvas_set_buffer(g_state->canvas, g_state->canvas_buf, 
                        CANVAS_WIDTH, CANVAS_HEIGHT, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(g_state->canvas, COLOR_BACKGROUND, LV_OPA_COVER);
    
    // Add touch handler to canvas
    lv_obj_add_flag(g_state->canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_state->canvas, canvas_event_cb, LV_EVENT_ALL, NULL);
    
    // Add border to canvas
    lv_obj_set_style_border_width(g_state->canvas, 3, 0);
    lv_obj_set_style_border_color(g_state->canvas, COLOR_FRAME, 0);
    lv_obj_set_style_radius(g_state->canvas, 5, 0);
    
    // Instructions
    lv_obj_t *inst_label = lv_label_create(g_state->screen);
    lv_obj_align(inst_label, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_label_set_text(inst_label, "Tap hourglass to flip & start");
    lv_obj_set_style_text_color(inst_label, lv_color_make(150, 150, 150), 0);
    lv_obj_set_style_text_font(inst_label, &lv_font_montserrat_12, 0);
    
    // Ensure everything is visible and rendered
    lv_obj_clear_flag(g_state->screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_state->canvas, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(g_state->screen);  // Force redraw
}

/**
 * @brief Initialize Pomodoro timer view
 */
lv_obj_t* lv_indicator_pomodoro_init(lv_obj_t *parent) {
    ESP_LOGI(TAG, "Initializing Pomodoro timer...");
    
    // Allocate state
    g_state = heap_caps_malloc(sizeof(pomodoro_state_t), MALLOC_CAP_8BIT);
    if (!g_state) {
        ESP_LOGE(TAG, "Failed to allocate state!");
        return NULL;
    }
    memset(g_state, 0, sizeof(pomodoro_state_t));
    
    // Allocate sand grids
    g_state->grid = heap_caps_malloc(GRID_WIDTH * GRID_HEIGHT, MALLOC_CAP_8BIT);
    g_state->grid_next = heap_caps_malloc(GRID_WIDTH * GRID_HEIGHT, MALLOC_CAP_8BIT);
    
    if (!g_state->grid || !g_state->grid_next) {
        ESP_LOGE(TAG, "Failed to allocate sand grids!");
        lv_indicator_pomodoro_deinit();
        return NULL;
    }
    
    // Create mutex
    g_state->grid_mutex = xSemaphoreCreateMutex();
    if (!g_state->grid_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex!");
        lv_indicator_pomodoro_deinit();
        return NULL;
    }
    
    // Initialize state
    g_state->remaining_seconds = POMODORO_DURATION_SEC;
    g_state->is_running = false;
    g_state->is_flipped = false;
    g_state->canvas_pressed = false;
    
    // Initialize STRICT flow rate limiter (budget system)
    g_state->grain_budget = 0.0f;
    g_state->total_grains_fallen = 0;
    
    // Create UI
    create_pomodoro_screen(parent);
    
    // Initialize sand grid
    init_sand_grid();
    
    // Create physics task on Core 1
    BaseType_t ret = xTaskCreatePinnedToCore(
        physics_task_func,
        "pomodoro_physics",
        3072,  // Reduced from 4096 to save memory
        NULL,
        5,  // Priority
        &g_state->physics_task,
        1   // Core 1
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create physics task!");
        lv_indicator_pomodoro_deinit();
        return NULL;
    }
    
    // Create LVGL render timer (Core 0)
    g_state->render_timer = lv_timer_create(render_timer_cb, RENDER_UPDATE_MS, NULL);
    
    // Initial render to show hourglass immediately
    render_canvas();
    
    ESP_LOGI(TAG, "Pomodoro timer initialized successfully");
    ESP_LOGI(TAG, "Grid: %dx%d, Canvas: %dx%d", GRID_WIDTH, GRID_HEIGHT, CANVAS_WIDTH, CANVAS_HEIGHT);
    
    return g_state->screen;
}

/**
 * @brief Cleanup and destroy Pomodoro view
 */
void lv_indicator_pomodoro_deinit(void) {
    if (!g_state) return;
    
    ESP_LOGI(TAG, "Deinitializing Pomodoro timer...");
    
    // Stop timer
    if (g_state->pomodoro_timer) {
        esp_timer_stop(g_state->pomodoro_timer);
        esp_timer_delete(g_state->pomodoro_timer);
        g_state->pomodoro_timer = NULL;
    }
    
    // Delete render timer
    if (g_state->render_timer) {
        lv_timer_del(g_state->render_timer);
        g_state->render_timer = NULL;
    }
    
    // Delete physics task
    if (g_state->physics_task) {
        TaskHandle_t task = g_state->physics_task;
        g_state->physics_task = NULL;  // Signal task to stop
        vTaskDelay(pdMS_TO_TICKS(100));  // Wait for task to finish
        // Task deletes itself
    }
    
    // Delete mutex
    if (g_state->grid_mutex) {
        vSemaphoreDelete(g_state->grid_mutex);
        g_state->grid_mutex = NULL;
    }
    
    // Free grids
    if (g_state->grid) {
        free(g_state->grid);
        g_state->grid = NULL;
    }
    if (g_state->grid_next) {
        free(g_state->grid_next);
        g_state->grid_next = NULL;
    }
    
    // Free canvas buffer
    if (g_state->canvas_buf) {
        free(g_state->canvas_buf);
        g_state->canvas_buf = NULL;
    }
    
    // Delete screen
    if (g_state->screen) {
        lv_obj_del(g_state->screen);
        g_state->screen = NULL;
    }
    
    // Free state
    free(g_state);
    g_state = NULL;
    
    ESP_LOGI(TAG, "Pomodoro timer deinitialized");
}

/**
 * @brief Get timer running state
 */
bool lv_indicator_pomodoro_is_running(void) {
    return g_state ? g_state->is_running : false;
}

/**
 * @brief Get remaining seconds
 */
int lv_indicator_pomodoro_get_remaining_seconds(void) {
    return g_state ? g_state->remaining_seconds : 0;
}
