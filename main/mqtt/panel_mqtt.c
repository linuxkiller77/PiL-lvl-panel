#include "panel_mqtt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hmi/hmi_tag_db.h"
#include "panel/panel_wifi.h"

#define MQTT_CONFIG_PATH "/spiffs/mqtt_config.json"
#define MQTT_CONFIG_TMP_PATH "/spiffs/mqtt_config.tmp"
#define MQTT_CONFIG_MAX_BYTES (24 * 1024)
#define MQTT_TICK_MS 100

static const char *TAG = "panel_mqtt";

typedef struct {
    char name[PANEL_MQTT_NAME_MAX];
    bool enabled;
    bool auto_connect;
    char host[PANEL_MQTT_TEXT_MAX];
    uint16_t port;
    char username[PANEL_MQTT_TEXT_MAX];
    char password[PANEL_MQTT_TEXT_MAX];
    char client_id[PANEL_MQTT_TEXT_MAX];
    char base_topic[PANEL_MQTT_TOPIC_MAX];
    bool use_tls;
} panel_mqtt_server_t;

typedef struct {
    bool enabled;
    uint8_t server;
    char tag[PANEL_MQTT_TAG_MAX];
    char topic[PANEL_MQTT_TOPIC_MAX];
    uint32_t rate_ms;
    int qos;
    bool retain;
    uint32_t remaining_ms;
} panel_mqtt_pub_t;

typedef struct {
    bool enabled;
    uint8_t server;
    char tag[PANEL_MQTT_TAG_MAX];
    char topic[PANEL_MQTT_TOPIC_MAX];
    int qos;
} panel_mqtt_sub_t;

typedef struct {
    bool enabled;
    uint8_t active_server;
    panel_mqtt_server_t servers[PANEL_MQTT_MAX_SERVERS];
    size_t publish_count;
    panel_mqtt_pub_t publish[PANEL_MQTT_MAX_PUBLISH];
    size_t subscribe_count;
    panel_mqtt_sub_t subscribe[PANEL_MQTT_MAX_SUBSCRIBE];
} panel_mqtt_config_t;

typedef struct {
    panel_mqtt_config_t cfg;
    bool started;
    bool connected;
    int64_t last_connect_us;
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t err_count;
    char last_error[128];
    char last_rx_topic[PANEL_MQTT_TOPIC_MAX];
    char last_rx_payload[PANEL_MQTT_TEXT_MAX];
} panel_mqtt_state_t;

static panel_mqtt_state_t *s_state;
#define s_cfg (s_state->cfg)
#define s_started (s_state->started)
#define s_connected (s_state->connected)
#define s_last_connect_us (s_state->last_connect_us)
#define s_tx_count (s_state->tx_count)
#define s_rx_count (s_state->rx_count)
#define s_err_count (s_state->err_count)
#define s_last_error (s_state->last_error)
#define s_last_rx_topic (s_state->last_rx_topic)
#define s_last_rx_payload (s_state->last_rx_payload)

static SemaphoreHandle_t s_mutex;
static esp_mqtt_client_handle_t s_client;
static TaskHandle_t s_task;
static TaskHandle_t s_autostart_task;
static bool s_task_running;

static esp_err_t ensure_state_allocated(void)
{
    if (s_state) return ESP_OK;
    s_state = heap_caps_calloc(1, sizeof(panel_mqtt_state_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_state) {
        ESP_LOGE(TAG, "failed to allocate MQTT state in PSRAM (%u bytes)", (unsigned)sizeof(panel_mqtt_state_t));
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "MQTT state allocated in PSRAM: %u bytes", (unsigned)sizeof(panel_mqtt_state_t));
    return ESP_OK;
}

static void copy_str(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) return;
    snprintf(dst, dst_size, "%s", src ? src : "");
}

static void trim_in_place(char *s)
{
    if (!s) return;
    char *start = s;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') ++start;
    if (start != s) memmove(s, start, strlen(start) + 1);
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n' || s[len - 1] == '/')) {
        s[--len] = '\0';
    }
}

