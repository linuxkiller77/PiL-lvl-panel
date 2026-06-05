#include "ui_demo.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "app/app_state.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"

#define SCREEN_W 800
#define SCREEN_H 480
#define ROOT_W 780
#define ROOT_H 460
#define BODY_H 332

typedef enum {
    UI_PAGE_HOME = 0,
    UI_PAGE_CONTROLS,
    UI_PAGE_DIAGNOSTICS,
    UI_PAGE_LOGS,
    UI_PAGE_COUNT,
} ui_page_t;

static lv_obj_t *s_root;
static lv_obj_t *s_title_label;
static lv_obj_t *s_body;
static lv_obj_t *s_nav;
static lv_obj_t *s_status_bar_label;
static lv_obj_t *s_pulse;
static lv_obj_t *s_pages[UI_PAGE_COUNT];
static ui_page_t s_current_page = UI_PAGE_HOME;

static lv_obj_t *s_home_touch_count_label;
static lv_obj_t *s_home_uptime_label;
static lv_obj_t *s_slider_label;
static lv_obj_t *s_switch_label;
static lv_obj_t *s_mode_label;
static lv_obj_t *s_slider;
static lv_obj_t *s_bar;
static lv_obj_t *s_switch;
static lv_obj_t *s_dropdown;

static lv_obj_t *s_diag_uptime_label;
static lv_obj_t *s_diag_task_label;
static lv_obj_t *s_diag_cpu_label;
static lv_obj_t *s_diag_heap_label;
static lv_obj_t *s_diag_psram_label;
static lv_obj_t *s_diag_stack_label;
static lv_obj_t *s_diag_logic_label;
static lv_obj_t *s_diag_ui_label;
static lv_obj_t *s_diag_diag_label;
static lv_obj_t *s_diag_rate_label;
static lv_obj_t *s_diag_timing_label;
static lv_obj_t *s_diag_page_label;

static const char *mode_names[] = {"Dashboard", "Setup", "Test", "Service"};
static const char *page_names[] = {"Home", "Controls", "Diagnostics", "Logs"};

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    return label;
}

static lv_obj_t *make_card(lv_obj_t *parent, int w, int h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_radius(card, 18, LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1D2939), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(0x475467), LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 9, LV_PART_MAIN);
    return card;
}

static void set_label_text_if_changed(lv_obj_t *label, const char *text)
{
    if (!label || !text) {
        return;
    }
    const char *old = lv_label_get_text(label);
    if (!old || strcmp(old, text) != 0) {
        lv_label_set_text(label, text);
    }
}

static void make_metric(lv_obj_t *parent, lv_obj_t **store, const char *text)
{
    *store = make_label(parent, text, 0xD0D5DD);
    lv_label_set_long_mode(*store, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(*store, 335);
}

static const char *get_mode_name(uint8_t mode_index)
{
    if (mode_index < (sizeof(mode_names) / sizeof(mode_names[0]))) {
        return mode_names[mode_index];
    }
    return "Unknown";
}

static void update_pulse(void)
{
    static bool bright;
    bright = !bright;
    if (s_pulse) {
        lv_obj_set_style_bg_color(s_pulse, lv_color_hex(bright ? 0x12B76A : 0x344054), LV_PART_MAIN);
    }
}

static void set_status(const char *text)
{
    set_label_text_if_changed(s_status_bar_label, text ? text : "Ready");
    update_pulse();
}

static void set_title_for_page(ui_page_t page)
{
    char line[96];
    snprintf(line, sizeof(line), "ESP-IDF 6 + LVGL 9 - %s", page_names[(int)page]);
    set_label_text_if_changed(s_title_label, line);
}

static void update_touch_label_immediate(void)
{
    app_state_snapshot_t snapshot;
    app_state_get_snapshot(&snapshot);

    char line[80];
    snprintf(line, sizeof(line), "Touch clicks: %lu", (unsigned long)snapshot.touch_clicks);
    set_label_text_if_changed(s_home_touch_count_label, line);
    set_status("Touch event handled immediately");
}

static void touch_button_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        app_state_note_touch_click();
        update_touch_label_immediate();
    }
}

