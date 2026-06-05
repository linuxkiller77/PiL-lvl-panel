#include "panel_wifi.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "nvs.h"
#include "lwip/ip4_addr.h"

#define PANEL_WIFI_AP_SSID "PiLab-Panel"
#define PANEL_WIFI_AP_PASS "pilabpanel"
#define PANEL_WIFI_AP_CHANNEL 6
#define PANEL_WIFI_AP_MAX_STA 4

#define PANEL_WIFI_NVS_NS "wifi_cfg"
#define PANEL_WIFI_NVS_SSID "ssid"
#define PANEL_WIFI_NVS_PASS "pass"
#define PANEL_WIFI_NVS_HOST "host"
#define PANEL_WIFI_DEFAULT_HOSTNAME "pilab-panel"

static const char *TAG = "panel_wifi";

static bool s_started = false;
static bool s_saved_sta = false;
static bool s_sta_connected = false;
static bool s_sta_connecting = false;
static char s_sta_ssid[33] = {0};
static char s_sta_ip[16] = {0};
static char s_hostname[33] = PANEL_WIFI_DEFAULT_HOSTNAME;
static bool s_mdns_started = false;
static esp_netif_t *s_ap_netif = NULL;
static esp_netif_t *s_sta_netif = NULL;

esp_err_t panel_wifi_set_hostname(const char *hostname);

static esp_err_t configure_ap_netif_ip(void)
{
    if (!s_ap_netif) {
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * esp_netif_create_default_wifi_ap() creates the AP netif with the DHCP
     * server already running on the default 192.168.4.1 address.  ESP-IDF
     * requires DHCP to be stopped before changing the AP IP address.
     */
    esp_err_t err = esp_netif_dhcps_stop(s_ap_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGW(TAG, "AP DHCP stop before IP config failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_netif_ip_info_t ip_info = {0};
    IP4_ADDR(&ip_info.ip, 192, 168, 50, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 50, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);

    err = esp_netif_set_ip_info(s_ap_netif, &ip_info);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "AP IP config failed: %s", esp_err_to_name(err));
        esp_netif_dhcps_start(s_ap_netif);
        return err;
    }

    err = esp_netif_dhcps_start(s_ap_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
        ESP_LOGW(TAG, "AP DHCP restart after IP config failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "AP netif configured for 192.168.50.1/24");
    return ESP_OK;
}


static const char *authmode_to_string(wifi_auth_mode_t mode)
{
    switch (mode) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WEP: return "wep";
    case WIFI_AUTH_WPA_PSK: return "wpa";
    case WIFI_AUTH_WPA2_PSK: return "wpa2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "wpa/wpa2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "wpa2-enterprise";
    case WIFI_AUTH_WPA3_PSK: return "wpa3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "wpa2/wpa3";
    case WIFI_AUTH_WAPI_PSK: return "wapi";
    default: return "unknown";
    }
}


static bool is_valid_hostname_char(char c)
{
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           c == '-';
}

static void sanitize_hostname(const char *in, char *out, size_t out_len)
{
    if (!out || out_len == 0) return;
    out[0] = 0;
    if (!in || !in[0]) {
        snprintf(out, out_len, "%s", PANEL_WIFI_DEFAULT_HOSTNAME);
        return;
    }

    size_t pos = 0;
    bool last_dash = false;
    for (const char *p = in; *p && pos + 1 < out_len; ++p) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c == '_' || c == ' ' || c == '.') c = '-';
        if (!is_valid_hostname_char(c)) continue;
        if (c == '-') {
            if (pos == 0 || last_dash) continue;
            last_dash = true;
        } else {
            last_dash = false;
        }
        out[pos++] = c;
    }
    while (pos > 0 && out[pos - 1] == '-') pos--;
    out[pos] = 0;
    if (out[0] == 0) snprintf(out, out_len, "%s", PANEL_WIFI_DEFAULT_HOSTNAME);
}

