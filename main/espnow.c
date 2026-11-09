#include "espnow.h"

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "espnow";

// The peers of a room share a common secret so that other people cannot
// control the boards. The broadcast frame cannot be encrypted, the secret
// inside the payload takes care of the access control instead.
#define ESPNOW_GROUP_SECRET "zclaw-espnow-v1!"
#define ESPNOW_SECRET_LEN 16

// The wire format is fixed so that both sides agree without negotiation.
//
//   byte 0      : magic 'Z'
//   byte 1      : packet type
//   byte 2      : sequence number
//   byte 3      : command (or the reported state for a response)
//   byte 4..19  : the group secret
//   byte 20..35 : the name of the sending device, zero padded
#define ESPNOW_MAGIC 'Z'
#define ESPNOW_PACKET_LEN 36
#define ESPNOW_NAME_OFFSET 20
#define ESPNOW_NAME_BYTES ESPNOW_NAME_MAX_LEN

#define ESPNOW_MAX_PEERS 16
#define ESPNOW_DEFAULT_TIMEOUT_MS 300
#define ESPNOW_RETRY_COUNT 3
#define ESPNOW_RETRY_DELAY_MS 60

// The GPIO that acts as the local relay output. A board without a relay
// still answers, it just reports the state it keeps in software. Set it to
// -1 to keep the state in software only.
#ifndef ESPNOW_RELAY_GPIO
#define ESPNOW_RELAY_GPIO -1
#endif

typedef struct {
    bool ready;
    bool started;
    uint8_t next_seq;
    espnow_peer_t peers[ESPNOW_MAX_PEERS];
    int peer_count;
    uint8_t local_state;
    uint32_t local_state_ms;
} espnow_state_t;

static espnow_state_t s_state = {
    .ready = false,
    .started = false,
    .next_seq = 1,
    .peer_count = 0,
    .local_state = 0,
    .local_state_ms = 0,
};

// Answers that arrived while we were waiting for them. The receive
// callback runs in the Wi-Fi task, so it only records the data and the
// caller picks it up from the task context.
typedef struct {
    uint8_t seq;
    uint8_t value;
    bool valid;
} espnow_answer_t;

static espnow_answer_t s_last_answer = {0};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

const char *espnow_device_name(void)
{
    return DEVICE_NAME;
}

bool espnow_parse_mac(const char *text, uint8_t *mac_out)
{
    unsigned int bytes[ESPNOW_MAC_LEN] = {0};

    if (!text || !mac_out) {
        return false;
    }
    if (sscanf(text, "%x:%x:%x:%x:%x:%x",
               &bytes[0], &bytes[1], &bytes[2],
               &bytes[3], &bytes[4], &bytes[5]) != ESPNOW_MAC_LEN) {
        return false;
    }
    for (int i = 0; i < ESPNOW_MAC_LEN; ++i) {
        if (bytes[i] > 0xFF) {
            return false;
        }
        mac_out[i] = (uint8_t)bytes[i];
    }
    return true;
}

bool espnow_command_from_text(const char *text, uint8_t *command_out)
{
    if (!text || !command_out) {
        return false;
    }

    if (strcmp(text, "on") == 0 || strcmp(text, "bat") == 0 ||
        strcmp(text, "1") == 0 || strcmp(text, "true") == 0) {
        *command_out = ESPNOW_CMD_ON;
        return true;
    }
    if (strcmp(text, "off") == 0 || strcmp(text, "tat") == 0 ||
        strcmp(text, "0") == 0 || strcmp(text, "false") == 0) {
        *command_out = ESPNOW_CMD_OFF;
        return true;
    }
    if (strcmp(text, "toggle") == 0 || strcmp(text, "flip") == 0) {
        *command_out = ESPNOW_CMD_TOGGLE;
        return true;
    }
    if (strcmp(text, "status") == 0 || strcmp(text, "state") == 0) {
        *command_out = ESPNOW_CMD_STATUS;
        return true;
    }

    return false;
}

