/*
 * Mock of the ESP-NOW API for host testing.
 */

#ifndef MOCK_ESP_NOW_H
#define MOCK_ESP_NOW_H

#include <stdint.h>

#include "esp_wifi.h"

// Reset the recorded state of the mock.
void mock_espnow_reset(void);

// Pretend that the Wi-Fi radio is in the given mode.
void mock_espnow_set_wifi_mode(wifi_mode_t mode);

// Number of packets that were sent through the mock.
int mock_espnow_send_count(void);

// Copy the payload of the last sent packet. Returns its length.
int mock_espnow_last_payload(uint8_t *out, int max_len);

// The destination of the last sent packet.
const uint8_t *mock_espnow_last_dest(void);

// Deliver a packet to the registered receive callback.
void mock_espnow_deliver(const uint8_t *src, const uint8_t *data, int length);

#endif // MOCK_ESP_NOW_H