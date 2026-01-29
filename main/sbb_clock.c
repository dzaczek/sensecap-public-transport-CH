/**
 * @file sbb_clock.c
 * @brief Swiss Railway (SBB) Clock – LVGL v8 widget with stop-to-go second hand.
 *
 * Visual: White dial, no numbers, 12 thick hour marks, 48 thin minute marks,
 * hour/minute hands (black), second hand (red + red dot), center black cap.
 * Logic: Second hand sweeps 360° in 58.5 s, freezes at 12 for 1.5 s; minute
 * jumps at new minute. At start all hands at 12; when NTP syncs, 10 s animation
 * to current time.
 * If hands/marks do not rotate: set LV_DRAW_COMPLEX 1 in lv_conf.h; dial pad_all(0).
 * If hands are invisible: container must have clip_corner false so rotating hands
 * are not clipped to the circle (SBB report §4.3 Visibility and Clipping).
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

/* SBB timing: second hand sweep 58.5 s, then pause 1.5 s (total 60 s). */
#define SBB_SWEEP_MS      58500
#define SBB_PAUSE_MS     1500
#define SBB_TOTAL_MS     (SBB_SWEEP_MS + SBB_PAUSE_MS)
#define ANIMATION_DURATION_MS  10000

/* LVGL 8: angle in 0.1 degrees (3600 = 360°). */
#define DEG_TO_LVGL(d)   ((int32_t)((d) * 10))

typedef struct {
    lv_obj_t *container;   /* main clock container */
    lv_obj_t *dial;         /* white circle background */
    lv_obj_t *hour_hand;
    lv_obj_t *minute_hand;
    lv_obj_t *second_hand;  /* red thin rectangle (replaces lv_line) */
    lv_obj_t *second_dot;   /* red circle near tip */
    lv_obj_t *center_cap;
    lv_timer_t *timer;
    lv_coord_t size;
    lv_coord_t cx;
    lv_coord_t cy;
    lv_coord_t r;           /* radius */

    bool time_synced;
    bool animation_active;
    uint32_t animation_start_ms;
    float anim_end_hour_deg;
    float anim_end_min_deg;
    float anim_end_sec_deg;

    int last_second;       /* for minute jump detection */
    
    /* Persistent points for lv_line (LVGL does not copy them!) */
    lv_point_t hour_pts[2];
    lv_point_t min_pts[2];
    lv_point_t sec_pts[2];
    
    /* Lengths stored for update calc */
    lv_coord_t hour_tip, hour_tail;
    lv_coord_t min_tip, min_tail;
    lv_coord_t sec_tip, sec_tail;
} sbb_clock_inst_t;

static void sbb_clock_update_internal(sbb_clock_t clock);

/* ----- SBB second angle: 0..58500 ms -> 0..360°, >=58500 -> 0 (freeze) ----- */
static float sbb_second_angle_deg(int ms_in_minute)
{
    if (ms_in_minute < 0) {
        ms_in_minute = 0;
    }
    if (ms_in_minute >= SBB_TOTAL_MS) {
        ms_in_minute = ms_in_minute % SBB_TOTAL_MS;
    }
    if (ms_in_minute >= SBB_SWEEP_MS) {
        return 0.0f;  /* freeze at 12 */
    }
    return (ms_in_minute / (float)SBB_SWEEP_MS) * 360.0f;
}