static void sanitize_host_and_port(panel_mqtt_server_t *srv)
{
    if (!srv) return;
    trim_in_place(srv->host);

    const char *scheme = strstr(srv->host, "://");
    if (scheme) {
        char tmp[PANEL_MQTT_TEXT_MAX];
        copy_str(tmp, sizeof(tmp), scheme + 3);
        copy_str(srv->host, sizeof(srv->host), tmp);
    }

    char *slash = strchr(srv->host, '/');
    if (slash) *slash = '\0';

    char *colon = strrchr(srv->host, ':');
    if (colon && colon != srv->host) {
        int parsed_port = atoi(colon + 1);
        if (parsed_port > 0 && parsed_port <= 65535) {
            srv->port = (uint16_t)parsed_port;
        }
        *colon = '\0';
    }

    trim_in_place(srv->host);
}

static const char *json_str(const cJSON *obj, const char *name, const char *fallback)
{
    const char *s = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(obj, name));
    return s ? s : fallback;
}


static char *print_json_psram(cJSON *root, size_t max_bytes)
{
    if (!root || max_bytes == 0) return NULL;
    char *json = heap_caps_malloc(max_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!json) return NULL;
    memset(json, 0, max_bytes);
    if (!cJSON_PrintPreallocated(root, json, (int)max_bytes, false)) {
        heap_caps_free(json);
        return NULL;
    }
    return json;
}

static bool json_bool(const cJSON *obj, const char *name, bool fallback)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, name);
    if (!v) return fallback;
    if (cJSON_IsBool(v)) return cJSON_IsTrue(v);
    if (cJSON_IsNumber(v)) return v->valuedouble != 0.0;
    if (cJSON_IsString(v)) {
        const char *s = cJSON_GetStringValue(v);
        return s && !(strcasecmp(s, "false") == 0 || strcmp(s, "0") == 0 || strcasecmp(s, "off") == 0 || strcasecmp(s, "no") == 0 || *s == '\0');
    }
    return fallback;
}

static int json_int(const cJSON *obj, const char *name, int fallback)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, name);
    if (cJSON_IsNumber(v)) return (int)v->valuedouble;
    if (cJSON_IsString(v)) return atoi(cJSON_GetStringValue(v));
    return fallback;
}

static void default_config_locked(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.enabled = false;
    s_cfg.active_server = 0;

    panel_mqtt_server_t *srv = &s_cfg.servers[0];
    copy_str(srv->name, sizeof(srv->name), "Local Broker");
    srv->enabled = true;
    srv->auto_connect = false;
    copy_str(srv->host, sizeof(srv->host), "192.168.1.50");
    srv->port = 1883;
    copy_str(srv->client_id, sizeof(srv->client_id), "pilab-panel");
    copy_str(srv->base_topic, sizeof(srv->base_topic), "pilab/panel/pilab-panel");
    srv->use_tls = false;

    s_cfg.publish_count = 0;
    s_cfg.subscribe_count = 0;
    copy_str(s_cfg.subscribe[0].tag, sizeof(s_cfg.subscribe[0].tag), "Run_Command");
    copy_str(s_cfg.subscribe[0].topic, sizeof(s_cfg.subscribe[0].topic), "tag/Run_Command/set");
}

static bool auto_start_wanted_locked(void)
{
    if (!s_cfg.enabled) return false;
    if (s_cfg.active_server >= PANEL_MQTT_MAX_SERVERS) return false;
    const panel_mqtt_server_t *srv = &s_cfg.servers[s_cfg.active_server];
    return srv->enabled && srv->auto_connect && srv->host[0];
}

static void add_server_json(cJSON *arr, const panel_mqtt_server_t *srv)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "name", srv->name);
    cJSON_AddBoolToObject(j, "enabled", srv->enabled);
    cJSON_AddBoolToObject(j, "autoConnect", srv->auto_connect);
    cJSON_AddStringToObject(j, "host", srv->host);
    cJSON_AddNumberToObject(j, "port", srv->port ? srv->port : 1883);
    cJSON_AddBoolToObject(j, "useTls", srv->use_tls);
    cJSON_AddStringToObject(j, "username", srv->username);
    cJSON_AddStringToObject(j, "password", srv->password);
    cJSON_AddStringToObject(j, "clientId", srv->client_id);
    cJSON_AddStringToObject(j, "baseTopic", srv->base_topic);
    cJSON_AddItemToArray(arr, j);
}

static void add_publish_json(cJSON *arr, const panel_mqtt_pub_t *p)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddBoolToObject(j, "enabled", p->enabled);
    cJSON_AddNumberToObject(j, "server", p->server);
    cJSON_AddStringToObject(j, "tag", p->tag);
    cJSON_AddStringToObject(j, "topic", p->topic);
    cJSON_AddNumberToObject(j, "rateMs", p->rate_ms ? p->rate_ms : 1000);
    cJSON_AddNumberToObject(j, "qos", p->qos);
    cJSON_AddBoolToObject(j, "retain", p->retain);
    cJSON_AddItemToArray(arr, j);
}

