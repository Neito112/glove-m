#include "tools_handlers.h"
#include "tools_common.h"
#include "espnow.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Tool handlers that expose the two-way ESP-NOW link to the agent.
//
// The agent can ask a peer for its state, switch it on or off, and list
// every peer that answered on the channel. Every command waits for the
// matching answer, so the reply reports the real state of the remote node
// instead of just "the packet was sent".

// Render a peer as "name(AA:BB:CC:DD:EE:FF)" or just the address when the
// peer did not announce a name yet.
static int format_peer(char *out, size_t out_len, const espnow_peer_t *peer)
{
    const char *state = peer->last_state == 255
        ? "?"
        : (peer->last_state ? "ON" : "OFF");

    if (peer->name[0] != '\0') {
        return snprintf(out, out_len, "%s(%s,%s)", peer->name, peer->address, state);
    }
    return snprintf(out, out_len, "%s(%s)", peer->address, state);
}

bool tools_espnow_send_handler(const cJSON *input, char *result, size_t result_len)
{
    cJSON *address_json = cJSON_GetObjectItem(input, "address");
    cJSON *command_json = cJSON_GetObjectItem(input, "command");

    if (!address_json || !cJSON_IsString(address_json)) {
        snprintf(result, result_len, "Error: 'address' required (string, AA:BB:CC:DD:EE:FF)");
        return false;
    }
    if (!command_json || !cJSON_IsString(command_json)) {
        snprintf(result, result_len, "Error: 'command' required (on, off, toggle or status)");
        return false;
    }

    uint8_t command = 0;
    if (!espnow_command_from_text(command_json->valuestring, &command)) {
        snprintf(result, result_len,
                 "Error: unknown command '%s' (use on, off, toggle or status)",
                 command_json->valuestring);
        return false;
    }

    if (!espnow_is_ready()) {
        snprintf(result, result_len, "Error: ESP-NOW is not ready yet");
        return false;
    }

    uint8_t state = 0;
    if (!espnow_send_command(address_json->valuestring, command, &state, 0)) {
        snprintf(result, result_len, "Error: no answer from %s", address_json->valuestring);
        return false;
    }

    snprintf(result, result_len, "%s is %s (state %u)",
             address_json->valuestring, state ? "ON" : "OFF", state);
    return true;
}

bool tools_espnow_ping_handler(const cJSON *input, char *result, size_t result_len)
{
    if (!espnow_is_ready()) {
        snprintf(result, result_len, "Error: ESP-NOW is not ready yet");
        return false;
    }

    espnow_broadcast_ping();

    // Give the peers a moment to answer before the list is read.
    vTaskDelay(pdMS_TO_TICKS(300));

    espnow_peer_t peers[ESPNOW_MAX_PEERS];
    int count = espnow_list_peers(peers, ESPNOW_MAX_PEERS);
    if (count == 0) {
        snprintf(result, result_len, "No peer answered the ping");
        return true;
    }

    int written = snprintf(result, result_len, "%d peer(s) answered:", count);
    for (int i = 0; i < count && written > 0 && (size_t)written < result_len; ++i) {
        char peer_text[64];
        int peer_len = format_peer(peer_text, sizeof(peer_text), &peers[i]);
        if (peer_len < 0) {
            break;
        }
        written += snprintf(result + written, result_len - (size_t)written, " %s", peer_text);
    }
    return true;
}

bool tools_espnow_list_peers_handler(const cJSON *input, char *result, size_t result_len)
{
    espnow_peer_t peers[ESPNOW_MAX_PEERS];
    int count = espnow_list_peers(peers, ESPNOW_MAX_PEERS);

    if (count == 0) {
        snprintf(result, result_len, "No peer seen on the ESP-NOW channel yet");
        return true;
    }

    int written = snprintf(result, result_len, "%d peer(s):", count);
    for (int i = 0; i < count && written > 0 && (size_t)written < result_len; ++i) {
        char peer_text[64];
        int peer_len = format_peer(peer_text, sizeof(peer_text), &peers[i]);
        if (peer_len < 0) {
            break;
        }
        written += snprintf(result + written, result_len - (size_t)written, " %s", peer_text);
    }
    return true;
}