static esp_err_t load_saved_hostname(char *host, size_t host_len)
{
    if (!host || host_len == 0) return ESP_ERR_INVALID_ARG;
    snprintf(host, host_len, "%s", PANEL_WIFI_DEFAULT_HOSTNAME);

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(PANEL_WIFI_NVS_NS, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    ESP_RETURN_ON_ERROR(err, TAG, "open wifi nvs failed");

    char raw[64] = {0};
    size_t len = sizeof(raw);
    err = nvs_get_str(nvs, PANEL_WIFI_NVS_HOST, raw, &len);
    nvs_close(nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    ESP_RETURN_ON_ERROR(err, TAG, "read hostname failed");
    sanitize_hostname(raw, host, host_len);
    return ESP_OK;
}

static esp_err_t save_hostname(const char *hostname)
{
    char clean[33];
    sanitize_hostname(hostname, clean, sizeof(clean));
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(PANEL_WIFI_NVS_NS, NVS_READWRITE, &nvs), TAG, "open wifi nvs failed");
    esp_err_t err = nvs_set_str(nvs, PANEL_WIFI_NVS_HOST, clean);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err == ESP_OK) snprintf(s_hostname, sizeof(s_hostname), "%s", clean);
    return err;
}

static esp_err_t apply_hostname_to_netifs(void)
{
    if (s_sta_netif) esp_netif_set_hostname(s_sta_netif, s_hostname);
    if (s_ap_netif) esp_netif_set_hostname(s_ap_netif, s_hostname);
    return ESP_OK;
}

static esp_err_t panel_mdns_start_or_update(void)
{
    if (!s_mdns_started) {
        esp_err_t err = mdns_init();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
        s_mdns_started = true;
        mdns_instance_name_set("PiLab Panel");
        mdns_service_add("PiLab Panel", "_http", "_tcp", 80, NULL, 0);
        mdns_service_txt_item_set("_http", "_tcp", "app", "PiLab Panel");
        mdns_service_txt_item_set("_http", "_tcp", "board", "waveshare-s3-7inch");
    }
    ESP_RETURN_ON_ERROR(mdns_hostname_set(s_hostname), TAG, "mDNS hostname set failed");
    ESP_LOGI(TAG, "mDNS active: http://%s.local", s_hostname);
    return ESP_OK;
}

static esp_err_t load_saved_sta(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    if (!ssid || !pass) return ESP_ERR_INVALID_ARG;
    ssid[0] = 0;
    pass[0] = 0;

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(PANEL_WIFI_NVS_NS, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_ERR_NOT_FOUND;
    ESP_RETURN_ON_ERROR(err, TAG, "open wifi nvs failed");

    size_t len = ssid_len;
    err = nvs_get_str(nvs, PANEL_WIFI_NVS_SSID, ssid, &len);
    if (err == ESP_OK) {
        len = pass_len;
        esp_err_t pass_err = nvs_get_str(nvs, PANEL_WIFI_NVS_PASS, pass, &len);
        if (pass_err == ESP_ERR_NVS_NOT_FOUND) pass[0] = 0;
        else if (pass_err != ESP_OK) err = pass_err;
    }
    nvs_close(nvs);

    if (err != ESP_OK) return err;
    if (ssid[0] == 0) return ESP_ERR_NOT_FOUND;
    return ESP_OK;
}

static esp_err_t save_sta_credentials(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0]) return ESP_ERR_INVALID_ARG;
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(PANEL_WIFI_NVS_NS, NVS_READWRITE, &nvs), TAG, "open wifi nvs failed");
    esp_err_t err = nvs_set_str(nvs, PANEL_WIFI_NVS_SSID, ssid);
    if (err == ESP_OK) err = nvs_set_str(nvs, PANEL_WIFI_NVS_PASS, pass ? pass : "");
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

