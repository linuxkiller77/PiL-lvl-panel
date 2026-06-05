#include "panel_web_server.h"
#include "pilab_panel_version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "hmi/hmi_layout_store.h"
#include "hmi/hmi_lvgl_runtime.h"
#include "hmi/hmi_tag_db.h"
#include "hmi/hmi_script_engine.h"
#include "hmi/hmi_diag_log.h"
#include "web/panel_web_assets.h"
#include "panel/panel_wifi.h"
#include "mqtt/panel_mqtt.h"

static const char *TAG = "panel_web";
static httpd_handle_t s_server;
static int64_t s_last_reload_us;

static esp_err_t send_json_text(httpd_req_t *req, const char *text)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, text ? text : "{}", HTTPD_RESP_USE_STRLEN);
}

static char *read_body(httpd_req_t *req, size_t max_len)
{
    if (req->content_len == 0 || req->content_len > max_len) return NULL;
    char *buf = (char *)calloc(1, req->content_len + 1);
    if (!buf) return NULL;
    size_t received = 0;
    while (received < req->content_len) {
        int r = httpd_req_recv(req, buf + received, req->content_len - received);
        if (r <= 0) { free(buf); return NULL; }
        received += (size_t)r;
    }
    return buf;
}

static esp_err_t index_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, panel_index_html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t layout_get(httpd_req_t *req)
{
    char *json = NULL;
    esp_err_t err = hmi_layout_store_load(&json);
    if (err != ESP_OK || !json) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "layout load failed");
    }
    esp_err_t send_err = send_json_text(req, json);
    free(json);
    return send_err;
}

static esp_err_t layout_runtime_get(httpd_req_t *req)
{
    char *json = NULL;
    const char *source = NULL;
    esp_err_t err = hmi_lvgl_runtime_get_current_layout_json(&json, &source);
    if (err != ESP_OK || !json) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "runtime layout load failed");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    if (source && source[0]) httpd_resp_set_hdr(req, "X-PiLab-Source", source);
    esp_err_t send_err = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    return send_err;
}

static esp_err_t validate_layout_json(const char *body)
{
    if (!body) return ESP_ERR_INVALID_ARG;
    cJSON *root = cJSON_Parse(body);
    if (!root) return ESP_ERR_INVALID_ARG;
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t layout_post(httpd_req_t *req)
{
    char *body = read_body(req, 96 * 1024);
    if (!body) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body");
    if (validate_layout_json(body) != ESP_OK) {
        free(body);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    }

    // Save to flash only. Do not rebuild the live LVGL screen here.
    // The browser now has an explicit /api/hmi/apply action for runtime RAM.
    esp_err_t err = hmi_layout_store_save(body);
    free(body);
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "layout save failed");
    return send_json_text(req, "{\"ok\":true,\"saved\":true,\"applied\":false}");
}

static esp_err_t layout_apply_post(httpd_req_t *req)
{
    int64_t t0 = esp_timer_get_time();
    char *body = read_body(req, 96 * 1024);
    int64_t t_read = esp_timer_get_time();
    if (!body) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body");
#if !HMI_LOG_HTTP_TIMING
    (void)t0;
    (void)t_read;
#endif
#if HMI_LOG_HTTP_TIMING
    ESP_LOGI(TAG, "HTTP Apply RAM received: bytes=%u read_time=%lld us",
             (unsigned)strlen(body), (long long)(t_read - t0));
#endif
    if (validate_layout_json(body) != ESP_OK) {
        free(body);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    }
    int64_t t_valid = esp_timer_get_time();
#if !HMI_LOG_HTTP_TIMING
    (void)t_valid;
#endif
#if HMI_LOG_HTTP_TIMING
    ESP_LOGI(TAG, "HTTP Apply RAM validated JSON in %lld us", (long long)(t_valid - t_read));
#endif

    // Apply to LVGL runtime RAM only. No SPIFFS write.
    esp_err_t err = hmi_lvgl_runtime_request_reload_json(body);
    int64_t t_queued = esp_timer_get_time();
    free(body);
#if !HMI_LOG_HTTP_TIMING
    (void)t_queued;
#endif
#if HMI_LOG_HTTP_TIMING
    ESP_LOGI(TAG, "HTTP Apply RAM queue result=%s queue_call_time=%lld us total_handler_time=%lld us",
             esp_err_to_name(err), (long long)(t_queued - t_valid), (long long)(t_queued - t0));
#endif
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "apply queue failed");
    return send_json_text(req, "{\"ok\":true,\"queued\":true,\"saved\":false}");
}


