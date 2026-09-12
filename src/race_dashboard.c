#include "race_dashboard.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TRACK_LENGTH_FT 12623.03f
#define TRACK_LAPS 4
#define WING_SEGMENTS 10
#define MAX_LIVE_SEGMENTS 16

/* Palette lifted from the browser build's colors.css so the two front ends
   agree.  Translucent CSS values are pre-composited against whatever sits
   behind them: a panel is white at 30 percent over black, and a panel border
   is white at 10 percent over the panel. */
/* Rail geometry.  The widths are fixed by the layout below; segments need
   them in pixels because a rail is usually only partly filled, so a
   percentage width would stretch the segments across the whole rail.
   RAIL_FULL_W is the drawable width of the bottom panel, and the four lap
   cells divide it evenly with RAIL_LAP_GAP between them. */
#define RAIL_INSET       4    /* one pixel of border and one of padding, both sides */
#define RAIL_FULL_W    994
#define RAIL_LAP_W     244
#define RAIL_LAP_STRIDE 250

#define C_BG              0x000000  /* --color-bg */
#define C_BG_SECONDARY    0x121212  /* --color-bg-secondary, empty rail */
#define C_PANEL           0x4D4D4D  /* --color-panel-background over black */
#define C_PANEL_BORDER    0x5F5F5F  /* --color-border-gray over a panel */
#define C_TEXT            0xFFFFFF  /* --color-text */
#define C_LABEL           0x61CBF4  /* --color-text-secondary, panel headings */
#define C_TECH            0x00A8FF  /* --color-tech, track marker */
#define C_GRAY            0xB4B4B4  /* --color-gray */
#define C_GREEN_HIGHLIGHT 0x00FF88  /* --color-green-highlight */
#define C_ALERT           0xFF0000  /* --color-alert */

/* Strategy rail fills.  Burn and coast each have a lived and a planned
   colour, and the pairs are deliberately unalike so the driver can tell the
   two rails apart at a glance. */
#define C_LIVE_COAST      0x00E060  /* --color-position-real */
#define C_LIVE_BURN       0xFFD500  /* --color-gas-real */
#define C_PLANNED_COAST   0x007BFF  /* --color-position-sim */
#define C_PLANNED_BURN    0xFF8C00  /* --color-gas-sim */

/* Engine status pills.  Unknown is a distinct third state, not a stand-in
   for off. */
#define C_ICON_ON         0x00A338  /* --color-icon-on */
#define C_ICON_OFF        0xC00000  /* --color-icon-off */
#define C_ICON_UNKNOWN    0xB4B4B4  /* --color-icon-disabled */
#define DEG_TO_RAD 0.01745329251994329577f

/* Montserrat digits at the size the browser build used for the speed readout
   (14em against a 16px root).  Generated into src/fonts; see that file's header
   for the exact command.  Rendering real glyphs at this size keeps the number
   sharp on the 1024x600 panel; scaling up a 48px face does not. */
LV_FONT_DECLARE(speed_digits_224);

/* A race segment is either engine-off coasting or engine-on burning. */
typedef enum { SEG_COAST, SEG_BURN } segment_type_t;
/* A planned strategy point records where the current segment finishes. */
typedef struct { float end_ft; segment_type_t type; } strategy_point_t;
/* A rendered bar segment records a width as a percentage of one lap. */
typedef struct { float pct; segment_type_t type; } progress_segment_t;
/* All object handles and persistent telemetry/race state for this one screen. */
typedef struct {
    lv_obj_t * root;
    lv_obj_t * speed_value;
    lv_obj_t * voltage_value;
    lv_obj_t * armed;
    lv_obj_t * running;
    lv_obj_t * wind;
    lv_obj_t * relative;
    lv_obj_t * lap_label;
    lv_obj_t * marker;
    lv_obj_t * wing[WING_SEGMENTS * 2];
    lv_obj_t * current_live;
    lv_obj_t * current_sim;
    lv_obj_t * full_live[TRACK_LAPS];
    float offset_ft;
    float previous_distance_ft;
    bool reset_was_pressed;
    bool has_distance;
    bool race_complete;
    segment_type_t previous_status;
    progress_segment_t live[TRACK_LAPS][MAX_LIVE_SEGMENTS];
    uint8_t live_count[TRACK_LAPS];
    lv_timer_t * sequence_timer;
    uint16_t sequence_step;
    uint16_t burn_steps;
    uint16_t coast_steps;
} dashboard_t;