static void add_subscribe_json(cJSON *arr, const panel_mqtt_sub_t *s)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddBoolToObject(j, "enabled", s->enabled);
    cJSON_AddNumberToObject(j, "server", s->server);
    cJSON_AddStringToObject(j, "tag", s->tag);
    cJSON_AddStringToObject(j, "topic", s->topic);
    cJSON_AddNumberToObject(j, "qos", s->qos);
    cJSON_AddItemToArray(arr, j);
}

static cJSON *config_to_json_locked(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddBoolToObject(root, "enabled", s_cfg.enabled);
    cJSON_AddNumberToObject(root, "activeServer", s_cfg.active_server);
    cJSON *servers = cJSON_AddArrayToObject(root, "servers");
    for (int i = 0; i < PANEL_MQTT_MAX_SERVERS; ++i) add_server_json(servers, &s_cfg.servers[i]);
    cJSON *pub = cJSON_AddArrayToObject(root, "publish");
    for (size_t i = 0; i < s_cfg.publish_count; ++i) add_publish_json(pub, &s_cfg.publish[i]);
    cJSON *sub = cJSON_AddArrayToObject(root, "subscribe");
    for (size_t i = 0; i < s_cfg.subscribe_count; ++i) add_subscribe_json(sub, &s_cfg.subscribe[i]);
    return root;
}

static void parse_server(panel_mqtt_server_t *srv, const cJSON *j, int index)
{
    char fallback_name[24];
    snprintf(fallback_name, sizeof(fallback_name), "Broker %d", index + 1);
    copy_str(srv->name, sizeof(srv->name), json_str(j, "name", fallback_name));
    srv->enabled = json_bool(j, "enabled", index == 0);
    srv->auto_connect = json_bool(j, "autoConnect", index == 0);
    copy_str(srv->host, sizeof(srv->host), json_str(j, "host", index == 0 ? "192.168.1.50" : ""));
    int port = json_int(j, "port", 1883);
    srv->port = (port > 0 && port <= 65535) ? (uint16_t)port : 1883;
    srv->use_tls = json_bool(j, "useTls", false);
    copy_str(srv->username, sizeof(srv->username), json_str(j, "username", ""));
    copy_str(srv->password, sizeof(srv->password), json_str(j, "password", ""));
    copy_str(srv->client_id, sizeof(srv->client_id), json_str(j, "clientId", "pilab-panel"));
    copy_str(srv->base_topic, sizeof(srv->base_topic), json_str(j, "baseTopic", "pilab/panel/pilab-panel"));
    trim_in_place(srv->base_topic);
    sanitize_host_and_port(srv);
}

static uint32_t normalize_rate_ms(int rate)
{
    const uint32_t allowed[] = {100, 250, 500, 1000, 5000, 10000, 30000, 60000};
    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); ++i) if ((uint32_t)rate == allowed[i]) return allowed[i];
    if (rate <= 100) return 100;
    if (rate <= 250) return 250;
    if (rate <= 500) return 500;
    if (rate <= 1000) return 1000;
    if (rate <= 5000) return 5000;
    if (rate <= 10000) return 10000;
    if (rate <= 30000) return 30000;
    return 60000;
}

