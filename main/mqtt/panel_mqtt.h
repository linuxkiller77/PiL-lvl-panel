#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_MQTT_MAX_SERVERS 3
#define PANEL_MQTT_MAX_PUBLISH 32
#define PANEL_MQTT_MAX_SUBSCRIBE 32
#define PANEL_MQTT_TOPIC_MAX 160
#define PANEL_MQTT_TAG_MAX 48
#define PANEL_MQTT_NAME_MAX 32
#define PANEL_MQTT_TEXT_MAX 96

esp_err_t panel_mqtt_init(void);
esp_err_t panel_mqtt_start(void);
esp_err_t panel_mqtt_stop(void);
esp_err_t panel_mqtt_reload_config(void);
esp_err_t panel_mqtt_save_config_json(const char *json);
esp_err_t panel_mqtt_get_config_json(char **out_json);
esp_err_t panel_mqtt_get_status_json(char **out_json);
esp_err_t panel_mqtt_test_publish(const char *topic, const char *payload, int qos, bool retain);

#ifdef __cplusplus
}
#endif