void espnow_format_mac(const uint8_t *mac, char *out, size_t out_len)
{
    if (!mac || !out || out_len == 0) {
        return;
    }
    snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static espnow_peer_t *espnow_find_peer(const uint8_t *mac)
{
    for (int i = 0; i < s_state.peer_count; ++i) {
        if (memcmp(s_state.peers[i].mac, mac, ESPNOW_MAC_LEN) == 0) {
            return &s_state.peers[i];
        }
    }
    return NULL;
}

static espnow_peer_t *espnow_upsert_peer(const uint8_t *mac)
{
    espnow_peer_t *peer = espnow_find_peer(mac);
    if (peer) {
        peer->last_seen_ms = esp_timer_get_time() / 1000;
        return peer;
    }
    if (s_state.peer_count >= ESPNOW_MAX_PEERS) {
        return NULL;
    }

    peer = &s_state.peers[s_state.peer_count++];
    memset(peer, 0, sizeof(*peer));
    memcpy(peer->mac, mac, ESPNOW_MAC_LEN);
    espnow_format_mac(mac, peer->address, sizeof(peer->address));
    peer->last_state = 255;
    peer->last_seen_ms = esp_timer_get_time() / 1000;
    return peer;
}

// ---------------------------------------------------------------------------
// Local relay state
// ---------------------------------------------------------------------------

uint8_t espnow_apply_local_command(uint8_t command)
{
    uint8_t state = s_state.local_state;

    switch (command) {
    case ESPNOW_CMD_ON:
        state = 1;
        break;
    case ESPNOW_CMD_OFF:
        state = 0;
        break;
    case ESPNOW_CMD_TOGGLE:
        state = state ? 0 : 1;
        break;
    case ESPNOW_CMD_STATUS:
    default:
        return s_state.local_state;
    }

    s_state.local_state = state;
    s_state.local_state_ms = esp_timer_get_time() / 1000;

    // Drive the output pin when one is configured. A board without a relay
    // keeps the state in software only.
    if (ESPNOW_RELAY_GPIO >= 0) {
        gpio_config_t io_conf = {
            .pin_bit_mask = 1ULL << ESPNOW_RELAY_GPIO,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
        gpio_set_level(ESPNOW_RELAY_GPIO, state ? 1 : 0);
    }

    ESP_LOGI(TAG, "Local command 0x%02X -> state %u", command, state);
    return state;
}

// ---------------------------------------------------------------------------
// Packet handling
// ---------------------------------------------------------------------------

static void espnow_send_packet(const uint8_t *dest, uint8_t type, uint8_t seq, uint8_t value)
{
    uint8_t packet[ESPNOW_PACKET_LEN] = {0};
    size_t name_len = strlen(DEVICE_NAME);

    if (name_len > ESPNOW_NAME_BYTES) {
        name_len = ESPNOW_NAME_BYTES;
    }

    packet[0] = ESPNOW_MAGIC;
    packet[1] = type;
    packet[2] = seq;
    packet[3] = value;
    memcpy(packet + 4, ESPNOW_GROUP_SECRET, ESPNOW_SECRET_LEN);
    memcpy(packet + ESPNOW_NAME_OFFSET, DEVICE_NAME, name_len);

    if (esp_now_send(dest, packet, sizeof(packet)) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send the packet");
    }
}

static void espnow_handle_packet(const uint8_t *sender, const uint8_t *data, size_t length)
{
    if (!sender || !data || length < ESPNOW_PACKET_LEN) {
        return;
    }
    if (data[0] != ESPNOW_MAGIC) {
        return;
    }
    // The group secret guards the channel against unrelated traffic.
    if (memcmp(data + 4, ESPNOW_GROUP_SECRET, ESPNOW_SECRET_LEN) != 0) {
        return;
    }

    uint8_t type = data[1];
    uint8_t seq = data[2];
    uint8_t value = data[3];

    espnow_peer_t *peer = espnow_upsert_peer(sender);
    if (peer) {
        // The sender announces its friendly name, so the peers can address
        // it by name instead of by MAC address only.
        size_t name_len = 0;
        while (name_len < ESPNOW_NAME_BYTES && data[ESPNOW_NAME_OFFSET + name_len] != '\0') {
            name_len++;
        }
        if (name_len > 0) {
            memcpy(peer->name, data + ESPNOW_NAME_OFFSET, name_len);
            peer->name[name_len] = '\0';
        }
    }

    switch (type) {
    case ESPNOW_PKT_PING:
        ESP_LOGI(TAG, "Ping from %s (%s)", peer ? peer->address : "?",
                 peer && peer->name[0] ? peer->name : "unnamed");
        espnow_send_packet(sender, ESPNOW_PKT_PONG, seq, s_state.local_state);
        break;

    case ESPNOW_PKT_COMMAND: {
        uint8_t state = espnow_apply_local_command(value);
        ESP_LOGI(TAG, "Command 0x%02X from %s (%s) -> state %u",
                 value, peer ? peer->address : "?",
                 peer && peer->name[0] ? peer->name : "unnamed", state);
        espnow_send_packet(sender, ESPNOW_PKT_RESPONSE, seq, state);
        break;
    }

    case ESPNOW_PKT_PONG:
        ESP_LOGI(TAG, "Pong from %s (%s), state %u",
                 peer ? peer->address : "?",
                 peer && peer->name[0] ? peer->name : "unnamed", value);
        s_last_answer.seq = seq;
        s_last_answer.value = value;
        s_last_answer.valid = true;
        break;

    case ESPNOW_PKT_RESPONSE:
        ESP_LOGI(TAG, "Response from %s (%s), state %u",
                 peer ? peer->address : "?",
                 peer && peer->name[0] ? peer->name : "unnamed", value);
        if (peer) {
            peer->last_state = value;
        }
        s_last_answer.seq = seq;
        s_last_answer.value = value;
        s_last_answer.valid = true;
        break;

    default:
        break;
    }
}

static void espnow_recv_callback(const esp_now_recv_info_t *info, const uint8_t *data, int length)
{
    if (!info) {
        return;
    }
    espnow_handle_packet(info->src_addr, data, (size_t)length);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool espnow_start(void)
{
    if (s_state.ready) {
        return true;
    }

    // ESP-NOW sits on top of the Wi-Fi driver, so the radio has to be up.
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) != ESP_OK || mode == WIFI_MODE_NULL) {
        ESP_LOGW(TAG, "The Wi-Fi radio is not running yet, delaying ESP-NOW");
        return false;
    }

    if (!s_state.started) {
        if (esp_now_init() != ESP_OK) {
            ESP_LOGW(TAG, "Could not initialize ESP-NOW");
            return false;
        }
        s_state.started = true;
    }

    // The primary key protects the channel from other people.
    esp_now_set_pmk((const uint8_t *)ESPNOW_GROUP_SECRET);

    // The broadcast address has to be added explicitly to reach every peer.
    esp_now_peer_info_t broadcast = {0};
    broadcast.channel = 0;
    broadcast.ifidx = WIFI_IF_STA;
    broadcast.encrypt = false;
    memcpy(broadcast.peer_addr, "\xFF\xFF\xFF\xFF\xFF\xFF", ESPNOW_MAC_LEN);
    esp_now_add_peer(&broadcast);

    esp_now_register_recv_cb(espnow_recv_callback);

    s_state.ready = true;
    ESP_LOGI(TAG, "ESP-NOW is ready, device name is %s", DEVICE_NAME);
    return true;
}

bool espnow_is_ready(void)
{
    return s_state.ready;
}

static bool espnow_ensure_peer(const uint8_t *mac)
{
    if (esp_now_is_peer_exist(mac)) {
        return true;
    }

    esp_now_peer_info_t peer = {0};
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    memcpy(peer.peer_addr, mac, ESPNOW_MAC_LEN);
    return esp_now_add_peer(&peer) == ESP_OK;
}

bool espnow_send_to(const char *address, uint8_t type, uint8_t command, uint8_t seq)
{
    uint8_t mac[ESPNOW_MAC_LEN] = {0};

    if (!s_state.ready) {
        return false;
    }
    if (!espnow_parse_mac(address, mac)) {
        ESP_LOGW(TAG, "The address %s is not valid", address ? address : "");
        return false;
    }

    if (!espnow_ensure_peer(mac)) {
        ESP_LOGW(TAG, "Could not add the peer %s", address);
        return false;
    }

    espnow_send_packet(mac, type, seq, command);

    ESP_LOGI(TAG, "Sent the packet 0x%02X to %s", type, address);
    return true;
}

bool espnow_send_command(const char *address, uint8_t command, uint8_t *state_out,
                         uint32_t timeout_ms)
{
    uint8_t seq;

    if (!s_state.ready) {
        return false;
    }
    if (timeout_ms == 0) {
        timeout_ms = ESPNOW_DEFAULT_TIMEOUT_MS;
    }

    seq = s_state.next_seq++;
    if (s_state.next_seq == 0) {
        s_state.next_seq = 1;
    }

    s_last_answer.valid = false;
    s_last_answer.seq = 0;

    for (int attempt = 0; attempt < ESPNOW_RETRY_COUNT; ++attempt) {
        if (!espnow_send_to(address, ESPNOW_PKT_COMMAND, command, seq)) {
            return false;
        }

        uint32_t waited = 0;
        while (waited < timeout_ms) {
            if (s_last_answer.valid && s_last_answer.seq == seq) {
                if (state_out) {
                    *state_out = s_last_answer.value;
                }
                s_last_answer.valid = false;
                return true;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            waited += 10;
        }

        vTaskDelay(pdMS_TO_TICKS(ESPNOW_RETRY_DELAY_MS));
    }

    ESP_LOGW(TAG, "No answer from %s within %u ms", address, timeout_ms);
    return false;
}

void espnow_broadcast_ping(void)
{
    uint8_t seq;

    if (!s_state.ready) {
        return;
    }

    seq = s_state.next_seq++;
    if (s_state.next_seq == 0) {
        s_state.next_seq = 1;
    }

    espnow_send_to("FF:FF:FF:FF:FF:FF", ESPNOW_PKT_PING, s_state.local_state, seq);
}

int espnow_list_peers(espnow_peer_t *out, int max_count)
{
    if (!out || max_count <= 0) {
        return 0;
    }

    int count = s_state.peer_count;
    if (count > max_count) {
        count = max_count;
    }
    memcpy(out, s_state.peers, (size_t)count * sizeof(espnow_peer_t));
    return count;
}
