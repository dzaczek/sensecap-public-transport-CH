/**
 * @file sbb_clock.c
 * @brief Swiss Railway (SBB) Clock – LVGL v8 widget with vector shadows.
 * Fixed: Compilation error (last_second), Center alignment, Shadow rendering.
 */
 #include "sbb_clock.h"
 #include "esp_log.h"
 #include <math.h>
 #include <string.h>
 #include <time.h>
 #include <sys/time.h>
 
 #ifndef M_PI
 #define M_PI 3.14159265358979323846
 #endif
 
 static const char *TAG = "sbb_clock";
 
/* Shadow configuration */
#define SHADOW_OFFSET_X  5
#define SHADOW_OFFSET_Y  5
#define SHADOW_OPACITY   LV_OPA_40
#define SHADOW_COLOR     0x000000 

/* SBB timing */
#define SBB_SWEEP_MS      58500
#define SBB_PAUSE_MS     1500
#define SBB_TOTAL_MS     (SBB_SWEEP_MS + SBB_PAUSE_MS)
#define ANIMATION_SPEED_MS_PER_360DEG  6000.0f

/* "Heavy Metal" Physics Parameters for minute hand damped oscillation */
#define MIN_BOUNCE_DURATION_MS 500   // Oscillation duration (optimized)
#define MIN_BOUNCE_AMP        -2.4f  // Initial amplitude (degrees)
#define MIN_BOUNCE_DAMP        8.0f  // Damping coefficient (inertia)
#define MIN_BOUNCE_FREQ        30.0f // Angular frequency (weight)
 
 #define DEG_TO_LVGL(d)   ((int32_t)((d) * 10))
 
 typedef struct {
     lv_obj_t *container;
     lv_obj_t *dial;
     
     /* Shadows */
     lv_obj_t *hour_shadow;
     lv_obj_t *minute_shadow;
     lv_obj_t *second_shadow;
     lv_obj_t *dot_shadow;
 
     /* Hands */
     lv_obj_t *hour_hand;
     lv_obj_t *minute_hand;
     lv_obj_t *second_hand;
     lv_obj_t *second_dot;
     lv_obj_t *center_cap;
     
     lv_timer_t *timer;
     lv_coord_t size;
     lv_coord_t cx; /* Center X of the clock space */
     lv_coord_t cy; /* Center Y of the clock space */
     lv_coord_t r;
 
    bool time_synced;
    bool animation_active;
    uint32_t animation_start_ms;
    uint32_t animation_duration_ms;
    float anim_start_hour_deg;
    float anim_start_min_deg;
    float anim_start_sec_deg;
    float anim_end_hour_deg;
    float anim_end_min_deg;
    float anim_end_sec_deg;

    /* Minute hand bounce physics state */
    int last_minute;
    uint32_t last_minute_jump_ms;

    /* Points buffers */
    lv_point_t hour_pts[2];
    lv_point_t min_pts[2];
    lv_point_t sec_pts[2];
     
     /* Hand lengths */
     lv_coord_t hour_tip, hour_tail;
     lv_coord_t min_tip, min_tail;
     lv_coord_t sec_tip, sec_tail;
 } sbb_clock_inst_t;
 
 static void sbb_clock_update_internal(sbb_clock_t clock);
 
 /* --- Logic Helpers --- */
 
 static float sbb_second_angle_deg(int ms_in_minute)
 {
     if (ms_in_minute < 0) ms_in_minute = 0;
     if (ms_in_minute >= SBB_TOTAL_MS) ms_in_minute = ms_in_minute % SBB_TOTAL_MS;
     if (ms_in_minute >= SBB_SWEEP_MS) return 0.0f;
     return (ms_in_minute / (float)SBB_SWEEP_MS) * 360.0f;
 }
 
 static void get_time_and_angles(sbb_clock_inst_t *c, uint32_t now_ms,
                                 float *hour_deg, float *min_deg, float *sec_deg)
 {
     if (!c->time_synced) {
         *hour_deg = 0.0f; *min_deg = 0.0f; *sec_deg = 0.0f;
         return;
     }
 
     if (c->animation_active) {
         uint32_t elapsed = now_ms - c->animation_start_ms;
         if (elapsed >= c->animation_duration_ms) {
             c->animation_active = false;
             *hour_deg = c->anim_end_hour_deg;
             *min_deg = c->anim_end_min_deg;
             *sec_deg = c->anim_end_sec_deg;
             return;
         }
         float t = elapsed / (float)c->animation_duration_ms;
         *hour_deg = c->anim_start_hour_deg + t * (c->anim_end_hour_deg - c->anim_start_hour_deg);
         *min_deg = c->anim_start_min_deg + t * (c->anim_end_min_deg - c->anim_start_min_deg);
         *sec_deg = c->anim_start_sec_deg + t * (c->anim_end_sec_deg - c->anim_start_sec_deg);
         return;
     }
 
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t sec = tv.tv_sec;
    int ms = (int)(tv.tv_usec / 1000);
    struct tm tm;
    localtime_r(&sec, &tm);

    int h = tm.tm_hour % 12;
    int m = tm.tm_min;
    int s = tm.tm_sec;
    int ms_in_minute = s * 1000 + ms;

    // Detect minute jump (resets bounce timer)
    if (m != c->last_minute) {
        c->last_minute = m;
        c->last_minute_jump_ms = now_ms;  // Reset diff to 0 → forces update
    }

    // Hour angle (smooth, continuous)
    *hour_deg = (h + m / 60.0f) * 30.0f;
    if (*hour_deg >= 360.0f) *hour_deg -= 360.0f;

    // Minute angle with damped oscillation physics
    float base_min_deg = m * 6.0f;
    uint32_t diff = now_ms - c->last_minute_jump_ms;
    if (diff < MIN_BOUNCE_DURATION_MS) {
        float t = diff / 1000.0f;
        // Damped oscillation formula: A * e^(-ζ*t) * cos(ω*t)
        // Simulates heavy metal hand with inertia
        float bounce = MIN_BOUNCE_AMP * expf(-MIN_BOUNCE_DAMP * t) * cosf(MIN_BOUNCE_FREQ * t);
        *min_deg = base_min_deg + bounce;
    } else {
        *min_deg = base_min_deg;
    }

    // Second angle (SBB stop-go behavior)
    *sec_deg = sbb_second_angle_deg(ms_in_minute);
}
 
 static void timer_cb(lv_timer_t *timer)
 {
     sbb_clock_inst_t *c = (sbb_clock_inst_t *)timer->user_data;
     if (c && c->container) sbb_clock_update_internal(c->container);
 }
 
 /* --- Drawing Helpers --- */
 
 static void update_hand_line_with_shadow(lv_obj_t *hand, lv_obj_t *shadow, lv_point_t *pts, 
                                        lv_coord_t cx, lv_coord_t cy, 
                                        lv_coord_t tip_len, lv_coord_t tail_len, float angle_deg)
 {
     double rad = (angle_deg - 90.0f) * M_PI / 180.0f;
     double cos_v = cos(rad);
     double sin_v = sin(rad);
 
     /* Points computed relative to dial center (cx, cy) */
     pts[0].x = cx + (lv_coord_t)(tip_len * cos_v);
     pts[0].y = cy + (lv_coord_t)(tip_len * sin_v);
     pts[1].x = cx - (lv_coord_t)(tail_len * cos_v);
     pts[1].y = cy - (lv_coord_t)(tail_len * sin_v);
 
    /* Update main hand */
    /* IMPORTANT: The 'hand' object has dial size and is at (0,0), so points fit perfectly */
     lv_line_set_points(hand, pts, 2);
     
     /* Aktualizacja cienia */
     if (shadow) {
         lv_line_set_points(shadow, pts, 2);
         /* Force shadow refresh */
         lv_obj_invalidate(shadow); 
     }
 }
 
 static void update_second_hand_geometry(sbb_clock_inst_t *c, float sec_deg)
 {
     update_hand_line_with_shadow(c->second_hand, c->second_shadow, c->sec_pts, c->cx, c->cy, c->sec_tip, c->sec_tail, sec_deg);
 
     /* Second hand dot (lollipop) */
     lv_coord_t tip_x = c->sec_pts[0].x;
     lv_coord_t tip_y = c->sec_pts[0].y;
     
     lv_coord_t dot_w = lv_obj_get_width(c->second_dot);
     lv_coord_t dot_h = lv_obj_get_height(c->second_dot);
 
     /* Position dot centrally at line tip */
     lv_obj_set_pos(c->second_dot, tip_x - dot_w / 2, tip_y - dot_h / 2);
 
     /* Dot shadow - offset from dot */
     if (c->dot_shadow) {
         lv_obj_set_pos(c->dot_shadow, 
                        tip_x - dot_w / 2 + SHADOW_OFFSET_X, 
                        tip_y - dot_h / 2 + SHADOW_OFFSET_Y);
     }
 }
 
