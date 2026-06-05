#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HMI_TAG_BOOL = 0,
    HMI_TAG_NUMBER,
    HMI_TAG_STRING,
} hmi_tag_type_t;

typedef struct {
    char name[48];
    hmi_tag_type_t type;
    bool writable;
    float number_value;
    bool bool_value;
    char string_value[96];
    // Configured boot/startup value. Runtime value is stored separately above.
    float initial_number_value;
    bool initial_bool_value;
    char initial_string_value[96];
    char units[16];
    float min_value;
    float max_value;
    char description[96];
} hmi_tag_t;

esp_err_t hmi_tag_db_init(void);
size_t hmi_tag_db_count(void);
bool hmi_tag_db_exists(const char *name);
esp_err_t hmi_tag_db_define_number(const char *name, float value, float min_value, float max_value, const char *units, bool writable);
esp_err_t hmi_tag_db_define_bool(const char *name, bool value, bool writable);
esp_err_t hmi_tag_db_define_string(const char *name, const char *value, bool writable);
esp_err_t hmi_tag_db_set_number(const char *name, float value);
esp_err_t hmi_tag_db_set_bool(const char *name, bool value);
esp_err_t hmi_tag_db_set_string(const char *name, const char *value);
float hmi_tag_db_get_number(const char *name, float fallback);
bool hmi_tag_db_get_bool(const char *name, bool fallback);
const char *hmi_tag_db_get_string(const char *name, const char *fallback);
esp_err_t hmi_tag_db_write_from_json(const char *name, const cJSON *value);
esp_err_t hmi_tag_db_write_from_string(const char *name, const char *value, size_t value_len);
esp_err_t hmi_tag_db_read_value_string(const char *name, char *out, size_t out_size, hmi_tag_type_t *out_type);
cJSON *hmi_tag_db_to_json(void);
esp_err_t hmi_tag_db_merge_from_json(const cJSON *root, bool save_to_flash);   // update/upsert only; does not delete omitted runtime tags
esp_err_t hmi_tag_db_replace_from_json(const cJSON *root, bool save_to_flash); // exact replace; omitted tags are deleted
esp_err_t hmi_tag_db_save_to_flash(void);
esp_err_t hmi_tag_db_load_from_flash(void);
uint32_t hmi_tag_db_generation(void);
bool hmi_tag_db_generation_changed(uint32_t *last_generation);

#ifdef __cplusplus
}
#endif