static esp_err_t reload_post(httpd_req_t *req)
{
    int64_t now = esp_timer_get_time();
    if (s_last_reload_us && (now - s_last_reload_us) < 750000) {
        return send_json_text(req, "{\"ok\":true,\"skipped\":true,\"reason\":\"reload debounce\"}");
    }
    s_last_reload_us = now;

    // Use the runtime's last in-RAM layout copy. Do not read SPIFFS during a
    // live Reload LCD request; flash/cache activity and RGB scanout are a bad
    // combination on this panel.
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = hmi_lvgl_runtime_request_reload_current();
#if !HMI_LOG_HTTP_TIMING
    (void)t0;
#endif
#if HMI_LOG_HTTP_TIMING
    ESP_LOGI(TAG, "HTTP Reload LCD queue result=%s handler_time=%lld us",
             esp_err_to_name(err), (long long)(esp_timer_get_time() - t0));
#endif
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "reload queue failed");
    return send_json_text(req, "{\"ok\":true,\"queued\":true}");
}



static esp_err_t tags_get(httpd_req_t *req)
{
    cJSON *root = hmi_tag_db_to_json();
    cJSON_AddNumberToObject(root, "generation", hmi_tag_db_generation());
    cJSON_AddStringToObject(root, "source", "lcd runtime");
    cJSON_AddBoolToObject(root, "ok", true);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!out) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "tag json failed");
    esp_err_t err = send_json_text(req, out);
    free(out);
    return err;
}

static esp_err_t tags_post(httpd_req_t *req)
{
    char *body = read_body(req, 32 * 1024);
    if (!body) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body");
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");

    esp_err_t err = hmi_tag_db_replace_from_json(root, false);
    cJSON_Delete(root);
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "tag registry apply failed");

    cJSON *reply = cJSON_CreateObject();
    cJSON_AddBoolToObject(reply, "ok", true);
    cJSON_AddBoolToObject(reply, "saved", false);
    cJSON_AddBoolToObject(reply, "applied", true);
    cJSON_AddStringToObject(reply, "target", "ram");
    cJSON_AddStringToObject(reply, "mode", "replace");
    cJSON_AddNumberToObject(reply, "generation", hmi_tag_db_generation());
    cJSON_AddNumberToObject(reply, "count", (double)hmi_tag_db_count());
    char *out = cJSON_PrintUnformatted(reply);
    cJSON_Delete(reply);
    if (!out) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "tag reply json failed");
    esp_err_t send_err = send_json_text(req, out);
    free(out);
    return send_err;
}

static esp_err_t tags_save_post(httpd_req_t *req)
{
    char *body = read_body(req, 32 * 1024);
    if (!body) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body");
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");

    esp_err_t err = hmi_tag_db_replace_from_json(root, true);
    cJSON_Delete(root);
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "tag registry flash save failed");

    cJSON *reply = cJSON_CreateObject();
    cJSON_AddBoolToObject(reply, "ok", true);
    cJSON_AddBoolToObject(reply, "saved", true);
    cJSON_AddBoolToObject(reply, "applied", true);
    cJSON_AddStringToObject(reply, "target", "flash");
    cJSON_AddStringToObject(reply, "mode", "replace");
    cJSON_AddNumberToObject(reply, "generation", hmi_tag_db_generation());
    cJSON_AddNumberToObject(reply, "count", (double)hmi_tag_db_count());
    char *out = cJSON_PrintUnformatted(reply);
    cJSON_Delete(reply);
    if (!out) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "tag reply json failed");
    esp_err_t send_err = send_json_text(req, out);
    free(out);
    return send_err;
}

static void add_script_runtime_stats(cJSON *root);