static void sbb_clock_update_internal(sbb_clock_t clock)
{
    sbb_clock_inst_t *c = (sbb_clock_inst_t *)lv_obj_get_user_data(clock);
    if (!c) return;

    uint32_t now_ms = (uint32_t)lv_tick_get();
    float hour_deg, min_deg, sec_deg;
    get_time_and_angles(c, now_ms, &hour_deg, &min_deg, &sec_deg);

    /* Hour hand - always update (changes slowly but continuously) */
    update_hand_line_with_shadow(c->hour_hand, c->hour_shadow, c->hour_pts, 
                                 c->cx, c->cy, c->hour_tip, c->hour_tail, hour_deg);

    /* Minute hand - update during startup animation OR bounce animation (performance optimization) */
    uint32_t diff = now_ms - c->last_minute_jump_ms;
    if (c->animation_active || (diff < MIN_BOUNCE_DURATION_MS)) {
        /* Update when: startup animation is active OR bounce physics is active */
        update_hand_line_with_shadow(c->minute_hand, c->minute_shadow, c->min_pts, 
                                     c->cx, c->cy, c->min_tip, c->min_tail, min_deg);
    }
    /* After both animations end, minute hand is NOT updated (stays at final position, saves CPU) */

    /* Second hand - always update (SBB stop-go behavior requires continuous refresh) */
    update_second_hand_geometry(c, sec_deg);
}
 
 /* --- Initialization --- */
 
 static void create_dial_marks(sbb_clock_inst_t *c)
 {
     /* The dial (white circle) is the parent of markers.
        Coordinates (0,0) are the top-left corner of the white circle. */
     lv_obj_t *parent = c->dial;
     
     /* Need to know center relative to parent (i.e. center of white circle) */
     lv_coord_t center_x = c->size / 2;
     lv_coord_t center_y = c->size / 2;
     lv_coord_t r = c->size / 2;  /* Radius is half the dial size */
 
     lv_coord_t hour_mark_len = (r * 18) / 100;
     lv_coord_t hour_mark_w = 10;
     lv_coord_t min_mark_len = (r * 6) / 100;
     lv_coord_t min_mark_w = 2;
     if (min_mark_len < 1) min_mark_len = 1;
 
     for (int i = 0; i < 60; i++) {
         bool is_hour = (i % 5 == 0);
         lv_coord_t w = is_hour ? hour_mark_w : min_mark_w;
         lv_coord_t h = is_hour ? hour_mark_len : min_mark_len;
 
         lv_obj_t *mark = lv_obj_create(parent);
         lv_obj_set_size(mark, w, h);
         lv_obj_set_style_bg_color(mark, lv_color_hex(0x000000), LV_PART_MAIN);
         lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, LV_PART_MAIN);
         lv_obj_set_style_border_width(mark, 0, LV_PART_MAIN);
         lv_obj_set_style_radius(mark, 0, LV_PART_MAIN);
         lv_obj_clear_flag(mark, LV_OBJ_FLAG_SCROLLABLE);
 
         float angle = i * 6.0f;
         float dist = r - (h / 2.0f);  /* Distance of marker center from dial center */
         double rad = (angle - 90.0) * M_PI / 180.0;
 
         lv_coord_t x = center_x + (lv_coord_t)(dist * cos(rad));
         lv_coord_t y = center_y + (lv_coord_t)(dist * sin(rad));
         
         lv_obj_set_pos(mark, x - w / 2, y - h / 2);
 
         lv_obj_set_style_transform_angle(mark, DEG_TO_LVGL(angle), LV_PART_MAIN);
         lv_obj_set_style_transform_pivot_x(mark, w / 2, LV_PART_MAIN);
         lv_obj_set_style_transform_pivot_y(mark, h / 2, LV_PART_MAIN);
     }
 }
 
 static void start_animation_to_current(sbb_clock_inst_t *c)
 {
     c->anim_start_hour_deg = 0.0f; c->anim_start_min_deg = 0.0f; c->anim_start_sec_deg = 0.0f;
     struct timeval tv; gettimeofday(&tv, NULL);
     struct tm tm; localtime_r(&tv.tv_sec, &tm);
     int h = tm.tm_hour % 12; int m = tm.tm_min; int s = tm.tm_sec; int ms = tv.tv_usec / 1000;
     
     float target_hour_deg = (h + m/60.0f)*30.0f; if(target_hour_deg >= 360) target_hour_deg -= 360;
     float target_min_deg = m*6.0f;
     float target_sec_deg = sbb_second_angle_deg(s*1000 + ms);
 
     c->anim_end_hour_deg = target_hour_deg;
     c->anim_end_min_deg = target_min_deg;
     c->anim_end_sec_deg = target_sec_deg;
     
     c->animation_duration_ms = 2000;
     c->animation_active = true;
     c->animation_start_ms = lv_tick_get();
 }
 
 static lv_obj_t* create_shadow_line(lv_obj_t *parent, lv_coord_t size, lv_coord_t width)
 {
     lv_obj_t *shadow = lv_line_create(parent);
     /* KEY FIX: Set line size to full clock.
        Thus line coordinates (0,0) match container (0,0),
        and computed points (cx, cy) are in the right place. */
     lv_obj_set_size(shadow, size, size);
     
     /* Offset the entire shadow object */
     lv_obj_set_pos(shadow, SHADOW_OFFSET_X, SHADOW_OFFSET_Y);
     
     lv_obj_set_style_line_width(shadow, width, LV_PART_MAIN);
     lv_obj_set_style_line_color(shadow, lv_color_hex(SHADOW_COLOR), LV_PART_MAIN);
     lv_obj_set_style_line_opa(shadow, SHADOW_OPACITY, LV_PART_MAIN);
     lv_obj_set_style_line_rounded(shadow, false, LV_PART_MAIN);
     return shadow;
 }
 
 sbb_clock_t sbb_clock_create(lv_obj_t *parent, lv_coord_t size)
 {
    sbb_clock_inst_t *c = (sbb_clock_inst_t *)lv_mem_alloc(sizeof(sbb_clock_inst_t));
    if (!c) return NULL;
    memset(c, 0, sizeof(*c));
    c->size = size;
    c->r = size / 2;
    /* Clock center is simply half its dimension - DO NOT CHANGE! */
    c->cx = c->size / 2;
    c->cy = c->size / 2;
    /* Initialize bounce physics state */
    c->last_minute = -1;
 
     /* 1. Main Container */
     lv_obj_t *cont = lv_obj_create(parent);
     lv_obj_set_size(cont, size, size);
     lv_obj_set_style_pad_all(cont, 0, LV_PART_MAIN);
     lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
     lv_obj_set_style_radius(cont, LV_RADIUS_CIRCLE, LV_PART_MAIN);
     /* Clip corner must be false so hands and shadows are not clipped on rotation */
     lv_obj_set_style_clip_corner(cont, false, LV_PART_MAIN);
     lv_obj_set_style_bg_color(cont, lv_color_hex(0x1A1A1A), LV_PART_MAIN);  /* Dark outer background */
     lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
     lv_obj_set_user_data(cont, c);
     c->container = cont;
 
     /* 2. White Dial (Tarcza) */
     c->dial = lv_obj_create(cont);
     lv_obj_set_size(c->dial, size, size);
     lv_obj_set_style_radius(c->dial, LV_RADIUS_CIRCLE, LV_PART_MAIN);
     lv_obj_set_style_bg_color(c->dial, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
     lv_obj_set_style_border_width(c->dial, 0, LV_PART_MAIN);
     lv_obj_set_style_pad_all(c->dial, 0, LV_PART_MAIN);
     lv_obj_set_style_clip_corner(c->dial, false, LV_PART_MAIN);
     lv_obj_center(c->dial);  /* Center in container */
     lv_obj_clear_flag(c->dial, LV_OBJ_FLAG_SCROLLABLE);
 
     /* Draw markers on the dial */
     create_dial_marks(c);
 
     /* Hand dimensions */
     lv_coord_t hour_w = size / 15; if (hour_w < 8) hour_w = 8;
     lv_coord_t min_w = size / 35; if (min_w < 4) min_w = 4;
     lv_coord_t sec_w = 3;
     lv_coord_t dot_r = size / 20; if (dot_r < 5) dot_r = 5;
 
     /* 3. Shadows (below) */
     /* All shadows have the full clock size (size, size) */
     c->hour_shadow = create_shadow_line(cont, size, hour_w);
     c->minute_shadow = create_shadow_line(cont, size, min_w);
     c->second_shadow = create_shadow_line(cont, size, sec_w);
     
     /* Dot shadow */
     c->dot_shadow = lv_obj_create(cont);
     lv_obj_set_size(c->dot_shadow, dot_r * 2, dot_r * 2);
     lv_obj_set_style_radius(c->dot_shadow, LV_RADIUS_CIRCLE, LV_PART_MAIN);
     lv_obj_set_style_bg_color(c->dot_shadow, lv_color_hex(SHADOW_COLOR), LV_PART_MAIN);
     lv_obj_set_style_bg_opa(c->dot_shadow, SHADOW_OPACITY, LV_PART_MAIN);
     lv_obj_set_style_border_width(c->dot_shadow, 0, LV_PART_MAIN);
     lv_obj_clear_flag(c->dot_shadow, LV_OBJ_FLAG_SCROLLABLE);
     /* Dot shadow position is set in update function (relative to dot) */
 
     /* 4. Hands (on top) */
 
     /* Hour Hand */
     c->hour_tip = (c->r * 60) / 100;
     c->hour_tail = (c->r * 15) / 100;
     c->hour_hand = lv_line_create(cont);
     /* KEY: Hand must occupy full area so points (cx, cy) are in the center */
     lv_obj_set_size(c->hour_hand, size, size); 
     lv_obj_set_pos(c->hour_hand, 0, 0);
     lv_obj_set_style_line_width(c->hour_hand, hour_w, LV_PART_MAIN);
     lv_obj_set_style_line_color(c->hour_hand, lv_color_hex(0x000000), LV_PART_MAIN);
     lv_obj_set_style_line_rounded(c->hour_hand, false, LV_PART_MAIN);
 
     /* Minute Hand */
     c->min_tip = (c->r * 90) / 100;
     c->min_tail = (c->r * 20) / 100;
     c->minute_hand = lv_line_create(cont);
     lv_obj_set_size(c->minute_hand, size, size);  /* KEY */
     lv_obj_set_pos(c->minute_hand, 0, 0);
     lv_obj_set_style_line_width(c->minute_hand, min_w, LV_PART_MAIN);
     lv_obj_set_style_line_color(c->minute_hand, lv_color_hex(0x000000), LV_PART_MAIN);
     lv_obj_set_style_line_rounded(c->minute_hand, false, LV_PART_MAIN);
 
     /* Second Hand */
     c->sec_tip = (c->r * 90) / 100;
     c->sec_tail = (c->r * 30) / 100;
     c->second_hand = lv_line_create(cont);
     lv_obj_set_size(c->second_hand, size, size);  /* KEY */
     lv_obj_set_pos(c->second_hand, 0, 0);
     lv_obj_set_style_line_width(c->second_hand, sec_w, LV_PART_MAIN);
     lv_obj_set_style_line_color(c->second_hand, lv_color_hex(0xD40000), LV_PART_MAIN);
     lv_obj_set_style_line_rounded(c->second_hand, false, LV_PART_MAIN);
 
     /* Second Dot */
     c->second_dot = lv_obj_create(cont);
     lv_obj_set_size(c->second_dot, dot_r * 2, dot_r * 2);
     lv_obj_set_style_radius(c->second_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
     lv_obj_set_style_bg_color(c->second_dot, lv_color_hex(0xD40000), LV_PART_MAIN);
     lv_obj_set_style_border_width(c->second_dot, 0, LV_PART_MAIN);
     lv_obj_clear_flag(c->second_dot, LV_OBJ_FLAG_SCROLLABLE);
 
     /* Center Cap */
     c->center_cap = lv_obj_create(cont);
     lv_obj_set_size(c->center_cap, dot_r, dot_r);
     lv_obj_set_style_radius(c->center_cap, LV_RADIUS_CIRCLE, LV_PART_MAIN);
     lv_obj_set_style_bg_color(c->center_cap, lv_color_hex(0x000000), LV_PART_MAIN);
     lv_obj_set_style_border_width(c->center_cap, 0, LV_PART_MAIN);
     lv_obj_center(c->center_cap);  /* Center cap alignment */
     lv_obj_clear_flag(c->center_cap, LV_OBJ_FLAG_SCROLLABLE);
 
    c->timer = lv_timer_create(timer_cb, 10, c);
    ESP_LOGI(TAG, "SBB clock created: aligned center with minute hand bounce physics");
 
     /* Force update to set initial positions */
     sbb_clock_update_internal(cont);
     return cont;
 }
 
 void sbb_clock_set_time_synced(sbb_clock_t clock, bool synced)
 {
     sbb_clock_inst_t *c = (sbb_clock_inst_t *)lv_obj_get_user_data(clock);
     if (!c) return;
 
     if (synced && !c->time_synced) {
         c->time_synced = true;
         start_animation_to_current(c);
     } else {
         c->time_synced = synced;
     }
     if (c->timer) lv_timer_resume(c->timer);
 }
 
 void sbb_clock_update(sbb_clock_t clock)
 {
     sbb_clock_update_internal(clock);
 }
 
 void sbb_clock_get_angles_deg(sbb_clock_t clock, float *hour_deg, float *min_deg, float *sec_deg)
 {
     sbb_clock_inst_t *c = (sbb_clock_inst_t *)lv_obj_get_user_data(clock);
     if (!c || !hour_deg || !min_deg || !sec_deg) return;
     uint32_t now_ms = (uint32_t)lv_tick_get();
     get_time_and_angles(c, now_ms, hour_deg, min_deg, sec_deg);
 }