static esp_err_t erase_sta_credentials(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(PANEL_WIFI_NVS_NS, NVS_READWRITE, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    ESP_RETURN_ON_ERROR(err, TAG, "open wifi nvs failed");
    esp_err_t e1 = nvs_erase_key(nvs, PANEL_WIFI_NVS_SSID);
    esp_err_t e2 = nvs_erase_key(nvs, PANEL_WIFI_NVS_PASS);
    (void)e1;
    (void)e2;
    err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            if (s_saved_sta) {
                s_sta_connecting = true;
                esp_wifi_connect();
                ESP_LOGI(TAG, "Connecting to saved WiFi network: %s", s_sta_ssid);
            }
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
            s_sta_connected = false;
            s_sta_connecting = s_saved_sta;
            s_sta_ip[0] = 0;
            ESP_LOGW(TAG, "STA disconnected from %s reason=%d", s_sta_ssid, disc ? disc->reason : -1);
            if (s_saved_sta) {
                esp_wifi_connect();
            }
        } else if (event_id == WIFI_EVENT_AP_STACONNECTED) {
            wifi_event_ap_staconnected_t *ev = (wifi_event_ap_staconnected_t *)event_data;
            ESP_LOGI(TAG, "AP client joined aid=%d", ev ? ev->aid : -1);
        } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
            wifi_event_ap_stadisconnected_t *ev = (wifi_event_ap_stadisconnected_t *)event_data;
            ESP_LOGI(TAG, "AP client left aid=%d", ev ? ev->aid : -1);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_sta_connected = true;
        s_sta_connecting = false;
        if (event) {
            snprintf(s_sta_ip, sizeof(s_sta_ip), IPSTR, IP2STR(&event->ip_info.ip));
        }
        ESP_LOGI(TAG, "STA connected: ssid=%s ip=%s", s_sta_ssid, s_sta_ip[0] ? s_sta_ip : "?");
    }
}

static esp_err_t apply_ap_config(void)
{
    wifi_config_t ap = {0};
    snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "%s", PANEL_WIFI_AP_SSID);
    ap.ap.ssid_len = strlen(PANEL_WIFI_AP_SSID);
    snprintf((char *)ap.ap.password, sizeof(ap.ap.password), "%s", PANEL_WIFI_AP_PASS);
    ap.ap.channel = PANEL_WIFI_AP_CHANNEL;
    ap.ap.max_connection = PANEL_WIFI_AP_MAX_STA;
    ap.ap.authmode = strlen(PANEL_WIFI_AP_PASS) ? WIFI_AUTH_WPA_WPA2_PSK : WIFI_AUTH_OPEN;
    ap.ap.pmf_cfg.required = false;
    return esp_wifi_set_config(WIFI_IF_AP, &ap);
}

static esp_err_t apply_sta_config(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0]) return ESP_ERR_INVALID_ARG;
    wifi_config_t sta = {0};
    snprintf((char *)sta.sta.ssid, sizeof(sta.sta.ssid), "%s", ssid);
    snprintf((char *)sta.sta.password, sizeof(sta.sta.password), "%s", pass ? pass : "");
    sta.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    sta.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    return esp_wifi_set_config(WIFI_IF_STA, &sta);
}

esp_err_t panel_wifi_start(void)
{
    if (s_started) return ESP_OK;

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    s_ap_netif = esp_netif_create_default_wifi_ap();
    esp_err_t ap_ip_err = configure_ap_netif_ip();
    if (ap_ip_err != ESP_OK) {
        ESP_LOGW(TAG, "AP IP config failed, continuing with ESP-IDF default AP IP: %s", esp_err_to_name(ap_ip_err));
    }
    s_sta_netif = esp_netif_create_default_wifi_sta();

    ESP_RETURN_ON_ERROR(load_saved_hostname(s_hostname, sizeof(s_hostname)), TAG, "load hostname failed");
    ESP_RETURN_ON_ERROR(apply_hostname_to_netifs(), TAG, "apply hostname failed");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL), TAG, "wifi event handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL), TAG, "ip event handler failed");

    char pass[65] = {0};
    esp_err_t load_err = load_saved_sta(s_sta_ssid, sizeof(s_sta_ssid), pass, sizeof(pass));
    s_saved_sta = (load_err == ESP_OK && s_sta_ssid[0]);

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "wifi mode failed");
    ESP_RETURN_ON_ERROR(apply_ap_config(), TAG, "AP config failed");
    if (s_saved_sta) {
        ESP_RETURN_ON_ERROR(apply_sta_config(s_sta_ssid, pass), TAG, "STA config failed");
    }
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");
    ESP_RETURN_ON_ERROR(panel_mdns_start_or_update(), TAG, "mDNS start failed");

    s_started = true;
    ESP_LOGI(TAG, "WiFi AP ready. SSID=%s password=%s url=http://192.168.50.1 mdns=http://%s.local", PANEL_WIFI_AP_SSID, PANEL_WIFI_AP_PASS, s_hostname);
    if (s_saved_sta) ESP_LOGI(TAG, "Saved STA network enabled: %s", s_sta_ssid);
    else ESP_LOGI(TAG, "No saved STA network; panel is in AP/recovery mode");
    return ESP_OK;
}

