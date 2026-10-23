#include "wifi_control.h"
#include "../shared/bytes.h"

static uint32_t read32(const uint8_t *data) {
    return badger_bytes_load_u32(data);
}

static void store(uint8_t *data, uint32_t value) {
    badger_bytes_store_u32(data, value);
}

uint32_t wifi_control_request(uint8_t packet[WIFI_CONTROL_MAX], uint8_t sequence,
                              uint16_t id, uint32_t command, uint8_t set,
                              const uint8_t *payload, uint32_t size) {
    if (packet == 0 || id == 0u || set > 1u || size > WIFI_CONTROL_MAX - 28u ||
        (size != 0u && payload == 0)) return 0u;
    uint32_t total = size + 28u;
    badger_bytes_zero(packet, total);
    packet[0] = (uint8_t)total;
    packet[1] = (uint8_t)(total >> 8u);
    packet[2] = (uint8_t)~total;
    packet[3] = (uint8_t)(~total >> 8u);
    packet[4] = sequence;
    packet[7] = 12u;
    store(packet + 12u, command);
    store(packet + 16u, size);
    store(packet + 20u, (uint32_t)id << 16u | (set ? 2u : 0u));
    badger_bytes_copy(packet + 28u, payload, size);
    return total;
}

static int parse(const uint8_t *packet, uint32_t length, wifi_control_packet_t *out,
                  uint8_t raw) {
    if (packet == 0 || out == 0 || length < 12u || length > WIFI_CONTROL_MAX) return 0;
    uint32_t size = (uint32_t)packet[0] | (uint32_t)packet[1] << 8u;
    uint32_t inverse = (uint32_t)packet[2] | (uint32_t)packet[3] << 8u;
    uint32_t header = packet[7];
    if ((size ^ inverse) != 0xffffu || size < 12u || size > length ||
        header < 12u || header > size || (!raw && (packet[5] & 15u) > 2u)) return 0;
    out->size = (uint16_t)size;
    out->channel = packet[5] & 15u;
    out->flow = packet[8];
    out->credit = packet[9];
    out->command = out->flags = out->status = out->id = 0u;
    out->payload = packet + header;
    out->payload_size = size - header;
    if (out->channel != 0u || size == header) return 1;
    if (size - header < 16u) return 0;
    out->command = read32(packet + header);
    uint32_t declared = read32(packet + header + 4u) & 0xffffu;
    out->flags = read32(packet + header + 8u);
    out->status = read32(packet + header + 12u);
    out->id = (uint16_t)(out->flags >> 16u);
    if (declared > size - header - 16u) return 0;
    out->payload = packet + header + 16u;
    out->payload_size = declared;
    return 1;
}

int wifi_control_parse(const uint8_t *packet, uint32_t length, wifi_control_packet_t *out) {
    return parse(packet, length, out, 0u);
}

int wifi_control_parse_raw(const uint8_t *packet, uint32_t length, wifi_control_packet_t *out) {
    return parse(packet, length, out, 1u);
}

int wifi_control_version(const wifi_control_packet_t *packet, uint16_t id, char output[192]) {
    if (output == 0) return 0;
    output[0] = '\0';
    if (packet == 0 || packet->channel != 0u || packet->command != 262u ||
        packet->id != id || packet->status != 0u || (packet->flags & 0xf003u) != 0u) return 0;
    uint32_t text_length = 0u;
    int ending = 0;
    for (uint32_t i = 0u; i < packet->payload_size && i < 192u; ++i) {
        uint8_t byte = packet->payload[i];
        if (byte == 0u) {
            output[text_length] = '\0';
            return text_length != 0u;
        }

        if (byte == '\r' || byte == '\n') {
            ending = 1;
            continue;
        }
        if (ending || byte < 0x20u || byte > 0x7eu || i == 191u) {
            output[0] = '\0';
            return 0;
        }
        output[i] = (char)byte;
        ++text_length;
    }
    output[0] = '\0';
    return 0;
}

static uint32_t data_frame(uint8_t packet[WIFI_CONTROL_MAX], uint8_t sequence,
                          uint8_t interface, const uint8_t *frame, uint32_t length,
                          uint32_t maximum) {
    if (packet == 0 || frame == 0 || interface > 1u || length < 14u || length > maximum) return 0u;
    uint32_t size = length + 18u, aligned = (size + 3u) & ~3u;
    badger_bytes_zero(packet, aligned);
    packet[0] = (uint8_t)size; packet[1] = (uint8_t)(size >> 8u);
    packet[2] = (uint8_t)~size; packet[3] = (uint8_t)(~size >> 8u);
    packet[4] = sequence;
    packet[5] = 2u;
    packet[7] = 14u;
    packet[14] = 0x20u;
    packet[16] = interface;
    badger_bytes_copy(packet + 18u, frame, length);
    return aligned;
}

uint32_t wifi_data_frame(uint8_t packet[WIFI_CONTROL_MAX], uint8_t sequence,
                         uint8_t interface, const uint8_t *frame, uint32_t length) {
    return data_frame(packet, sequence, interface, frame, length, WIFI_ETHERNET_MAX);
}

uint32_t wifi_data_frame_diagnostic(uint8_t packet[WIFI_CONTROL_MAX], uint8_t sequence,
                                    uint8_t interface, const uint8_t *frame, uint32_t length) {
    return data_frame(packet, sequence, interface, frame, length, WIFI_CONTROL_MAX - 18u);
}

static int data_decode(const wifi_control_packet_t *packet, uint8_t *interface,
                        const uint8_t **frame, uint32_t *length, uint32_t maximum) {
    if (packet == 0 || interface == 0 || frame == 0 || length == 0 ||
        packet->channel != 2u || packet->payload == 0 || packet->payload_size < 4u) return 0;
    const uint8_t *data = packet->payload;
    uint32_t offset = 4u + (uint32_t)data[3] * 4u;
    if ((data[0] >> 4u) != 2u || (data[2] & 15u) > 1u || offset > packet->payload_size ||
        packet->payload_size - offset < 14u || packet->payload_size - offset > maximum) return 0;
    *interface = data[2] & 15u;
    *frame = data + offset;
    *length = packet->payload_size - offset;
    return 1;
}

int wifi_data_decode(const wifi_control_packet_t *packet, uint8_t *interface,
                     const uint8_t **frame, uint32_t *length) {
    return data_decode(packet, interface, frame, length, WIFI_ETHERNET_MAX);
}

int wifi_data_decode_diagnostic(const wifi_control_packet_t *packet, uint8_t *interface,
                                const uint8_t **frame, uint32_t *length) {
    return data_decode(packet, interface, frame, length, WIFI_CONTROL_MAX - 16u);
}
