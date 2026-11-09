/*
 * Mock of the I2C master driver for host testing.
 */

#include "driver/i2c_master.h"

#include <string.h>
#include <stddef.h>

#define I2C_TEST_MAX_BYTES 64

static int s_bus_created = 0;
static int s_bus_deleted = 0;
static int s_device_added = 0;
static esp_err_t s_probe_result = ESP_FAIL;
static esp_err_t s_transmit_result = ESP_OK;
static esp_err_t s_receive_result = ESP_OK;
static esp_err_t s_transmit_receive_result = ESP_OK;
static uint8_t s_read_data[I2C_TEST_MAX_BYTES];
static size_t s_read_data_len = 0;
static uint16_t s_last_address = 0;
static uint8_t s_last_write[I2C_TEST_MAX_BYTES];
static size_t s_last_write_len = 0;
static size_t s_last_read_len = 0;

void i2c_test_reset(void)
{
    s_bus_created = 0;
    s_bus_deleted = 0;
    s_device_added = 0;
    s_probe_result = ESP_FAIL;
    s_transmit_result = ESP_OK;
    s_receive_result = ESP_OK;
    s_transmit_receive_result = ESP_OK;
    memset(s_read_data, 0, sizeof(s_read_data));
    s_read_data_len = 0;
    s_last_address = 0;
    memset(s_last_write, 0, sizeof(s_last_write));
    s_last_write_len = 0;
    s_last_read_len = 0;
}

void i2c_test_set_probe_result(esp_err_t result)
{
    s_probe_result = result;
}

void i2c_test_set_transmit_result(esp_err_t result)
{
    s_transmit_result = result;
}

void i2c_test_set_receive_result(esp_err_t result)
{
    s_receive_result = result;
}

void i2c_test_set_transmit_receive_result(esp_err_t result)
{
    s_transmit_receive_result = result;
}

void i2c_test_set_read_data(const uint8_t *data, size_t len)
{
    size_t copy_len = len;

    if (copy_len > sizeof(s_read_data)) {
        copy_len = sizeof(s_read_data);
    }
    memset(s_read_data, 0, sizeof(s_read_data));
    if (data && copy_len > 0) {
        memcpy(s_read_data, data, copy_len);
    }
    s_read_data_len = copy_len;
}

int i2c_test_get_bus_created(void)
{
    return s_bus_created;
}

int i2c_test_get_bus_deleted(void)
{
    return s_bus_deleted;
}

int i2c_test_get_device_added(void)
{
    return s_device_added;
}

uint16_t i2c_test_get_last_address(void)
{
    return s_last_address;
}

size_t i2c_test_get_last_write_length(void)
{
    return s_last_write_len;
}

size_t i2c_test_get_last_read_length(void)
{
    return s_last_read_len;
}

uint8_t i2c_test_get_last_write_byte(size_t index)
{
    if (index >= s_last_write_len) {
        return 0;
    }
    return s_last_write[index];
}

esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *bus_config,
                              i2c_master_bus_handle_t *ret_bus_handle)
{
    (void)bus_config;
    s_bus_created++;
    *ret_bus_handle = (i2c_master_bus_handle_t)1;
    return ESP_OK;
}

esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus_handle)
{
    (void)bus_handle;
    s_bus_deleted++;
    return ESP_OK;
}

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus_handle,
                                     const i2c_device_config_t *dev_config,
                                     i2c_master_dev_handle_t *ret_handle)
{
    (void)bus_handle;
    s_device_added++;
    s_last_address = dev_config->device_address;
    *ret_handle = (i2c_master_dev_handle_t)1;
    return ESP_OK;
}

esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus_handle,
                            uint16_t address,
                            int xfer_timeout_ms)
{
    (void)bus_handle;
    (void)address;
    (void)xfer_timeout_ms;
    return s_probe_result;
}

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t i2c_dev,
                               const uint8_t *write_buffer,
                               size_t write_size,
                               int xfer_timeout_ms)
{
    size_t copy_len = write_size;

    (void)i2c_dev;
    (void)xfer_timeout_ms;
    s_last_write_len = write_size;
    s_last_read_len = 0;
    if (copy_len > sizeof(s_last_write)) {
        copy_len = sizeof(s_last_write);
    }
    memset(s_last_write, 0, sizeof(s_last_write));
    if (write_buffer && copy_len > 0) {
        memcpy(s_last_write, write_buffer, copy_len);
    }
    return s_transmit_result;
}

esp_err_t i2c_master_receive(i2c_master_dev_handle_t i2c_dev,
                              uint8_t *read_buffer,
                              size_t read_size,
                              int xfer_timeout_ms)
{
    size_t copy_len = read_size;

    (void)i2c_dev;
    (void)xfer_timeout_ms;
    s_last_write_len = 0;
    s_last_read_len = read_size;
    if (copy_len > s_read_data_len) {
        copy_len = s_read_data_len;
    }
    if (read_buffer && copy_len > 0) {
        memcpy(read_buffer, s_read_data, copy_len);
    }
    if (read_buffer && read_size > copy_len) {
        memset(read_buffer + copy_len, 0, read_size - copy_len);
    }
    return s_receive_result;
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t i2c_dev,
                                       const uint8_t *write_buffer,
                                       size_t write_size,
                                       uint8_t *read_buffer,
                                       size_t read_size,
                                       int xfer_timeout_ms)
{
    size_t copy_len = write_size;
    size_t read_copy_len = read_size;

    (void)i2c_dev;
    (void)xfer_timeout_ms;
    s_last_write_len = write_size;
    s_last_read_len = read_size;
    if (copy_len > sizeof(s_last_write)) {
        copy_len = sizeof(s_last_write);
    }
    memset(s_last_write, 0, sizeof(s_last_write));
    if (write_buffer && copy_len > 0) {
        memcpy(s_last_write, write_buffer, copy_len);
    }
    if (read_copy_len > s_read_data_len) {
        read_copy_len = s_read_data_len;
    }
    if (read_buffer && read_copy_len > 0) {
        memcpy(read_buffer, s_read_data, read_copy_len);
    }
    if (read_buffer && read_size > read_copy_len) {
        memset(read_buffer + read_copy_len, 0, read_size - read_copy_len);
    }
    return s_transmit_receive_result;
}
