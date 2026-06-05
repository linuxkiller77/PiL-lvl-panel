#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev_mode;
    i2c_master_dev_handle_t dev_in;
    i2c_master_dev_handle_t dev_out;
    i2c_master_dev_handle_t dev_out_upper;
    uint16_t output_bits;
    uint8_t mode_value;
} ch422g_t;

esp_err_t ch422g_init(ch422g_t *dev, i2c_master_bus_handle_t bus, uint16_t initial_outputs);
esp_err_t ch422g_write_pin(ch422g_t *dev, uint8_t pin, bool value);
esp_err_t ch422g_write_outputs(ch422g_t *dev, uint16_t outputs);

#ifdef __cplusplus
}
#endif