static void slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int value = (int)lv_slider_get_value(slider);
    app_state_set_slider_value(value);

    char line[64];
    snprintf(line, sizeof(line), "Slider: %d", value);
    set_label_text_if_changed(s_slider_label, line);
    if (s_bar) {
        lv_bar_set_value(s_bar, value, LV_ANIM_OFF);
    }
    set_status("Slider updated immediately");
}

static void switch_event_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    app_state_set_switch_enabled(enabled);
    set_label_text_if_changed(s_switch_label, enabled ? "Switch: ON" : "Switch: OFF");
    set_status(enabled ? "Switch turned ON" : "Switch turned OFF");
}

static void dropdown_event_cb(lv_event_t *e)
{
    lv_obj_t *dropdown = lv_event_get_target(e);
    uint8_t mode_index = (uint8_t)lv_dropdown_get_selected(dropdown);
    app_state_set_mode_index(mode_index);

    char line[96];
    snprintf(line, sizeof(line), "Mode: %s", get_mode_name(mode_index));
    set_label_text_if_changed(s_mode_label, line);
    snprintf(line, sizeof(line), "Mode changed to %s", get_mode_name(mode_index));
    set_status(line);
}

static void select_page(ui_page_t page)
{
    if (page >= UI_PAGE_COUNT) {
        page = UI_PAGE_HOME;
    }

    int64_t start_us = esp_timer_get_time();

    s_current_page = page;
    for (int i = 0; i < UI_PAGE_COUNT; ++i) {
        if (s_pages[i]) {
            if (i == (int)page) {
                lv_obj_clear_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    set_title_for_page(page);
    char line[96];
    snprintf(line, sizeof(line), "%s screen selected", page_names[(int)page]);
    set_status(line);

    app_state_set_page_switch_time_us((uint32_t)(esp_timer_get_time() - start_us));
}

static void nav_button_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    intptr_t page_value = (intptr_t)lv_event_get_user_data(e);
    select_page((ui_page_t)page_value);
}

static void make_nav_button(lv_obj_t *parent, const char *text, ui_page_t page)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 132, 42);
    lv_obj_add_event_cb(btn, nav_button_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)page);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
}