static esp_err_t parse_config_json_locked(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return ESP_ERR_INVALID_ARG;

    default_config_locked();
    s_cfg.enabled = json_bool(root, "enabled", false);
    int active = json_int(root, "activeServer", 0);
    s_cfg.active_server = (active >= 0 && active < PANEL_MQTT_MAX_SERVERS) ? (uint8_t)active : 0;

    const cJSON *servers = cJSON_GetObjectItemCaseSensitive(root, "servers");
    if (cJSON_IsArray(servers)) {
        const cJSON *item = NULL;
        int i = 0;
        cJSON_ArrayForEach(item, servers) {
            if (i >= PANEL_MQTT_MAX_SERVERS) break;
            if (cJSON_IsObject(item)) parse_server(&s_cfg.servers[i], item, i);
            ++i;
        }
    }

    s_cfg.publish_count = 0;
    const cJSON *pub = cJSON_GetObjectItemCaseSensitive(root, "publish");
    if (cJSON_IsArray(pub)) {
        const cJSON *item = NULL;
        cJSON_ArrayForEach(item, pub) {
            if (s_cfg.publish_count >= PANEL_MQTT_MAX_PUBLISH) break;
            if (!cJSON_IsObject(item)) continue;
            panel_mqtt_pub_t *p = &s_cfg.publish[s_cfg.publish_count++];
            memset(p, 0, sizeof(*p));
            p->enabled = json_bool(item, "enabled", true);
            int server = json_int(item, "server", 0);
            p->server = (server >= 0 && server < PANEL_MQTT_MAX_SERVERS) ? (uint8_t)server : 0;
            copy_str(p->tag, sizeof(p->tag), json_str(item, "tag", ""));
            copy_str(p->topic, sizeof(p->topic), json_str(item, "topic", ""));
            p->rate_ms = normalize_rate_ms(json_int(item, "rateMs", 1000));
            p->qos = json_int(item, "qos", 0);
            if (p->qos < 0 || p->qos > 1) p->qos = 0;
            p->retain = json_bool(item, "retain", false);
            p->remaining_ms = p->rate_ms;
        }
    }

    s_cfg.subscribe_count = 0;
    const cJSON *sub = cJSON_GetObjectItemCaseSensitive(root, "subscribe");
    if (cJSON_IsArray(sub)) {
        const cJSON *item = NULL;
        cJSON_ArrayForEach(item, sub) {
            if (s_cfg.subscribe_count >= PANEL_MQTT_MAX_SUBSCRIBE) break;
            if (!cJSON_IsObject(item)) continue;
            panel_mqtt_sub_t *s = &s_cfg.subscribe[s_cfg.subscribe_count++];
            memset(s, 0, sizeof(*s));
            s->enabled = json_bool(item, "enabled", true);
            int server = json_int(item, "server", 0);
            s->server = (server >= 0 && server < PANEL_MQTT_MAX_SERVERS) ? (uint8_t)server : 0;
            copy_str(s->tag, sizeof(s->tag), json_str(item, "tag", ""));
            copy_str(s->topic, sizeof(s->topic), json_str(item, "topic", ""));
            s->qos = json_int(item, "qos", 0);
            if (s->qos < 0 || s->qos > 1) s->qos = 0;
        }
    }

    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t save_config_locked(void)
{
    cJSON *root = config_to_json_locked();
    if (!root) return ESP_ERR_NO_MEM;
    char *json = print_json_psram(root, MQTT_CONFIG_MAX_BYTES);
    cJSON_Delete(root);
    if (!json) return ESP_ERR_NO_MEM;
    FILE *f = fopen(MQTT_CONFIG_TMP_PATH, "wb");
    if (!f) { heap_caps_free(json); return ESP_FAIL; }
    size_t len = strlen(json);
    size_t n = fwrite(json, 1, len, f);
    int close_rc = fclose(f);
    heap_caps_free(json);
    if (n != len || close_rc != 0) {
        remove(MQTT_CONFIG_TMP_PATH);
        return ESP_FAIL;
    }
    remove(MQTT_CONFIG_PATH);
    if (rename(MQTT_CONFIG_TMP_PATH, MQTT_CONFIG_PATH) != 0) {
        remove(MQTT_CONFIG_TMP_PATH);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t load_config_locked(void)
{
    FILE *f = fopen(MQTT_CONFIG_PATH, "rb");
    if (!f) {
        default_config_locked();
        return save_config_locked();
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    if (len <= 0 || len > MQTT_CONFIG_MAX_BYTES) { fclose(f); return ESP_ERR_INVALID_SIZE; }
    char *buf = heap_caps_calloc(1, (size_t)len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) { fclose(f); return ESP_ERR_NO_MEM; }
    size_t n = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if (n != (size_t)len) { heap_caps_free(buf); return ESP_FAIL; }
    esp_err_t err = parse_config_json_locked(buf);
    heap_caps_free(buf);
    return err;
}

static void build_topic(const panel_mqtt_server_t *srv, const char *topic, char *out, size_t out_size)
{
    if (!topic || !*topic) {
        copy_str(out, out_size, "");
        return;
    }
    if (strstr(topic, "/") == topic || strstr(topic, srv->base_topic) == topic || strchr(topic, '/') == topic) {
        copy_str(out, out_size, topic[0] == '/' ? topic + 1 : topic);
        return;
    }
    if (strchr(topic, '/') && (strncmp(topic, "tag/", 4) != 0 && strncmp(topic, "event/", 6) != 0 && strncmp(topic, "status", 6) != 0)) {
        copy_str(out, out_size, topic);
        return;
    }
    if (srv->base_topic[0]) snprintf(out, out_size, "%s/%s", srv->base_topic, topic);
    else copy_str(out, out_size, topic);
}

static void subscribe_configured_topics_locked(void)
{
    if (!s_client) return;
    const uint8_t active = s_cfg.active_server;
    const panel_mqtt_server_t *srv = &s_cfg.servers[active];
    for (size_t i = 0; i < s_cfg.subscribe_count; ++i) {
        const panel_mqtt_sub_t *s = &s_cfg.subscribe[i];
        // v6.45.8: mappings are global to the active broker. The UI currently
        // connects one active broker at a time and does not expose per-row
        // server selection, so do not filter by the legacy row.server field.
        if (!s->enabled || !s->tag[0] || !s->topic[0]) continue;
        char topic[PANEL_MQTT_TOPIC_MAX * 2];
        build_topic(srv, s->topic, topic, sizeof(topic));
        if (topic[0]) {
            esp_mqtt_client_subscribe(s_client, topic, s->qos);
            ESP_LOGI(TAG, "subscribed %s -> tag %s", topic, s->tag);
        }
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    if (event_id == MQTT_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "MQTT connected");
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_connected = true;
        s_last_connect_us = esp_timer_get_time();
        copy_str(s_last_error, sizeof(s_last_error), "connected");
        subscribe_configured_topics_locked();
        panel_mqtt_server_t srv = s_cfg.servers[s_cfg.active_server];
        xSemaphoreGive(s_mutex);
        char topic[PANEL_MQTT_TOPIC_MAX * 2];
        build_topic(&srv, "online", topic, sizeof(topic));
        esp_mqtt_client_publish(s_client, topic, "true", 0, 0, 1);
    } else if (event_id == MQTT_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "MQTT disconnected");
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_connected = false;
        copy_str(s_last_error, sizeof(s_last_error), "disconnected");
        xSemaphoreGive(s_mutex);
    } else if (event_id == MQTT_EVENT_DATA) {
        char topic[PANEL_MQTT_TOPIC_MAX * 2];
        char payload[PANEL_MQTT_TEXT_MAX];
        size_t topic_len = event->topic_len < (int)sizeof(topic) - 1 ? (size_t)event->topic_len : sizeof(topic) - 1;
        size_t data_len = event->data_len < (int)sizeof(payload) - 1 ? (size_t)event->data_len : sizeof(payload) - 1;
        memcpy(topic, event->topic, topic_len);
        topic[topic_len] = '\0';
        memcpy(payload, event->data, data_len);
        payload[data_len] = '\0';

        char matched_tag[PANEL_MQTT_TAG_MAX] = {0};

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        ++s_rx_count;
        copy_str(s_last_rx_topic, sizeof(s_last_rx_topic), topic);
        copy_str(s_last_rx_payload, sizeof(s_last_rx_payload), payload);
        uint8_t active = s_cfg.active_server;
        panel_mqtt_server_t srv = s_cfg.servers[active];

        // Do not copy the entire subscribe table onto the MQTT event task stack.
        // The first MQTT build copied ~7 KB here, which could overflow/corrupt
        // the MQTT client task stack and later show up as TLSF/LWIP heap faults.
        for (size_t i = 0; i < s_cfg.subscribe_count; ++i) {
            const panel_mqtt_sub_t *sub = &s_cfg.subscribe[i];
            // Mappings are evaluated against the active broker/base topic.
            if (!sub->enabled || !sub->tag[0] || !sub->topic[0]) continue;
            char full[PANEL_MQTT_TOPIC_MAX * 2];
            build_topic(&srv, sub->topic, full, sizeof(full));
            if (strcmp(full, topic) == 0) {
                copy_str(matched_tag, sizeof(matched_tag), sub->tag);
                break;
            }
        }
        xSemaphoreGive(s_mutex);

        if (matched_tag[0]) {
            esp_err_t err = hmi_tag_db_write_from_string(matched_tag, payload, strlen(payload));
            if (err != ESP_OK) {
                xSemaphoreTake(s_mutex, portMAX_DELAY);
                ++s_err_count;
                snprintf(s_last_error, sizeof(s_last_error), "tag write failed: %s", esp_err_to_name(err));
                xSemaphoreGive(s_mutex);
            }
        }
    } else if (event_id == MQTT_EVENT_ERROR) {
        ESP_LOGW(TAG, "MQTT event error");
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        ++s_err_count;
        copy_str(s_last_error, sizeof(s_last_error), "mqtt event error - check broker/port/network");
        xSemaphoreGive(s_mutex);
    }
}

static esp_err_t client_stop_locked(void)
{
    if (s_client) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    s_started = false;
    s_connected = false;
    return ESP_OK;
}

static esp_err_t client_start_locked(void)
{
    if (!s_cfg.enabled) {
        copy_str(s_last_error, sizeof(s_last_error), "MQTT disabled - enable MQTT and save config");
        ESP_LOGW(TAG, "start requested but MQTT is disabled");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_client) return ESP_OK;
    if (s_cfg.active_server >= PANEL_MQTT_MAX_SERVERS) s_cfg.active_server = 0;
    panel_mqtt_server_t *srv = &s_cfg.servers[s_cfg.active_server];
    sanitize_host_and_port(srv);
    if (!srv->enabled || !srv->host[0]) {
        copy_str(s_last_error, sizeof(s_last_error), "no active enabled broker/host");
        ESP_LOGW(TAG, "start requested but active broker is not enabled or has no host");
        return ESP_ERR_INVALID_STATE;
    }
    if (!panel_wifi_sta_is_connected()) {
        copy_str(s_last_error, sizeof(s_last_error), "wait for WiFi STA IP before MQTT start");
        ESP_LOGW(TAG, "start requested before STA has an IP address; MQTT will not start yet");
        return ESP_ERR_INVALID_STATE;
    }

    char uri[192];
    int uri_len = snprintf(uri, sizeof(uri), "%s://%s:%u", srv->use_tls ? "mqtts" : "mqtt", srv->host, (unsigned)(srv->port ? srv->port : (srv->use_tls ? 8883 : 1883)));
    if (uri_len < 0 || uri_len >= (int)sizeof(uri)) {
        copy_str(s_last_error, sizeof(s_last_error), "MQTT broker URI too long");
        return ESP_ERR_INVALID_ARG;
    }
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = uri,
        .credentials.client_id = srv->client_id[0] ? srv->client_id : NULL,
        .credentials.username = srv->username[0] ? srv->username : NULL,
        .credentials.authentication.password = srv->password[0] ? srv->password : NULL,
        .task.stack_size = 8192,
        .task.priority = 5,
        .buffer.size = 1024,
        .buffer.out_size = 1024,
    };

    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) {
        copy_str(s_last_error, sizeof(s_last_error), "esp_mqtt_client_init failed");
        return ESP_ERR_NO_MEM;
    }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        snprintf(s_last_error, sizeof(s_last_error), "start failed: %s", esp_err_to_name(err));
        return err;
    }
    s_started = true;
    copy_str(s_last_error, sizeof(s_last_error), "connecting");
    ESP_LOGI(TAG, "connecting to %s client_id=%s", uri, srv->client_id[0] ? srv->client_id : "<auto>");
    return ESP_OK;
}