/* Single dashboard instance; this port intentionally exposes one full-screen UI. */
static dashboard_t dash;

/* Original SAMPLE_SIMULATION reduced to change points. */
static const strategy_point_t simulation[] = {
    { 3000, SEG_COAST }, { 5500, SEG_BURN }, { 8500, SEG_COAST },
    {10800, SEG_BURN }, {12621, SEG_COAST },
};
#define SIMULATION_POINTS (sizeof(simulation) / sizeof(simulation[0]))

/* User-supplied ShellTrackFixed samples, decimated only along straight runs.
   They are scaled once into the 225 x 120 px map viewport at startup. */
static const float track_source[][2] = {
    {226.99f,419.037f},{226.99f,369.043f},{226.080f,319.058f},{225.630f,269.062f},{224.720f,219.114f},{222.000f,169.210f},{222.000f,119.254f},
    {234.339f,100.230f},{263.765f,96.941f},{273.700f,81.020f},{271.767f,61.139f},{282.837f,33.953f},{307.226f,17.005f},{345.156f,5.000f},
    {370.217f,19.751f},{374.015f,39.006f},{374.381f,69.001f},{373.436f,86.365f},{397.948f,102.774f},{400.047f,132.236f},{400.627f,162.228f},
    {401.130f,192.221f},{401.712f,232.228f},{402.378f,252.212f},{403.087f,282.189f},{403.779f,342.196f},{404.283f,382.197f},{412.545f,408.200f},
    {441.719f,412.548f},{471.451f,414.065f},{487.118f,425.699f},{498.895f,441.167f},{528.202f,444.110f},{558.149f,442.620f},{572.626f,454.550f},
    {577.840f,473.683f},{573.312f,503.232f},{565.289f,542.412f},{549.879f,567.722f},{524.950f,583.585f},{496.144f,591.559f},{466.299f,594.090f},
    {426.301f,594.514f},{386.346f,594.399f},{379.453f,591.097f},{381.405f,551.240f},{373.077f,534.190f},{354.845f,536.124f},{326.194f,545.020f},
    {296.853f,550.395f},{267.590f,544.538f},{244.121f,526.572f},{229.982f,500.265f},{228.222f,480.612f},{227.575f,440.612f},{226.990f,419.037f}
};
#define TRACK_POINT_COUNT (sizeof(track_source) / sizeof(track_source[0]))
static lv_point_precise_t track_points[TRACK_POINT_COUNT];

static void prepare_track_points(void)
{
    /* Raw bounds: x=222..578, y=5..595. Keep aspect ratio, center in map. */
    for(size_t i = 0; i < TRACK_POINT_COUNT; i++) {
        track_points[i].x = (lv_coord_t)(67.0f + (track_source[i][0] - 222.0f) * 0.19f);
        track_points[i].y = (lv_coord_t)(2.0f + (track_source[i][1] - 5.0f) * 0.19f);
    }
}

/* Choose the live-race color for a burn or coast state. */
static lv_color_t color(segment_type_t status)
{
    return lv_color_hex(status == SEG_BURN ? C_LIVE_BURN : C_LIVE_COAST);
}

/* Choose the planned-strategy color for a burn or coast state. */
static lv_color_t simulation_color(segment_type_t status)
{
    return lv_color_hex(status == SEG_BURN ? C_PLANNED_BURN : C_PLANNED_COAST);
}

/* Create a label with the shared dashboard typography setup. */
static lv_obj_t * make_label(lv_obj_t * parent, const char * text, lv_color_t c, lv_font_t const * font)
{
    lv_obj_t * label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, c, 0);
    lv_obj_set_style_text_font(label, font, 0);
    return label;
}

