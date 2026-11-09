#ifndef ESP_NOW_H
#define ESP_NOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint8_t src_addr[6];
    uint8_t dst_addr[6];
    uint8_t rx_ctrl;
} esp_now_recv_info_t;

typedef struct {
    uint8_t peer_addr[6];
    uint8_t lmk[16];
    uint8_t channel;
    uint8_t ifidx;
    bool encrypt;
    uint16_t iface_mask;
} esp_now_peer_info_t;

typedef void (*esp_now_recv_cb_t)(const esp_now_recv_info_t *info,
                                  const uint8_t *data, int length);

typedef enum {
    ESP_NOW_SEND_SUCCESS = 0,
    ESP_NOW_SEND_FAIL = 1,
} esp_now_send_status_t;

typedef struct {
    uint8_t mac_addr[6];
} esp_now_send_info_t;

typedef void (*esp_now_send_cb_t)(const esp_now_send_info_t *tx_info,
                                  esp_now_send_status_t status);

esp_err_t esp_now_init(void);
esp_err_t esp_now_register_recv_cb(esp_now_recv_cb_t cb);
esp_err_t esp_now_register_send_cb(esp_now_send_cb_t cb);
esp_err_t esp_now_send(const uint8_t *peer_addr, const uint8_t *data, size_t len);
esp_err_t esp_now_add_peer(const esp_now_peer_info_t *peer);
bool esp_now_is_peer_exist(const uint8_t *peer_addr);
esp_err_t esp_now_set_pmk(const uint8_t *pmk);

#endif // ESP_NOW_H