/* ----- Get current time and angles (real time or animation) ----- */
static void get_time_and_angles(sbb_clock_inst_t *c, uint32_t now_ms,
                                float *hour_deg, float *min_deg, float *sec_deg)
{
    if (!c->time_synced) {
        *hour_deg = 0.0f;
        *min_deg = 0.0f;
        *sec_deg = 0.0f;
        return;
    }

    if (c->animation_active) {
        uint32_t elapsed = now_ms - c->animation_start_ms;
        if (elapsed >= ANIMATION_DURATION_MS) {
            c->animation_active = false;
            *hour_deg = c->anim_end_hour_deg;
            *min_deg = c->anim_end_min_deg;
            *sec_deg = c->anim_end_sec_deg;
            return;
        }
        float t = elapsed / (float)ANIMATION_DURATION_MS;
        *hour_deg = t * c->anim_end_hour_deg;
        *min_deg = t * c->anim_end_min_deg;
        *sec_deg = t * c->anim_end_sec_deg;
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

    /* GODZINA: Wskazówka godzinowa porusza się wolno, uwzględniając minuty.
     * Uwaga: bez ułamka sekund, żeby była stabilna względem skoków minuty. */
    *hour_deg = (h + m / 60.0f) * 30.0f;
    if (*hour_deg >= 360.0f) {
        *hour_deg -= 360.0f;
    }

    /* MINUTA - POPRAWKA SBB:
     * Wskazówka minutowa zależy TYLKO od 'm', nie od sekund.
     * Stoi nieruchomo przez całe 60 s i skacze równo z nową minutą. */
    *min_deg = m * 6.0f; // 6° na minutę (360/60)

    /* SEKUNDA (SBB Stop-to-Go):
     * 0..58.5s -> obrót 360°, 58.5..60.0s -> zatrzymanie na 0° (12:00) */
    *sec_deg = sbb_second_angle_deg(ms_in_minute);
}

static void timer_cb(lv_timer_t *timer)
{
    sbb_clock_inst_t *c = (sbb_clock_inst_t *)timer->user_data;
    if (!c || !c->container) {
        ESP_LOGE(TAG, "Timer callback: invalid data!");
        return;
    }
    sbb_clock_update_internal(c->container);
}

static void update_hand_line(lv_obj_t *line, lv_point_t *pts, lv_coord_t cx, lv_coord_t cy, 
                             lv_coord_t tip_len, lv_coord_t tail_len, float angle_deg)
{
    /* Convert angle to radians (0 deg = 12 o'clock = -90 trig) */
    double rad = (angle_deg - 90.0f) * M_PI / 180.0f;
    double cos_v = cos(rad);
    double sin_v = sin(rad);

    /* Tip point (forward) */
    pts[0].x = cx + (lv_coord_t)(tip_len * cos_v);
    pts[0].y = cy + (lv_coord_t)(tip_len * sin_v);

    /* Tail point (backward) */
    pts[1].x = cx - (lv_coord_t)(tail_len * cos_v);
    pts[1].y = cy - (lv_coord_t)(tail_len * sin_v);

    /* CRITICAL: Must call lv_line_set_points again to notify LVGL! */
    lv_line_set_points(line, pts, 2);
}

static void update_second_hand_geometry(sbb_clock_inst_t *c, float sec_deg)
{
    /* Update line points using trig */
    update_hand_line(c->second_hand, c->sec_pts, c->cx, c->cy, c->sec_tip, c->sec_tail, sec_deg);

    /* Red dot at tip - use EXACT same coordinates as pts[0] (tip point from line) */
    lv_coord_t x = c->sec_pts[0].x;
    lv_coord_t y = c->sec_pts[0].y;
    
    lv_obj_set_pos(c->second_dot, x - lv_obj_get_width(c->second_dot) / 2,
                   y - lv_obj_get_height(c->second_dot) / 2);
}

static void sbb_clock_update_internal(sbb_clock_t clock)
{
    sbb_clock_inst_t *c = (sbb_clock_inst_t *)lv_obj_get_user_data(clock);
    if (!c) return;

    uint32_t now_ms = (uint32_t)lv_tick_get();
    float hour_deg, min_deg, sec_deg;
    get_time_and_angles(c, now_ms, &hour_deg, &min_deg, &sec_deg);

    static int log_cnt = 0;
    if (log_cnt++ % 100 == 0) {
        ESP_LOGI(TAG, "Clock update: H=%.1f° M=%.1f° S=%.1f° synced=%d anim=%d", 
                 hour_deg, min_deg, sec_deg, c->time_synced, c->animation_active);
    }

    /* Update Hour Hand Line */
    update_hand_line(c->hour_hand, c->hour_pts, c->cx, c->cy, c->hour_tip, c->hour_tail, hour_deg);

    /* Update Minute Hand Line */
    update_hand_line(c->minute_hand, c->min_pts, c->cx, c->cy, c->min_tip, c->min_tail, min_deg);

    /* Update Second Hand */
    update_second_hand_geometry(c, sec_deg);
}

/* Dial marks: 60 ticks (12 hour + 48 minute). Position by sin/cos so they sit
 * on the circle even if LV_DRAW_COMPLEX is off; rotation then points them inward. */
static void create_dial_marks(sbb_clock_inst_t *c)
{
    lv_obj_t *parent = c->dial;
    lv_coord_t r = c->r;

    lv_coord_t hour_mark_len = (r * 18) / 100;
    lv_coord_t hour_mark_w = 10;  /* Pogrubione kreski godzinowe (było 6) */
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

        /* Angle: 0 = 12 o'clock, 6° per minute */
        float angle = i * 6.0f;
        float distance_from_center = r - (h / 2.0f);
        double rad = (angle - 90.0) * M_PI / 180.0;

        lv_coord_t x = c->cx + (lv_coord_t)(distance_from_center * cos(rad));
        lv_coord_t y = c->cy + (lv_coord_t)(distance_from_center * sin(rad));
        lv_obj_set_pos(mark, x - w / 2, y - h / 2);

        /* Rotate so mark points toward center; pivot at mark center */
        lv_obj_set_style_transform_angle(mark, DEG_TO_LVGL(angle), LV_PART_MAIN);
        lv_obj_set_style_transform_pivot_x(mark, w / 2, LV_PART_MAIN);
        lv_obj_set_style_transform_pivot_y(mark, h / 2, LV_PART_MAIN);
    }
}

