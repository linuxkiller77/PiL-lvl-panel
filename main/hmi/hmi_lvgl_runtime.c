#include "hmi_lvgl_runtime.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hmi/hmi_layout_store.h"
#include "hmi/hmi_tag_db.h"
#include "hmi/hmi_script_engine.h"
#include "board/pilab_board.h"
#include "hmi/hmi_diag_log.h"
#include "hmi/hmi_memory_diag.h"

#define HMI_MAX_WIDGETS 96
#define HMI_MAX_PAGES 12
#define HMI_SCREEN_W 800
#define HMI_SCREEN_H 480

static const char *TAG = "hmi_lvgl";
static volatile bool s_panel_tick_enabled = false;
static volatile uint32_t s_panel_tick_rate_ms = 0;

// v5.21: Prefer PSRAM for PiLab runtime bookkeeping allocations.  This is
// separate from LVGL's own internal allocations, but it prevents the HMI model,
// JSON copies, and runtime wrapper structures from competing with LVGL for the
// scarce internal heap during large Apply-to-RAM operations.
static void *hmi_heap_malloc(size_t size)
{
    void *p = heap_caps_malloc_prefer(size, 2,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!p) ESP_LOGE(TAG, "hmi_heap_malloc failed size=%u internal=%u psram=%u largest_internal=%u largest_psram=%u",
                     (unsigned)size,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return p;
}

static void *hmi_heap_calloc(size_t n, size_t size)
{
    size_t total = n * size;
    void *p = hmi_heap_malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

static char *hmi_heap_strdup(const char *s)
{
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *p = (char *)hmi_heap_malloc(len);
    if (p) memcpy(p, s, len);
    return p;
}

static void hmi_heap_log(const char *where)
{
#if HMI_LOG_MEMORY_DETAIL
    ESP_LOGI(TAG, "heap %s: internal=%u largest_internal=%u psram=%u largest_psram=%u",
             where ? where : "",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
    (void)where;
#endif
}

typedef enum {
    WIDGET_NONE = 0,
    WIDGET_LABEL,
    WIDGET_READOUT,
    WIDGET_LED,
    WIDGET_BUTTON,
    WIDGET_TOGGLE,
    WIDGET_SETPOINT,
    WIDGET_BAR,
    WIDGET_TANK,
} widget_kind_t;

typedef struct {
    widget_kind_t kind;
    char id[40];
    char tag[48];
    char label[64];
    char unit[16];
    char mode[16];
    char event_click[64];
    char event_change[64];
    float min;
    float max;
    float step;
    bool snap_to_step;
    int decimals;
    bool press_bool;
    bool release_bool;
    float press_number;
    float release_number;
    bool press_is_bool;
    bool release_is_bool;
    uint32_t color;
    lv_obj_t *root;
    lv_obj_t *value_label;
    lv_obj_t *state_obj;
    lv_obj_t *bar;
    lv_obj_t *spinbox;
    lv_obj_t *knob;
    bool has_last_number;
    float last_number;
    bool has_last_bool;
    bool last_bool;
    int last_percent;
    float static_number;
    bool static_bool;
    char static_text[96];
    char last_text[96];
    int page_index;
    uint32_t generation;
} runtime_widget_t;

typedef struct {
    char id[40];
    char name[48];
    lv_obj_t *obj;
} runtime_page_t;


typedef struct {
    widget_kind_t kind;
    char id[40];
    char tag[48];
    char label[64];
    char unit[16];
    char mode[16];
    char event_click[64];
    char event_change[64];
    float min;
    float max;
    float step;
    bool snap_to_step;
    int decimals;
    bool press_bool;
    bool release_bool;
    float press_number;
    float release_number;
    bool press_is_bool;
    bool release_is_bool;
    uint32_t color;
    float static_number;
    bool static_bool;
    char static_text[96];
    int x;
    int y;
    int w;
    int h;
} layout_widget_model_t;

typedef struct {
    char id[40];
    char name[48];
    size_t first_widget;
    size_t widget_count;
} layout_page_model_t;

typedef struct {
    uint32_t background;
    char startup_page[48];
    layout_page_model_t pages[HMI_MAX_PAGES];
    size_t page_count;
    layout_widget_model_t widgets[HMI_MAX_WIDGETS];
    size_t widget_count;
} layout_model_t;

typedef struct layout_build_ctx_t {
    layout_model_t *model;
    struct runtime_state_t *rt;
    struct runtime_state_t *old_rt;
    lv_obj_t *placeholder;
    char *json_copy;
    size_t page_index;
    size_t widget_index_in_page;
    int64_t request_us;
    int64_t model_ready_us;
    int64_t queued_us;
    int64_t reload_begin_us;
    int64_t first_widget_us;
    int64_t start_us;
    lv_timer_t *timer;
} layout_build_ctx_t;

typedef struct runtime_state_t {
    lv_obj_t *screen;
    layout_model_t *model;   // v6.0 experimental: keep parsed HMI model in RAM; build only active LVGL page on demand.
    runtime_page_t pages[HMI_MAX_PAGES];
    size_t page_count;
    int active_page;
    runtime_widget_t widgets[HMI_MAX_WIDGETS];
    size_t count;
    uint32_t generation;
    bool ready;
} runtime_state_t;

typedef struct deferred_free_t {
    runtime_state_t *rt;
    uint8_t remaining_cycles;
} deferred_free_t;

typedef struct deferred_obj_delete_t {
    lv_obj_t *obj;
    uint8_t remaining_cycles;
} deferred_obj_delete_t;

typedef struct deferred_page_hide_t {
    runtime_state_t *rt;
    int keep_index;
    uint8_t remaining_cycles;
} deferred_page_hide_t;


typedef enum {
    HMI_RUNTIME_STATE_BOOTING = 0,
    HMI_RUNTIME_STATE_READY,
    HMI_RUNTIME_STATE_LAYOUT_RELOADING,
    HMI_RUNTIME_STATE_PAGE_SWITCHING,
    HMI_RUNTIME_STATE_SCRIPT_COMPILING,
    HMI_RUNTIME_STATE_EXTERNAL_BUSY,
    HMI_RUNTIME_STATE_BLOCKED,
} hmi_runtime_state_t;

static const char *runtime_state_name(hmi_runtime_state_t state)
{
    switch (state) {
    case HMI_RUNTIME_STATE_BOOTING: return "BOOTING";
    case HMI_RUNTIME_STATE_READY: return "READY";
    case HMI_RUNTIME_STATE_LAYOUT_RELOADING: return "LAYOUT_RELOADING";
    case HMI_RUNTIME_STATE_PAGE_SWITCHING: return "PAGE_SWITCHING";
    case HMI_RUNTIME_STATE_SCRIPT_COMPILING: return "SCRIPT_COMPILING";
    case HMI_RUNTIME_STATE_EXTERNAL_BUSY: return "EXTERNAL_BUSY";
    case HMI_RUNTIME_STATE_BLOCKED: return "BLOCKED";
    default: return "UNKNOWN";
    }
}

static hmi_runtime_state_t runtime_state_from_reason(bool busy, const char *reason)
{
    if (!busy) return HMI_RUNTIME_STATE_READY;
    if (!reason) return HMI_RUNTIME_STATE_BLOCKED;
    if (strstr(reason, "ScreenShow") || strstr(reason, "page")) return HMI_RUNTIME_STATE_PAGE_SWITCHING;
    if (strstr(reason, "reload") || strstr(reason, "Reload") || strstr(reason, "layout") || strstr(reason, "build")) return HMI_RUNTIME_STATE_LAYOUT_RELOADING;
    if (strstr(reason, "script compile") || strstr(reason, "AngelScript") || strstr(reason, "blocking")) return HMI_RUNTIME_STATE_SCRIPT_COMPILING;
    if (strstr(reason, "external")) return HMI_RUNTIME_STATE_EXTERNAL_BUSY;
    return HMI_RUNTIME_STATE_BLOCKED;
}

static lv_display_t *s_display;
static runtime_state_t *s_active;
static char *s_last_layout_json;
static char *s_pending_reload_json;
static layout_model_t *s_pending_reload_model;
static bool s_reload_async_scheduled;
static bool s_reload_in_progress;
static uint32_t s_runtime_generation;
static hmi_runtime_state_t s_runtime_state = HMI_RUNTIME_STATE_BOOTING;
static bool s_runtime_input_enabled = true;
static bool s_runtime_tag_updates_enabled = true;
static uint32_t s_external_busy_depth;
static bool s_lvgl_blocking_pause_held;
static bool s_blocking_hmi_dropped;
static bool s_backlight_deferred_until_reload_ready;
static bool s_backlight_deferred_error_mode;
static lv_obj_t *s_blocking_overlay;
static lv_obj_t *s_blocking_title;
static lv_obj_t *s_blocking_message;
static char s_pending_page_name[64];
static bool s_page_async_scheduled;
static SemaphoreHandle_t s_render_mutex;
static int64_t s_pending_reload_request_us;
static int64_t s_pending_reload_model_ready_us;
static int64_t s_pending_reload_queued_us;

static void layout_build_timer_cb(lv_timer_t *timer);
static lv_obj_t *create_page_container(runtime_state_t *rt, int page_index);
static void destroy_active_page_objects(runtime_state_t *rt);
static void build_active_page_now(runtime_state_t *rt, int page_index);

static void ensure_render_mutex(void)
{
    if (!s_render_mutex) {
        s_render_mutex = xSemaphoreCreateMutex();
    }
}


static void runtime_set_busy(bool busy, const char *reason)
{
    hmi_runtime_state_t next_state = runtime_state_from_reason(busy, reason);

    if (!busy && s_external_busy_depth > 0) {
        s_runtime_input_enabled = false;
        s_runtime_tag_updates_enabled = false;
        next_state = HMI_RUNTIME_STATE_EXTERNAL_BUSY;
        if (s_runtime_state != next_state) {
#if HMI_LOG_RUNTIME_STATE
            ESP_LOGI(TAG, "Runtime state: %s -> %s%s%s",
                     runtime_state_name(s_runtime_state), runtime_state_name(next_state),
                     reason ? " - " : "", reason ? reason : "");
#endif
            s_runtime_state = next_state;
        }
#if HMI_LOG_RUNTIME_STATE
        ESP_LOGI(TAG, "Runtime guard: ready deferred by external activity depth=%lu%s%s",
                 (unsigned long)s_external_busy_depth, reason ? " - " : "", reason ? reason : "");
#endif
        return;
    }

    s_runtime_input_enabled = !busy;
    s_runtime_tag_updates_enabled = !busy;

    if (s_runtime_state != next_state) {
#if HMI_LOG_RUNTIME_STATE
        ESP_LOGI(TAG, "Runtime state: %s -> %s%s%s",
                 runtime_state_name(s_runtime_state), runtime_state_name(next_state),
                 reason ? " - " : "", reason ? reason : "");
#endif
        s_runtime_state = next_state;
    } else {
#if HMI_LOG_RUNTIME_STATE
        ESP_LOGI(TAG, "Runtime state: %s%s%s",
                 runtime_state_name(s_runtime_state), reason ? " - " : "", reason ? reason : "");
#endif
    }
}

const char *hmi_lvgl_runtime_state_name(void)
{
    return runtime_state_name(s_runtime_state);
}

static void blocking_status_update_locked(const char *title, const char *message, bool is_error)
{
    runtime_state_t *rt = s_active;
    lv_obj_t *parent = (rt && rt->screen) ? rt->screen : lv_screen_active();
    if (!parent) return;

    if (!s_blocking_overlay || !lv_obj_is_valid(s_blocking_overlay)) {
        s_blocking_overlay = lv_obj_create(parent);
        if (!s_blocking_overlay) return;
        lv_obj_set_size(s_blocking_overlay, HMI_SCREEN_W, HMI_SCREEN_H);
        lv_obj_set_pos(s_blocking_overlay, 0, 0);
        lv_obj_set_style_bg_color(s_blocking_overlay, lv_color_hex(0x020617), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_blocking_overlay, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_blocking_overlay, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(s_blocking_overlay, 0, LV_PART_MAIN);
        lv_obj_clear_flag(s_blocking_overlay, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *card = lv_obj_create(s_blocking_overlay);
        lv_obj_set_size(card, 520, 170);
        lv_obj_center(card);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x111827), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, lv_color_hex(0x334155), LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 2, LV_PART_MAIN);
        lv_obj_set_style_radius(card, 14, LV_PART_MAIN);
        lv_obj_set_style_pad_all(card, 18, LV_PART_MAIN);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        s_blocking_title = lv_label_create(card);
        lv_obj_set_width(s_blocking_title, 480);
        lv_label_set_long_mode(s_blocking_title, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(s_blocking_title, lv_color_hex(0xF8FAFC), LV_PART_MAIN);
        lv_obj_align(s_blocking_title, LV_ALIGN_TOP_MID, 0, 8);

        s_blocking_message = lv_label_create(card);
        lv_obj_set_width(s_blocking_message, 480);
        lv_label_set_long_mode(s_blocking_message, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(s_blocking_message, lv_color_hex(0xCBD5E1), LV_PART_MAIN);
        lv_obj_align(s_blocking_message, LV_ALIGN_TOP_MID, 0, 58);
    }

    lv_obj_set_style_bg_color(s_blocking_overlay, lv_color_hex(is_error ? 0x240B0B : 0x020617), LV_PART_MAIN);
    if (s_blocking_title && lv_obj_is_valid(s_blocking_title)) {
        lv_label_set_text(s_blocking_title, title ? title : "Working...");
        lv_obj_set_style_text_color(s_blocking_title, lv_color_hex(is_error ? 0xFCA5A5 : 0xF8FAFC), LV_PART_MAIN);
    }
    if (s_blocking_message && lv_obj_is_valid(s_blocking_message)) {
        lv_label_set_text(s_blocking_message, message ? message : "Please wait.");
    }
    lv_obj_clear_flag(s_blocking_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_blocking_overlay);
    lv_obj_invalidate(s_blocking_overlay);
}

static void blocking_status_delete_locked(void)
{
    if (s_blocking_overlay && lv_obj_is_valid(s_blocking_overlay)) {
        lv_obj_delete(s_blocking_overlay);
    }
    s_blocking_overlay = NULL;
    s_blocking_title = NULL;
    s_blocking_message = NULL;
}


typedef struct deferred_input_enable_t {
    uint32_t generation;
    uint8_t remaining_cycles;
} deferred_input_enable_t;

static void enable_input_deferred_async(void *user_data)
{
    deferred_input_enable_t *defer = (deferred_input_enable_t *)user_data;
    if (!defer) return;
    if (defer->remaining_cycles > 0) {
        defer->remaining_cycles--;
        lv_async_call(enable_input_deferred_async, defer);
        return;
    }

    ensure_render_mutex();
    if (!s_render_mutex || xSemaphoreTake(s_render_mutex, 0) != pdTRUE) {
        lv_async_call(enable_input_deferred_async, defer);
        return;
    }

    runtime_state_t *rt = s_active;
    if (rt && rt->ready && rt->generation == defer->generation && !s_reload_in_progress) {
        runtime_set_busy(false, "reload settled");

        // v6.14: If a blocking maintenance operation dropped/rebuilt the HMI
        // while the LCD backlight was off, do not re-enable the backlight until
        // the replacement page has been built, LVGL has had a few cycles to
        // settle, and input is being re-enabled.  In v6.13 the backlight was
        // turned on immediately after script compile, before the HMI rebuild,
        // which could expose a partially stable RGB scanout and cause the
        // occasional offset/jitter Tony saw after Compile-to-RAM.
        if (s_backlight_deferred_until_reload_ready) {
            s_backlight_deferred_until_reload_ready = false;
            s_backlight_deferred_error_mode = false;
            esp_err_t bl_err = pilab_board_set_backlight(true);
            if (bl_err != ESP_OK) {
                ESP_LOGW(TAG, "LCD backlight on failed after deferred HMI reload: %s", esp_err_to_name(bl_err));
            } else {
#if HMI_LOG_BACKLIGHT_DETAIL
                ESP_LOGI(TAG, "LCD backlight on after HMI reload settled");
#endif
            }
        }
    } else {
        ESP_LOGW(TAG, "Runtime guard: input enable skipped gen=%u active_gen=%u ready=%d reload=%d",
                 (unsigned)defer->generation,
                 (unsigned)(rt ? rt->generation : 0U),
                 rt ? (int)rt->ready : 0,
                 (int)s_reload_in_progress);
    }
    xSemaphoreGive(s_render_mutex);
    free(defer);
}

static void defer_enable_input(uint32_t generation, uint8_t cycles)
{
    deferred_input_enable_t *defer = (deferred_input_enable_t *)hmi_heap_calloc(1, sizeof(deferred_input_enable_t));
    if (!defer) {
        runtime_state_t *rt = s_active;
        if (rt && rt->generation == generation && rt->ready && !s_reload_in_progress) runtime_set_busy(false, "reload settled fallback");
        return;
    }
    defer->generation = generation;
    defer->remaining_cycles = cycles;
    lv_async_call(enable_input_deferred_async, defer);
}

static bool runtime_widget_event_allowed(const runtime_widget_t *w, const char *source)
{
    runtime_state_t *rt = s_active;
    if (!w) return false;
    if (s_reload_in_progress || !s_runtime_input_enabled) {
        ESP_LOGW(TAG, "Ignoring %s event while runtime busy: widget=%s gen=%u", source ? source : "widget", w->id, (unsigned)w->generation);
        return false;
    }
    if (!rt || !rt->ready || w->generation != rt->generation) {
        ESP_LOGW(TAG, "Ignoring stale %s event: widget=%s widget_gen=%u active_gen=%u ready=%d",
                 source ? source : "widget", w->id, (unsigned)w->generation,
                 (unsigned)(rt ? rt->generation : 0U), rt ? (int)rt->ready : 0);
        return false;
    }
    if (rt->page_count > 1 && w->page_index >= 0 && w->page_index != rt->active_page) {
        ESP_LOGW(TAG, "Ignoring hidden-page %s event: widget=%s page=%d active=%d",
                 source ? source : "widget", w->id, w->page_index, rt->active_page);
        return false;
    }
#if HMI_LOG_EVENTS
    ESP_LOGI(TAG, "%s event accepted: widget=%s page=%d gen=%u handler=%s tag=%s",
             source ? source : "Widget", w->id, w->page_index, (unsigned)w->generation,
             w->event_click[0] ? w->event_click : "", w->tag);
#endif
    return true;
}

static void log_page_visibility(const runtime_state_t *rt, const char *context)
{
#if HMI_LOG_PAGE_VISIBILITY_DETAIL
    if (!rt) return;
    ESP_LOGI(TAG, "Page visibility after %s: active=%d pages=%u gen=%u ready=%d",
             context ? context : "update", rt->active_page, (unsigned)rt->page_count,
             (unsigned)rt->generation, (int)rt->ready);
    for (size_t i = 0; i < rt->page_count; ++i) {
        const runtime_page_t *p = &rt->pages[i];
        bool hidden = p->obj ? lv_obj_has_flag(p->obj, LV_OBJ_FLAG_HIDDEN) : true;
        ESP_LOGI(TAG, "  page[%u] id=%s name=%s hidden=%d obj=%p",
                 (unsigned)i, p->id, p->name, hidden ? 1 : 0, (void *)p->obj);
    }
#else
    (void)rt;
    (void)context;
#endif
}

static uint32_t parse_hex_color(const char *text, uint32_t fallback)
{
    if (!text || text[0] != '#') return fallback;
    unsigned int v = 0;
    if (sscanf(text + 1, "%x", &v) == 1) return v & 0xFFFFFFu;
    return fallback;
}

static const char *json_str(const cJSON *obj, const char *name, const char *fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, name);
    const char *s = cJSON_GetStringValue(item);
    return s ? s : fallback;
}

static const char *json_event_str(const cJSON *wj, const cJSON *props, const char *name)
{
    const cJSON *events = cJSON_GetObjectItemCaseSensitive(wj, "events");
    const char *s = NULL;
    if (cJSON_IsObject(events)) {
        s = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(events, name));
        if (s) return s;
    }
    s = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(props, name));
    if (s) return s;
    if (strcmp(name, "onClick") == 0) {
        s = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(props, "eventClick"));
        if (s) return s;
    }
    if (strcmp(name, "onChange") == 0) {
        s = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(props, "eventChange"));
        if (s) return s;
    }
    return "";
}