static void mqtt_tick_task(void *arg)
{
    (void)arg;
    while (true) {
        bool connected = false;
        uint8_t active = 0;
        panel_mqtt_server_t srv = {0};
        esp_mqtt_client_handle_t client = NULL;

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        connected = s_connected && s_client != NULL;
        active = s_cfg.active_server;
        srv = s_cfg.servers[active];
        client = s_client;

        // Keep the MQTT scheduler stack small. The first version copied the
        // entire publish table onto the task stack every 100ms, which wasted
        // scarce internal RAM. This version handles one expired row at a time.
        for (size_t i = 0; connected && i < s_cfg.publish_count; ++i) {
            panel_mqtt_pub_t *p = &s_cfg.publish[i];
            // Mappings are evaluated against the active broker/base topic.
            if (!p->enabled || !p->tag[0] || !p->topic[0]) continue;
            if (p->remaining_ms > MQTT_TICK_MS) {
                p->remaining_ms -= MQTT_TICK_MS;
                continue;
            }

            panel_mqtt_pub_t pub = *p;
            p->remaining_ms = p->rate_ms ? p->rate_ms : 1000;
            xSemaphoreGive(s_mutex);

            char payload[128];
            char topic[PANEL_MQTT_TOPIC_MAX * 2];
            if (hmi_tag_db_read_value_string(pub.tag, payload, sizeof(payload), NULL) == ESP_OK) {
                build_topic(&srv, pub.topic, topic, sizeof(topic));
                if (topic[0] && client) {
                    int msg_id = esp_mqtt_client_publish(client, topic, payload, 0, pub.qos, pub.retain ? 1 : 0);
                    xSemaphoreTake(s_mutex, portMAX_DELAY);
                    if (msg_id >= 0) ++s_tx_count;
                    else {
                        ++s_err_count;
                        copy_str(s_last_error, sizeof(s_last_error), "publish failed");
                    }
                    xSemaphoreGive(s_mutex);
                }
            }

            xSemaphoreTake(s_mutex, portMAX_DELAY);
            connected = s_connected && s_client != NULL;
            active = s_cfg.active_server;
            srv = s_cfg.servers[active];
            client = s_client;
        }
        xSemaphoreGive(s_mutex);
        vTaskDelay(pdMS_TO_TICKS(MQTT_TICK_MS));
    }
}