esp_err_t panel_wifi_start_ap(void)
{
    return panel_wifi_start();
}

esp_err_t panel_wifi_connect_and_save(const char *ssid, const char *password, const char *hostname)
{
    if (!ssid || !ssid[0] || strlen(ssid) > 32) return ESP_ERR_INVALID_ARG;
    if (password && strlen(password) > 64) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(panel_wifi_start(), TAG, "wifi not started");
    if (hostname && hostname[0]) {
        ESP_RETURN_ON_ERROR(panel_wifi_set_hostname(hostname), TAG, "save hostname failed");
    }
    ESP_RETURN_ON_ERROR(save_sta_credentials(ssid, password ? password : ""), TAG, "save sta credentials failed");

    snprintf(s_sta_ssid, sizeof(s_sta_ssid), "%s", ssid);
    s_saved_sta = true;
    s_sta_connected = false;
    s_sta_connecting = true;
    s_sta_ip[0] = 0;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "wifi mode APSTA failed");
    ESP_RETURN_ON_ERROR(apply_ap_config(), TAG, "AP reconfig failed");
    ESP_RETURN_ON_ERROR(apply_sta_config(ssid, password ? password : ""), TAG, "STA config failed");
    esp_wifi_disconnect();
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "STA connect failed");
    ESP_LOGI(TAG, "Saved WiFi STA credentials and started connection: %s", s_sta_ssid);
    return ESP_OK;
}


esp_err_t panel_wifi_set_hostname(const char *hostname)
{
    ESP_RETURN_ON_ERROR(save_hostname(hostname), TAG, "save hostname failed");
    apply_hostname_to_netifs();
    if (s_mdns_started) {
        ESP_RETURN_ON_ERROR(panel_mdns_start_or_update(), TAG, "mDNS hostname update failed");
    } else if (s_started) {
        ESP_RETURN_ON_ERROR(panel_mdns_start_or_update(), TAG, "mDNS hostname start failed");
    }
    ESP_LOGI(TAG, "Device hostname saved: %s.local", s_hostname);
    return ESP_OK;
}

esp_err_t panel_wifi_forget_sta(void)
{
    ESP_RETURN_ON_ERROR(panel_wifi_start(), TAG, "wifi not started");
    ESP_RETURN_ON_ERROR(erase_sta_credentials(), TAG, "erase sta credentials failed");
    s_saved_sta = false;
    s_sta_connected = false;
    s_sta_connecting = false;
    s_sta_ssid[0] = 0;
    s_sta_ip[0] = 0;
    esp_wifi_disconnect();
    // APSTA is left enabled so scanning from the configuration dialog remains
    // available. With no saved credentials the panel behaves as AP/recovery mode.
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "wifi mode APSTA failed");
    ESP_RETURN_ON_ERROR(apply_ap_config(), TAG, "AP config failed");
    ESP_LOGI(TAG, "Saved WiFi STA credentials removed; panel remains available as AP SSID=%s", PANEL_WIFI_AP_SSID);
    return ESP_OK;
}