/* Create one rounded gray card used by the four side/bottom panels. */
static lv_obj_t * panel(lv_obj_t * parent)
{
    lv_obj_t * p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_set_style_bg_color(p, lv_color_hex(C_PANEL), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p, 16, 0);
    lv_obj_set_style_pad_all(p, 10, 0);
    lv_obj_set_style_border_width(p, 1, 0);
    lv_obj_set_style_border_color(p, lv_color_hex(C_PANEL_BORDER), 0);
    return p;
}

/* Rebuild a progress rail from its current segment list.  content_width is
   the drawable width of the rail in pixels. */
static void populate_progress(lv_obj_t * host, const progress_segment_t * segments, uint8_t count,
                              bool simulated, lv_coord_t content_width)
{
    lv_obj_clean(host);
    lv_obj_set_layout(host, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(host, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(host, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for(uint8_t i = 0; i < count; i++) {
        if(segments[i].pct <= 0.0f) continue;
        lv_coord_t w = (lv_coord_t)lroundf(segments[i].pct * content_width / 100.0f);
        if(w <= 0) continue;
        lv_obj_t * item = lv_obj_create(host);
        lv_obj_remove_style_all(item);
        lv_obj_set_size(item, w, LV_PCT(100));
        lv_obj_set_style_bg_color(item, simulated ? simulation_color(segments[i].type) : color(segments[i].type), 0);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
    }
}

/* Create the black, bordered container behind a single progress rail. */
static lv_obj_t * progress_host(lv_obj_t * parent, lv_coord_t x, lv_coord_t y,
                                lv_coord_t width, lv_coord_t height)
{
    lv_obj_t * host = lv_obj_create(parent);
    lv_obj_remove_style_all(host);
    lv_obj_set_pos(host, x, y);
    lv_obj_set_size(host, width, height);
    lv_obj_set_style_bg_color(host, lv_color_hex(C_BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(host, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(host, lv_color_hex(C_GRAY), 0);
    lv_obj_set_style_border_width(host, 1, 0);
    lv_obj_set_style_pad_all(host, 1, 0);
    return host;
}

/* Refresh the live current-lap rail, planned rail, and four-lap summary. */
static void refresh_progress(void)
{
    uint8_t lap = 0;
    if(dash.has_distance) {
        float adjusted = LV_MAX(0.0f, dash.previous_distance_ft - dash.offset_ft);
        lap = (uint8_t)LV_MIN((int)(adjusted / TRACK_LENGTH_FT), TRACK_LAPS - 1);
    }

    /* Top rail: the lap being driven right now, at full width. */
    populate_progress(dash.current_live, dash.live[lap], dash.live_count[lap],
                      false, RAIL_FULL_W - RAIL_INSET);

    /* Bottom row: every lap of the race, each in its own quarter-width cell. */
    for(uint8_t i = 0; i < TRACK_LAPS; i++) {
        populate_progress(dash.full_live[i], dash.live[i], dash.live_count[i],
                          false, RAIL_LAP_W - RAIL_INSET);
    }
}

/* Draw the planned strategy rail.  The plan does not change during a race,
   so this runs once at startup rather than on every telemetry sample. */
static void build_planned_rail(void)
{
    progress_segment_t planned[SIMULATION_POINTS];
    float start = 0.0f;
    for(uint8_t i = 0; i < SIMULATION_POINTS; i++) {
        planned[i].pct = (simulation[i].end_ft - start) * 100.0f / TRACK_LENGTH_FT;
        planned[i].type = simulation[i].type;
        start = simulation[i].end_ft;
    }
    populate_progress(dash.current_sim, planned, SIMULATION_POINTS, true, RAIL_FULL_W - RAIL_INSET);
}

/* Start a new race at the supplied odometer value and clear live history. */
static void reset_race(float distance, segment_type_t status)
{
    dash.offset_ft = distance;
    dash.previous_distance_ft = distance;
    dash.has_distance = true;
    dash.race_complete = false;
    dash.previous_status = status;
    memset(dash.live_count, 0, sizeof(dash.live_count));
    dash.live_count[0] = 1;
    dash.live[0][0] = (progress_segment_t){ 0, status };
    refresh_progress();
}

/* Move the cyan vehicle marker and update the card's displayed lap number. */
static void update_track(float distance)
{
    float adjusted = LV_MAX(0.0f, distance - dash.offset_ft);
    float normalized = fmodf(adjusted, TRACK_LENGTH_FT) / TRACK_LENGTH_FT;
    float position = normalized * (float)(TRACK_POINT_COUNT - 1);
    size_t i = (size_t)position;
    float f = position - i;
    if( i >= TRACK_POINT_COUNT - 1 ) {
        i = TRACK_POINT_COUNT - 2;
        f = 1.0f;
    }
    lv_coord_t x = (lv_coord_t)(track_points[i].x + (track_points[i + 1].x - track_points[i].x) * f);
    lv_coord_t y = (lv_coord_t)(track_points[i].y + (track_points[i + 1].y - track_points[i].y) * f);
    lv_obj_set_pos(dash.marker, x - 7, y - 7);
    char buf[24];
    snprintf(buf, sizeof(buf), "Current Lap: %d", (int)(adjusted / TRACK_LENGTH_FT) + 1);
    lv_label_set_text(dash.lap_label, buf);
}

/* Extend live progress by distance travelled and split it when engine state changes. */
static void append_live(float distance, segment_type_t status)
{
    if(!dash.has_distance) { reset_race(distance, status); return; }
    if(dash.race_complete) return;
    float adjusted = LV_MAX(0.0f, distance - dash.offset_ft);
    float previous = LV_MAX(0.0f, dash.previous_distance_ft - dash.offset_ft);
    uint8_t lap = (uint8_t)LV_MIN((int)(adjusted / TRACK_LENGTH_FT), TRACK_LAPS - 1);
    uint8_t prior_lap = (uint8_t)LV_MIN((int)(previous / TRACK_LENGTH_FT), TRACK_LAPS - 1);
    if(lap != prior_lap && dash.live_count[lap] == 0) {
        dash.live_count[lap] = 1;
        dash.live[lap][0] = (progress_segment_t){ 0, status };
    }
    if(dash.previous_status != status && dash.live_count[lap] < MAX_LIVE_SEGMENTS) {
        dash.live[lap][dash.live_count[lap]++] = (progress_segment_t){ 0, status };
        dash.previous_status = status;
    }
    float delta_pct = (distance - dash.previous_distance_ft) * 100.0f / TRACK_LENGTH_FT;
    if(delta_pct > 0 && dash.live_count[lap])
        dash.live[lap][dash.live_count[lap] - 1].pct += delta_pct;
    dash.previous_distance_ft = distance;
    /* Freeze the rails once the final lap is done so the last segment stops
       growing past the end of the race. */
    if(adjusted >= TRACK_LENGTH_FT * TRACK_LAPS) dash.race_complete = true;
    refresh_progress();
}


/* Build a centered status pill and return its label so telemetry can recolor it. */
static void make_status(lv_obj_t * parent, const char * text, lv_obj_t ** target)
{
    *target = make_label(parent, text, lv_color_hex(C_TEXT), &lv_font_montserrat_16);
    lv_obj_set_width(*target, LV_PCT(100));
    lv_obj_set_style_bg_color(*target, lv_color_hex(C_ICON_UNKNOWN), 0);
    lv_obj_set_style_bg_opa(*target, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(*target, 8, 0);
    lv_obj_set_style_pad_ver(*target, 7, 0);
    lv_obj_set_style_text_align(*target, LV_TEXT_ALIGN_CENTER, 0);
}

/* Construct every screen object once. Call only after LVGL/display initialization. */
void race_dashboard_create(lv_obj_t * parent)
{
    memset(&dash, 0, sizeof(dash));
    dash.root = parent;
    lv_obj_set_size(parent, 1024, 600);
    lv_obj_set_style_bg_color(parent, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    /* Reference layout: four compact side cards, an unboxed central speedometer,
       and a full-width, three-rail strategy panel. */
    lv_obj_t * left = panel(parent);
    lv_obj_set_pos(left, 36, 58);
    lv_obj_set_size(left, 228, 195);

    lv_obj_t * left_voltage = panel(parent);
    lv_obj_set_pos(left_voltage, 36, 277);
    lv_obj_set_size(left_voltage, 228, 164);

    lv_obj_t * center = lv_obj_create(parent);
    lv_obj_remove_style_all(center);
    lv_obj_set_pos(center, 282, 45);
    lv_obj_set_size(center, 460, 410);

    lv_obj_t * right = panel(parent);
    lv_obj_set_pos(right, 797, 58);
    lv_obj_set_size(right, 192, 175);

    lv_obj_t * right_status = panel(parent);
    lv_obj_set_pos(right_status, 797, 255);
    lv_obj_set_size(right_status, 192, 164);

    lv_obj_t * bottom = panel(parent);
    lv_obj_set_pos(bottom, 4, 470);
    lv_obj_set_size(bottom, 1016, 126);

    dash.lap_label = make_label(left, "Current Lap: 1", lv_color_hex(C_TEXT), &lv_font_montserrat_16);
    lv_obj_align(dash.lap_label, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t * map = lv_obj_create(left); lv_obj_remove_style_all(map); lv_obj_set_size(map, 225, 120); lv_obj_align(map, LV_ALIGN_TOP_MID, 0, 28);
    prepare_track_points();
    lv_obj_t * line = lv_line_create(map); lv_line_set_points(line, track_points, TRACK_POINT_COUNT);
    lv_obj_set_size(line, LV_PCT(100), LV_PCT(100)); lv_obj_set_style_line_color(line, lv_color_hex(C_GRAY), 0);
    lv_obj_set_style_line_width(line, 7, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    dash.marker = lv_obj_create(map);

    lv_obj_remove_style_all(dash.marker);
    lv_obj_set_size(dash.marker, 14, 14);
    lv_obj_set_style_bg_color(dash.marker, lv_color_hex(C_TECH), 0);
    lv_obj_set_style_bg_opa(dash.marker, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dash.marker, LV_RADIUS_CIRCLE, 0);

    lv_obj_t * voltage_title = make_label(left_voltage, "VOLTAGE", lv_color_hex(C_LABEL), &lv_font_montserrat_16);
    lv_obj_align(voltage_title, LV_ALIGN_TOP_MID, 0, 20);

    dash.voltage_value = make_label(left_voltage, "--", lv_color_hex(C_TEXT), &lv_font_montserrat_48);
    lv_obj_align(dash.voltage_value, LV_ALIGN_CENTER, 0, 9);

    lv_obj_t * vunit = make_label(left_voltage, "V", lv_color_hex(C_TEXT), &lv_font_montserrat_16);
    lv_obj_align(vunit, LV_ALIGN_BOTTOM_MID, 0, -14);

    lv_obj_t * speed_box = lv_obj_create(center);
    lv_obj_remove_style_all(speed_box);
    lv_obj_set_size(speed_box, LV_PCT(100), LV_PCT(100));

    /* Place the 20 tick objects on two circular arcs.  Each rectangle is
       tangentially rotated; this avoids the non-circular chevron made by
       positioning the ticks along two straight lines. */
    for(uint8_t i = 0; i < WING_SEGMENTS; i++) {
        float left_angle = (135.0f + i * 10.0f) * DEG_TO_RAD;
        float right_angle = (45.0f - i * 10.0f) * DEG_TO_RAD;
        float angles[2] = { left_angle, right_angle };
        for(uint8_t side = 0; side < 2; side++) {
            uint8_t index = side == 0 ? i : WING_SEGMENTS + i;
            float a = angles[side];
            dash.wing[index] = lv_obj_create(speed_box);
            lv_obj_remove_style_all(dash.wing[index]);
            lv_obj_set_size(dash.wing[index], 16, 32);
            lv_obj_set_pos(dash.wing[index],
                           (lv_coord_t)lroundf(230.0f + 200.0f * cosf(a) - 8.0f),
                           (lv_coord_t)lroundf(205.0f + 200.0f * sinf(a) - 16.0f));
            lv_obj_set_style_bg_color(dash.wing[index], lv_color_hex(C_GRAY), 0);
            lv_obj_set_style_bg_opa(dash.wing[index], LV_OPA_COVER, 0);
            lv_obj_set_style_transform_angle(dash.wing[index],
                                             (int32_t)lroundf((a / DEG_TO_RAD + 90.0f) * 10.0f), 0);
            lv_obj_set_style_transform_width(dash.wing[index], 6, 0);
            lv_obj_set_style_transform_height(dash.wing[index], 6, 0);
        }
    }

    dash.speed_value = make_label(speed_box, "0", lv_color_hex(C_TEXT), &speed_digits_224);
    lv_obj_align(dash.speed_value, LV_ALIGN_CENTER, 0, -16);

    lv_obj_t * mph = make_label(speed_box, "MPH", lv_color_hex(C_TEXT), &lv_font_montserrat_24);
    lv_obj_align(mph, LV_ALIGN_CENTER, 0, 120);

    lv_obj_t * wind_title = make_label(right, "HEADWIND SPEED", lv_color_hex(C_LABEL), &lv_font_montserrat_16); lv_obj_align(wind_title, LV_ALIGN_TOP_MID, 0, 20);
    dash.wind = make_label(right, "0.0", lv_color_hex(C_TEXT), &lv_font_montserrat_48);
    lv_obj_align(dash.wind, LV_ALIGN_CENTER, 0, 5);

    dash.relative = make_label(right, "", lv_color_hex(C_TEXT), &lv_font_montserrat_16);
    lv_obj_add_flag(dash.relative, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t * wind_units = make_label(right, "MPH", lv_color_hex(C_TEXT), &lv_font_montserrat_16);
    lv_obj_align(wind_units, LV_ALIGN_BOTTOM_MID, 0, -17);

    lv_obj_t * stitle = make_label(right_status, "ENGINE STATUS", lv_color_hex(C_LABEL), &lv_font_montserrat_16);
    lv_obj_align(stitle, LV_ALIGN_TOP_MID, 0, 18);

    make_status(right_status, "Armed", &dash.armed);
    lv_obj_align(dash.armed, LV_ALIGN_TOP_MID, 0, 58);

    make_status(right_status, "Running", &dash.running);
    lv_obj_align(dash.running, LV_ALIGN_TOP_MID, 0, 108);

    dash.current_live = progress_host(bottom, 0, 0, RAIL_FULL_W, 34);
    dash.current_sim = progress_host(bottom, 0, 38, RAIL_FULL_W, 34);
    for(uint8_t i = 0; i < TRACK_LAPS; i++) dash.full_live[i] = progress_host(bottom, i * RAIL_LAP_STRIDE, 80, RAIL_LAP_W, 22);
    build_planned_rail();
    reset_race(0, SEG_COAST);
    update_track(0);
}

/* Apply one normalized telemetry sample to the existing UI and race state. */
void race_dashboard_set_telemetry(const race_telemetry_t * t)
{
    if(!t || !dash.root) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", (int)lroundf(t->speed_mph));
    lv_label_set_text(dash.speed_value, buf);

    snprintf(buf, sizeof(buf), "%.1f", t->airspeed_mph);
    lv_label_set_text(dash.wind, buf);

    snprintf(buf, sizeof(buf), "%.1f", t->speed_mph - t->airspeed_mph);
    lv_label_set_text(dash.relative, buf);

    if(t->voltage_valid) {
        snprintf(buf, sizeof(buf), "%d", (int)lroundf(t->voltage_v));
        lv_label_set_text(dash.voltage_value, buf);
    } else lv_label_set_text(dash.voltage_value, "--");

    lv_obj_set_style_bg_color(dash.armed, lv_color_hex(t->engine_armed ? C_ICON_ON : C_ICON_OFF), 0);

    lv_obj_set_style_bg_color(dash.running, lv_color_hex(t->engine_on ? C_ICON_ON : C_ICON_OFF), 0);
    segment_type_t status = t->engine_on ? SEG_BURN : SEG_COAST;

    /* Restart on the press, not for as long as the button is held down. */
    if(t->timer_reset && !dash.reset_was_pressed) reset_race(t->distance_ft, status);

    dash.reset_was_pressed = t->timer_reset;
    append_live(t->distance_ft, status);
    update_track(t->distance_ft);
}
