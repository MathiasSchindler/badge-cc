#ifndef PICOCTL_MESHTASTIC_H
#define PICOCTL_MESHTASTIC_H

#include <stdint.h>
#include <stddef.h>

/*
 * Host-side, receive-only Meshtastic decoding for picoctl listen.
 * Wire format (firmware RadioInterface.h / CryptoEngine.cpp):
 *   16-byte little-endian header: to, from, id, flags, channel hash,
 *   next_hop, relay_node; then the channel-encrypted meshtastic_Data protobuf.
 *   AES-CTR, 128- or 256-bit key, IV = id (u64 LE) | from (u32 LE) | 32-bit
 *   big-endian block counter starting at 0.
 * The channel PSK never leaves this host process.
 */

#define MESH_HEADER_SIZE 16u
#define MESH_BROADCAST 0xffffffffu
#define MESH_MAX_CHANNELS 8u

typedef struct {
    uint32_t to, from, id;
    uint8_t flags, channel, next_hop, relay_node;
    uint8_t hop_limit, hop_start, want_ack, via_mqtt;
} mesh_header_t;

typedef struct {
    char name[32];
    uint8_t key[32];
    uint32_t key_length; /* 0 (no encryption), 16 or 32 */
    uint8_t hash;
} mesh_channel_t;

typedef struct {
    uint32_t portnum;
    const uint8_t *payload;
    uint32_t payload_length;
    uint32_t dest, source, request_id, reply_id, emoji;
    uint8_t want_response, has_portnum;
} mesh_data_t;

typedef struct {
    uint32_t rounds;
    uint8_t round_keys[240];
} mesh_aes_t;

int mesh_base64_decode(const char *text, uint8_t *output, uint32_t capacity, uint32_t *size);
/* Meshtastic PSK expansion: 1 byte selects the well-known default key. */
int mesh_channel_init(mesh_channel_t *channel, const char *name,
                      const uint8_t *psk, uint32_t psk_length);
/* "NAME BASE64PSK" line; leading/trailing whitespace ignored. */
int mesh_channel_parse(mesh_channel_t *channel, const char *line);
uint8_t mesh_channel_hash(const char *name, const uint8_t *key, uint32_t key_length);

int mesh_header(const uint8_t *frame, uint32_t length, mesh_header_t *header);
int mesh_aes_init(mesh_aes_t *aes, const uint8_t *key, uint32_t key_length);
void mesh_aes_encrypt(const mesh_aes_t *aes, const uint8_t input[16], uint8_t output[16]);
void mesh_ctr_iv(const uint8_t *key, uint32_t key_length, const uint8_t iv[16],
                 uint8_t *data, uint32_t length);
void mesh_ctr(const uint8_t *key, uint32_t key_length, uint32_t from, uint32_t id,
              uint8_t *data, uint32_t length);
/* Strict proto3 parse of meshtastic_Data; requires a well-formed buffer. */
int mesh_data_decode(const uint8_t *data, uint32_t length, mesh_data_t *out);
const char *mesh_port_name(uint32_t portnum);

/*
 * Decode one received frame into a single printable UTF-8 line (no newline),
 * trying every channel whose hash matches. Returns 1 when decrypted.
 */
int mesh_describe(const uint8_t *frame, uint32_t length,
                  const mesh_channel_t *channels, uint32_t channel_count,
                  char *output, size_t capacity);

#endif