bool panel_wifi_sta_is_connected(void)
{
    return s_started && s_sta_connected && s_sta_ip[0] != '\0';
}

esp_err_t panel_wifi_status_json(char **out_json)
{
    if (!out_json) return ESP_ERR_INVALID_ARG;
    *out_json = NULL;
    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    cJSON_AddBoolToObject(root, "started", s_started);
    cJSON_AddStringToObject(root, "mode", s_saved_sta ? "apsta" : "ap");
    cJSON_AddStringToObject(root, "ap_ssid", PANEL_WIFI_AP_SSID);
    cJSON_AddStringToObject(root, "ap_password", PANEL_WIFI_AP_PASS);
    cJSON_AddStringToObject(root, "ap_ip", "192.168.50.1");
    cJSON_AddStringToObject(root, "hostname", s_hostname);
    char mdns_url[64];
    snprintf(mdns_url, sizeof(mdns_url), "http://%s.local", s_hostname);
    cJSON_AddStringToObject(root, "mdns_url", mdns_url);
    cJSON_AddBoolToObject(root, "sta_saved", s_saved_sta);
    cJSON_AddBoolToObject(root, "sta_connected", s_sta_connected);
    cJSON_AddBoolToObject(root, "sta_connecting", s_sta_connecting);
    cJSON_AddStringToObject(root, "sta_ssid", s_saved_sta ? s_sta_ssid : "");
    cJSON_AddStringToObject(root, "sta_ip", s_sta_connected ? s_sta_ip : "");

    wifi_ap_record_t ap_info = {0};
    if (s_sta_connected && esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        cJSON_AddNumberToObject(root, "sta_rssi", ap_info.rssi);
        cJSON_AddStringToObject(root, "sta_auth", authmode_to_string(ap_info.authmode));
    }

    *out_json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return *out_json ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t panel_wifi_scan_json(char **out_json)
{
    if (!out_json) return ESP_ERR_INVALID_ARG;
    *out_json = NULL;
    ESP_RETURN_ON_ERROR(panel_wifi_start(), TAG, "wifi not started");

    wifi_scan_config_t scan_cfg = {0};
    scan_cfg.show_hidden = false;
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) return err;

    uint16_t count = 0;
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_num(&count), TAG, "scan count failed");
    wifi_ap_record_t *records = NULL;
    if (count > 0) {
        records = (wifi_ap_record_t *)calloc(count, sizeof(wifi_ap_record_t));
        if (!records) return ESP_ERR_NO_MEM;
        err = esp_wifi_scan_get_ap_records(&count, records);
        if (err != ESP_OK) {
            free(records);
            return err;
        }
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "networks");
    if (!root || !arr) {
        free(records);
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    for (uint16_t i = 0; i < count; ++i) {
        // Hide duplicate SSIDs; keep the strongest AP because the scan is sorted
        // by the WiFi driver on most targets but still check defensively.
        bool duplicate = false;
        for (uint16_t j = 0; j < i; ++j) {
            if (strncmp((const char *)records[i].ssid, (const char *)records[j].ssid, sizeof(records[i].ssid)) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate || records[i].ssid[0] == 0) continue;
        cJSON *item = cJSON_CreateObject();
        if (!item) continue;
        cJSON_AddStringToObject(item, "ssid", (const char *)records[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", records[i].rssi);
        cJSON_AddNumberToObject(item, "channel", records[i].primary);
        cJSON_AddStringToObject(item, "auth", authmode_to_string(records[i].authmode));
        cJSON_AddBoolToObject(item, "secure", records[i].authmode != WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(arr, item);
    }

    cJSON_AddNumberToObject(root, "count", cJSON_GetArraySize(arr));
    cJSON_AddStringToObject(root, "ap_ssid", PANEL_WIFI_AP_SSID);
    cJSON_AddBoolToObject(root, "ok", true);
    *out_json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(records);
    return *out_json ? ESP_OK : ESP_ERR_NO_MEM;
}
