#ifndef UNIVERSE26_WIFI_CONTROL_H
#define UNIVERSE26_WIFI_CONTROL_H

#include <stdint.h>

#define WIFI_CONTROL_MAX 2040u
#define WIFI_ETHERNET_MAX 1514u
typedef struct {
    uint16_t size;
    uint8_t channel;
    uint8_t flow;
    uint8_t credit;
    uint32_t command;
    uint32_t flags;
    uint32_t status;
    uint16_t id;
    const uint8_t *payload;
    uint32_t payload_size;
} wifi_control_packet_t;

uint32_t wifi_control_request(uint8_t packet[WIFI_CONTROL_MAX], uint8_t sequence,
                              uint16_t id, uint32_t command, uint8_t set,
                              const uint8_t *payload, uint32_t size);
int wifi_control_parse(const uint8_t *packet, uint32_t length, wifi_control_packet_t *out);
/* Diagnostic SDPCM envelope parsing; does not interpret non-control channels. */
int wifi_control_parse_raw(const uint8_t *packet, uint32_t length, wifi_control_packet_t *out);
int wifi_control_version(const wifi_control_packet_t *packet, uint16_t id, char output[192]);
uint32_t wifi_data_frame(uint8_t packet[WIFI_CONTROL_MAX], uint8_t sequence,
                         uint8_t interface, const uint8_t *frame, uint32_t length);
/* Explicit oversized-TX diagnostic; does not change Ethernet RX or the normal MTU. */
uint32_t wifi_data_frame_diagnostic(uint8_t packet[WIFI_CONTROL_MAX], uint8_t sequence,
                                    uint8_t interface, const uint8_t *frame, uint32_t length);
int wifi_data_decode(const wifi_control_packet_t *packet, uint8_t *interface,
                     const uint8_t **frame, uint32_t *length);
/* Explicit oversized-RX diagnostic; use with a validated SDPCM packet, not the normal RX queue. */
int wifi_data_decode_diagnostic(const wifi_control_packet_t *packet, uint8_t *interface,
                                const uint8_t **frame, uint32_t *length);

#endif