static float json_float(const cJSON *obj, const char *name, float fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, name);
    return cJSON_IsNumber(item) ? (float)item->valuedouble : fallback;
}

static bool json_bool(const cJSON *obj, const char *name, bool fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, name);
    if (cJSON_IsBool(item)) return cJSON_IsTrue(item);
    if (cJSON_IsNumber(item)) return fabsf((float)item->valuedouble) > 0.0001f;
    const char *s = cJSON_GetStringValue(item);
    if (!s) return fallback;
    if (strcasecmp(s, "true") == 0 || strcmp(s, "1") == 0 || strcasecmp(s, "yes") == 0 || strcasecmp(s, "on") == 0) return true;
    if (strcasecmp(s, "false") == 0 || strcmp(s, "0") == 0 || strcasecmp(s, "no") == 0 || strcasecmp(s, "off") == 0) return false;
    return fallback;
}

static int json_int(const cJSON *obj, const char *name, int fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, name);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

static uint32_t normalize_panel_tick_rate(uint32_t rate_ms)
{
    switch (rate_ms) {
        case 100:
        case 250:
        case 500:
        case 1000:
            return rate_ms;
        default:
            return 0;
    }
}

static void update_panel_tick_config_from_root(const cJSON *root)
{
    bool enabled = false;
    uint32_t rate_ms = 0;
    const cJSON *tick = cJSON_GetObjectItemCaseSensitive(root, "scriptTick");
    if (cJSON_IsObject(tick)) {
        enabled = json_bool(tick, "enabled", false);
        rate_ms = (uint32_t)json_int(tick, "rateMs", 0);
    } else {
        enabled = json_bool(root, "scriptTickEnabled", false);
        rate_ms = (uint32_t)json_int(root, "scriptTickMs", 0);
    }
    rate_ms = normalize_panel_tick_rate(rate_ms);
    if (!enabled || rate_ms == 0) {
        s_panel_tick_enabled = false;
        s_panel_tick_rate_ms = 0;
        return;
    }
    s_panel_tick_rate_ms = rate_ms;
    s_panel_tick_enabled = true;
}

uint32_t hmi_lvgl_runtime_panel_tick_rate_ms(void)
{
    return s_panel_tick_enabled ? s_panel_tick_rate_ms : 0;
}

static bool json_bool_or_string(const cJSON *obj, const char *name, bool fallback, bool *is_bool, float *as_number)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, name);
    if (is_bool) *is_bool = false;
    if (as_number) *as_number = fallback ? 1.0f : 0.0f;
    if (cJSON_IsBool(item)) {
        if (is_bool) *is_bool = true;
        bool b = cJSON_IsTrue(item);
        if (as_number) *as_number = b ? 1.0f : 0.0f;
        return b;
    }
    if (cJSON_IsNumber(item)) {
        if (as_number) *as_number = (float)item->valuedouble;
        return fabsf((float)item->valuedouble) > 0.0001f;
    }
    const char *s = cJSON_GetStringValue(item);
    if (s) {
        if (strcasecmp(s, "true") == 0) { if (is_bool) *is_bool = true; if (as_number) *as_number = 1.0f; return true; }
        if (strcasecmp(s, "false") == 0) { if (is_bool) *is_bool = true; if (as_number) *as_number = 0.0f; return false; }
        char *end = NULL;
        float f = strtof(s, &end);
        if (end && *end == '\0') { if (as_number) *as_number = f; return fabsf(f) > 0.0001f; }
    }
    return fallback;
}


static widget_kind_t kind_from_type(const char *type);

static void layout_model_free(layout_model_t *model)
{
    free(model);
}

static void parse_widget_model(const cJSON *wj, layout_widget_model_t *m)
{
    if (!wj || !m) return;
    const cJSON *props = cJSON_GetObjectItemCaseSensitive(wj, "props");
    if (!cJSON_IsObject(props)) props = wj;

    const char *type = json_str(wj, "type", "");
    memset(m, 0, sizeof(*m));
    m->kind = kind_from_type(type);
    if (m->kind == WIDGET_NONE) return;

    m->x = json_int(wj, "x", 20);
    m->y = json_int(wj, "y", 20);
    m->w = json_int(wj, "w", 160);
    m->h = json_int(wj, "h", 70);
    if (m->w < 30) m->w = 30;
    if (m->h < 24) m->h = 24;
    if (m->x < -HMI_SCREEN_W) m->x = -HMI_SCREEN_W;
    if (m->y < -HMI_SCREEN_H) m->y = -HMI_SCREEN_H;
    if (m->x > HMI_SCREEN_W * 2) m->x = HMI_SCREEN_W * 2;
    if (m->y > HMI_SCREEN_H * 2) m->y = HMI_SCREEN_H * 2;
    if (m->w > HMI_SCREEN_W * 2) m->w = HMI_SCREEN_W * 2;
    if (m->h > HMI_SCREEN_H * 2) m->h = HMI_SCREEN_H * 2;

    snprintf(m->id, sizeof(m->id), "%s", json_str(wj, "id", "widget"));
    snprintf(m->tag, sizeof(m->tag), "%s", json_str(props, "pin", json_str(props, "tag", "")));
    snprintf(m->label, sizeof(m->label), "%s", json_str(props, "label", json_str(wj, "text", type)));
    snprintf(m->unit, sizeof(m->unit), "%s", json_str(props, "unit", ""));
    snprintf(m->mode, sizeof(m->mode), "%s", json_str(props, "buttonMode", "write"));
    snprintf(m->event_click, sizeof(m->event_click), "%s", json_event_str(wj, props, "onClick"));
    snprintf(m->event_change, sizeof(m->event_change), "%s", json_event_str(wj, props, "onChange"));
    m->min = json_float(props, "min", json_float(props, "setMin", 0.0f));
    m->max = json_float(props, "max", json_float(props, "setMax", 100.0f));
    m->step = json_float(props, "step", 1.0f);
    if (m->step <= 0.0f) m->step = 1.0f;
    m->snap_to_step = json_bool(props, "snapToStep", false);
    m->decimals = json_int(props, "decimals", 1);
    if (m->decimals < 0) m->decimals = 0;
    if (m->decimals > 4) m->decimals = 4;
    m->color = parse_hex_color(json_str(props, "color", "#38bdf8"), 0x38bdf8);
    m->press_bool = json_bool_or_string(props, "pressValue", true, &m->press_is_bool, &m->press_number);
    m->release_bool = json_bool_or_string(props, "releaseValue", false, &m->release_is_bool, &m->release_number);
    m->static_number = json_float(props, "value", json_float(props, "defaultValue", 0.0f));
    m->static_bool = json_bool_or_string(props, "checked", fabsf(m->static_number) > 0.0001f, NULL, NULL);
    snprintf(m->static_text, sizeof(m->static_text), "%s", json_str(props, "value", json_str(props, "text", m->label)));

    // Tag definitions are part of the layout model compile phase, not the LVGL
    // object build phase.  This keeps JSON/tag/schema work out of the LVGL task.
    if (m->tag[0] && !hmi_tag_db_exists(m->tag)) {
        if (m->kind == WIDGET_LED || m->kind == WIDGET_TOGGLE || m->kind == WIDGET_BUTTON) {
            hmi_tag_db_define_bool(m->tag, false, true);
        } else if (m->kind == WIDGET_LABEL) {
            hmi_tag_db_define_string(m->tag, m->label, true);
        } else {
            hmi_tag_db_define_number(m->tag, m->static_number, m->min, m->max, m->unit, true);
        }
    }
}

static esp_err_t layout_model_from_json(const char *json, layout_model_t **out_model)
{
    if (!json || !out_model) return ESP_ERR_INVALID_ARG;
    *out_model = NULL;

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGE(TAG, "Layout JSON parse failed");
        return ESP_ERR_INVALID_ARG;
    }
    update_panel_tick_config_from_root(root);

    layout_model_t *model = (layout_model_t *)hmi_heap_calloc(1, sizeof(layout_model_t));
    if (!model) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    const cJSON *screen = cJSON_GetObjectItemCaseSensitive(root, "screen");
    model->background = parse_hex_color(json_str(screen, "background", "#101828"), 0x101828);
    snprintf(model->startup_page, sizeof(model->startup_page), "%s",
             json_str(root, "startupPage", json_str(root, "bootPage", json_str(root, "activePage", json_str(root, "currentPage", "")))));

    const cJSON *pages = cJSON_GetObjectItemCaseSensitive(root, "pages");
    const cJSON *page = NULL;
    const cJSON *wj = NULL;
    if (cJSON_IsArray(pages) && cJSON_GetArraySize(pages) > 0) {
        cJSON_ArrayForEach(page, pages) {
            if (model->page_count >= HMI_MAX_PAGES) break;
            layout_page_model_t *mp = &model->pages[model->page_count];
            snprintf(mp->id, sizeof(mp->id), "%s", json_str(page, "id", ""));
            snprintf(mp->name, sizeof(mp->name), "%s", json_str(page, "name", json_str(page, "title", mp->id[0] ? mp->id : "Screen")));
            if (!mp->id[0]) snprintf(mp->id, sizeof(mp->id), "screen_%u", (unsigned)(model->page_count + 1));
            mp->first_widget = model->widget_count;

            const cJSON *arr = cJSON_GetObjectItemCaseSensitive(page, "widgets");
            if (!cJSON_IsArray(arr)) arr = cJSON_GetObjectItemCaseSensitive(page, "objects");
            cJSON_ArrayForEach(wj, arr) {
                if (model->widget_count >= HMI_MAX_WIDGETS) break;
                layout_widget_model_t tmp;
                parse_widget_model(wj, &tmp);
                if (tmp.kind == WIDGET_NONE) continue;
                model->widgets[model->widget_count++] = tmp;
                mp->widget_count++;
                if ((model->widget_count % 16U) == 0U) vTaskDelay(1);
            }
            model->page_count++;
        }
    } else {
        layout_page_model_t *mp = &model->pages[0];
        snprintf(mp->id, sizeof(mp->id), "main");
        snprintf(mp->name, sizeof(mp->name), "Main");
        mp->first_widget = 0;
        const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "widgets");
        if (!cJSON_IsArray(arr)) arr = cJSON_GetObjectItemCaseSensitive(root, "objects");
        cJSON_ArrayForEach(wj, arr) {
            if (model->widget_count >= HMI_MAX_WIDGETS) break;
            layout_widget_model_t tmp;
            parse_widget_model(wj, &tmp);
            if (tmp.kind == WIDGET_NONE) continue;
            model->widgets[model->widget_count++] = tmp;
            mp->widget_count++;
            if ((model->widget_count % 16U) == 0U) vTaskDelay(1);
        }
        model->page_count = 1;
    }

    if (model->page_count == 0) {
        snprintf(model->pages[0].id, sizeof(model->pages[0].id), "main");
        snprintf(model->pages[0].name, sizeof(model->pages[0].name), "Main");
        model->page_count = 1;
    }

    cJSON_Delete(root);
    *out_model = model;
#if HMI_LOG_SUMMARY
    ESP_LOGI(TAG, "HMI layout model: pages=%u widgets=%u", (unsigned)model->page_count, (unsigned)model->widget_count);
#endif
    return ESP_OK;
}

static widget_kind_t kind_from_type(const char *type)
{
    if (!type) return WIDGET_NONE;
    if (strcmp(type, "label") == 0) return WIDGET_LABEL;
    if (strcmp(type, "readout") == 0 || strcmp(type, "numeric") == 0) return WIDGET_READOUT;
    if (strcmp(type, "led") == 0 || strcmp(type, "indicator") == 0) return WIDGET_LED;
    if (strcmp(type, "button") == 0) return WIDGET_BUTTON;
    if (strcmp(type, "toggle") == 0) return WIDGET_TOGGLE;
    if (strcmp(type, "setpoint") == 0) return WIDGET_SETPOINT;
    if (strcmp(type, "gauge") == 0 || strcmp(type, "bar") == 0) return WIDGET_BAR;
    if (strcmp(type, "tank") == 0 || strcmp(type, "thermometer") == 0) return WIDGET_TANK;
    return WIDGET_NONE;
}

static uint32_t mix_rgb(uint32_t a, uint32_t b, uint8_t b_pct)
{
    if (b_pct > 100) b_pct = 100;
    uint8_t a_pct = 100 - b_pct;
    uint32_t ar = (a >> 16) & 0xff, ag = (a >> 8) & 0xff, ab = a & 0xff;
    uint32_t br = (b >> 16) & 0xff, bg = (b >> 8) & 0xff, bb = b & 0xff;
    uint32_t r = (ar * a_pct + br * b_pct) / 100;
    uint32_t g = (ag * a_pct + bg * b_pct) / 100;
    uint32_t bl = (ab * a_pct + bb * b_pct) / 100;
    return (r << 16) | (g << 8) | bl;
}

static inline uint32_t lighten_rgb(uint32_t c, uint8_t pct) { return mix_rgb(c, 0xffffff, pct); }
static inline uint32_t darken_rgb(uint32_t c, uint8_t pct) { return mix_rgb(c, 0x000000, pct); }

