#include "ch422g.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "ch422g";

/* CH422G uses separate I2C addresses for internal registers. */
#define CH422G_REG_MODE      0x24
#define CH422G_REG_IN        0x26
#define CH422G_REG_OUT       0x38
#define CH422G_REG_OUT_UPPER 0x23

#define CH422G_MODE_OUTPUT      0x01
#define CH422G_MODE_OPEN_DRAIN  0x04

static esp_err_t add_reg_device(i2c_master_bus_handle_t bus, uint8_t address, i2c_master_dev_handle_t *out)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 400000,
    };
    return i2c_master_bus_add_device(bus, &dev_cfg, out);
}

static esp_err_t write_reg(i2c_master_dev_handle_t handle, uint8_t value)
{
    return i2c_master_transmit(handle, &value, 1, 1000);
}

esp_err_t ch422g_write_outputs(ch422g_t *dev, uint16_t outputs)
{
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_INVALID_ARG, TAG, "dev is NULL");
    dev->output_bits = outputs;

    ESP_RETURN_ON_ERROR(write_reg(dev->dev_out, (uint8_t)(outputs & 0xff)), TAG, "write lower outputs failed");
    ESP_RETURN_ON_ERROR(write_reg(dev->dev_out_upper, (uint8_t)((outputs >> 8) & 0x0f)), TAG, "write upper outputs failed");
    return ESP_OK;
}

esp_err_t ch422g_write_pin(ch422g_t *dev, uint8_t pin, bool value)
{
    ESP_RETURN_ON_FALSE(pin < 12, ESP_ERR_INVALID_ARG, TAG, "pin out of range");

    uint16_t outputs = dev->output_bits;
    if (value) {
        outputs |= (uint16_t)(1U << pin);
    } else {
        outputs &= (uint16_t)~(1U << pin);
    }
    return ch422g_write_outputs(dev, outputs);
}

esp_err_t ch422g_init(ch422g_t *dev, i2c_master_bus_handle_t bus, uint16_t initial_outputs)
{
    ESP_RETURN_ON_FALSE(dev && bus, ESP_ERR_INVALID_ARG, TAG, "bad args");

    dev->bus = bus;
    dev->output_bits = initial_outputs;
    dev->mode_value = CH422G_MODE_OUTPUT;

    ESP_RETURN_ON_ERROR(add_reg_device(bus, CH422G_REG_MODE, &dev->dev_mode), TAG, "add mode device failed");
    ESP_RETURN_ON_ERROR(add_reg_device(bus, CH422G_REG_IN, &dev->dev_in), TAG, "add input device failed");
    ESP_RETURN_ON_ERROR(add_reg_device(bus, CH422G_REG_OUT, &dev->dev_out), TAG, "add output device failed");
    ESP_RETURN_ON_ERROR(add_reg_device(bus, CH422G_REG_OUT_UPPER, &dev->dev_out_upper), TAG, "add upper output device failed");

    /* Write outputs before switching 0-7 into output mode, matching known CH422G bring-up practice. */
    ESP_RETURN_ON_ERROR(ch422g_write_outputs(dev, initial_outputs), TAG, "write initial outputs failed");
    ESP_RETURN_ON_ERROR(write_reg(dev->dev_mode, dev->mode_value), TAG, "set output mode failed");

    ESP_LOGI(TAG, "CH422G initialized, outputs=0x%04x", initial_outputs);
    return ESP_OK;
}
