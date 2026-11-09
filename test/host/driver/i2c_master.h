#ifndef DRIVER_I2C_MASTER_H
#define DRIVER_I2C_MASTER_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef int i2c_port_num_t;
typedef int i2c_port_t;
typedef int i2c_clock_source_t;
typedef int i2c_addr_bit_len_t;
typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;

#define I2C_NUM_0 0
#define I2C_CLK_SRC_DEFAULT 0
#define I2C_ADDR_BIT_LEN_7 0

typedef struct {
    i2c_port_num_t i2c_port;
    int sda_io_num;
    int scl_io_num;
    union {
        i2c_clock_source_t clk_source;
    };
    uint8_t glitch_ignore_cnt;
    int intr_priority;
    size_t trans_queue_depth;
    struct {
        uint32_t enable_internal_pullup: 1;
        uint32_t allow_pd: 1;
    } flags;
} i2c_master_bus_config_t;

typedef struct {
    i2c_addr_bit_len_t dev_addr_length;
    uint16_t device_address;
    uint32_t scl_speed_hz;
    uint32_t scl_wait_us;
    struct {
        uint32_t disable_ack_check: 1;
    } flags;
} i2c_device_config_t;

esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *bus_config,
                              i2c_master_bus_handle_t *ret_bus_handle);
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus_handle);
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus_handle,
                                     const i2c_device_config_t *dev_config,
                                     i2c_master_dev_handle_t *ret_handle);
esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus_handle,
                            uint16_t address,
                            int xfer_timeout_ms);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t i2c_dev,
                               const uint8_t *write_buffer,
                               size_t write_size,
                               int xfer_timeout_ms);
esp_err_t i2c_master_receive(i2c_master_dev_handle_t i2c_dev,
                              uint8_t *read_buffer,
                              size_t read_size,
                              int xfer_timeout_ms);
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t i2c_dev,
                                       const uint8_t *write_buffer,
                                       size_t write_size,
                                       uint8_t *read_buffer,
                                       size_t read_size,
                                       int xfer_timeout_ms);

void i2c_test_reset(void);
void i2c_test_set_probe_result(esp_err_t result);
void i2c_test_set_transmit_result(esp_err_t result);
void i2c_test_set_receive_result(esp_err_t result);
void i2c_test_set_transmit_receive_result(esp_err_t result);
void i2c_test_set_read_data(const uint8_t *data, size_t len);
int i2c_test_get_bus_created(void);
int i2c_test_get_bus_deleted(void);
int i2c_test_get_device_added(void);
uint16_t i2c_test_get_last_address(void);
size_t i2c_test_get_last_write_length(void);
size_t i2c_test_get_last_read_length(void);
uint8_t i2c_test_get_last_write_byte(size_t index);

#endif  // DRIVER_I2C_MASTER_H