static esp_err_t tag_write_post(httpd_req_t *req)
{
    char *body = read_body(req, 4096);
    if (!body) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body");
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    const char *name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "name"));
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(root, "value");
    esp_err_t err = hmi_tag_db_write_from_json(name, value);
    cJSON_Delete(root);
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "write failed");
    return send_json_text(req, "{\"ok\":true}");
}


static esp_err_t script_get(httpd_req_t *req)
{
    char *script = NULL;
    esp_err_t err = hmi_script_engine_load_script(&script);
    if (err != ESP_OK || !script) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "script load failed");
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "script", script);
    cJSON_AddStringToObject(root, "status", hmi_script_engine_last_error());
    cJSON_AddStringToObject(root, "compile_state", hmi_script_engine_compile_state());
    cJSON_AddBoolToObject(root, "runtime_valid", hmi_script_engine_runtime_valid());
    cJSON_AddNumberToObject(root, "compile_generation", hmi_script_engine_compile_generation());
    cJSON_AddStringToObject(root, "active", hmi_script_engine_active_script_name());
    cJSON_AddStringToObject(root, "source", hmi_script_engine_active_script_name());
    add_script_runtime_stats(root);
    cJSON_AddBoolToObject(root, "ok", true);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(script);
    if (!out) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "script json failed");
    err = send_json_text(req, out);
    free(out);
    return err;
}

static esp_err_t script_runtime_get(httpd_req_t *req)
{
    char *script = NULL;
    const char *source = NULL;
    esp_err_t err = hmi_script_engine_get_runtime_script(&script, &source);
    if (err != ESP_OK || !script) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "runtime script load failed");
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "script", script);
    cJSON_AddStringToObject(root, "status", hmi_script_engine_last_error());
    cJSON_AddStringToObject(root, "compile_state", hmi_script_engine_compile_state());
    cJSON_AddBoolToObject(root, "runtime_valid", hmi_script_engine_runtime_valid());
    cJSON_AddNumberToObject(root, "compile_generation", hmi_script_engine_compile_generation());
    cJSON_AddStringToObject(root, "active", hmi_script_engine_active_script_name());
    cJSON_AddStringToObject(root, "source", source ? source : "runtime RAM");
    add_script_runtime_stats(root);
    cJSON_AddBoolToObject(root, "ok", true);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(script);
    if (!out) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "runtime script json failed");
    err = send_json_text(req, out);
    free(out);
    return err;
}

static const char *json_string_field(cJSON *root, const char *name)
{
    return cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, name));
}


static void add_script_runtime_stats(cJSON *root)
{
    hmi_script_engine_stats_t st;
    hmi_script_engine_get_stats(&st);
    cJSON *script = cJSON_CreateObject();
    if (!script) return;
    cJSON_AddBoolToObject(script, "engine_created", st.engine_created);
    cJSON_AddBoolToObject(script, "module_active", st.module_active);
    cJSON_AddBoolToObject(script, "runtime_valid", st.runtime_valid);
    cJSON_AddBoolToObject(script, "compile_task_active", st.compile_task_active);
    cJSON_AddBoolToObject(script, "compile_worker_running", st.compile_worker_running);
    cJSON_AddBoolToObject(script, "event_queue_created", st.event_queue_created);
    cJSON_AddBoolToObject(script, "event_task_running", st.event_task_running);
    cJSON_AddNumberToObject(script, "compile_generation", st.compile_generation);
    cJSON_AddNumberToObject(script, "engine_create_count", st.engine_create_count);
    cJSON_AddNumberToObject(script, "module_count", st.module_count);
    cJSON_AddNumberToObject(script, "compile_ok_count", st.compile_ok_count);
    cJSON_AddNumberToObject(script, "compile_fail_count", st.compile_fail_count);
    cJSON_AddNumberToObject(script, "old_module_discard_count", st.old_module_discard_count);
    cJSON_AddNumberToObject(script, "event_queue_waiting", st.event_queue_waiting);
    cJSON_AddNumberToObject(script, "event_task_stack_high_water", st.event_task_stack_high_water);
    cJSON_AddNumberToObject(script, "compile_worker_stack_high_water", st.compile_worker_stack_high_water);
    cJSON_AddBoolToObject(script, "panel_tick_pending", st.panel_tick_pending);
    cJSON_AddBoolToObject(script, "panel_tick_missing", st.panel_tick_missing);
    cJSON_AddNumberToObject(script, "panel_tick_posted_count", st.panel_tick_posted_count);
    cJSON_AddNumberToObject(script, "panel_tick_skipped_count", st.panel_tick_skipped_count);
    cJSON_AddNumberToObject(script, "panel_tick_executed_count", st.panel_tick_executed_count);
    cJSON_AddNumberToObject(script, "panel_tick_missing_count", st.panel_tick_missing_count);
    cJSON_AddNumberToObject(script, "panel_tick_queue_full_count", st.panel_tick_queue_full_count);
    cJSON_AddStringToObject(script, "compile_state", st.compile_state ? st.compile_state : "unknown");
    cJSON_AddStringToObject(script, "active_module", st.active_module_name ? st.active_module_name : "");
    cJSON_AddStringToObject(script, "active_script", st.active_script_name ? st.active_script_name : "");
    cJSON_AddItemToObject(root, "script_runtime", script);
}