static void style_shadow(lv_obj_t *obj, uint32_t color, uint8_t opa, int width, int y)
{
    lv_obj_set_style_shadow_width(obj, width, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(obj, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(obj, opa, LV_PART_MAIN);
    lv_obj_set_style_shadow_ofs_y(obj, y, LV_PART_MAIN);
    lv_obj_set_style_shadow_spread(obj, 0, LV_PART_MAIN);
}

static void style_card(lv_obj_t *obj, uint32_t bg_top, uint32_t bg_bottom, uint32_t border)
{
    // v4.7: allow a little more LVGL polish now that script execution is event-only
    // and screen reloads are queued/coalesced.  These are static object styles, not
    // per-refresh animation effects, so they should be much safer than the old
    // rapid rebuild path that caused tearing.
    lv_obj_set_style_radius(obj, 16, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg_top), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(obj, lv_color_hex(bg_bottom), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_hex(border), LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 8, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, lv_color_hex(0xCBD5E1), LV_PART_MAIN);
    style_shadow(obj, 0x000000, LV_OPA_30, 14, 5);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void style_box(lv_obj_t *obj, uint32_t bg, uint32_t border)
{
    style_card(obj, lighten_rgb(bg, 8), darken_rgb(bg, 12), border);
}

static void safe_label_set_text(lv_obj_t *label, const char *text);
static lv_obj_t *add_text(lv_obj_t *parent, const char *text, uint32_t color, int font_size_hint);
static void setpoint_editor_open(runtime_widget_t *w);
static void setpoint_editor_close(void);

static void style_toggle_root(lv_obj_t *obj)
{
    // v5.13: toggles use a simpler root style than the card/button widgets.
    // The recurring 71-widget crash consistently occurred while building the
    // first/second custom toggle.  The last decoded trace stopped in
    // lv_obj_set_style_shadow_width() from style_card()/style_box() while the
    // toggle root was being created.  Toggle widgets were also visually broken,
    // so keep the polished dark panel look but avoid the heavy per-toggle box
    // shadow/local-style path and use LVGL's native switch control inside.
    lv_obj_set_style_radius(obj, 14, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x10182A), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(obj, lv_color_hex(0x0B1220), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x26364E), LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 8, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, lv_color_hex(0xCBD5E1), LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void style_setpoint_spinbox_display(lv_obj_t *spinbox, uint32_t color)
{
    if (!spinbox) return;

    lv_obj_set_style_radius(spinbox, 12, LV_PART_MAIN);
    lv_obj_set_style_border_width(spinbox, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(spinbox, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_color(spinbox, lv_color_hex(0x07101F), LV_PART_MAIN);
    lv_obj_set_style_text_color(spinbox, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_align(spinbox, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // v6.45.30: setpoint widgets are display fields on the LCD; tapping them
    // opens PiLab's numeric keypad.  LVGL spinbox shows a cursor/selected-digit
    // rectangle by default, which looked like a permanent focus box and covered
    // the last digit.  Hide focus/cursor styling and leave the keypad as the
    // actual edit affordance.
    lv_obj_set_style_outline_width(spinbox, 0, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_outline_opa(spinbox, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(spinbox, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_border_width(spinbox, 0, LV_PART_CURSOR);
    lv_obj_set_style_text_opa(spinbox, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_clear_state(spinbox, LV_STATE_FOCUSED | LV_STATE_EDITED);
    lv_obj_clear_flag(spinbox, LV_OBJ_FLAG_SCROLLABLE);
}

static void static_toggle_set(runtime_widget_t *w, bool on)
{
    if (!w) return;

    if (w->value_label) {
        safe_label_set_text(w->value_label, on ? "ON" : "OFF");
    }

    if (w->root) {
        // v6.45.30: keep the surrounding toggle card visually neutral.
        // The ON color belongs to the native switch indicator/knob only;
        // coloring the whole card made toggles look selected instead of
        // simply switched on.
        lv_obj_set_style_bg_color(w->root, lv_color_hex(0x10182A), LV_PART_MAIN);
        lv_obj_set_style_bg_grad_color(w->root, lv_color_hex(0x0B1220), LV_PART_MAIN);
        lv_obj_set_style_border_color(w->root, lv_color_hex(0x26364E), LV_PART_MAIN);
    }

    // v6.18: the toggle body is now the native LVGL switch again.  The earlier
    // custom switch was useful as a diagnostic workaround, but the later v6.x
    // memory/lifecycle fixes made it reasonable to return to lv_switch.
    if (w->state_obj) {
        if (on) lv_obj_add_state(w->state_obj, LV_STATE_CHECKED);
        else lv_obj_clear_state(w->state_obj, LV_STATE_CHECKED);
    }
}

static void create_pilab_toggle_visual(runtime_widget_t *w, int ww, int hh)
{
    if (!w || !w->root) return;

    // Treat the entire toggle card as the touch target, not only the
    // small native lv_switch.  This makes toggles feel like normal
    // PiLab buttons while still preserving LVGL's native switch visual.
    lv_obj_add_flag(w->root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(w->root, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *name = add_text(w->root, w->label, 0x9AA9BF, 12);
    lv_obj_set_width(name, ww - 16);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 2);

    int switch_w = ww - 56;
    if (switch_w < 64) switch_w = ww - 24;
    if (switch_w < 48) switch_w = 48;
    if (switch_w > 96) switch_w = 96;

    int switch_h = 34;
    if (hh < 62) switch_h = 28;
    if (switch_h > hh - 28) switch_h = hh - 28;
    if (switch_h < 22) switch_h = 22;

    w->state_obj = lv_switch_create(w->root);
    lv_obj_set_size(w->state_obj, switch_w, switch_h);
    lv_obj_align(w->state_obj, LV_ALIGN_CENTER, 0, 6);
    lv_obj_add_flag(w->state_obj, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Style the native LVGL switch so it is visually close to the browser
    // designer while still using LVGL's own switch object and checked state.
    lv_obj_set_style_radius(w->state_obj, switch_h / 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(w->state_obj, lv_color_hex(0x07101F), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(w->state_obj, lv_color_hex(0x111B2D), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(w->state_obj, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(w->state_obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(w->state_obj, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(w->state_obj, lv_color_hex(0x26364E), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(w->state_obj, 8, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(w->state_obj, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(w->state_obj, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_shadow_ofs_y(w->state_obj, 2, LV_PART_MAIN);

    lv_obj_set_style_radius(w->state_obj, switch_h / 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(w->state_obj, lv_color_hex(mix_rgb(w->color, 0x07101F, 48)), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_grad_color(w->state_obj, lv_color_hex(mix_rgb(w->color, 0x020617, 35)), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_grad_dir(w->state_obj, LV_GRAD_DIR_VER, LV_PART_INDICATOR | LV_STATE_CHECKED);

    lv_obj_set_style_radius(w->state_obj, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_bg_color(w->state_obj, lv_color_hex(0x94A3B8), LV_PART_KNOB);
    lv_obj_set_style_bg_color(w->state_obj, lv_color_hex(lighten_rgb(w->color, 42)), LV_PART_KNOB | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(w->state_obj, 1, LV_PART_KNOB);
    lv_obj_set_style_border_color(w->state_obj, lv_color_hex(0xCBD5E1), LV_PART_KNOB);
    lv_obj_set_style_shadow_width(w->state_obj, 5, LV_PART_KNOB);
    lv_obj_set_style_shadow_opa(w->state_obj, LV_OPA_30, LV_PART_KNOB);
    lv_obj_set_style_shadow_color(w->state_obj, lv_color_hex(0x000000), LV_PART_KNOB);

    // Hidden state text preserves the old diagnostic/update path without
    // drawing ON/OFF over the native switch.
    w->value_label = add_text(w->root, "OFF", 0xFFFFFF, 1);
    lv_obj_add_flag(w->value_label, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_flag(w->root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(w->root, LV_OBJ_FLAG_EVENT_BUBBLE);
    static_toggle_set(w, w->tag[0] ? hmi_tag_db_get_bool(w->tag, w->static_bool) : w->static_bool);
}

static void style_plain_label_root(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void safe_label_set_text(lv_obj_t *label, const char *text)
{
    if (!label) return;
    char safe_text[192];
    snprintf(safe_text, sizeof(safe_text), "%s", text ? text : "");
    lv_label_set_text(label, safe_text);
}

static lv_obj_t *add_text(lv_obj_t *parent, const char *text, uint32_t color, int font_size_hint)
{
    (void)font_size_hint;
    (void)color;

    // v5.11: keep dynamic label creation conservative.  The last
    // watchdog logs stopped inside LVGL's local-style path while add_text() was
    // setting per-label text color during a large staged build.  That strongly
    // suggests local style churn/corruption/fragmentation rather than an event
    // or script issue.  Let labels inherit text color from their parent/screen
    // during construction instead of creating a local text-color style on every
    // label.  This preserves the widget structure and avoids downgrading the
    // card/button look; it only removes one expensive per-label style write.
    char safe_text[96];
    snprintf(safe_text, sizeof(safe_text), "%s", text ? text : "");

    lv_obj_t *label = lv_label_create(parent);
    safe_label_set_text(label, safe_text);
    return label;
}

static float widget_value(const runtime_widget_t *w)
{
    if (!w) return 0.0f;
    if (!w->tag[0]) return w->static_number;
    return hmi_tag_db_get_number(w->tag, w->static_number);
}

static bool widget_bool_value(const runtime_widget_t *w)
{
    if (!w) return false;
    if (!w->tag[0]) return w->static_bool;
    return hmi_tag_db_get_bool(w->tag, w->static_bool);
}

static int percent_value(const runtime_widget_t *w)
{
    float min = w->min;
    float max = w->max;
    if (fabsf(max - min) < 0.0001f) return 0;
    float pct = ((widget_value(w) - min) * 100.0f) / (max - min);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return (int)(pct + 0.5f);
}


typedef struct {
    lv_obj_t *overlay;
    lv_obj_t *panel;
    lv_obj_t *textarea;
    lv_obj_t *keyboard;
    runtime_widget_t *widget;
} setpoint_editor_state_t;

static setpoint_editor_state_t s_setpoint_editor;

static void runtime_widget_apply_value(runtime_widget_t *w, bool force);

static void setpoint_editor_close(void)
{
    if (s_setpoint_editor.overlay) {
        lv_obj_delete(s_setpoint_editor.overlay);
    }
    memset(&s_setpoint_editor, 0, sizeof(s_setpoint_editor));
}

static void setpoint_editor_commit(void)
{
    runtime_widget_t *w = s_setpoint_editor.widget;
    if (!w || !w->tag[0] || !s_setpoint_editor.textarea) {
        setpoint_editor_close();
        return;
    }

    const char *txt = lv_textarea_get_text(s_setpoint_editor.textarea);
    char *end = NULL;
    float value = strtof(txt ? txt : "", &end);
    if (end == txt) {
        setpoint_editor_close();
        return;
    }

    if (value < w->min) value = w->min;
    if (value > w->max) value = w->max;

    float entered_value = value;

    // Setpoint entry should not silently invent a different number.
    // The step value controls spinner/button increments, but typed keypad values
    // are only rounded when a widget explicitly opts in with "snapToStep": true.
    if (w->snap_to_step && w->step > 0.0f) {
        float steps = roundf((value - w->min) / w->step);
        value = w->min + steps * w->step;
        if (value < w->min) value = w->min;
        if (value > w->max) value = w->max;
    }

    if (runtime_widget_event_allowed(w, "Setpoint")) {
        hmi_tag_db_set_number(w->tag, value);
        if (w->spinbox) {
            lv_spinbox_set_value(w->spinbox, (int32_t)lroundf(value));
        }

        // Force the runtime cache to observe this committed value immediately.
        // Without this, the next periodic tag update can look like the setpoint
        // reverted, especially when the commit came from the keyboard/textarea
        // event path.
        w->has_last_number = false;
        runtime_widget_apply_value(w, true);

        ESP_LOGI(TAG, "Setpoint committed: widget=%s tag=%s entered=%.3f value=%.3f snap=%d step=%.3f handler=%s",
                 w->id, w->tag, entered_value, value, w->snap_to_step ? 1 : 0, w->step, w->event_change);

        if (w->event_change[0]) hmi_script_engine_post_call(w->event_change);
    }

    setpoint_editor_close();
}

static void setpoint_value_entry_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    // LVGL's keyboard can emit READY/CANCEL either from the keyboard object
    // itself or forward the event through the attached textarea depending on
    // LVGL version/configuration.  Listen on both objects and route them through
    // one handler so the numeric setpoint is committed reliably.
    if (code == LV_EVENT_READY) {
        setpoint_editor_commit();
    } else if (code == LV_EVENT_CANCEL) {
        setpoint_editor_close();
    }
}

static void setpoint_overlay_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (lv_event_get_target(e) == s_setpoint_editor.overlay) {
        setpoint_editor_close();
    }
}

static void setpoint_editor_open(runtime_widget_t *w)
{
    if (!w || !w->tag[0]) return;
    if (!runtime_widget_event_allowed(w, "Setpoint")) return;

    // onClick means the setpoint editor was opened. onChange is fired later
    // when the user presses OK and the numeric value is committed.
    if (w->event_click[0]) hmi_script_engine_post_call(w->event_click);

    setpoint_editor_close();
    s_setpoint_editor.widget = w;

    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    s_setpoint_editor.overlay = overlay;
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x020617), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(overlay, setpoint_overlay_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *panel = lv_obj_create(overlay);
    s_setpoint_editor.panel = panel;
    lv_obj_set_size(panel, 430, 330);
    lv_obj_center(panel);
    lv_obj_set_style_radius(panel, 18, LV_PART_MAIN);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x10182A), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(panel, lv_color_hex(0x0B1220), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(panel, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x38BDF8), LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 14, LV_PART_MAIN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    style_shadow(panel, 0x000000, LV_OPA_50, 18, 6);

    char title[128];
    snprintf(title, sizeof(title), "Edit %s", w->label[0] ? w->label : "Setpoint");
    lv_obj_t *title_label = add_text(panel, title, 0xFFFFFF, 18);
    lv_obj_set_width(title_label, 390);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(title_label, 1, LV_PART_MAIN);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 0);

    char subtitle[128];
    if (w->tag[0]) {
        snprintf(subtitle, sizeof(subtitle), "Tag: %s", w->tag);
    } else {
        snprintf(subtitle, sizeof(subtitle), "Numeric setpoint");
    }
    lv_obj_t *subtitle_label = add_text(panel, subtitle, 0x94A3B8, 12);
    lv_obj_set_width(subtitle_label, 390);
    lv_obj_set_style_text_align(subtitle_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(subtitle_label, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_align(subtitle_label, LV_ALIGN_TOP_MID, 0, 24);

    lv_obj_t *ta = lv_textarea_create(panel);
    s_setpoint_editor.textarea = ta;
    lv_obj_set_size(ta, 390, 52);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 52);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, 16);
    lv_textarea_set_accepted_chars(ta, "0123456789.-");
    lv_obj_set_style_radius(ta, 12, LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(ta, lv_color_hex(w->color), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x07101F), LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, lv_color_hex(0xFFFFFF), LV_PART_CURSOR);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0xFFFFFF), LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_CURSOR);
    lv_obj_set_style_width(ta, 3, LV_PART_CURSOR);
    lv_obj_set_style_pad_all(ta, 10, LV_PART_MAIN);

    float value = widget_value(w);
    char value_text[32];
    int decimals = w->decimals;
    if (decimals < 0) decimals = 0;
    if (decimals > 4) decimals = 4;
    snprintf(value_text, sizeof(value_text), "%.*f", decimals, value);
    lv_textarea_set_text(ta, value_text);

    lv_obj_t *kb = lv_keyboard_create(panel);
    s_setpoint_editor.keyboard = kb;
    lv_obj_set_size(kb, 390, 190);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_set_style_radius(kb, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x0B1220), LV_PART_MAIN);
    lv_obj_set_style_border_width(kb, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(ta, setpoint_value_entry_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(kb, setpoint_value_entry_event_cb, LV_EVENT_ALL, NULL);
}

// v6.3: Apply the current TagDB value directly to a widget.
//
// The on-demand/off-screen page model creates a fresh LVGL object tree each
// time ScreenShow() changes pages.  Earlier builds initialized tagged readouts,
// bars, tanks, and LEDs with safe placeholders and relied on the normal
// periodic hmi_lvgl_runtime_update() tick to populate them.  That made page
// changes feel like: widgets first, tag values shortly after.
//
// This helper is used both by the live update loop and immediately after each
// widget is created, so a newly built hidden/staged page already contains the
// latest tag values before it is shown.
static void runtime_widget_apply_value(runtime_widget_t *w, bool force)
{
    if (!w || !w->tag[0]) return;

    char text[96];

    if (w->kind == WIDGET_LABEL && w->value_label) {
        const char *s = hmi_tag_db_get_string(w->tag, w->label);
        char safe[sizeof(w->last_text)];
        snprintf(safe, sizeof(safe), "%s", s ? s : "");
        if (force || strcmp(w->last_text, safe) != 0) {
            snprintf(w->last_text, sizeof(w->last_text), "%s", safe);
            safe_label_set_text(w->value_label, safe);
        }
        return;
    }

    if (w->kind == WIDGET_LED || w->kind == WIDGET_BUTTON || w->kind == WIDGET_TOGGLE) {
        bool on = widget_bool_value(w);
        if (!force && w->has_last_bool && w->last_bool == on) {
            return;
        }
        w->has_last_bool = true;
        w->last_bool = on;

        if (w->kind == WIDGET_LED && w->state_obj) {
            lv_obj_set_style_bg_color(w->state_obj, lv_color_hex(on ? lighten_rgb(w->color, 8) : 0x0f172a), LV_PART_MAIN);
            lv_obj_set_style_bg_grad_color(w->state_obj, lv_color_hex(on ? darken_rgb(w->color, 18) : 0x0b1220), LV_PART_MAIN);
            lv_obj_set_style_bg_grad_dir(w->state_obj, LV_GRAD_DIR_VER, LV_PART_MAIN);
            lv_obj_set_style_border_color(w->state_obj, lv_color_hex(on ? lighten_rgb(w->color, 35) : 0x334155), LV_PART_MAIN);
            style_shadow(w->state_obj, on ? w->color : 0x000000, on ? LV_OPA_50 : LV_OPA_20, on ? 16 : 4, 0);
        } else if (w->kind == WIDGET_TOGGLE && w->state_obj) {
            static_toggle_set(w, on);
        }
        return;
    }

    float v = widget_value(w);
    if (!force && w->has_last_number && fabsf(w->last_number - v) < 0.0005f) {
        return;
    }
    w->has_last_number = true;
    w->last_number = v;

    if (w->kind == WIDGET_READOUT && w->value_label) {
        snprintf(text, sizeof(text), "%.*f%s", w->decimals, v, w->unit);
        if (force || strcmp(w->last_text, text) != 0) {
            snprintf(w->last_text, sizeof(w->last_text), "%s", text);
            safe_label_set_text(w->value_label, text);
        }
    } else if (w->kind == WIDGET_SETPOINT && w->spinbox) {
        int32_t current = lv_spinbox_get_value(w->spinbox);
        int32_t next = (int32_t)(v + 0.5f);
        if (force || current != next) lv_spinbox_set_value(w->spinbox, next);
        lv_obj_clear_state(w->spinbox, LV_STATE_FOCUSED | LV_STATE_EDITED);
    } else if ((w->kind == WIDGET_BAR || w->kind == WIDGET_TANK) && w->bar) {
        int pct = percent_value(w);
        if (force || pct != w->last_percent) {
            w->last_percent = pct;
            lv_bar_set_value(w->bar, pct, LV_ANIM_OFF);
        }
        if (w->value_label) {
            snprintf(text, sizeof(text), "%.*f%s", w->decimals, v, w->unit);
            if (force || strcmp(w->last_text, text) != 0) {
                snprintf(w->last_text, sizeof(w->last_text), "%s", text);
                safe_label_set_text(w->value_label, text);
            }
        }
    }
}

static void button_event_cb(lv_event_t *e)
{
    runtime_widget_t *w = (runtime_widget_t *)lv_event_get_user_data(e);
    if (!w) return;
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (!runtime_widget_event_allowed(w, "Button")) return;

    // Script events are first-class. A button with an onClick handler does not
    // need a tag binding; it can be a pure script/event button. The previous
    // guard returned early when tag was empty, so custom script-only buttons
    // looked correct in the designer but never called AngelScript on the LCD.
    if (w->event_click[0]) {
        hmi_script_engine_post_call(w->event_click);
        return;
    }

    if (!w->tag[0]) return;

    bool current = hmi_tag_db_get_bool(w->tag, false);
    if (strcmp(w->mode, "toggle") == 0) {
        hmi_tag_db_set_bool(w->tag, !current);
    } else if (w->press_is_bool) {
        hmi_tag_db_set_bool(w->tag, w->press_bool);
    } else {
        hmi_tag_db_set_number(w->tag, w->press_number);
    }
}

static void toggle_event_cb(lv_event_t *e)
{
    runtime_widget_t *w = (runtime_widget_t *)lv_event_get_user_data(e);
    if (!w) return;
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        if (!runtime_widget_event_allowed(w, "Toggle")) return;
        if (w->event_click[0]) {
            hmi_script_engine_post_call(w->event_click);
            return;
        }
        if (!w->tag[0]) return;
        bool next = !hmi_tag_db_get_bool(w->tag, false);
        hmi_tag_db_set_bool(w->tag, next);
        if (w->event_change[0]) hmi_script_engine_post_call(w->event_change);
    }
}

static void spinbox_event_cb(lv_event_t *e)
{
    runtime_widget_t *w = (runtime_widget_t *)lv_event_get_user_data(e);
    if (!w || !w->tag[0]) return;
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        setpoint_editor_open(w);
    }
}

static runtime_widget_t *find_widget_from_target(runtime_state_t *rt, lv_obj_t *target)
{
    if (!rt || !target) return NULL;

    // Walk up a small part of the LVGL parent chain so clicks on labels/knobs
    // still resolve to their owning PiLab widget root.  This lets us use a
    // single page-level event callback instead of installing one LVGL callback
    // on every dynamic widget.
    for (lv_obj_t *obj = target; obj; obj = lv_obj_get_parent(obj)) {
        for (size_t i = 0; i < rt->count; ++i) {
            runtime_widget_t *w = &rt->widgets[i];
            if (obj == w->root || obj == w->spinbox || obj == w->state_obj || obj == w->knob || obj == w->bar) {
                return w;
            }
        }
        if (obj == rt->screen) break;
    }
    return NULL;
}

static void dispatch_button_widget(runtime_widget_t *w)
{
    if (!w) return;
    if (!runtime_widget_event_allowed(w, "Button")) return;
    if (w->event_click[0]) {
        hmi_script_engine_post_call(w->event_click);
        return;
    }
    if (!w->tag[0]) return;
    bool current = hmi_tag_db_get_bool(w->tag, false);
    if (strcmp(w->mode, "toggle") == 0) {
        hmi_tag_db_set_bool(w->tag, !current);
    } else if (w->press_is_bool) {
        hmi_tag_db_set_bool(w->tag, w->press_bool);
    } else {
        hmi_tag_db_set_number(w->tag, w->press_number);
    }
}

static void dispatch_toggle_widget(runtime_widget_t *w)
{
    if (!w) return;
    if (!runtime_widget_event_allowed(w, "Toggle")) return;

    // Native lv_switch changes its own checked state before VALUE_CHANGED
    // bubbles to the page router, so use the LVGL checked state as the source
    // of truth instead of blindly inverting the tag.  Card/label clicks set
    // the checked state first and then call this same function.
    bool next = w->state_obj ? lv_obj_has_state(w->state_obj, LV_STATE_CHECKED)
                             : !hmi_tag_db_get_bool(w->tag, false);

    // Tag update must always happen before script callbacks.  Earlier builds
    // returned immediately when onClick was present, which meant toggle tags
    // could fail to update and onChange could be skipped.
    if (w->tag[0]) hmi_tag_db_set_bool(w->tag, next);

    // For toggles, onChange is the primary event.  Keep onClick as a fallback
    // for older layouts that only configured a click handler.
    if (w->event_change[0]) hmi_script_engine_post_call(w->event_change);
    else if (w->event_click[0]) hmi_script_engine_post_call(w->event_click);
}

static void dispatch_setpoint_widget(runtime_widget_t *w)
{
    if (!w || !w->tag[0] || !w->spinbox) return;
    if (!runtime_widget_event_allowed(w, "Setpoint")) return;
    hmi_tag_db_set_number(w->tag, (float)lv_spinbox_get_value(w->spinbox));
    if (w->event_change[0]) hmi_script_engine_post_call(w->event_change);
}

static void page_event_router_cb(lv_event_t *e)
{
    runtime_state_t *rt = (runtime_state_t *)lv_event_get_user_data(e);
    if (!rt) return;

    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_VALUE_CHANGED) return;

    lv_obj_t *target = lv_event_get_target(e);
    runtime_widget_t *w = find_widget_from_target(rt, target);
    if (!w) return;

    if (code == LV_EVENT_CLICKED) {
        if (w->kind == WIDGET_BUTTON) {
            dispatch_button_widget(w);
        } else if (w->kind == WIDGET_SETPOINT) {
            setpoint_editor_open(w);
        } else if (w->kind == WIDGET_TOGGLE) {
            // The inner LVGL switch will emit VALUE_CHANGED by itself.
            // Ignore its CLICKED event here to avoid double toggles/callbacks.
            if (target != w->state_obj) {
                bool next = w->state_obj ? !lv_obj_has_state(w->state_obj, LV_STATE_CHECKED)
                                         : !hmi_tag_db_get_bool(w->tag, false);
                if (w->state_obj) {
                    if (next) lv_obj_add_state(w->state_obj, LV_STATE_CHECKED);
                    else lv_obj_clear_state(w->state_obj, LV_STATE_CHECKED);
                }
                dispatch_toggle_widget(w);
            }
        }
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        if (w->kind == WIDGET_TOGGLE) dispatch_toggle_widget(w);
    }
}


static void create_widget_from_model(runtime_state_t *rt, lv_obj_t *parent, const layout_widget_model_t *m, int page_index)
{
    if (!rt || !m || rt->count >= HMI_MAX_WIDGETS || m->kind == WIDGET_NONE) return;

    int x = m->x;
    int y = m->y;
    int ww = m->w;
    int hh = m->h;
    if (ww < 30) ww = 30;
    if (hh < 24) hh = 24;

    runtime_widget_t *w = &rt->widgets[rt->count++];
    memset(w, 0, sizeof(*w));
    w->kind = m->kind;
    w->page_index = page_index;
    w->generation = rt->generation;
    w->last_percent = -1;
    snprintf(w->id, sizeof(w->id), "%s", m->id[0] ? m->id : "widget");
    snprintf(w->tag, sizeof(w->tag), "%s", m->tag);
    snprintf(w->label, sizeof(w->label), "%s", m->label);
    snprintf(w->unit, sizeof(w->unit), "%s", m->unit);
    snprintf(w->mode, sizeof(w->mode), "%s", m->mode[0] ? m->mode : "write");
    snprintf(w->event_click, sizeof(w->event_click), "%s", m->event_click);
    snprintf(w->event_change, sizeof(w->event_change), "%s", m->event_change);
    w->min = m->min;
    w->max = m->max;
    w->step = m->step > 0.0f ? m->step : 1.0f;
    w->snap_to_step = m->snap_to_step;
    w->decimals = m->decimals;
    w->color = m->color ? m->color : 0x38bdf8;
    w->press_bool = m->press_bool;
    w->release_bool = m->release_bool;
    w->press_number = m->press_number;
    w->release_number = m->release_number;
    w->press_is_bool = m->press_is_bool;
    w->release_is_bool = m->release_is_bool;
    w->static_number = m->static_number;
    w->static_bool = m->static_bool;
    snprintf(w->static_text, sizeof(w->static_text), "%s", m->static_text[0] ? m->static_text : m->label);

    w->root = lv_obj_create(parent ? parent : rt->screen);
    if (!w->root) return;
    lv_obj_set_pos(w->root, x, y);
    lv_obj_set_size(w->root, ww, hh);
    if (m->kind == WIDGET_TOGGLE) {
        style_toggle_root(w->root);
    } else {
        style_box(w->root, 0x1D2939, 0x344054);
    }

    if (m->kind == WIDGET_LABEL) {
        style_plain_label_root(w->root);
        lv_obj_set_style_text_color(w->root, lv_color_hex(w->color), LV_PART_MAIN);
        lv_obj_t *label = add_text(w->root, w->tag[0] ? hmi_tag_db_get_string(w->tag, w->label) : w->label, w->color, 24);
        lv_obj_set_width(label, ww);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_center(label);
        w->value_label = label;
    } else if (m->kind == WIDGET_READOUT) {
        lv_obj_set_style_bg_color(w->root, lv_color_hex(0x0B1220), LV_PART_MAIN);
        lv_obj_set_style_border_color(w->root, lv_color_hex(0x26364E), LV_PART_MAIN);
        lv_obj_set_style_radius(w->root, 14, LV_PART_MAIN);
        lv_obj_t *name = add_text(w->root, w->label, 0x9AA9BF, 12);
        lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 2);
        w->value_label = add_text(w->root, "---", w->color, 28);
        lv_obj_set_width(w->value_label, ww - 16);
        lv_obj_align(w->value_label, LV_ALIGN_CENTER, 0, 10);
        lv_obj_set_style_text_align(w->value_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        // v6.45.33: ReadOut values should honor the widget color from the web designer.
        // add_text() intentionally avoids setting color globally during creation, so apply
        // the value color only to this final value label.
        lv_obj_set_style_text_color(w->value_label, lv_color_hex(w->color), LV_PART_MAIN);
    } else if (m->kind == WIDGET_LED) {
        lv_obj_set_style_bg_color(w->root, lv_color_hex(0x10182A), LV_PART_MAIN);
        lv_obj_set_style_border_color(w->root, lv_color_hex(0x26364E), LV_PART_MAIN);
        lv_obj_set_style_radius(w->root, 14, LV_PART_MAIN);
        w->state_obj = lv_obj_create(w->root);
        int d = hh > ww ? ww / 2 : hh / 2;
        if (d < 22) d = 22;
        if (d > 48) d = 48;
        lv_obj_set_size(w->state_obj, d, d);
        lv_obj_set_style_radius(w->state_obj, d / 2, LV_PART_MAIN);
        lv_obj_set_style_bg_color(w->state_obj, lv_color_hex(0x0B1220), LV_PART_MAIN);
        lv_obj_set_style_border_width(w->state_obj, 3, LV_PART_MAIN);
        lv_obj_set_style_border_color(w->state_obj, lv_color_hex(0x334155), LV_PART_MAIN);
        lv_obj_clear_flag(w->state_obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(w->state_obj, LV_ALIGN_TOP_MID, 0, 4);
        lv_obj_t *name = add_text(w->root, w->label, 0xC1CCDC, 12);
        lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -2);
    } else if (m->kind == WIDGET_BUTTON) {
        lv_obj_set_style_bg_color(w->root, lv_color_hex(lighten_rgb(w->color, 10)), LV_PART_MAIN);
        lv_obj_set_style_bg_grad_color(w->root, lv_color_hex(darken_rgb(w->color, 18)), LV_PART_MAIN);
        lv_obj_set_style_bg_grad_dir(w->root, LV_GRAD_DIR_VER, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(w->root, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(w->root, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(w->root, lv_color_hex(lighten_rgb(w->color, 45)), LV_PART_MAIN);
        lv_obj_set_style_radius(w->root, 15, LV_PART_MAIN);
        style_shadow(w->root, darken_rgb(w->color, 45), LV_OPA_40, 16, 5);
        lv_obj_set_style_text_color(w->root, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_add_flag(w->root, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(w->root, LV_OBJ_FLAG_EVENT_BUBBLE);
        w->state_obj = w->root;
        lv_obj_t *label = add_text(w->root, w->label, 0xFFFFFF, 16);
        lv_obj_set_width(label, ww - 16);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_center(label);
        w->value_label = label;
    } else if (m->kind == WIDGET_TOGGLE) {
        // v6.18: use the native LVGL switch again.
        create_pilab_toggle_visual(w, ww, hh);
    } else if (m->kind == WIDGET_SETPOINT) {
        lv_obj_set_style_bg_color(w->root, lv_color_hex(0x10182A), LV_PART_MAIN);
        lv_obj_set_style_border_color(w->root, lv_color_hex(0x26364E), LV_PART_MAIN);
        lv_obj_set_style_radius(w->root, 14, LV_PART_MAIN);
        lv_obj_t *name = add_text(w->root, w->label, 0x9AA9BF, 12);
        lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 0);
        w->spinbox = lv_spinbox_create(w->root);
        lv_spinbox_set_range(w->spinbox, (int32_t)w->min, (int32_t)w->max);
        lv_spinbox_set_digit_format(w->spinbox, 5, 0);
        lv_spinbox_set_step(w->spinbox, (uint32_t)w->step);
        lv_obj_set_width(w->spinbox, ww - 26);
        style_setpoint_spinbox_display(w->spinbox, w->color);
        lv_obj_align(w->spinbox, LV_ALIGN_CENTER, 0, 12);
        lv_obj_add_flag(w->root, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(w->root, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_flag(w->spinbox, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(w->spinbox, LV_OBJ_FLAG_EVENT_BUBBLE);
        w->value_label = w->spinbox;
    } else if (m->kind == WIDGET_BAR || m->kind == WIDGET_TANK) {
        lv_obj_set_style_bg_color(w->root, lv_color_hex(0x10182A), LV_PART_MAIN);
        lv_obj_set_style_border_color(w->root, lv_color_hex(0x26364E), LV_PART_MAIN);
        lv_obj_set_style_radius(w->root, 14, LV_PART_MAIN);
        lv_obj_t *name = add_text(w->root, w->label, 0x9AA9BF, 12);
        lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 0);
        w->bar = lv_bar_create(w->root);
        lv_bar_set_range(w->bar, 0, 100);
        lv_obj_set_style_radius(w->bar, 12, LV_PART_MAIN);
        lv_obj_set_style_radius(w->bar, 12, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(w->bar, lv_color_hex(0x07101F), LV_PART_MAIN);
        lv_obj_set_style_bg_color(w->bar, lv_color_hex(w->color), LV_PART_INDICATOR);
        lv_obj_set_style_border_width(w->bar, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(w->bar, lv_color_hex(0x334155), LV_PART_MAIN);
        lv_obj_set_style_shadow_width(w->bar, 8, LV_PART_MAIN);
        lv_obj_set_style_shadow_color(w->bar, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(w->bar, LV_OPA_20, LV_PART_MAIN);
        if (m->kind == WIDGET_TANK) {
            // v6.20: reserve a larger bottom value-label band on the physical
            // LCD.  The LCD font rasterization is taller than the browser
            // preview, so the earlier tank bar still crowded the numeric value.
            int tank_h = hh - 92;
            if (tank_h < 28) tank_h = 28;
            int tank_w = ww / 2;
            if (tank_w < 12) tank_w = 12;
            lv_obj_set_size(w->bar, tank_w, tank_h);
            lv_obj_align(w->bar, LV_ALIGN_TOP_MID, 0, 34);
        } else {
            // Keep the gauge fill higher in the card and slightly thinner so
            // the numeric value has its own clean line below the bar.
            int bw = ww - 26;
            if (bw < 12) bw = 12;
            lv_obj_set_size(w->bar, bw, 18);
            lv_obj_align(w->bar, LV_ALIGN_TOP_MID, 0, 32);
        }
        w->value_label = add_text(w->root, "---", w->color, 16);
        lv_obj_set_width(w->value_label, ww - 12);
        lv_obj_set_style_text_align(w->value_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        if (m->kind == WIDGET_TANK) {
            lv_obj_align(w->value_label, LV_ALIGN_BOTTOM_MID, 0, -10);
        } else {
            lv_obj_align(w->value_label, LV_ALIGN_BOTTOM_MID, 0, -5);
        }
    }

    if (!w->tag[0]) {
        char text[96];
        if (w->kind == WIDGET_READOUT && w->value_label) {
            snprintf(text, sizeof(text), "%.*f%s", w->decimals, w->static_number, w->unit);
            safe_label_set_text(w->value_label, text);
        } else if ((w->kind == WIDGET_BAR || w->kind == WIDGET_TANK) && w->bar) {
            int pct = percent_value(w);
            lv_bar_set_value(w->bar, pct, LV_ANIM_OFF);
            if (w->value_label) {
                snprintf(text, sizeof(text), "%.*f%s", w->decimals, w->static_number, w->unit);
                safe_label_set_text(w->value_label, text);
            }
        } else if (w->kind == WIDGET_LED && w->state_obj) {
            bool on = w->static_bool;
            lv_obj_set_style_bg_color(w->state_obj, lv_color_hex(on ? lighten_rgb(w->color, 8) : 0x0f172a), LV_PART_MAIN);
            lv_obj_set_style_border_color(w->state_obj, lv_color_hex(on ? lighten_rgb(w->color, 35) : 0x334155), LV_PART_MAIN);
        } else if (w->kind == WIDGET_TOGGLE && w->state_obj) {
            static_toggle_set(w, w->static_bool);
        }
    } else {
        runtime_widget_apply_value(w, true);
    }
}

static void create_widget(runtime_state_t *rt, lv_obj_t *parent, const cJSON *wj)
{
    if (!rt || rt->count >= HMI_MAX_WIDGETS) return;
    const cJSON *props = cJSON_GetObjectItemCaseSensitive(wj, "props");
    if (!cJSON_IsObject(props)) props = wj;

    const char *type = json_str(wj, "type", "");
    widget_kind_t kind = kind_from_type(type);
    if (kind == WIDGET_NONE) return;

    int x = json_int(wj, "x", 20);
    int y = json_int(wj, "y", 20);
    int ww = json_int(wj, "w", 160);
    int hh = json_int(wj, "h", 70);
    if (ww < 30) ww = 30;
    if (hh < 24) hh = 24;

    runtime_widget_t *w = &rt->widgets[rt->count++];
    memset(w, 0, sizeof(*w));
    w->kind = kind;
    w->last_percent = -1;
    snprintf(w->id, sizeof(w->id), "%s", json_str(wj, "id", "widget"));
    snprintf(w->tag, sizeof(w->tag), "%s", json_str(props, "pin", json_str(props, "tag", "")));
    snprintf(w->label, sizeof(w->label), "%s", json_str(props, "label", json_str(wj, "text", type)));
    snprintf(w->unit, sizeof(w->unit), "%s", json_str(props, "unit", ""));
    snprintf(w->mode, sizeof(w->mode), "%s", json_str(props, "buttonMode", "write"));
    snprintf(w->event_click, sizeof(w->event_click), "%s", json_event_str(wj, props, "onClick"));
    snprintf(w->event_change, sizeof(w->event_change), "%s", json_event_str(wj, props, "onChange"));
    w->min = json_float(props, "min", json_float(props, "setMin", 0.0f));
    w->max = json_float(props, "max", json_float(props, "setMax", 100.0f));
    w->step = json_float(props, "step", 1.0f);
    if (w->step <= 0.0f) w->step = 1.0f;
    w->snap_to_step = json_bool(props, "snapToStep", false);
    w->decimals = json_int(props, "decimals", 1);
    w->color = parse_hex_color(json_str(props, "color", "#38bdf8"), 0x38bdf8);
    w->press_bool = json_bool_or_string(props, "pressValue", true, &w->press_is_bool, &w->press_number);
    w->release_bool = json_bool_or_string(props, "releaseValue", false, &w->release_is_bool, &w->release_number);
    w->static_number = json_float(props, "value", json_float(props, "defaultValue", 0.0f));
    w->static_bool = json_bool_or_string(props, "checked", fabsf(w->static_number) > 0.0001f, NULL, NULL);
    snprintf(w->static_text, sizeof(w->static_text), "%s", json_str(props, "value", json_str(props, "text", w->label)));

    if (w->tag[0] && !hmi_tag_db_exists(w->tag)) {
        if (kind == WIDGET_LED || kind == WIDGET_TOGGLE || kind == WIDGET_BUTTON) {
            hmi_tag_db_define_bool(w->tag, false, true);
        } else if (kind == WIDGET_LABEL) {
            hmi_tag_db_define_string(w->tag, w->label, true);
        } else {
            hmi_tag_db_define_number(w->tag, w->static_number, w->min, w->max, w->unit, true);
        }
    }

    w->root = lv_obj_create(parent ? parent : rt->screen);
    lv_obj_set_pos(w->root, x, y);
    lv_obj_set_size(w->root, ww, hh);
    if (kind == WIDGET_TOGGLE) {
        style_toggle_root(w->root);
    } else {
        style_box(w->root, 0x1D2939, 0x344054);
    }

    if (kind == WIDGET_LABEL) {
        // Labels are text-only on the LCD. The designer still shows a selection
        // outline, but the runtime should not draw a surrounding box.
        style_plain_label_root(w->root);
        lv_obj_set_style_text_color(w->root, lv_color_hex(w->color), LV_PART_MAIN);
        lv_obj_t *label = add_text(w->root, w->label, w->color, json_int(props, "fontSize", 24));
        lv_obj_set_width(label, ww);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_center(label);
        w->value_label = label;
    } else if (kind == WIDGET_READOUT) {
        lv_obj_set_style_bg_color(w->root, lv_color_hex(0x0B1220), LV_PART_MAIN);
        lv_obj_set_style_border_color(w->root, lv_color_hex(0x26364E), LV_PART_MAIN);
        lv_obj_set_style_radius(w->root, 14, LV_PART_MAIN);
        lv_obj_t *name = add_text(w->root, w->label, 0x9AA9BF, 12);
        lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 2);
        w->value_label = add_text(w->root, "---", w->color, 28);
        lv_obj_set_width(w->value_label, ww - 16);
        lv_obj_align(w->value_label, LV_ALIGN_CENTER, 0, 10);
        lv_obj_set_style_text_align(w->value_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        // v6.45.33: ReadOut values should honor the widget color from the web designer.
        // add_text() intentionally avoids setting color globally during creation, so apply
        // the value color only to this final value label.
        lv_obj_set_style_text_color(w->value_label, lv_color_hex(w->color), LV_PART_MAIN);
    } else if (kind == WIDGET_LED) {
        lv_obj_set_style_bg_color(w->root, lv_color_hex(0x10182A), LV_PART_MAIN);
        lv_obj_set_style_border_color(w->root, lv_color_hex(0x26364E), LV_PART_MAIN);
        lv_obj_set_style_radius(w->root, 14, LV_PART_MAIN);
        w->state_obj = lv_obj_create(w->root);
        int d = hh > ww ? ww / 2 : hh / 2;
        if (d < 22) d = 22;
        if (d > 48) d = 48;
        lv_obj_set_size(w->state_obj, d, d);
        lv_obj_set_style_radius(w->state_obj, d / 2, LV_PART_MAIN);
        lv_obj_set_style_bg_color(w->state_obj, lv_color_hex(0x0B1220), LV_PART_MAIN);
        lv_obj_set_style_border_width(w->state_obj, 3, LV_PART_MAIN);
        lv_obj_set_style_border_color(w->state_obj, lv_color_hex(0x334155), LV_PART_MAIN);
        lv_obj_clear_flag(w->state_obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(w->state_obj, LV_ALIGN_TOP_MID, 0, 4);
        lv_obj_t *name = add_text(w->root, w->label, 0xC1CCDC, 12);
        lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -2);
    } else if (kind == WIDGET_BUTTON) {
        // Use the root object itself as the clickable button with a soft
        // vertical gradient and drop shadow so it matches the Vue preview.
        lv_obj_set_style_bg_color(w->root, lv_color_hex(lighten_rgb(w->color, 10)), LV_PART_MAIN);
        lv_obj_set_style_bg_grad_color(w->root, lv_color_hex(darken_rgb(w->color, 18)), LV_PART_MAIN);
        lv_obj_set_style_bg_grad_dir(w->root, LV_GRAD_DIR_VER, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(w->root, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(w->root, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(w->root, lv_color_hex(lighten_rgb(w->color, 45)), LV_PART_MAIN);
        lv_obj_set_style_radius(w->root, 15, LV_PART_MAIN);
        style_shadow(w->root, darken_rgb(w->color, 45), LV_OPA_40, 16, 5);
        lv_obj_set_style_text_color(w->root, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_add_flag(w->root, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(w->root, LV_OBJ_FLAG_EVENT_BUBBLE);
        w->state_obj = w->root;
        lv_obj_t *label = add_text(w->root, w->label, 0xFFFFFF, 16);
        lv_obj_set_width(label, ww - 16);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_center(label);
        w->value_label = label;
    } else if (kind == WIDGET_TOGGLE) {
        // v6.18: use the native LVGL switch again.
        create_pilab_toggle_visual(w, ww, hh);
    } else if (kind == WIDGET_SETPOINT) {
        lv_obj_set_style_bg_color(w->root, lv_color_hex(0x10182A), LV_PART_MAIN);
        lv_obj_set_style_border_color(w->root, lv_color_hex(0x26364E), LV_PART_MAIN);
        lv_obj_set_style_radius(w->root, 14, LV_PART_MAIN);
        lv_obj_t *name = add_text(w->root, w->label, 0x9AA9BF, 12);
        lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 0);
        w->spinbox = lv_spinbox_create(w->root);
        lv_spinbox_set_range(w->spinbox, (int32_t)w->min, (int32_t)w->max);
        lv_spinbox_set_digit_format(w->spinbox, 5, 0);
        lv_spinbox_set_step(w->spinbox, (uint32_t)w->step);
        lv_obj_set_width(w->spinbox, ww - 26);
        style_setpoint_spinbox_display(w->spinbox, w->color);
        lv_obj_align(w->spinbox, LV_ALIGN_CENTER, 0, 12);
        lv_obj_add_flag(w->root, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(w->root, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_flag(w->spinbox, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(w->spinbox, LV_OBJ_FLAG_EVENT_BUBBLE);
        w->value_label = w->spinbox;
    } else if (kind == WIDGET_BAR || kind == WIDGET_TANK) {
        lv_obj_set_style_bg_color(w->root, lv_color_hex(0x10182A), LV_PART_MAIN);
        lv_obj_set_style_border_color(w->root, lv_color_hex(0x26364E), LV_PART_MAIN);
        lv_obj_set_style_radius(w->root, 14, LV_PART_MAIN);
        lv_obj_t *name = add_text(w->root, w->label, 0x9AA9BF, 12);
        lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 0);
        w->bar = lv_bar_create(w->root);
        lv_bar_set_range(w->bar, 0, 100);
        lv_obj_set_style_radius(w->bar, 12, LV_PART_MAIN);
        lv_obj_set_style_radius(w->bar, 12, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(w->bar, lv_color_hex(0x07101F), LV_PART_MAIN);
        lv_obj_set_style_bg_color(w->bar, lv_color_hex(w->color), LV_PART_INDICATOR);
        lv_obj_set_style_border_width(w->bar, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(w->bar, lv_color_hex(0x334155), LV_PART_MAIN);
        lv_obj_set_style_shadow_width(w->bar, 8, LV_PART_MAIN);
        lv_obj_set_style_shadow_color(w->bar, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(w->bar, LV_OPA_20, LV_PART_MAIN);
        if (kind == WIDGET_TANK) {
            // v6.20: reserve a larger bottom value-label band on the physical
            // LCD.  The LCD font rasterization is taller than the browser
            // preview, so the earlier tank bar still crowded the numeric value.
            int tank_h = hh - 92;
            if (tank_h < 28) tank_h = 28;
            int tank_w = ww / 2;
            if (tank_w < 12) tank_w = 12;
            lv_obj_set_size(w->bar, tank_w, tank_h);
            lv_obj_align(w->bar, LV_ALIGN_TOP_MID, 0, 34);
        } else {
            // Keep the gauge fill higher in the card and slightly thinner so
            // the numeric value has its own clean line below the bar.
            int bw = ww - 26;
            if (bw < 12) bw = 12;
            lv_obj_set_size(w->bar, bw, 18);
            lv_obj_align(w->bar, LV_ALIGN_TOP_MID, 0, 32);
        }
        w->value_label = add_text(w->root, "---", w->color, 16);
        lv_obj_set_width(w->value_label, ww - 12);
        lv_obj_set_style_text_align(w->value_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        if (kind == WIDGET_TANK) {
            lv_obj_align(w->value_label, LV_ALIGN_BOTTOM_MID, 0, -10);
        } else {
            lv_obj_align(w->value_label, LV_ALIGN_BOTTOM_MID, 0, -5);
        }
    }

    // Untagged dynamic widgets are legal. They act as static/demo widgets and
    // must not be added to the live Tag update path. Initialize their visible
    // state here so gauges/tanks/readouts without tags still render safely.
    if (!w->tag[0]) {
        char text[96];
        if (w->kind == WIDGET_READOUT && w->value_label) {
            snprintf(text, sizeof(text), "%.*f%s", w->decimals, w->static_number, w->unit);
            safe_label_set_text(w->value_label, text);
        } else if ((w->kind == WIDGET_BAR || w->kind == WIDGET_TANK) && w->bar) {
            int pct = percent_value(w);
            lv_bar_set_value(w->bar, pct, LV_ANIM_OFF);
            if (w->value_label) {
                snprintf(text, sizeof(text), "%.*f%s", w->decimals, w->static_number, w->unit);
                safe_label_set_text(w->value_label, text);
            }
        } else if (w->kind == WIDGET_LED && w->state_obj) {
            bool on = w->static_bool;
            lv_obj_set_style_bg_color(w->state_obj, lv_color_hex(on ? lighten_rgb(w->color, 8) : 0x0f172a), LV_PART_MAIN);
            lv_obj_set_style_border_color(w->state_obj, lv_color_hex(on ? lighten_rgb(w->color, 35) : 0x334155), LV_PART_MAIN);
        } else if (w->kind == WIDGET_TOGGLE && w->state_obj) {
            static_toggle_set(w, w->static_bool);
        }
    } else {
        runtime_widget_apply_value(w, true);
    }
}


static void delete_obj_deferred_async(void *user_data)
{
    deferred_obj_delete_t *defer = (deferred_obj_delete_t *)user_data;
    if (!defer) return;

    if (defer->remaining_cycles > 0) {
        defer->remaining_cycles--;
        lv_async_call(delete_obj_deferred_async, defer);
        return;
    }

    if (defer->obj) {
        lv_obj_delete(defer->obj);
    }
    free(defer);
}

static void defer_delete_obj(lv_obj_t *obj, uint8_t cycles)
{
    if (!obj) return;
    deferred_obj_delete_t *defer = (deferred_obj_delete_t *)hmi_heap_calloc(1, sizeof(deferred_obj_delete_t));
    if (!defer) {
        lv_obj_delete(obj);
        return;
    }
    defer->obj = obj;
    defer->remaining_cycles = cycles;
    lv_async_call(delete_obj_deferred_async, defer);
}

static void free_runtime_state_now(runtime_state_t *rt)
{
    if (!rt) return;
    if (rt->screen) {
        lv_obj_delete(rt->screen);
    }
    layout_model_free(rt->model);
    rt->model = NULL;
    free(rt);
}

static lv_obj_t *create_reload_placeholder_screen(void)
{
    lv_obj_t *loading = lv_obj_create(NULL);
    if (!loading) return NULL;
    lv_obj_set_size(loading, HMI_SCREEN_W, HMI_SCREEN_H);
    lv_obj_set_style_bg_color(loading, lv_color_hex(0x101828), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(loading, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(loading, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(loading, 0, LV_PART_MAIN);
    lv_obj_clear_flag(loading, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *msg = lv_label_create(loading);
    lv_label_set_long_mode(msg, LV_LABEL_LONG_DOT);
    lv_label_set_text(msg, "Applying screen...");
    lv_obj_set_style_text_color(msg, lv_color_hex(0xCBD5E1), LV_PART_MAIN);
    lv_obj_center(msg);
    return loading;
}

static void free_runtime_state_deferred_async(void *user_data)
{
    deferred_free_t *defer = (deferred_free_t *)user_data;
    if (!defer) return;

    // Give LVGL/RGB refresh a few timer cycles after lv_screen_load() before
    // deleting the previous screen tree. Deleting the old tree immediately on
    // the next async tick was usually fine, but rapid browser reload/apply
    // operations could still expose tearing or offset-looking artifacts on the
    // RGB panel.
    if (defer->remaining_cycles > 0) {
        defer->remaining_cycles--;
        lv_async_call(free_runtime_state_deferred_async, defer);
        return;
    }

    runtime_state_t *rt = defer->rt;
    if (rt && rt->screen) {
        lv_obj_delete(rt->screen);
    }
    if (rt) {
        layout_model_free(rt->model);
        rt->model = NULL;
    }
    free(rt);
    free(defer);
}

static void defer_free_runtime_state(runtime_state_t *rt)
{
    if (!rt) return;
    deferred_free_t *defer = (deferred_free_t *)hmi_heap_calloc(1, sizeof(deferred_free_t));
    if (!defer) {
        // Last-resort fallback. This still runs in LVGL context.
        if (rt->screen) lv_obj_delete(rt->screen);
        layout_model_free(rt->model);
        rt->model = NULL;
        free(rt);
        return;
    }
    defer->rt = rt;
    defer->remaining_cycles = 3;
    lv_async_call(free_runtime_state_deferred_async, defer);
}

static esp_err_t build_runtime_from_json(cJSON *root, runtime_state_t **out_rt)
{
    if (!root || !out_rt) return ESP_ERR_INVALID_ARG;

    runtime_state_t *rt = (runtime_state_t *)hmi_heap_calloc(1, sizeof(runtime_state_t));
    if (!rt) return ESP_ERR_NO_MEM;
    rt->generation = ++s_runtime_generation;
    rt->ready = false;

    rt->screen = lv_obj_create(NULL);
    if (!rt->screen) {
        free(rt);
        return ESP_ERR_NO_MEM;
    }

    const cJSON *screen = cJSON_GetObjectItemCaseSensitive(root, "screen");
    const char *bg = json_str(screen, "background", "#101828");
    lv_obj_set_size(rt->screen, HMI_SCREEN_W, HMI_SCREEN_H);
    lv_obj_set_style_bg_color(rt->screen, lv_color_hex(parse_hex_color(bg, 0x101828)), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rt->screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(rt->screen, LV_OBJ_FLAG_SCROLLABLE);

    const char *active_page = json_str(root, "startupPage", json_str(root, "bootPage", json_str(root, "activePage", json_str(root, "currentPage", ""))));
    const cJSON *pages = cJSON_GetObjectItemCaseSensitive(root, "pages");
    const cJSON *page = NULL;
    const cJSON *wj = NULL;
    uint32_t built_widgets = 0;

    if (cJSON_IsArray(pages) && cJSON_GetArraySize(pages) > 0) {
        cJSON_ArrayForEach(page, pages) {
            if (rt->page_count >= HMI_MAX_PAGES) break;
            const size_t pi = rt->page_count++;
            runtime_page_t *rp = &rt->pages[pi];
            snprintf(rp->id, sizeof(rp->id), "%s", json_str(page, "id", ""));
            snprintf(rp->name, sizeof(rp->name), "%s", json_str(page, "name", json_str(page, "title", rp->id[0] ? rp->id : "Screen")));
            if (!rp->id[0]) snprintf(rp->id, sizeof(rp->id), "screen_%u", (unsigned)(pi + 1));

            rp->obj = lv_obj_create(rt->screen);
            lv_obj_set_pos(rp->obj, 0, 0);
            lv_obj_set_size(rp->obj, HMI_SCREEN_W, HMI_SCREEN_H);
            // Page containers are deliberately opaque.  On ESP32-S3 RGB panels
            // a transparent full-page container causes LVGL to redraw lots of
            // individual child/background areas during navigation, which can
            // look like tearing/glitching.  An opaque page lets the newly shown
            // page cover the previous one as a single full-screen layer.
            lv_obj_set_style_bg_color(rp->obj, lv_color_hex(parse_hex_color(bg, 0x101828)), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(rp->obj, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_text_color(rp->obj, lv_color_hex(0xCBD5E1), LV_PART_MAIN);
            lv_obj_set_style_border_width(rp->obj, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(rp->obj, 0, LV_PART_MAIN);
            lv_obj_clear_flag(rp->obj, LV_OBJ_FLAG_SCROLLABLE);

            if ((active_page[0] && (strcmp(active_page, rp->id) == 0 || strcmp(active_page, rp->name) == 0)) || (!active_page[0] && pi == 0)) {
                rt->active_page = (int)pi;
            }

            // Do not hide inactive page containers until after their child
            // widgets have been built.  On LVGL 9.x/RGB panels, creating and
            // sizing labels under a hidden full-screen parent can produce very
            // expensive invalidation/layout work and has been observed to hang
            // inside lv_label_set_text() during multi-page reloads.  Build all
            // pages visible first, then hide inactive pages in one final pass.
            const cJSON *arr = cJSON_GetObjectItemCaseSensitive(page, "widgets");
            if (!cJSON_IsArray(arr)) arr = cJSON_GetObjectItemCaseSensitive(page, "objects");
            cJSON_ArrayForEach(wj, arr) {
                create_widget(rt, rp->obj, wj);
                if ((++built_widgets % 8U) == 0U) vTaskDelay(1);
            }
        }
    } else {
        // Backward compatibility with v3/v4 single-screen layouts.
        rt->page_count = 1;
        rt->active_page = 0;
        snprintf(rt->pages[0].id, sizeof(rt->pages[0].id), "main");
        snprintf(rt->pages[0].name, sizeof(rt->pages[0].name), "Main");
        rt->pages[0].obj = rt->screen;
        const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "widgets");
        if (!cJSON_IsArray(arr)) arr = cJSON_GetObjectItemCaseSensitive(root, "objects");
        cJSON_ArrayForEach(wj, arr) {
            create_widget(rt, rt->screen, wj);
            if ((++built_widgets % 8U) == 0U) vTaskDelay(1);
        }
    }

    if (rt->page_count > 0) {
        for (size_t i = 0; i < rt->page_count; ++i) {
            if ((int)i == rt->active_page) lv_obj_clear_flag(rt->pages[i].obj, LV_OBJ_FLAG_HIDDEN);
            else if (rt->pages[i].obj != rt->screen) lv_obj_add_flag(rt->pages[i].obj, LV_OBJ_FLAG_HIDDEN);
        }
    }

    *out_rt = rt;
    return ESP_OK;
}


static esp_err_t build_runtime_shell_from_model(const layout_model_t *model, runtime_state_t **out_rt)
{
    if (!model || !out_rt) return ESP_ERR_INVALID_ARG;
    *out_rt = NULL;
    runtime_state_t *rt = (runtime_state_t *)hmi_heap_calloc(1, sizeof(runtime_state_t));
    if (!rt) return ESP_ERR_NO_MEM;
    rt->generation = ++s_runtime_generation;
    rt->ready = false;

    rt->screen = lv_obj_create(NULL);
    if (!rt->screen) {
        free(rt);
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_size(rt->screen, HMI_SCREEN_W, HMI_SCREEN_H);
    lv_obj_set_style_bg_color(rt->screen, lv_color_hex(model->background), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rt->screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(rt->screen, lv_color_hex(0xCBD5E1), LV_PART_MAIN);
    lv_obj_clear_flag(rt->screen, LV_OBJ_FLAG_SCROLLABLE);
    rt->active_page = 0;

    // v6.0 experimental on-demand page model:
    // Keep only page metadata in the runtime shell.  Do not create LVGL
    // containers or child widgets for inactive pages.  The active page is
    // created by the build timer, and ScreenShow destroys/rebuilds page
    // containers as needed.
    rt->model = (layout_model_t *)model;
    for (size_t i = 0; i < model->page_count && i < HMI_MAX_PAGES; ++i) {
        const layout_page_model_t *mp = &model->pages[i];
        runtime_page_t *rp = &rt->pages[rt->page_count++];
        snprintf(rp->id, sizeof(rp->id), "%s", mp->id[0] ? mp->id : "screen");
        snprintf(rp->name, sizeof(rp->name), "%s", mp->name[0] ? mp->name : rp->id);
        rp->obj = NULL;
        if (model->startup_page[0] &&
            (strcasecmp(model->startup_page, rp->id) == 0 || strcasecmp(model->startup_page, rp->name) == 0)) {
            rt->active_page = (int)i;
        }
    }

    if (rt->page_count == 0) {
        rt->page_count = 1;
        snprintf(rt->pages[0].id, sizeof(rt->pages[0].id), "main");
        snprintf(rt->pages[0].name, sizeof(rt->pages[0].name), "Main");
        rt->pages[0].obj = NULL;
    }

    *out_rt = rt;
    return ESP_OK;
}

static void layout_build_ctx_free(layout_build_ctx_t *ctx)
{
    if (!ctx) return;
    layout_model_free(ctx->model);
    free(ctx->json_copy);
    free(ctx);
}

static lv_obj_t *create_page_container_at(runtime_state_t *rt, int page_index, int x_pos)
{
    if (!rt || !rt->model || page_index < 0 || page_index >= (int)rt->page_count) return NULL;
    runtime_page_t *rp = &rt->pages[page_index];
    if (rp->obj) return rp->obj;

    rp->obj = lv_obj_create(rt->screen);
    if (!rp->obj) return NULL;
    lv_obj_set_pos(rp->obj, x_pos, 0);
    lv_obj_set_size(rp->obj, HMI_SCREEN_W, HMI_SCREEN_H);
    lv_obj_set_style_bg_color(rp->obj, lv_color_hex(rt->model->background), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rp->obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(rp->obj, lv_color_hex(0xCBD5E1), LV_PART_MAIN);
    lv_obj_set_style_border_width(rp->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(rp->obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(rp->obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(rp->obj, page_event_router_cb, LV_EVENT_ALL, rt);
    lv_obj_clear_flag(rp->obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(rp->obj);
    return rp->obj;
}

static lv_obj_t *create_page_container(runtime_state_t *rt, int page_index)
{
    return create_page_container_at(rt, page_index, 0);
}

static void destroy_active_page_objects(runtime_state_t *rt)
{
    if (!rt || rt->active_page < 0 || rt->active_page >= (int)rt->page_count) return;
    runtime_page_t *rp = &rt->pages[rt->active_page];
    if (rp->obj) {
        lv_obj_delete(rp->obj);
        rp->obj = NULL;
    }
    memset(rt->widgets, 0, sizeof(rt->widgets));
    rt->count = 0;
}

static void build_page_now_at(runtime_state_t *rt, int page_index, int x_pos)
{
    if (!rt || !rt->model || page_index < 0 || page_index >= (int)rt->page_count) return;
    lv_obj_t *parent = create_page_container_at(rt, page_index, x_pos);
    if (!parent) return;

    const layout_page_model_t *mp = &rt->model->pages[page_index];
    rt->count = 0;
    memset(rt->widgets, 0, sizeof(rt->widgets));
    for (size_t n = 0; n < mp->widget_count; ++n) {
        size_t wi = mp->first_widget + n;
        if (wi >= rt->model->widget_count) break;
        const layout_widget_model_t *mw = &rt->model->widgets[wi];
#if HMI_LOG_WIDGET_BUILD
        ESP_LOGI(TAG, "Build on-demand widget %u/%u page=%s type=%d id=%s tag=%s",
                 (unsigned)(n + 1), (unsigned)mp->widget_count, mp->name, (int)mw->kind, mw->id, mw->tag);
#endif
        create_widget_from_model(rt, parent, mw, page_index);
    }
}

static void build_active_page_now(runtime_state_t *rt, int page_index)
{
    build_page_now_at(rt, page_index, 0);
}

static void layout_build_timer_cb(lv_timer_t *timer)
{
    layout_build_ctx_t *ctx = (layout_build_ctx_t *)lv_timer_get_user_data(timer);
    if (!ctx || !ctx->model || !ctx->rt) {
        if (timer) lv_timer_delete(timer);
        layout_build_ctx_free(ctx);
        s_reload_async_scheduled = false;
        s_reload_in_progress = false;
        runtime_set_busy(false, "build ctx invalid");
        return;
    }

    ensure_render_mutex();
    if (!s_render_mutex || xSemaphoreTake(s_render_mutex, 0) != pdTRUE) {
        return;
    }

    if (!ctx->rt->pages[ctx->rt->active_page].obj) {
        create_page_container(ctx->rt, ctx->rt->active_page);
    }

    const layout_page_model_t *mp = &ctx->model->pages[ctx->rt->active_page];
    const uint32_t max_widgets_this_tick = 2;
    uint32_t built_this_tick = 0;

    while (ctx->widget_index_in_page < mp->widget_count && built_this_tick < max_widgets_this_tick) {
        size_t wi = mp->first_widget + ctx->widget_index_in_page;
        if (wi < ctx->model->widget_count) {
            const layout_widget_model_t *mw = &ctx->model->widgets[wi];
            if (ctx->first_widget_us == 0) {
                ctx->first_widget_us = esp_timer_get_time();
#if HMI_LOG_TIMING_DETAIL
                ESP_LOGI(TAG, "Reload timing: first active-page widget after request=%lld us, after queue=%lld us, after reload_begin=%lld us",
                         (long long)(ctx->first_widget_us - ctx->request_us),
                         (long long)(ctx->first_widget_us - ctx->queued_us),
                         (long long)(ctx->first_widget_us - ctx->reload_begin_us));
#endif
            }
#if HMI_LOG_WIDGET_BUILD
            ESP_LOGI(TAG, "Build active-page widget %u/%u page=%s type=%d id=%s pos=%d,%d size=%dx%d tag=%s",
                     (unsigned)(ctx->widget_index_in_page + 1), (unsigned)mp->widget_count, mp->name, (int)mw->kind, mw->id,
                     mw->x, mw->y, mw->w, mw->h, mw->tag);
#endif
            create_widget_from_model(ctx->rt, ctx->rt->pages[ctx->rt->active_page].obj ? ctx->rt->pages[ctx->rt->active_page].obj : ctx->rt->screen, mw, ctx->rt->active_page);
            built_this_tick++;
        }
        ctx->widget_index_in_page++;
    }

#if HMI_LOG_WIDGET_BUILD
    if (ctx->rt->count > 0 && (ctx->rt->count % 10U) == 0U) {
        hmi_heap_log("during active-page build pump");
    }
#endif

    if (ctx->widget_index_in_page < mp->widget_count) {
        xSemaphoreGive(s_render_mutex);
        return;
    }

    ctx->rt->ready = true;
    lv_screen_load(ctx->rt->screen);
    s_active = ctx->rt;
    log_page_visibility(ctx->rt, "on-demand reload complete");

    if (ctx->old_rt) {
        defer_free_runtime_state(ctx->old_rt);
        ctx->old_rt = NULL;
    }

    if (ctx->placeholder) {
        defer_delete_obj(ctx->placeholder, 2);
    }

    if (ctx->json_copy) {
        free(s_last_layout_json);
        s_last_layout_json = ctx->json_copy;
        ctx->json_copy = NULL;
    }

    ctx->model = NULL; // ownership moved to rt->model

    int64_t done_us = esp_timer_get_time();
    int64_t elapsed_us = done_us - ctx->start_us;
#if HMI_LOG_SUMMARY
    ESP_LOGI(TAG, "HMI reload: active=%s widgets=%u/%u layout_widgets=%u build=%lld ms total=%lld ms",
             ctx->rt->pages[ctx->rt->active_page].name,
             (unsigned)ctx->rt->count, (unsigned)mp->widget_count,
             (unsigned)ctx->rt->model->widget_count,
             (long long)(elapsed_us / 1000),
             (long long)((done_us - ctx->request_us) / 1000));
    hmi_memory_diag_checkpoint("hmi: reload complete");
#endif
#if HMI_LOG_TIMING_DETAIL
    ESP_LOGI(TAG, "Reload timing detail: total_from_request=%lld us, model_prepare=%lld us, queue_wait=%lld us, shell_plus_placeholder=%lld us, active_page_build_to_load=%lld us",
             (long long)(done_us - ctx->request_us),
             (long long)(ctx->model_ready_us - ctx->request_us),
             (long long)(ctx->reload_begin_us - ctx->queued_us),
             (long long)(ctx->start_us - ctx->reload_begin_us),
             (long long)(done_us - (ctx->first_widget_us ? ctx->first_widget_us : ctx->start_us)));
#endif
    s_reload_async_scheduled = false;
    s_reload_in_progress = false;
    defer_enable_input(ctx->rt->generation, 8);

    ctx->rt = NULL;
    ctx->timer = NULL;
    xSemaphoreGive(s_render_mutex);
    if (timer) lv_timer_delete(timer);
    layout_build_ctx_free(ctx);
}


static esp_err_t apply_layout_json_in_lvgl_context(const char *json, bool remember_json)
{
    if (!json) return ESP_ERR_INVALID_ARG;

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGE(TAG, "Layout JSON parse failed");
        return ESP_ERR_INVALID_ARG;
    }
    update_panel_tick_config_from_root(root);

    runtime_state_t *old_rt = s_active;
    lv_obj_t *placeholder = NULL;
    int64_t start_us = esp_timer_get_time();

    // Full layout reloads can temporarily require old_screen + new_screen LVGL
    // objects at the same time. With multi-screen layouts this can push the S3
    // close to internal-heap fragmentation limits and has been observed to hang
    // inside lv_obj_create()/lv_label_set_text().  For live Apply/Reload, switch
    // first to a tiny opaque placeholder screen, free the old runtime, then build
    // the new runtime. This trades a brief "Applying screen" flash for a much
    // lower peak LVGL object/memory load and avoids the old/new tree overlap.
    if (old_rt && old_rt->screen) {
        placeholder = create_reload_placeholder_screen();
        if (placeholder) {
            lv_screen_load(placeholder);
            ESP_LOGI(TAG, "Legacy reload timing: Applying placeholder shown at %lld us after start",
                     (long long)(esp_timer_get_time() - start_us));
            s_active = NULL;
            old_rt->ready = false;
            defer_free_runtime_state(old_rt);
            old_rt = NULL;
        }
    }

    runtime_state_t *new_rt = NULL;
    esp_err_t err = build_runtime_from_json(root, &new_rt);
    cJSON_Delete(root);

    if (err != ESP_OK || !new_rt) {
        ESP_LOGE(TAG, "Layout build failed: %s", esp_err_to_name(err));
        // Leave the placeholder visible if we had to free the old runtime. It is
        // safer than attempting to resurrect partially deleted LVGL objects.
        return err != ESP_OK ? err : ESP_FAIL;
    }

    new_rt->ready = true;
    lv_screen_load(new_rt->screen);
    s_active = new_rt;
    log_page_visibility(new_rt, "legacy reload complete");

    if (placeholder) {
        defer_delete_obj(placeholder, 2);
    } else if (old_rt) {
        // Initial/legacy path fallback. Normally old_rt is already freed above.
        defer_free_runtime_state(old_rt);
    }

    if (remember_json) {
        char *copy = hmi_heap_strdup(json);
        if (copy) {
            free(s_last_layout_json);
            s_last_layout_json = copy;
        }
    }

    int64_t elapsed_us = esp_timer_get_time() - start_us;
#if HMI_LOG_SUMMARY
    ESP_LOGI(TAG, "HMI reload legacy: widgets=%u build=%lld ms", (unsigned)new_rt->count, (long long)(elapsed_us / 1000));
#endif
    return ESP_OK;
}

esp_err_t hmi_lvgl_runtime_load_layout(const char *json)
{
    // Keep the public API, but route it through the same staged reload path as
    // boot and browser Apply.  Direct synchronous JSON parse/build inside LVGL
    // is intentionally avoided in v5.7.
    return hmi_lvgl_runtime_request_reload_json(json);
}

static void reload_async_cb(void *user_data)
{
    (void)user_data;

    ensure_render_mutex();
    if (!s_render_mutex || xSemaphoreTake(s_render_mutex, 0) != pdTRUE) {
        lv_async_call(reload_async_cb, NULL);
        return;
    }

    // Close any transient numeric entry popup before replacing the screen tree.
    setpoint_editor_close();

    layout_model_t *model = s_pending_reload_model;
    char *json = s_pending_reload_json;
    int64_t request_us = s_pending_reload_request_us;
    int64_t model_ready_us = s_pending_reload_model_ready_us;
    int64_t queued_us = s_pending_reload_queued_us;
    int64_t reload_begin_us = esp_timer_get_time();
    s_pending_reload_model = NULL;
    s_pending_reload_json = NULL;
    s_pending_reload_request_us = 0;
    s_pending_reload_model_ready_us = 0;
    s_pending_reload_queued_us = 0;

    if (!model) {
        free(json);
        s_reload_async_scheduled = false;
        s_reload_in_progress = false;
        runtime_set_busy(false, "reload no model");
        xSemaphoreGive(s_render_mutex);
        return;
    }

#if HMI_LOG_TIMING_DETAIL
    ESP_LOGI(TAG, "Reload begin in LVGL context from prepared model: pages=%u widgets=%u bytes=%u queue_wait=%lld us",
             (unsigned)model->page_count, (unsigned)model->widget_count, json ? (unsigned)strlen(json) : 0U,
             (long long)(queued_us ? (reload_begin_us - queued_us) : 0));
#endif

    runtime_state_t *old_rt = s_active;
    lv_obj_t *placeholder = NULL;
    hmi_heap_log("reload begin before placeholder");
    if (old_rt && old_rt->screen) {
        placeholder = create_reload_placeholder_screen();
        if (placeholder) {
            lv_screen_load(placeholder);
#if HMI_LOG_TIMING_DETAIL
            ESP_LOGI(TAG, "Reload timing: Applying placeholder shown at %lld us after request",
                     (long long)(esp_timer_get_time() - request_us));
#endif
            s_active = NULL;
            old_rt->ready = false;

            // v5.21: reduce peak LVGL object memory before building the new
            // off-screen screen.  The previous v5.17-v5.20 path kept the old
            // runtime screen alive until the new one finished building.  The
            // logs showed internal RAM collapsing to ~17 KB and the WDT firing
            // in lv_obj_allocate_spec_attr()/lv_label_create().  Once the
            // placeholder screen has been loaded, the old screen is no longer
            // active, so free it now while still in LVGL context.
            free_runtime_state_now(old_rt);
            old_rt = NULL;
            hmi_heap_log("after old runtime free before new shell");
        }
    }

    runtime_state_t *new_rt = NULL;
    esp_err_t err = build_runtime_shell_from_model(model, &new_rt);
    if (err != ESP_OK || !new_rt) {
        ESP_LOGE(TAG, "Layout shell build failed: %s", esp_err_to_name(err));
        layout_model_free(model);
        free(json);
        s_reload_async_scheduled = false;
        s_reload_in_progress = false;
        if (old_rt && old_rt->screen) {
            s_active = old_rt;
            old_rt->ready = true;
            lv_screen_load(old_rt->screen);
        }
        runtime_set_busy(false, "shell build failed");
        xSemaphoreGive(s_render_mutex);
        return;
    }

    hmi_heap_log("after new shell before build ctx");
    layout_build_ctx_t *ctx = (layout_build_ctx_t *)hmi_heap_calloc(1, sizeof(layout_build_ctx_t));
    if (!ctx) {
        free_runtime_state_now(new_rt);
        model = NULL;
        free(json);
        s_reload_async_scheduled = false;
        s_reload_in_progress = false;
        if (old_rt && old_rt->screen) {
            s_active = old_rt;
            old_rt->ready = true;
            lv_screen_load(old_rt->screen);
        }
        runtime_set_busy(false, "build ctx alloc failed");
        xSemaphoreGive(s_render_mutex);
        return;
    }

    ctx->model = model;
    ctx->rt = new_rt;
    ctx->old_rt = old_rt;
    ctx->placeholder = placeholder;
    ctx->json_copy = json;
    ctx->request_us = request_us ? request_us : reload_begin_us;
    ctx->model_ready_us = model_ready_us ? model_ready_us : reload_begin_us;
    ctx->queued_us = queued_us ? queued_us : reload_begin_us;
    ctx->reload_begin_us = reload_begin_us;
    ctx->start_us = esp_timer_get_time();

#if HMI_LOG_TIMING_DETAIL
    ESP_LOGI(TAG, "Reload timing: build timer create at %lld us after request",
             (long long)(ctx->start_us - ctx->request_us));
#endif
    ctx->timer = lv_timer_create(layout_build_timer_cb, 5, ctx);
    if (!ctx->timer) {
        ESP_LOGE(TAG, "Layout build timer create failed");
        free_runtime_state_now(new_rt);
        model = NULL;
        free(json);
        s_reload_async_scheduled = false;
        s_reload_in_progress = false;
        if (old_rt && old_rt->screen) {
            s_active = old_rt;
            old_rt->ready = true;
            lv_screen_load(old_rt->screen);
        }
        runtime_set_busy(false, "build timer alloc failed");
        xSemaphoreGive(s_render_mutex);
        return;
    }
    lv_timer_ready(ctx->timer);
    xSemaphoreGive(s_render_mutex);
}

esp_err_t hmi_lvgl_runtime_request_reload_json(const char *json)
{
    if (!json) return ESP_ERR_INVALID_ARG;

    int64_t request_us = esp_timer_get_time();
    size_t json_len = strlen(json);
#if HMI_LOG_TIMING_DETAIL
    ESP_LOGI(TAG, "Reload request received: bytes=%u", (unsigned)json_len);
#endif

    // Phase 1: parse/validate/sanitize into a small fixed-size model outside the
    // LVGL task.  This mirrors LVGL XML's definition-then-instance lifecycle and
    // keeps cJSON/string/schema work from blocking the display timer.
    layout_model_t *model = NULL;
    esp_err_t err = layout_model_from_json(json, &model);
    int64_t model_ready_us = esp_timer_get_time();
    if (err != ESP_OK || !model) {
        ESP_LOGE(TAG, "Reload model prepare failed after %lld us: %s",
                 (long long)(model_ready_us - request_us), esp_err_to_name(err));
        return err != ESP_OK ? err : ESP_FAIL;
    }
#if HMI_LOG_SUMMARY
    ESP_LOGI(TAG, "HMI reload requested: bytes=%u pages=%u widgets=%u parse=%lld ms",
             (unsigned)json_len, (unsigned)model->page_count, (unsigned)model->widget_count,
             (long long)((model_ready_us - request_us) / 1000));
#endif
#if HMI_LOG_TIMING_DETAIL
    ESP_LOGI(TAG, "Reload timing: model prepared in %lld us", (long long)(model_ready_us - request_us));
#endif

    char *copy = hmi_heap_strdup(json);
    if (!copy) {
        layout_model_free(model);
        return ESP_ERR_NO_MEM;
    }

    ensure_render_mutex();
    if (!s_render_mutex || xSemaphoreTake(s_render_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        layout_model_free(model);
        free(copy);
        ESP_LOGW(TAG, "Reload queue failed: renderer busy");
        return ESP_ERR_TIMEOUT;
    }

    if (s_reload_in_progress) {
        xSemaphoreGive(s_render_mutex);
        layout_model_free(model);
        free(copy);
        ESP_LOGW(TAG, "Reload queue failed: staged build already in progress");
        return ESP_ERR_TIMEOUT;
    }

    layout_model_free(s_pending_reload_model);
    free(s_pending_reload_json);
    s_pending_reload_model = model;
    s_pending_reload_json = copy;
    s_pending_reload_request_us = request_us;
    s_pending_reload_model_ready_us = model_ready_us;
    s_pending_reload_queued_us = esp_timer_get_time();
    s_reload_in_progress = true;
    runtime_set_busy(true, "reload queued");
#if HMI_LOG_TIMING_DETAIL
    ESP_LOGI(TAG, "Reload timing: queued after %lld us", (long long)(s_pending_reload_queued_us - request_us));
#endif

    if (!s_reload_async_scheduled) {
        s_reload_async_scheduled = true;
        if (!lvgl_port_lock(pdMS_TO_TICKS(1000))) {
            s_reload_async_scheduled = false;
            s_reload_in_progress = false;
            s_pending_reload_model = NULL;
            s_pending_reload_json = NULL;
            s_pending_reload_request_us = 0;
            s_pending_reload_model_ready_us = 0;
            s_pending_reload_queued_us = 0;
            runtime_set_busy(false, "lv_async lock failed");
            xSemaphoreGive(s_render_mutex);
            layout_model_free(model);
            free(copy);
            ESP_LOGW(TAG, "Reload queue failed: LVGL port lock timeout");
            return ESP_ERR_TIMEOUT;
        }
        lv_async_call(reload_async_cb, NULL);
        lvgl_port_unlock();
    }

#if HMI_LOG_TIMING_DETAIL
    ESP_LOGI(TAG, "Reload queued/coalesced from prepared model, bytes=%u total_queue_time=%lld us",
             (unsigned)strlen(json), (long long)(esp_timer_get_time() - request_us));
#endif
    xSemaphoreGive(s_render_mutex);
    return ESP_OK;
}

esp_err_t hmi_lvgl_runtime_request_reload_current(void)
{
    const char *json = s_last_layout_json ? s_last_layout_json : hmi_layout_default_json();
    return hmi_lvgl_runtime_request_reload_json(json);
}

esp_err_t hmi_lvgl_runtime_get_current_layout_json(char **out_json, const char **out_source)
{
    if (!out_json) return ESP_ERR_INVALID_ARG;
    *out_json = NULL;
    if (out_source) *out_source = "unknown";

    const char *src = NULL;
    const char *source = "unknown";

    ensure_render_mutex();
    bool locked = false;
    if (s_render_mutex && xSemaphoreTake(s_render_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        locked = true;
    }

    // If a browser Apply-to-LCD request has been accepted but has not finished
    // building yet, the pending JSON is the best representation of what the LCD
    // is being changed to. Otherwise, s_last_layout_json is the last successfully
    // activated runtime layout. This endpoint intentionally reports runtime RAM,
    // not the flash-saved copy, so Load from LCD never pulls a stale screen.
    if (s_pending_reload_json) {
        src = s_pending_reload_json;
        source = "lcd runtime pending";
    } else if (s_last_layout_json) {
        src = s_last_layout_json;
        source = "lcd runtime";
    }

    char *copy = src ? hmi_heap_strdup(src) : NULL;
    if (locked) xSemaphoreGive(s_render_mutex);

    if (!copy) {
        char *flash_json = NULL;
        esp_err_t load_err = hmi_layout_store_load(&flash_json);
        if (load_err == ESP_OK && flash_json) {
            copy = hmi_heap_strdup(flash_json);
            free(flash_json);
            source = "device flash fallback";
        }
    }

    if (!copy) {
        copy = hmi_heap_strdup(hmi_layout_default_json());
        source = "embedded default fallback";
    }

    if (!copy) return ESP_ERR_NO_MEM;
    *out_json = copy;
    if (out_source) *out_source = source;
    return ESP_OK;
}

esp_err_t hmi_lvgl_runtime_create(lv_display_t *display)
{
    s_display = display;
    ensure_render_mutex();

    char *json = NULL;
    esp_err_t err = hmi_layout_store_load(&json);
    if (err != ESP_OK || !json) {
        json = hmi_heap_strdup(hmi_layout_default_json());
    }
    if (!json) return ESP_ERR_NO_MEM;

    // IMPORTANT: initial boot rendering must use the same LVGL-safe path as
    // the browser Reload LCD operation. Earlier versions built the first
    // screen directly from app_main using hmi_lvgl_runtime_load_layout().
    // That worked sometimes, but it meant boot and reload used different
    // screen-lifecycle paths. Queue the first layout so the actual object
    // creation/screen load happens from LVGL's timer context, just like
    // live reload.
    err = hmi_lvgl_runtime_request_reload_json(json);
    if (err == ESP_OK) {
#if HMI_LOG_BOOT
        ESP_LOGI(TAG, "Initial HMI layout queued for LVGL-context render");
#endif
    } else {
        ESP_LOGE(TAG, "Initial HMI layout queue failed: %s", esp_err_to_name(err));
    }

    free(json);
    return err;
}


static void page_hide_old_async_cb(void *user_data)
{
    deferred_page_hide_t *hide = (deferred_page_hide_t *)user_data;
    if (!hide) return;

    if (hide->remaining_cycles > 0) {
        hide->remaining_cycles--;
        lv_async_call(page_hide_old_async_cb, hide);
        return;
    }

    ensure_render_mutex();
    if (!s_render_mutex || xSemaphoreTake(s_render_mutex, 0) != pdTRUE) {
        lv_async_call(page_hide_old_async_cb, hide);
        return;
    }

    // Only hide pages if this is still the active runtime.  A full layout
    // reload may have replaced the runtime while this deferred callback was
    // waiting for the display pipeline to advance.
    runtime_state_t *rt = s_active;
    if (rt == hide->rt && rt && rt->page_count > 0 && rt->active_page == hide->keep_index && hide->keep_index >= 0 && hide->keep_index < (int)rt->page_count) {
        for (size_t i = 0; i < rt->page_count; ++i) {
            if (!rt->pages[i].obj || rt->pages[i].obj == rt->screen) continue;
            if ((int)i == hide->keep_index) {
                lv_obj_clear_flag(rt->pages[i].obj, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(rt->pages[i].obj);
            } else {
                lv_obj_add_flag(rt->pages[i].obj, LV_OBJ_FLAG_HIDDEN);
            }
        }
        lv_obj_invalidate(rt->pages[hide->keep_index].obj);
        log_page_visibility(rt, "deferred ScreenShow hide");
    } else {
        ESP_LOGW(TAG, "Deferred page hide skipped: active_rt=%p hide_rt=%p active=%d keep=%d pages=%u",
                 (void *)rt, (void *)hide->rt, rt ? rt->active_page : -1, hide->keep_index,
                 rt ? (unsigned)rt->page_count : 0U);
    }

    xSemaphoreGive(s_render_mutex);
    free(hide);
}

static void hide_all_pages_except(runtime_state_t *rt, int keep_index)
{
    if (!rt || keep_index < 0 || keep_index >= (int)rt->page_count) return;

    for (size_t i = 0; i < rt->page_count; ++i) {
        if (!rt->pages[i].obj || rt->pages[i].obj == rt->screen) continue;
        if ((int)i == keep_index) {
            lv_obj_clear_flag(rt->pages[i].obj, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(rt->pages[i].obj);
        } else {
            lv_obj_add_flag(rt->pages[i].obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
}


static void page_show_async_cb(void *user_data)
{
    (void)user_data;
    ensure_render_mutex();
    if (!s_render_mutex || xSemaphoreTake(s_render_mutex, 0) != pdTRUE) {
        lv_async_call(page_show_async_cb, NULL);
        return;
    }

    char target[64];
    snprintf(target, sizeof(target), "%s", s_pending_page_name);
    s_page_async_scheduled = false;

    runtime_state_t *rt = s_active;
    if (s_reload_in_progress || !s_runtime_input_enabled) {
        ESP_LOGW(TAG, "ScreenShow ignored while runtime busy: %s", target);
        xSemaphoreGive(s_render_mutex);
        return;
    }
    if (!rt || !rt->ready || rt->page_count == 0 || !target[0]) {
        xSemaphoreGive(s_render_mutex);
        return;
    }

    int found = -1;
    for (size_t i = 0; i < rt->page_count; ++i) {
        if (strcasecmp(target, rt->pages[i].id) == 0 || strcasecmp(target, rt->pages[i].name) == 0) {
            found = (int)i;
            break;
        }
    }

    if (found < 0) {
        ESP_LOGW(TAG, "ScreenShow target not found: %s", target);
        xSemaphoreGive(s_render_mutex);
        return;
    }

    if (found == rt->active_page) {
        xSemaphoreGive(s_render_mutex);
        return;
    }

    // v6.2: build the destination page outside the visible panel area, then
    // move it into place and defer-delete the old page.  This keeps the v6.0
    // one-screen/on-demand architecture, but avoids deleting/rebuilding the
    // visible object tree while the RGB panel is scanning it out.  Unlike the
    // v6.1 experiment, this does not call lv_screen_load() during ScreenShow,
    // so it cannot bounce between LVGL screens or expose the display driver's
    // blank transition frame.
    runtime_set_busy(true, "ScreenShow offscreen-page rebuild");
    int old_page = rt->active_page;
    lv_obj_t *old_obj = rt->pages[old_page].obj;

    rt->pages[found].obj = NULL;
    build_page_now_at(rt, found, HMI_SCREEN_W);

    lv_obj_t *new_obj = rt->pages[found].obj;
    if (!new_obj) {
        // Build failed; keep the old page alive and restore active state.
        rt->active_page = old_page;
        runtime_set_busy(false, "ScreenShow build failed");
        ESP_LOGE(TAG, "ScreenShow failed: could not build page %s", target);
        xSemaphoreGive(s_render_mutex);
        return;
    }

    rt->active_page = found;
    lv_obj_move_foreground(new_obj);
    lv_obj_set_pos(new_obj, 0, 0);
    lv_obj_invalidate(new_obj);

    if (old_obj && old_obj != new_obj) {
        // Detach the old page pointer immediately so only the active page is
        // considered live, but delay the actual delete a few LVGL cycles so the
        // display pipeline is no longer using it.
        rt->pages[old_page].obj = NULL;
        lv_obj_add_flag(old_obj, LV_OBJ_FLAG_HIDDEN);
        defer_delete_obj(old_obj, 3);
    }

    runtime_set_busy(false, "ScreenShow complete");

#if HMI_LOG_SUMMARY
    ESP_LOGI(TAG, "ScreenShow: %s -> %s widgets=%u layout_widgets=%u",
             rt->pages[old_page].name, rt->pages[found].name, (unsigned)rt->count,
             rt->model ? (unsigned)rt->model->widget_count : 0U);
    hmi_memory_diag_checkpoint("hmi: ScreenShow complete");
#endif
    log_page_visibility(rt, "ScreenShow offscreen-page rebuild");
    xSemaphoreGive(s_render_mutex);
}

esp_err_t hmi_lvgl_runtime_show_page(const char *page_name_or_id)
{
    if (!page_name_or_id || !page_name_or_id[0]) return ESP_ERR_INVALID_ARG;
    ensure_render_mutex();
    snprintf(s_pending_page_name, sizeof(s_pending_page_name), "%s", page_name_or_id);
    if (!s_page_async_scheduled) {
        s_page_async_scheduled = true;
        if (!lvgl_port_lock(pdMS_TO_TICKS(1000))) {
            s_page_async_scheduled = false;
            ESP_LOGW(TAG, "ScreenShow queue failed: LVGL port lock timeout");
            return ESP_ERR_TIMEOUT;
        }
        lv_async_call(page_show_async_cb, NULL);
        lvgl_port_unlock();
    }
    return ESP_OK;
}


bool hmi_lvgl_runtime_blocking_status_begin(const char *title, const char *message, uint32_t render_ms, const char *reason)
{
    ensure_render_mutex();
    if (s_render_mutex) xSemaphoreTake(s_render_mutex, portMAX_DELAY);
    s_external_busy_depth++;
    runtime_set_busy(true, reason ? reason : "blocking activity");

    if (!lvgl_port_lock(pdMS_TO_TICKS(2000))) {
        ESP_LOGW(TAG, "LVGL blocking status begin failed: port lock timeout");
        if (s_external_busy_depth > 0) s_external_busy_depth--;
        if (s_render_mutex) xSemaphoreGive(s_render_mutex);
        return false;
    }
    blocking_status_update_locked(title ? title : "Working...", message ? message : "Please wait.", false);
    lvgl_port_unlock();
    if (s_render_mutex) xSemaphoreGive(s_render_mutex);

    // Give the normal LVGL task enough time to run lv_timer_handler() and flush
    // the status overlay to the RGB framebuffer before we pause LVGL below.
    if (render_ms < 50) render_ms = 50;
    vTaskDelay(pdMS_TO_TICKS(render_ms));

    // Now hold the LVGL port lock across the long non-LVGL operation. The
    // esp_lvgl_port task wraps lv_timer_handler() with this same lock, so this
    // effectively pauses LVGL rendering/timers without stopping the RGB LCD
    // peripheral's scan-out of the already-rendered framebuffer.
    if (!lvgl_port_lock(portMAX_DELAY)) {
        ESP_LOGW(TAG, "LVGL blocking status begin failed: pause lock failed");
        if (s_render_mutex) xSemaphoreTake(s_render_mutex, portMAX_DELAY);
        if (s_external_busy_depth > 0) s_external_busy_depth--;
        if (s_render_mutex) xSemaphoreGive(s_render_mutex);
        return false;
    }
    s_lvgl_blocking_pause_held = true;

    // v6.6 maintenance-mode compile: while the LVGL port lock is held, turn
    // the backlight off and drop the active HMI page object tree. The RGB LCD
    // peripheral may keep scanning, but with the backlight off the user will
    // not see any PSRAM starvation/offset jitter. Removing the active page also
    // leaves LVGL with only the small blocking-status overlay alive while the
    // compiler runs. The normal HMI page is rebuilt from the retained layout
    // JSON after the compile finishes, exactly like Apply/Load-to-RAM.
    s_blocking_hmi_dropped = false;
    esp_err_t bl_err = pilab_board_set_backlight(false);
    if (bl_err != ESP_OK) {
        ESP_LOGW(TAG, "LCD backlight off failed before blocking activity: %s", esp_err_to_name(bl_err));
    }
    runtime_state_t *rt = s_active;
    if (rt && rt->ready) {
        destroy_active_page_objects(rt);
        s_blocking_hmi_dropped = true;
#if HMI_LOG_RUNTIME_STATE
        ESP_LOGI(TAG, "Blocking status maintenance mode: active HMI page tree dropped%s%s",
                 reason ? " - " : "", reason ? reason : "");
#endif
        hmi_memory_diag_checkpoint("hmi: maintenance page tree dropped");
    }

#if HMI_LOG_RUNTIME_STATE
    ESP_LOGI(TAG, "LVGL timer/rendering paused%s%s", reason ? " - " : "", reason ? reason : "");
#endif
    return true;
}

void hmi_lvgl_runtime_blocking_status_end(const char *title, const char *message, bool is_error, uint32_t hold_ms, const char *reason)
{
    bool should_defer = false;
    uint32_t generation = 0;

    // If begin() reached the pause stage, this task still holds the LVGL
    // port lock. Update the visible result while LVGL is paused, then release
    // the LVGL lock so the result can flush.
    if (s_lvgl_blocking_pause_held) {
        blocking_status_update_locked(title ? title : (is_error ? "Compile error" : "Compile complete"),
                                      message ? message : (is_error ? "Script compile failed." : "Script compile OK."),
                                      is_error);
        s_lvgl_blocking_pause_held = false;
        lvgl_port_unlock();
#if HMI_LOG_RUNTIME_STATE
        ESP_LOGI(TAG, "LVGL timer/rendering resumed%s%s", reason ? " - " : "", reason ? reason : "");
#endif

        // Let LVGL flush the compile result to the framebuffer while the
        // backlight is still off.  v6.14 changes the visible recovery order:
        // if the HMI page tree was dropped, keep the backlight off until the
        // HMI has been rebuilt and the reload has fully settled.  For errors,
        // briefly show the error overlay first, then blank again before the
        // rebuild so the user still sees the AngelScript message.
        vTaskDelay(pdMS_TO_TICKS(120));
        if (s_blocking_hmi_dropped && !is_error) {
            s_backlight_deferred_until_reload_ready = true;
            s_backlight_deferred_error_mode = false;
#if HMI_LOG_BACKLIGHT_DETAIL
            ESP_LOGI(TAG, "LCD backlight remains off until HMI reload settles%s%s",
                     reason ? " - " : "", reason ? reason : "");
#endif
        } else {
            esp_err_t bl_err = pilab_board_set_backlight(true);
            if (bl_err != ESP_OK) {
                ESP_LOGW(TAG, "LCD backlight on failed after blocking activity: %s", esp_err_to_name(bl_err));
            }
            if (s_blocking_hmi_dropped && is_error) {
                s_backlight_deferred_error_mode = true;
            }
        }
    } else {
        if (lvgl_port_lock(pdMS_TO_TICKS(1000))) {
            blocking_status_update_locked(title ? title : (is_error ? "Compile error" : "Compile complete"),
                                          message ? message : (is_error ? "Script compile failed." : "Script compile OK."),
                                          is_error);
            lvgl_port_unlock();
        }
    }

    if (hold_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(hold_ms));
    }

    // If an error was shown to the user, blank the LCD again before deleting
    // the overlay and rebuilding the HMI.  This keeps success and error paths
    // visually stable while still preserving the useful on-screen error text.
    if (s_blocking_hmi_dropped && s_backlight_deferred_error_mode) {
        s_backlight_deferred_until_reload_ready = true;
        s_backlight_deferred_error_mode = false;
        esp_err_t bl_err = pilab_board_set_backlight(false);
        if (bl_err != ESP_OK) {
            ESP_LOGW(TAG, "LCD backlight off failed before deferred HMI rebuild: %s", esp_err_to_name(bl_err));
        } else {
#if HMI_LOG_BACKLIGHT_DETAIL
            ESP_LOGI(TAG, "LCD backlight off before deferred HMI rebuild");
#endif
        }
        vTaskDelay(pdMS_TO_TICKS(80));
    }

    if (lvgl_port_lock(pdMS_TO_TICKS(1000))) {
        blocking_status_delete_locked();
        lvgl_port_unlock();
    }

    bool rebuild_hmi = s_blocking_hmi_dropped;
    s_blocking_hmi_dropped = false;

    if (s_render_mutex) xSemaphoreTake(s_render_mutex, portMAX_DELAY);
    if (s_external_busy_depth > 0) s_external_busy_depth--;
    runtime_state_t *rt = s_active;
    if (rt) generation = rt->generation;
#if HMI_LOG_RUNTIME_STATE
    ESP_LOGI(TAG, "Runtime guard: blocking activity complete depth=%lu%s%s",
             (unsigned long)s_external_busy_depth, reason ? " - " : "", reason ? reason : "");
#endif
    should_defer = (!rebuild_hmi && s_external_busy_depth == 0 && rt && rt->ready && !s_reload_in_progress);
    if (s_render_mutex) xSemaphoreGive(s_render_mutex);

    if (rebuild_hmi) {
        s_backlight_deferred_until_reload_ready = true;
#if HMI_LOG_RUNTIME_STATE
        ESP_LOGI(TAG, "Blocking status maintenance mode: rebuilding HMI layout after %s", reason ? reason : "blocking activity");
#endif
        hmi_memory_diag_checkpoint("hmi: maintenance before HMI rebuild");
        esp_err_t err = hmi_lvgl_runtime_request_reload_current();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "HMI rebuild after blocking activity failed: %s", esp_err_to_name(err));
            if (rt && rt->ready) defer_enable_input(generation, 4);
        }
    } else if (should_defer) {
        defer_enable_input(generation, 4);
    }
}


void hmi_lvgl_runtime_external_activity_begin(const char *reason)
{
    ensure_render_mutex();
    if (s_render_mutex) xSemaphoreTake(s_render_mutex, portMAX_DELAY);
    s_external_busy_depth++;
    runtime_set_busy(true, reason ? reason : "external activity");
    if (s_render_mutex) xSemaphoreGive(s_render_mutex);
}

void hmi_lvgl_runtime_external_activity_end(const char *reason)
{
    ensure_render_mutex();
    uint32_t generation = 0;
    bool should_defer = false;

    if (s_render_mutex) xSemaphoreTake(s_render_mutex, portMAX_DELAY);
    if (s_external_busy_depth > 0) s_external_busy_depth--;

    runtime_state_t *rt = s_active;
    if (rt) generation = rt->generation;

    ESP_LOGI(TAG, "Runtime guard: external activity complete depth=%lu%s%s",
             (unsigned long)s_external_busy_depth, reason ? " - " : "", reason ? reason : "");

    // Keep input/tag redraws paused for several LVGL cycles after heavy PSRAM
    // activity. This gives the RGB panel and LVGL port time to drain any pending
    // refresh work before script-driven tag writes are allowed to repaint.
    should_defer = (s_external_busy_depth == 0 && rt && rt->ready && !s_reload_in_progress);
    if (s_render_mutex) xSemaphoreGive(s_render_mutex);

    if (should_defer) {
        defer_enable_input(generation, 8);
    }
}

bool hmi_lvgl_runtime_wait_until_ready(uint32_t timeout_ms)
{
    int64_t start_us = esp_timer_get_time();
    int64_t timeout_us = (int64_t)timeout_ms * 1000LL;
    while (true) {
        bool ready = false;
        ensure_render_mutex();
        if (s_render_mutex && xSemaphoreTake(s_render_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            runtime_state_t *rt = s_active;
            ready = (rt && rt->ready && !s_reload_in_progress && !s_page_async_scheduled && !s_reload_async_scheduled);
            xSemaphoreGive(s_render_mutex);
        }
        if (ready) return true;
        if ((esp_timer_get_time() - start_us) >= timeout_us) return false;
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}

void hmi_lvgl_runtime_update(void)
{
    // Registry and LVGL object updates must be serialized with screen reloads.
    // Take the render mutex first so the active runtime pointer cannot be
    // swapped while this update cycle is reading object pointers.
    if (s_reload_in_progress || !s_runtime_tag_updates_enabled) {
        return;
    }
    if (s_render_mutex && xSemaphoreTake(s_render_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        return;
    }

    runtime_state_t *rt = s_active;
    if (!rt || !rt->screen || !rt->ready || s_reload_in_progress || !s_runtime_tag_updates_enabled) {
        if (s_render_mutex) xSemaphoreGive(s_render_mutex);
        return;
    }

    if (!lvgl_port_lock(pdMS_TO_TICKS(20))) {
        if (s_render_mutex) xSemaphoreGive(s_render_mutex);
        return;
    }

    for (size_t i = 0; i < rt->count; ++i) {
        runtime_widget_t *w = &rt->widgets[i];
        if (!w->tag[0]) continue;
        // Hidden page widgets do not need live LVGL updates. Tags continue to
        // change in the database; visible widgets refresh when their page is
        // active. This avoids background redraw work as projects gain pages.
        if (rt->page_count > 1 && w->page_index >= 0 && w->page_index != rt->active_page) continue;
        runtime_widget_apply_value(w, false);
    }
    lvgl_port_unlock();
    if (s_render_mutex) xSemaphoreGive(s_render_mutex);
}

size_t hmi_lvgl_runtime_widget_count(void)
{
    runtime_state_t *rt = s_active;
    return rt ? rt->count : 0;
}