static void mqtt_autostart_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "MQTT auto-start task waiting for WiFi STA IP");

    // Give WiFi/DHCP/DNS time to settle. MQTT previously tried DNS during AP+STA
    // bring-up and could destabilize the network stack. This task only starts the
    // client when the saved config explicitly requests autoConnect and STA has IP.
    for (int attempt = 0; attempt < 120; ++attempt) { // about 60 seconds
        bool wanted = false;
        bool already_started = false;

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        wanted = auto_start_wanted_locked();
        already_started = s_started || s_client != NULL;
        xSemaphoreGive(s_mutex);

        if (!wanted) {
            ESP_LOGI(TAG, "MQTT auto-start cancelled: config no longer requests autoConnect");
            break;
        }
        if (already_started) {
            ESP_LOGI(TAG, "MQTT auto-start skipped: client already started");
            break;
        }

        if (panel_wifi_sta_is_connected()) {
            ESP_LOGI(TAG, "MQTT auto-start: WiFi STA has IP, starting client");
            esp_err_t err = panel_mqtt_start();
            if (err == ESP_OK) {
                break;
            }
            ESP_LOGW(TAG, "MQTT auto-start attempt failed: %s", esp_err_to_name(err));
            if (err != ESP_ERR_INVALID_STATE) {
                // Avoid tight retry loops on parser/allocation errors. The user can
                // fix config and press Start manually from the MQTT page.
                break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (!s_started && auto_start_wanted_locked()) {
        copy_str(s_last_error, sizeof(s_last_error), "auto-start waiting timed out; press Start");
    }
    xSemaphoreGive(s_mutex);

    s_autostart_task = NULL;
    vTaskDelete(NULL);
}

static void maybe_start_autostart_task_locked(void)
{
    if (s_autostart_task || !auto_start_wanted_locked()) return;
    BaseType_t ok = xTaskCreatePinnedToCore(mqtt_autostart_task, "mqtt_autostart", 3072, NULL, 3, &s_autostart_task, 0);
    if (ok != pdPASS) {
        copy_str(s_last_error, sizeof(s_last_error), "MQTT auto-start task alloc failed");
        ESP_LOGW(TAG, "failed to create MQTT auto-start task");
    }
}

esp_err_t panel_mqtt_init(void)
{
    esp_err_t state_err = ensure_state_allocated();
    if (state_err != ESP_OK) return state_err;
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t err = load_config_locked();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "MQTT config load failed, using defaults: %s", esp_err_to_name(err));
        default_config_locked();
        save_config_locked();
    }
    xSemaphoreGive(s_mutex);
    if (!s_task) {
        // Stack remains internal RAM on this ESP-IDF config, so keep it modest.
        if (xTaskCreatePinnedToCore(mqtt_tick_task, "panel_mqtt", 4096, NULL, 4, &s_task, 0) != pdPASS) return ESP_ERR_NO_MEM;
        s_task_running = true;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    maybe_start_autostart_task_locked();
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t panel_mqtt_start(void)
{
    if (!s_state || !s_mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t err = client_start_locked();
    xSemaphoreGive(s_mutex);
    return err;
}

esp_err_t panel_mqtt_stop(void)
{
    if (!s_state || !s_mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t err = client_stop_locked();
    xSemaphoreGive(s_mutex);
    return err;
}

esp_err_t panel_mqtt_reload_config(void)
{
    if (!s_state || !s_mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    client_stop_locked();
    esp_err_t err = load_config_locked();
    copy_str(s_last_error, sizeof(s_last_error), "config reloaded; press Start to connect");
    xSemaphoreGive(s_mutex);
    return err;
}

esp_err_t panel_mqtt_save_config_json(const char *json)
{
    if (!s_state || !s_mutex || !json) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    client_stop_locked();
    esp_err_t err = parse_config_json_locked(json);
    if (err == ESP_OK) {
        for (size_t i = 0; i < PANEL_MQTT_MAX_SERVERS; ++i) sanitize_host_and_port(&s_cfg.servers[i]);
        err = save_config_locked();
    }
    if (err == ESP_OK) {
        copy_str(s_last_error, sizeof(s_last_error), auto_start_wanted_locked() ? "config saved; auto-start will run after reboot" : "config saved; press Start to connect");
    }
    xSemaphoreGive(s_mutex);
    return err;
}

esp_err_t panel_mqtt_get_config_json(char **out_json)
{
    if (!out_json || !s_state || !s_mutex) return ESP_ERR_INVALID_ARG;
    *out_json = NULL;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    cJSON *root = config_to_json_locked();
    xSemaphoreGive(s_mutex);
    if (!root) return ESP_ERR_NO_MEM;
    *out_json = print_json_psram(root, MQTT_CONFIG_MAX_BYTES);
    cJSON_Delete(root);
    return *out_json ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t panel_mqtt_get_status_json(char **out_json)
{
    if (!out_json || !s_state || !s_mutex) return ESP_ERR_INVALID_ARG;
    *out_json = NULL;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddBoolToObject(root, "mqttStateInPsram", true);
    cJSON_AddNumberToObject(root, "mqttStateBytes", sizeof(panel_mqtt_state_t));
    cJSON_AddBoolToObject(root, "enabled", s_cfg.enabled);
    cJSON_AddBoolToObject(root, "started", s_started);
    cJSON_AddBoolToObject(root, "connected", s_connected);
    cJSON_AddNumberToObject(root, "activeServer", s_cfg.active_server);
    cJSON_AddStringToObject(root, "broker", s_cfg.servers[s_cfg.active_server].host);
    cJSON_AddNumberToObject(root, "port", s_cfg.servers[s_cfg.active_server].port);
    cJSON_AddStringToObject(root, "baseTopic", s_cfg.servers[s_cfg.active_server].base_topic);
    cJSON_AddNumberToObject(root, "publishCount", s_cfg.publish_count);
    cJSON_AddNumberToObject(root, "subscribeCount", s_cfg.subscribe_count);
    cJSON_AddNumberToObject(root, "txCount", s_tx_count);
    cJSON_AddNumberToObject(root, "rxCount", s_rx_count);
    cJSON_AddNumberToObject(root, "errorCount", s_err_count);
    cJSON_AddStringToObject(root, "lastError", s_last_error);
    cJSON_AddStringToObject(root, "lastRxTopic", s_last_rx_topic);
    cJSON_AddStringToObject(root, "lastRxPayload", s_last_rx_payload);
    cJSON_AddNumberToObject(root, "connectedMs", s_last_connect_us && s_connected ? (double)((esp_timer_get_time() - s_last_connect_us) / 1000) : 0);
    xSemaphoreGive(s_mutex);
    if (!root) return ESP_ERR_NO_MEM;
    *out_json = print_json_psram(root, 4096);
    cJSON_Delete(root);
    return *out_json ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t panel_mqtt_test_publish(const char *topic, const char *payload, int qos, bool retain)
{
    if (!s_state || !s_mutex || !topic || !topic[0]) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (!s_client || !s_connected) {
        copy_str(s_last_error, sizeof(s_last_error), s_started ? "not connected yet" : "MQTT not started");
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    panel_mqtt_server_t srv = s_cfg.servers[s_cfg.active_server];
    esp_mqtt_client_handle_t client = s_client;
    xSemaphoreGive(s_mutex);

    char full[PANEL_MQTT_TOPIC_MAX * 2];
    build_topic(&srv, topic, full, sizeof(full));
    int msg_id = esp_mqtt_client_publish(client, full, payload ? payload : "", 0, qos < 0 || qos > 1 ? 0 : qos, retain ? 1 : 0);
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (msg_id >= 0) ++s_tx_count; else ++s_err_count;
    xSemaphoreGive(s_mutex);
    return msg_id >= 0 ? ESP_OK : ESP_FAIL;
}