static esp_err_t script_save_post(httpd_req_t *req)
{
    char *body = read_body(req, 128 * 1024);
    if (!body) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body");
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    const char *script = json_string_field(root, "script");
    esp_err_t err = script ? hmi_script_engine_save_script(script) : ESP_ERR_INVALID_ARG;
    cJSON_Delete(root);
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "script save failed");
    return send_json_text(req, "{\"ok\":true}");
}

static esp_err_t script_compile_post(httpd_req_t *req)
{
    char *body = read_body(req, 128 * 1024);
    if (!body) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body");
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    const char *script = json_string_field(root, "script");
    esp_err_t err = script ? hmi_script_engine_compile_async(script) : ESP_ERR_INVALID_ARG;
    cJSON_Delete(root);
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "script compile queue failed");
    cJSON *reply = cJSON_CreateObject();
    cJSON_AddBoolToObject(reply, "ok", true);
    cJSON_AddStringToObject(reply, "status", "compile queued");
    cJSON_AddStringToObject(reply, "compile_state", hmi_script_engine_compile_state());
    cJSON_AddNumberToObject(reply, "compile_generation", hmi_script_engine_compile_generation());
    char *out = cJSON_PrintUnformatted(reply);
    cJSON_Delete(reply);
    if (!out) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "compile reply json failed");
    esp_err_t send_err = send_json_text(req, out);
    free(out);
    return send_err;
}

static esp_err_t script_call_post(httpd_req_t *req)
{
    char *body = read_body(req, 4096);
    if (!body) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body");
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    const char *function = json_string_field(root, "function");
    esp_err_t err = function ? hmi_script_engine_post_call(function) : ESP_ERR_INVALID_ARG;
    cJSON_Delete(root);
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "script call failed");
    return send_json_text(req, "{\"ok\":true}");
}

static esp_err_t script_status_get(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", hmi_script_engine_last_error());
    cJSON_AddStringToObject(root, "compile_state", hmi_script_engine_compile_state());
    cJSON_AddBoolToObject(root, "runtime_valid", hmi_script_engine_runtime_valid());
    cJSON_AddNumberToObject(root, "compile_generation", hmi_script_engine_compile_generation());
    cJSON_AddStringToObject(root, "active", hmi_script_engine_active_script_name());
    cJSON_AddNumberToObject(root, "free_internal", heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(root, "free_psram", heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(root, "largest_internal", heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(root, "largest_psram", heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    add_script_runtime_stats(root);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!out) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "script status json failed");
    esp_err_t err = send_json_text(req, out);
    free(out);
    return err;
}


static esp_err_t wifi_status_get(httpd_req_t *req)
{
    char *json = NULL;
    esp_err_t err = panel_wifi_status_json(&json);
    if (err != ESP_OK || !json) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "wifi status failed");
    }
    esp_err_t send_err = send_json_text(req, json);
    free(json);
    return send_err;
}