static void start_animation_to_current(sbb_clock_inst_t *c)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm;
    localtime_r(&tv.tv_sec, &tm);
    int h = tm.tm_hour % 12;
    int m = tm.tm_min;
    int s = tm.tm_sec;
    int ms = (int)(tv.tv_usec / 1000);
    int ms_in_min = s * 1000 + ms;

    c->anim_end_hour_deg = (h + m / 60.0f + (s + ms / 1000.0f) / 3600.0f) * 30.0f;
    if (c->anim_end_hour_deg >= 360.0f) c->anim_end_hour_deg -= 360.0f;
    c->anim_end_min_deg = (m + (s + ms / 1000.0f) / 60.0f) * 6.0f;
    if (c->anim_end_min_deg >= 360.0f) c->anim_end_min_deg -= 360.0f;
    c->anim_end_sec_deg = sbb_second_angle_deg(ms_in_min);

    c->animation_active = true;
    c->animation_start_ms = (uint32_t)lv_tick_get();
    ESP_LOGI(TAG, "SBB clock: 10 s animation to %02d:%02d:%02d", tm.tm_hour, m, s);
}

sbb_clock_t sbb_clock_create(lv_obj_t *parent, lv_coord_t size)
{
    sbb_clock_inst_t *c = (sbb_clock_inst_t *)lv_mem_alloc(sizeof(sbb_clock_inst_t));
    if (!c) {
        return NULL;
    }
    memset(c, 0, sizeof(*c));
    c->size = size;
    c->r = size / 2;
    c->cx = c->r;
    c->cy = c->r;
    c->last_second = -1;

    /* Container: do NOT clip children. If clip_corner is true, rotating hands
     * are clipped to the circle and become invisible (SBB report §4.3). */
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, size, size);
    lv_obj_set_style_pad_all(cont, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(cont, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(cont, false, LV_PART_MAIN);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x1A1A1A), LV_PART_MAIN);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(cont, c);
    c->container = cont;

    /* White circular dial – pad_all(0) for correct position math; no clip so hands can draw on top */
    c->dial = lv_obj_create(cont);
    lv_obj_set_size(c->dial, size, size);
    lv_obj_set_style_radius(c->dial, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(c->dial, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_border_width(c->dial, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(c->dial, 0, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(c->dial, false, LV_PART_MAIN);
    lv_obj_center(c->dial);
    lv_obj_clear_flag(c->dial, LV_OBJ_FLAG_SCROLLABLE);

    /* Draw marks first (they stay under the hands) */
    create_dial_marks(c);

    /* Hands as lv_line (vectors) to avoid rotation artifacts */

    /* Hour hand - pogrubiona dla lepszych proporcji SBB */
    c->hour_tip = (c->r * 60) / 100;
    c->hour_tail = (c->r * 15) / 100;
    lv_coord_t hour_w = size / 15;  /* Grubsza (było size/20) */
    if (hour_w < 8) hour_w = 8;     /* Minimum 8px (było 6) */
    
    c->hour_hand = lv_line_create(cont);
    lv_obj_set_pos(c->hour_hand, 0, 0); /* Points are absolute in cont */
    lv_obj_set_style_line_width(c->hour_hand, hour_w, LV_PART_MAIN);
    lv_obj_set_style_line_color(c->hour_hand, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_line_rounded(c->hour_hand, false, LV_PART_MAIN); /* Square ends */
    lv_line_set_points(c->hour_hand, c->hour_pts, 2);

    /* Minute hand */
    c->min_tip = (c->r * 90) / 100;
    c->min_tail = (c->r * 20) / 100;
    lv_coord_t min_w = size / 35;
    if (min_w < 4) min_w = 4;

    c->minute_hand = lv_line_create(cont);
    lv_obj_set_pos(c->minute_hand, 0, 0);
    lv_obj_set_style_line_width(c->minute_hand, min_w, LV_PART_MAIN);
    lv_obj_set_style_line_color(c->minute_hand, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_line_rounded(c->minute_hand, false, LV_PART_MAIN);
    lv_line_set_points(c->minute_hand, c->min_pts, 2);

    /* Second hand (Line part) */
    c->sec_tip = (c->r * 90) / 100;
    c->sec_tail = (c->r * 30) / 100;
    lv_coord_t sec_w = 3;

    c->second_hand = lv_line_create(cont);
    lv_obj_set_pos(c->second_hand, 0, 0);
    lv_obj_set_style_line_width(c->second_hand, sec_w, LV_PART_MAIN);
    lv_obj_set_style_line_color(c->second_hand, lv_color_hex(0xD40000), LV_PART_MAIN);
    lv_obj_set_style_line_rounded(c->second_hand, false, LV_PART_MAIN);
    lv_line_set_points(c->second_hand, c->sec_pts, 2);

    /* Red dot at second hand tip (lizak) */
    lv_coord_t dot_r = size / 20;
    if (dot_r < 5) dot_r = 5;
    c->second_dot = lv_obj_create(cont);
    lv_obj_set_size(c->second_dot, dot_r * 2, dot_r * 2);
    lv_obj_set_style_radius(c->second_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(c->second_dot, lv_color_hex(0xD40000), LV_PART_MAIN);
    lv_obj_set_style_border_width(c->second_dot, 0, LV_PART_MAIN);
    lv_obj_clear_flag(c->second_dot, LV_OBJ_FLAG_SCROLLABLE);

    /* Center black cap */
    c->center_cap = lv_obj_create(cont);
    lv_obj_set_size(c->center_cap, dot_r, dot_r);
    lv_obj_set_style_radius(c->center_cap, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(c->center_cap, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_border_width(c->center_cap, 0, LV_PART_MAIN);
    lv_obj_center(c->center_cap);
    lv_obj_clear_flag(c->center_cap, LV_OBJ_FLAG_SCROLLABLE);

    /* Create timer (20 ms ≈ 50 Hz for smooth sweep) */
    c->timer = lv_timer_create(timer_cb, 20, c);
    ESP_LOGI(TAG, "SBB clock created: size=%d, r=%d, timer=%p", size, c->r, c->timer);

    /* Initial update to set points at 12:00 */
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
    if (c->timer) {
        lv_timer_resume(c->timer);
    }
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
