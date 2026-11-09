/*
 * Mock of the ESP-NOW API for host testing.
 *
 * The mock keeps the registered peers and the last sent packet so that the
 * host tests can check the wire format without a radio.
 */

#include "esp_now.h"
#include "esp_wifi.h"
#include "mock_esp.h"

#include <string.h>

#define MOCK_MAX_PEERS 16

static bool s_initialized = false;
static esp_now_recv_cb_t s_recv_cb = NULL;
static esp_now_peer_info_t s_peers[MOCK_MAX_PEERS];
static int s_peer_count = 0;
static uint8_t s_pmk[16];
static wifi_mode_t s_wifi_mode = WIFI_MODE_NULL;

static uint8_t s_last_dest[6];
static uint8_t s_last_payload[256];
static int s_last_payload_len = 0;
static int s_send_count = 0;

esp_err_t esp_wifi_get_mode(wifi_mode_t *mode)
{
    if (!mode) {
        return ESP_ERR_INVALID_ARG;
    }
    *mode = s_wifi_mode;
    return ESP_OK;
}

void mock_espnow_reset(void)
{
    s_initialized = false;
    s_recv_cb = NULL;
    s_peer_count = 0;
    memset(s_peers, 0, sizeof(s_peers));
    memset(s_pmk, 0, sizeof(s_pmk));
    memset(s_last_dest, 0, sizeof(s_last_dest));
    memset(s_last_payload, 0, sizeof(s_last_payload));
    s_last_payload_len = 0;
    s_send_count = 0;
    s_wifi_mode = WIFI_MODE_NULL;
}

void mock_espnow_set_wifi_mode(wifi_mode_t mode)
{
    s_wifi_mode = mode;
}

int mock_espnow_send_count(void)
{
    return s_send_count;
}

int mock_espnow_last_payload(uint8_t *out, int max_len)
{
    int len = s_last_payload_len < max_len ? s_last_payload_len : max_len;
    if (out) {
        memcpy(out, s_last_payload, (size_t)len);
    }
    return len;
}

const uint8_t *mock_espnow_last_dest(void)
{
    return s_last_dest;
}

esp_err_t esp_now_init(void)
{
    s_initialized = true;
    return ESP_OK;
}

esp_err_t esp_now_register_recv_cb(esp_now_recv_cb_t cb)
{
    s_recv_cb = cb;
    return ESP_OK;
}

esp_err_t esp_now_register_send_cb(esp_now_send_cb_t cb)
{
    (void)cb;
    return ESP_OK;
}

esp_err_t esp_now_send(const uint8_t *peer_addr, const uint8_t *data, size_t len)
{
    if (!peer_addr || !data || len == 0 || len > sizeof(s_last_payload)) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(s_last_dest, peer_addr, 6);
    memcpy(s_last_payload, data, len);
    s_last_payload_len = (int)len;
    s_send_count++;
    return ESP_OK;
}

esp_err_t esp_now_add_peer(const esp_now_peer_info_t *peer)
{
    if (!peer) {
        return ESP_ERR_INVALID_ARG;
    }
    for (int i = 0; i < s_peer_count; ++i) {
        if (memcmp(s_peers[i].peer_addr, peer->peer_addr, 6) == 0) {
            return ESP_OK;
        }
    }
    if (s_peer_count >= MOCK_MAX_PEERS) {
        return ESP_ERR_NO_MEM;
    }
    s_peers[s_peer_count++] = *peer;
    return ESP_OK;
}

bool esp_now_is_peer_exist(const uint8_t *peer_addr)
{
    if (!peer_addr) {
        return false;
    }
    for (int i = 0; i < s_peer_count; ++i) {
        if (memcmp(s_peers[i].peer_addr, peer_addr, 6) == 0) {
            return true;
        }
    }
    return false;
}

esp_err_t esp_now_set_pmk(const uint8_t *pmk)
{
    if (!pmk) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(s_pmk, pmk, sizeof(s_pmk));
    return ESP_OK;
}

/* Deliver a packet to the registered receive callback. */
void mock_espnow_deliver(const uint8_t *src, const uint8_t *data, int length)
{
    esp_now_recv_info_t info;

    if (!s_recv_cb || !src || !data) {
        return;
    }
    memset(&info, 0, sizeof(info));
    memcpy(info.src_addr, src, 6);
    s_recv_cb(&info, data, length);
}