static esp_err_t wifi_scan_get(httpd_req_t *req)
{
    char *json = NULL;
    esp_err_t err = panel_wifi_scan_json(&json);
    if (err != ESP_OK || !json) {
        ESP_LOGW(TAG, "WiFi scan failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "wifi scan failed");
    }
    esp_err_t send_err = send_json_text(req, json);
    free(json);
    return send_err;
}

static esp_err_t wifi_connect_post(httpd_req_t *req)
{
    char *body = read_body(req, 1024);
    if (!body) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body");
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    const char *ssid = json_string_field(root, "ssid");
    const char *password = json_string_field(root, "password");
    const char *hostname = json_string_field(root, "hostname");
    esp_err_t err = panel_wifi_connect_and_save(ssid, password ? password : "", hostname ? hostname : "");
    cJSON_Delete(root);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi connect/save failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "wifi connect/save failed");
    }
    return send_json_text(req, "{\"ok\":true,\"saved\":true,\"connecting\":true}");
}

static esp_err_t wifi_hostname_post(httpd_req_t *req)
{
    char *body = read_body(req, 512);
    if (!body) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body");
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    const char *hostname = json_string_field(root, "hostname");
    esp_err_t err = panel_wifi_set_hostname(hostname ? hostname : "");
    cJSON_Delete(root);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi hostname save failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "hostname save failed");
    }
    return send_json_text(req, "{\"ok\":true,\"saved\":true}");
}

static esp_err_t wifi_forget_post(httpd_req_t *req)
{
    (void)req;
    esp_err_t err = panel_wifi_forget_sta();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi forget failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "wifi forget failed");
    }
    return send_json_text(req, "{\"ok\":true,\"forgot\":true,\"mode\":\"ap\"}");
}


static esp_err_t mqtt_config_get(httpd_req_t *req)
{
    char *json = NULL;
    esp_err_t err = panel_mqtt_get_config_json(&json);
    if (err != ESP_OK || !json) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "mqtt config load failed");
    }
    esp_err_t send_err = send_json_text(req, json);
    free(json);
    return send_err;
}

static esp_err_t mqtt_config_post(httpd_req_t *req)
{
    char *body = read_body(req, 24 * 1024);
    if (!body) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body");
    esp_err_t err = panel_mqtt_save_config_json(body);
    free(body);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "MQTT config save failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "mqtt config save failed");
    }
    return send_json_text(req, "{\"ok\":true,\"saved\":true}");
}

static esp_err_t mqtt_status_get(httpd_req_t *req)
{
    char *json = NULL;
    esp_err_t err = panel_mqtt_get_status_json(&json);
    if (err != ESP_OK || !json) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "mqtt status failed");
    }
    esp_err_t send_err = send_json_text(req, json);
    free(json);
    return send_err;
}

static esp_err_t mqtt_start_post(httpd_req_t *req)
{
    (void)req;
    esp_err_t err = panel_mqtt_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "MQTT start failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "mqtt start failed");
    }
    return send_json_text(req, "{\"ok\":true,\"started\":true}");
}

static esp_err_t mqtt_stop_post(httpd_req_t *req)
{
    (void)req;
    esp_err_t err = panel_mqtt_stop();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "MQTT stop failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "mqtt stop failed");
    }
    return send_json_text(req, "{\"ok\":true,\"started\":false}");
}

static esp_err_t mqtt_test_publish_post(httpd_req_t *req)
{
    char *body = read_body(req, 2048);
    if (!body) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body");
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    const char *topic = json_string_field(root, "topic");
    const char *payload = json_string_field(root, "payload");
    const cJSON *qos_j = cJSON_GetObjectItemCaseSensitive(root, "qos");
    const cJSON *retain_j = cJSON_GetObjectItemCaseSensitive(root, "retain");
    int qos = cJSON_IsNumber(qos_j) ? (int)qos_j->valuedouble : 0;
    bool retain = cJSON_IsBool(retain_j) ? cJSON_IsTrue(retain_j) : false;
    esp_err_t err = panel_mqtt_test_publish(topic, payload ? payload : "", qos, retain);
    cJSON_Delete(root);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "MQTT test publish failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "mqtt test publish failed");
    }
    return send_json_text(req, "{\"ok\":true,\"published\":true}");
}

