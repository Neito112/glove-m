#ifndef ESPNOW_H
#define ESPNOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Two-way ESP-NOW transport.
//
// ESP-NOW runs directly on the Wi-Fi radio, so two boards in the same room
// talk to each other without a router and with a latency in the millisecond
// range. Every packet carries a sequence number so the receiver can answer
// with the very same sequence, which lets the sender tell a real reply from
// a stale one.
//
// Every packet also carries the name of the sending device, so the peers can
// address each other by name instead of by MAC address only.
//
// The same firmware plays both roles:
//   - initiator: sends a command and waits for the matching answer,
//   - responder: executes the command and reports the new state back.

#define ESPNOW_MAC_LEN 6
#define ESPNOW_MAX_PEERS 16
#define ESPNOW_NAME_MAX_LEN 16

// Packet types shared by both directions.
typedef enum {
    ESPNOW_PKT_PING = 0x01,       // initiator -> peer, "are you there?"
    ESPNOW_PKT_PONG = 0x02,       // peer -> initiator, "yes, here I am"
    ESPNOW_PKT_COMMAND = 0x03,    // initiator -> peer, run this command
    ESPNOW_PKT_RESPONSE = 0x04,   // peer -> initiator, here is the result
} espnow_packet_type_t;

// Commands understood by a peer that acts as a relay node.
typedef enum {
    ESPNOW_CMD_ON = 0x01,
    ESPNOW_CMD_OFF = 0x02,
    ESPNOW_CMD_TOGGLE = 0x03,
    ESPNOW_CMD_STATUS = 0x04,
} espnow_command_t;

// A peer seen on the ESP-NOW network.
typedef struct {
    uint8_t mac[ESPNOW_MAC_LEN];
    char address[18];      // "AA:BB:CC:DD:EE:FF"
    char name[ESPNOW_NAME_MAX_LEN + 1]; // friendly name reported by the peer
    uint8_t last_state;    // last reported relay state, 0/1, 255 = unknown
    uint32_t last_seen_ms; // uptime of the last contact
} espnow_peer_t;

// Bring the ESP-NOW stack up. The Wi-Fi radio has to be running already,
// otherwise the function returns false and the caller retries later.
bool espnow_start(void);

// True once espnow_start() succeeded.
bool espnow_is_ready(void);

// Send a raw packet to one peer. The address uses the "AA:BB:CC:DD:EE:FF"
// notation. Returns false when the address is malformed or the radio is down.
bool espnow_send_to(const char *address, uint8_t type, uint8_t command, uint8_t seq);

// Send a command and wait for the matching response. The answer is copied
// into state_out when the peer replies before the timeout expires.
bool espnow_send_command(const char *address, uint8_t command, uint8_t *state_out,
                         uint32_t timeout_ms);

// Ask every peer on the channel to report its state.
void espnow_broadcast_ping(void);

// Copy the peers seen so far into the caller's buffer. Returns the count.
int espnow_list_peers(espnow_peer_t *out, int max_count);

// Apply a command locally, e.g. to a relay GPIO. The default implementation
// reports the state of the configured output pin. Returns the new state.
uint8_t espnow_apply_local_command(uint8_t command);

// Parse "AA:BB:CC:DD:EE:FF" into a 6 byte MAC. Returns false when malformed.
bool espnow_parse_mac(const char *text, uint8_t *mac_out);

// Parse a command name like "on", "off", "toggle" or "status" into the
// matching espnow_command_t value. Returns false when the name is unknown.
bool espnow_command_from_text(const char *text, uint8_t *command_out);

// Format a MAC into the "AA:BB:CC:DD:EE:FF" notation.
void espnow_format_mac(const uint8_t *mac, char *out, size_t out_len);

// The friendly name of this device, as announced to the peers.
const char *espnow_device_name(void);

#endif // ESPNOW_H