static lv_obj_t *create_page(ui_page_t page)
{
    lv_obj_t *p = lv_obj_create(s_body);
    lv_obj_set_size(p, ROOT_W, BODY_H);
    lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(p, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(p, 0, LV_PART_MAIN);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(p, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(p, 16, LV_PART_MAIN);
    if (page != UI_PAGE_HOME) {
        lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    }
    s_pages[page] = p;
    return p;
}

static void build_home_page(void)
{
    lv_obj_t *page = create_page(UI_PAGE_HOME);

    lv_obj_t *left = make_card(page, 370, 324);
    make_label(left, "Touchscreen appliance base", 0xFFFFFF);
    make_label(left, "This version builds screens once and hide/shows them. Screen changes should be less CPU-spiky than delete/rebuild switching.", 0xD0D5DD);

    lv_obj_t *btn = lv_button_create(left);
    lv_obj_set_size(btn, 240, 52);
    lv_obj_add_event_cb(btn, touch_button_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Touch test button");
    lv_obj_center(btn_label);

    make_metric(left, &s_home_touch_count_label, "Touch clicks: 0");
    make_metric(left, &s_home_uptime_label, "Uptime: 0 s");

    lv_obj_t *right = make_card(page, 370, 324);
    make_label(right, "Current architecture", 0xFFFFFF);
    make_label(right, "Board bring-up: ESP-IDF native", 0xD0D5DD);
    make_label(right, "Display: RGB LCD through esp_lcd", 0xD0D5DD);
    make_label(right, "Touch: GT911 through esp_lcd_touch", 0xD0D5DD);
    make_label(right, "UI: LVGL 9 through esp_lvgl_port", 0xD0D5DD);
    make_label(right, "App: FreeRTOS tasks + shared state", 0xD0D5DD);
    make_label(right, "Diagnostics: low-rate and non-intrusive", 0xD0D5DD);
}

static void build_controls_page(void)
{
    lv_obj_t *page = create_page(UI_PAGE_CONTROLS);

    lv_obj_t *left = make_card(page, 370, 324);
    make_label(left, "Controls", 0xFFFFFF);

    s_slider_label = make_label(left, "Slider: 50", 0xD0D5DD);
    s_slider = lv_slider_create(left);
    lv_obj_set_width(s_slider, 300);
    lv_slider_set_range(s_slider, 0, 100);
    lv_slider_set_value(s_slider, 50, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_bar = lv_bar_create(left);
    lv_obj_set_width(s_bar, 300);
    lv_bar_set_range(s_bar, 0, 100);
    lv_bar_set_value(s_bar, 50, LV_ANIM_OFF);

    s_switch_label = make_label(left, "Switch: ON", 0xD0D5DD);
    s_switch = lv_switch_create(left);
    lv_obj_add_state(s_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_switch, switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *right = make_card(page, 370, 324);
    make_label(right, "Mode selection", 0xFFFFFF);
    s_mode_label = make_label(right, "Mode: Dashboard", 0xD0D5DD);
    s_dropdown = lv_dropdown_create(right);
    lv_dropdown_set_options(s_dropdown, "Dashboard\nSetup\nTest\nService");
    lv_dropdown_set_selected(s_dropdown, 0);
    lv_obj_set_width(s_dropdown, 270);
    lv_obj_add_event_cb(s_dropdown, dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    make_label(right, "Direct controls update immediately. Only low-rate telemetry uses the UI task.", 0x98A2B3);
}

static void build_diagnostics_page(void)
{
    lv_obj_t *page = create_page(UI_PAGE_DIAGNOSTICS);

    lv_obj_t *left = make_card(page, 370, 324);
    make_label(left, "FreeRTOS / memory", 0xFFFFFF);
    make_metric(left, &s_diag_uptime_label, "Uptime: waiting...");
    make_metric(left, &s_diag_task_label, "Tasks: waiting...");
    make_metric(left, &s_diag_cpu_label, "Display task budget: waiting...");
    make_metric(left, &s_diag_heap_label, "Internal heap: waiting...");
    make_metric(left, &s_diag_psram_label, "PSRAM: waiting...");
    make_metric(left, &s_diag_stack_label, "Stack HWM: waiting...");

    lv_obj_t *right = make_card(page, 370, 324);
    make_label(right, "Rates / UI timing", 0xFFFFFF);
    make_metric(right, &s_diag_logic_label, "Logic task ticks: 0");
    make_metric(right, &s_diag_ui_label, "UI task ticks: 0");
    make_metric(right, &s_diag_diag_label, "Diagnostics ticks: 0");
    make_metric(right, &s_diag_rate_label, "UI update rate: waiting...");
    make_metric(right, &s_diag_timing_label, "UI update time: waiting...");
    make_metric(right, &s_diag_page_label, "Page switch time: waiting...");
    make_label(right, "The old busy-loop CPU probe is removed. These numbers show display/UI overhead without burning CPU to measure CPU.", 0x98A2B3);
}

static void build_logs_page(void)
{
    lv_obj_t *page = create_page(UI_PAGE_LOGS);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(page, 10, LV_PART_MAIN);

    lv_obj_t *card = make_card(page, 756, 324);
    make_label(card, "Event log placeholder", 0xFFFFFF);
    make_label(card, "Screen/touch bring-up complete", 0xD0D5DD);
    make_label(card, "Screens are built once, then hide/shown", 0xD0D5DD);
    make_label(card, "No serial spam from every touch event", 0xD0D5DD);
    make_label(card, "Diagnostics sample once per second", 0xD0D5DD);
    make_label(card, "Next step: replace demo state with real app state", 0xD0D5DD);
}

void ui_demo_create(lv_display_t *display)
{
    (void)display;

    lvgl_port_lock(0);

    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101828), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 10, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    s_root = lv_obj_create(screen);
    lv_obj_set_size(s_root, ROOT_W, ROOT_H);
    lv_obj_center(s_root);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_root, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_root, 8, LV_PART_MAIN);

    lv_obj_t *header = lv_obj_create(s_root);
    lv_obj_set_size(header, ROOT_W, 48);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(header, 0, LV_PART_MAIN);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    s_title_label = make_label(header, "ESP-IDF 6 + LVGL 9 - Home", 0xFFFFFF);
    lv_obj_align(s_title_label, LV_ALIGN_TOP_LEFT, 4, 0);
    lv_obj_t *subtitle = make_label(header, "Persistent pages + low-overhead diagnostics", 0xD0D5DD);
    lv_obj_align(subtitle, LV_ALIGN_TOP_LEFT, 4, 24);

    s_body = lv_obj_create(s_root);
    lv_obj_set_size(s_body, ROOT_W, BODY_H);
    lv_obj_set_style_bg_opa(s_body, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_body, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_body, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_body, LV_OBJ_FLAG_SCROLLABLE);

    build_home_page();
    build_controls_page();
    build_diagnostics_page();
    build_logs_page();

    s_nav = lv_obj_create(s_root);
    lv_obj_set_size(s_nav, ROOT_W, 64);
    lv_obj_set_style_radius(s_nav, 18, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_nav, lv_color_hex(0x1D2939), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_nav, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_nav, lv_color_hex(0x475467), LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_nav, 10, LV_PART_MAIN);
    lv_obj_clear_flag(s_nav, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_nav, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(s_nav, 12, LV_PART_MAIN);

    make_nav_button(s_nav, "Home", UI_PAGE_HOME);
    make_nav_button(s_nav, "Controls", UI_PAGE_CONTROLS);
    make_nav_button(s_nav, "Diagnostics", UI_PAGE_DIAGNOSTICS);
    make_nav_button(s_nav, "Logs", UI_PAGE_LOGS);

    s_pulse = lv_obj_create(s_nav);
    lv_obj_set_size(s_pulse, 26, 26);
    lv_obj_set_style_radius(s_pulse, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_pulse, lv_color_hex(0x344054), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_pulse, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_pulse, LV_OBJ_FLAG_SCROLLABLE);

    s_status_bar_label = make_label(s_nav, "Ready", 0xD0D5DD);
    lv_obj_set_width(s_status_bar_label, 120);
    lv_label_set_long_mode(s_status_bar_label, LV_LABEL_LONG_DOT);

    lv_screen_load(screen);
    select_page(UI_PAGE_HOME);

    lvgl_port_unlock();
}

void ui_demo_update_from_state(const app_state_snapshot_t *snapshot)
{
    if (!snapshot || !s_body) {
        return;
    }

    char line[176];

    lvgl_port_lock(0);

    if (s_home_touch_count_label) {
        snprintf(line, sizeof(line), "Touch clicks: %lu", (unsigned long)snapshot->touch_clicks);
        set_label_text_if_changed(s_home_touch_count_label, line);
    }
    if (s_home_uptime_label) {
        snprintf(line, sizeof(line), "Uptime: %lu s", (unsigned long)(snapshot->uptime_ms / 1000));
        set_label_text_if_changed(s_home_uptime_label, line);
    }

    if (s_current_page == UI_PAGE_DIAGNOSTICS) {
        snprintf(line, sizeof(line), "Uptime: %lu s", (unsigned long)(snapshot->uptime_ms / 1000));
        set_label_text_if_changed(s_diag_uptime_label, line);

        snprintf(line, sizeof(line), "FreeRTOS tasks: %lu", (unsigned long)snapshot->task_count);
        set_label_text_if_changed(s_diag_task_label, line);

        snprintf(line, sizeof(line), "Display UI task budget: %lu%% of 500 ms", (unsigned long)snapshot->display_task_load_percent_est);
        set_label_text_if_changed(s_diag_cpu_label, line);

        snprintf(line, sizeof(line), "Internal heap: %lu KB free, %lu KB minimum",
                 (unsigned long)(snapshot->free_internal_bytes / 1024),
                 (unsigned long)(snapshot->min_free_internal_bytes / 1024));
        set_label_text_if_changed(s_diag_heap_label, line);

        snprintf(line, sizeof(line), "PSRAM: %lu KB free, largest %lu KB",
                 (unsigned long)(snapshot->free_psram_bytes / 1024),
                 (unsigned long)(snapshot->largest_psram_block_bytes / 1024));
        set_label_text_if_changed(s_diag_psram_label, line);

        snprintf(line, sizeof(line), "Stack HWM words L/UI/D: %lu/%lu/%lu",
                 (unsigned long)snapshot->stack_logic_words,
                 (unsigned long)snapshot->stack_ui_words,
                 (unsigned long)snapshot->stack_diag_words);
        set_label_text_if_changed(s_diag_stack_label, line);

        snprintf(line, sizeof(line), "Logic task ticks: %lu", (unsigned long)snapshot->logic_ticks);
        set_label_text_if_changed(s_diag_logic_label, line);

        snprintf(line, sizeof(line), "UI task ticks: %lu", (unsigned long)snapshot->ui_ticks);
        set_label_text_if_changed(s_diag_ui_label, line);

        snprintf(line, sizeof(line), "Diagnostics ticks: %lu", (unsigned long)snapshot->diagnostics_ticks);
        set_label_text_if_changed(s_diag_diag_label, line);

        snprintf(line, sizeof(line), "UI update rate: %lu Hz", (unsigned long)snapshot->ui_updates_per_sec);
        set_label_text_if_changed(s_diag_rate_label, line);

        snprintf(line, sizeof(line), "UI update time: last %lu us, max %lu us",
                 (unsigned long)snapshot->ui_update_last_us,
                 (unsigned long)snapshot->ui_update_max_us);
        set_label_text_if_changed(s_diag_timing_label, line);

        snprintf(line, sizeof(line), "Page switch time: last %lu us, max %lu us",
                 (unsigned long)snapshot->page_switch_last_us,
                 (unsigned long)snapshot->page_switch_max_us);
        set_label_text_if_changed(s_diag_page_label, line);
    }

    if (s_current_page == UI_PAGE_CONTROLS) {
        snprintf(line, sizeof(line), "Slider: %d", snapshot->slider_value);
        set_label_text_if_changed(s_slider_label, line);
        if (s_slider && lv_slider_get_value(s_slider) != snapshot->slider_value) {
            lv_slider_set_value(s_slider, snapshot->slider_value, LV_ANIM_OFF);
        }
        if (s_bar && lv_bar_get_value(s_bar) != snapshot->slider_value) {
            lv_bar_set_value(s_bar, snapshot->slider_value, LV_ANIM_OFF);
        }

        snprintf(line, sizeof(line), "Switch: %s", snapshot->switch_enabled ? "ON" : "OFF");
        set_label_text_if_changed(s_switch_label, line);
        if (s_switch) {
            if (snapshot->switch_enabled) {
                lv_obj_add_state(s_switch, LV_STATE_CHECKED);
            } else {
                lv_obj_remove_state(s_switch, LV_STATE_CHECKED);
            }
        }

        snprintf(line, sizeof(line), "Mode: %s", get_mode_name(snapshot->mode_index));
        set_label_text_if_changed(s_mode_label, line);
        if (s_dropdown && lv_dropdown_get_selected(s_dropdown) != snapshot->mode_index) {
            lv_dropdown_set_selected(s_dropdown, snapshot->mode_index);
        }
    }

    lvgl_port_unlock();
}