static esp_err_t device_status_get(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", "PiLab_Panel");
    cJSON_AddStringToObject(root, "firmware_version", PILAB_PANEL_VERSION);
    cJSON_AddStringToObject(root, "build_note", PILAB_PANEL_BUILD_NOTE);
    cJSON_AddNumberToObject(root, "uptime_ms", (double)(esp_timer_get_time() / 1000));
    cJSON_AddNumberToObject(root, "free_internal", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(root, "free_psram", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    cJSON_AddNumberToObject(root, "tags", hmi_tag_db_count());
    cJSON_AddNumberToObject(root, "widgets", hmi_lvgl_runtime_widget_count());
    cJSON_AddStringToObject(root, "hmi_runtime_state", hmi_lvgl_runtime_state_name());
    cJSON_AddStringToObject(root, "script", hmi_script_engine_active_script_name());
    cJSON_AddStringToObject(root, "script_status", hmi_script_engine_last_error());
    cJSON_AddStringToObject(root, "script_compile_state", hmi_script_engine_compile_state());
    cJSON_AddBoolToObject(root, "script_runtime_valid", hmi_script_engine_runtime_valid());
    add_script_runtime_stats(root);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!out) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "status json failed");
    esp_err_t err = send_json_text(req, out);
    free(out);
    return err;
}

static void register_uri(const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *))
{
    httpd_uri_t cfg = { .uri = uri, .method = method, .handler = handler, .user_ctx = NULL };
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &cfg));
}

esp_err_t panel_web_server_start(void)
{
    if (s_server) return ESP_OK;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 8192;
    config.max_uri_handlers = 40;
    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), TAG, "http server start failed");
    register_uri("/", HTTP_GET, index_get);
    register_uri("/api/hmi/layout", HTTP_GET, layout_get);
    register_uri("/api/hmi/runtime", HTTP_GET, layout_runtime_get);
    register_uri("/api/hmi/layout", HTTP_POST, layout_post);
    register_uri("/api/hmi/apply", HTTP_POST, layout_apply_post);
    register_uri("/api/hmi/reload", HTTP_POST, reload_post);
    register_uri("/api/tags", HTTP_GET, tags_get);
    register_uri("/api/tags", HTTP_POST, tags_post);       // Apply tag registry to RAM only
    register_uri("/api/tags/save", HTTP_POST, tags_save_post); // Apply and persist tag registry to SPIFFS
    register_uri("/api/tags/write", HTTP_POST, tag_write_post);
    register_uri("/api/script", HTTP_GET, script_get);
    register_uri("/api/script/runtime", HTTP_GET, script_runtime_get);
    register_uri("/api/script", HTTP_POST, script_save_post);
    register_uri("/api/script/compile", HTTP_POST, script_compile_post);
    register_uri("/api/script/call", HTTP_POST, script_call_post);
    register_uri("/api/script/status", HTTP_GET, script_status_get);
    register_uri("/api/device/status", HTTP_GET, device_status_get);
    register_uri("/api/wifi/status", HTTP_GET, wifi_status_get);
    register_uri("/api/wifi/scan", HTTP_GET, wifi_scan_get);
    register_uri("/api/wifi/connect", HTTP_POST, wifi_connect_post);
    register_uri("/api/wifi/hostname", HTTP_POST, wifi_hostname_post);
    register_uri("/api/wifi/forget", HTTP_POST, wifi_forget_post);
    register_uri("/api/mqtt/config", HTTP_GET, mqtt_config_get);
    register_uri("/api/mqtt/config", HTTP_POST, mqtt_config_post);
    register_uri("/api/mqtt/status", HTTP_GET, mqtt_status_get);
    register_uri("/api/mqtt/start", HTTP_POST, mqtt_start_post);
    register_uri("/api/mqtt/stop", HTTP_POST, mqtt_stop_post);
    register_uri("/api/mqtt/test_publish", HTTP_POST, mqtt_test_publish_post);
    ESP_LOGI(TAG, "HTTP server started");
    return ESP_OK;
}
