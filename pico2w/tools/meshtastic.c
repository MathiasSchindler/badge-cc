#include "meshtastic.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const uint8_t sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

/* Firmware Channels.cpp defaultpsk, selected by a 1-byte PSK of 1 ("AQ=="). */
static const uint8_t default_psk[16] = {
    0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
    0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01
};

static uint8_t xtime(uint8_t x) {
    return (uint8_t)((x << 1) ^ ((x & 0x80u) ? 0x1bu : 0u));
}

int mesh_aes_init(mesh_aes_t *aes, const uint8_t *key, uint32_t key_length) {
    if (key_length != 16u && key_length != 32u) return 0;
    uint32_t nk = key_length / 4u;
    aes->rounds = nk + 6u;
    uint32_t words = 4u * (aes->rounds + 1u);
    uint8_t *w = aes->round_keys, rcon = 1u;
    memcpy(w, key, key_length);
    for (uint32_t i = nk; i < words; ++i) {
        uint8_t t[4];
        memcpy(t, w + 4u * (i - 1u), 4u);
        if (i % nk == 0u) {
            uint8_t first = t[0];
            t[0] = (uint8_t)(sbox[t[1]] ^ rcon);
            t[1] = sbox[t[2]];
            t[2] = sbox[t[3]];
            t[3] = sbox[first];
            rcon = xtime(rcon);
        } else if (nk > 6u && i % nk == 4u) {
            for (uint32_t j = 0u; j < 4u; ++j) t[j] = sbox[t[j]];
        }
        for (uint32_t j = 0u; j < 4u; ++j)
            w[4u * i + j] = (uint8_t)(w[4u * (i - nk) + j] ^ t[j]);
    }
    return 1;
}

void mesh_aes_encrypt(const mesh_aes_t *aes, const uint8_t input[16], uint8_t output[16]) {
    uint8_t s[16], t[16];
    for (uint32_t i = 0u; i < 16u; ++i) s[i] = (uint8_t)(input[i] ^ aes->round_keys[i]);
    for (uint32_t round = 1u; round <= aes->rounds; ++round) {
        /* SubBytes + ShiftRows; the state is column-major (s[row + 4 * column]). */
        for (uint32_t c = 0u; c < 4u; ++c)
            for (uint32_t r = 0u; r < 4u; ++r)
                t[r + 4u * c] = sbox[s[r + 4u * ((c + r) & 3u)]];
        if (round != aes->rounds) {
            for (uint32_t c = 0u; c < 4u; ++c) {
                uint8_t *a = t + 4u * c;
                uint8_t all = (uint8_t)(a[0] ^ a[1] ^ a[2] ^ a[3]), first = a[0];
                a[0] ^= (uint8_t)(all ^ xtime((uint8_t)(a[0] ^ a[1])));
                a[1] ^= (uint8_t)(all ^ xtime((uint8_t)(a[1] ^ a[2])));
                a[2] ^= (uint8_t)(all ^ xtime((uint8_t)(a[2] ^ a[3])));
                a[3] ^= (uint8_t)(all ^ xtime((uint8_t)(a[3] ^ first)));
            }
        }
        for (uint32_t i = 0u; i < 16u; ++i) s[i] = (uint8_t)(t[i] ^ aes->round_keys[16u * round + i]);
    }
    memcpy(output, s, 16u);
}

void mesh_ctr_iv(const uint8_t *key, uint32_t key_length, const uint8_t iv[16],
                 uint8_t *data, uint32_t length) {
    mesh_aes_t aes;
    uint8_t counter[16], stream[16];
    if (!mesh_aes_init(&aes, key, key_length)) return;
    memcpy(counter, iv, 16u);
    for (uint32_t offset = 0u; offset < length; offset += 16u) {
        mesh_aes_encrypt(&aes, counter, stream);
        for (uint32_t i = 0u; i < 16u && offset + i < length; ++i) data[offset + i] ^= stream[i];
        /* Arduino Crypto CTR with setCounterSize(4): big-endian bytes 12..15. */
        for (uint32_t i = 16u; i > 12u && ++counter[i - 1u] == 0u; --i) {}
    }
}

void mesh_ctr(const uint8_t *key, uint32_t key_length, uint32_t from, uint32_t id,
              uint8_t *data, uint32_t length) {
    uint8_t iv[16] = {0};
    for (uint32_t i = 0u; i < 4u; ++i) {
        iv[i] = (uint8_t)(id >> (8u * i));
        iv[8u + i] = (uint8_t)(from >> (8u * i));
    }
    mesh_ctr_iv(key, key_length, iv, data, length);
}

static int base64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

int mesh_base64_decode(const char *text, uint8_t *output, uint32_t capacity, uint32_t *size) {
    uint32_t bits = 0u, count = 0u, used = 0u, padding = 0u;
    uint32_t characters = 0u;
    for (const char *p = text; *p; ++p) {
        if (*p == '=') { ++padding; continue; }
        int value = base64_value(*p);
        if (value < 0 || padding) return 0;
        ++characters;
        bits = (bits << 6u) | (uint32_t)value;
        count += 6u;
        if (count >= 8u) {
            count -= 8u;
            if (used == capacity) return 0;
            output[used++] = (uint8_t)(bits >> count);
        }
    }
    if (characters % 4u == 1u || (padding && (characters + padding) % 4u != 0u)) return 0;
    if (padding > 2u || count >= 6u || (bits & ((1u << count) - 1u)) != 0u) return 0;
    *size = used;
    return 1;
}

uint8_t mesh_channel_hash(const char *name, const uint8_t *key, uint32_t key_length) {
    uint8_t hash = 0u;
    for (const char *p = name; *p; ++p) hash ^= (uint8_t)*p;
    for (uint32_t i = 0u; i < key_length; ++i) hash ^= key[i];
    return hash;
}

int mesh_channel_init(mesh_channel_t *channel, const char *name,
                      const uint8_t *psk, uint32_t psk_length) {
    size_t name_length = strlen(name);
    if (name_length == 0u || name_length >= sizeof(channel->name) || psk_length > 32u) return 0;
    memset(channel, 0, sizeof(*channel));
    memcpy(channel->name, name, name_length);
    if (psk_length == 1u) {
        if (psk[0] != 0u) {
            memcpy(channel->key, default_psk, 16u);
            channel->key[15] = (uint8_t)(channel->key[15] + psk[0] - 1u);
            channel->key_length = 16u;
        }
    } else if (psk_length != 0u) {
        memcpy(channel->key, psk, psk_length);
        channel->key_length = psk_length <= 16u ? 16u : 32u;
    }
    channel->hash = mesh_channel_hash(channel->name, channel->key, channel->key_length);
    return 1;
}

static int space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

int mesh_channel_parse(mesh_channel_t *channel, const char *line) {
    char name[32], encoded[64];
    uint8_t psk[48];
    uint32_t size, n = 0u, e = 0u;
    while (space(*line)) ++line;
    while (*line && !space(*line)) {
        if (n + 1u >= sizeof(name)) return 0;
        name[n++] = *line++;
    }
    name[n] = 0;
    while (space(*line)) ++line;
    while (*line && !space(*line)) {
        if (e + 1u >= sizeof(encoded)) return 0;
        encoded[e++] = *line++;
    }
    encoded[e] = 0;
    while (space(*line)) ++line;
    if (!n || !e || *line) return 0;
    if (!mesh_base64_decode(encoded, psk, sizeof(psk), &size)) return 0;
    int ok = mesh_channel_init(channel, name, psk, size);
    memset(psk, 0, sizeof(psk));
    memset(encoded, 0, sizeof(encoded));
    return ok;
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8u | (uint32_t)p[2] << 16u | (uint32_t)p[3] << 24u;
}

int mesh_header(const uint8_t *frame, uint32_t length, mesh_header_t *header) {
    if (length < MESH_HEADER_SIZE) return 0;
    header->to = le32(frame);
    header->from = le32(frame + 4u);
    header->id = le32(frame + 8u);
    header->flags = frame[12];
    header->channel = frame[13];
    header->next_hop = frame[14];
    header->relay_node = frame[15];
    header->hop_limit = (uint8_t)(frame[12] & 7u);
    header->want_ack = (uint8_t)((frame[12] >> 3u) & 1u);
    header->via_mqtt = (uint8_t)((frame[12] >> 4u) & 1u);
    header->hop_start = (uint8_t)(frame[12] >> 5u);
    return 1;
}

typedef struct {
    const uint8_t *data;
    uint32_t length, position;
} pb_t;

typedef struct {
    uint32_t field, wire;
    uint64_t value;
    const uint8_t *bytes;
    uint32_t size;
} pb_field_t;

static int pb_varint(pb_t *pb, uint64_t *value) {
    *value = 0u;
    for (uint32_t shift = 0u; shift < 70u; shift += 7u) {
        if (pb->position == pb->length) return 0;
        uint8_t byte = pb->data[pb->position++];
        if (shift == 63u && byte > 1u) return 0;
        *value |= (uint64_t)(byte & 0x7fu) << shift;
        if (!(byte & 0x80u)) return 1;
    }
    return 0;
}

/* 1: field read, 0: clean end, -1: malformed. */
static int pb_next(pb_t *pb, pb_field_t *f) {
    uint64_t key;
    if (pb->position == pb->length) return 0;
    if (!pb_varint(pb, &key) || key >> 32u) return -1;
    f->field = (uint32_t)(key >> 3u);
    f->wire = (uint32_t)(key & 7u);
    f->value = 0u;
    f->bytes = 0;
    f->size = 0u;
    if (f->field == 0u) return -1;
    if (f->wire == 0u) return pb_varint(pb, &f->value) ? 1 : -1;
    uint32_t size = f->wire == 1u ? 8u : f->wire == 5u ? 4u : 0u;
    if (f->wire == 2u) {
        uint64_t length;
        if (!pb_varint(pb, &length) || length > pb->length - pb->position) return -1;
        size = (uint32_t)length;
    } else if (!size) return -1;
    if (size > pb->length - pb->position) return -1;
    f->bytes = pb->data + pb->position;
    f->size = size;
    if (f->wire == 5u) f->value = le32(f->bytes);
    else if (f->wire == 1u) f->value = le32(f->bytes) | (uint64_t)le32(f->bytes + 4u) << 32u;
    pb->position += size;
    return 1;
}

int mesh_data_decode(const uint8_t *data, uint32_t length, mesh_data_t *out) {
    static const uint8_t wires[11] = {9, 0, 2, 0, 5, 5, 5, 5, 5, 0, 2};
    pb_t pb = {data, length, 0u};
    pb_field_t f;
    int result;
    memset(out, 0, sizeof(*out));
    while ((result = pb_next(&pb, &f)) == 1) {
        if (f.field <= 10u && f.wire != wires[f.field]) return 0;
        switch (f.field) {
        case 1u: out->portnum = (uint32_t)f.value; out->has_portnum = f.value <= 511u; break;
        case 2u: out->payload = f.bytes; out->payload_length = f.size; break;
        case 3u: out->want_response = f.value != 0u; break;
        case 4u: out->dest = (uint32_t)f.value; break;
        case 5u: out->source = (uint32_t)f.value; break;
        case 6u: out->request_id = (uint32_t)f.value; break;
        case 7u: out->reply_id = (uint32_t)f.value; break;
        case 8u: out->emoji = (uint32_t)f.value; break;
        default: break;
        }
    }
    return result == 0 && out->has_portnum;
}

const char *mesh_port_name(uint32_t portnum) {
    static const struct { uint32_t number; const char *name; } ports[] = {
        {0, "UNKNOWN_APP"}, {1, "TEXT_MESSAGE_APP"}, {2, "REMOTE_HARDWARE_APP"},
        {3, "POSITION_APP"}, {4, "NODEINFO_APP"}, {5, "ROUTING_APP"}, {6, "ADMIN_APP"},
        {7, "TEXT_MESSAGE_COMPRESSED_APP"}, {8, "WAYPOINT_APP"}, {9, "AUDIO_APP"},
        {10, "DETECTION_SENSOR_APP"}, {11, "ALERT_APP"}, {12, "KEY_VERIFICATION_APP"},
        {13, "REMOTE_SHELL_APP"}, {32, "REPLY_APP"}, {33, "IP_TUNNEL_APP"},
        {34, "PAXCOUNTER_APP"}, {35, "STORE_FORWARD_PLUSPLUS_APP"}, {36, "NODE_STATUS_APP"},
        {37, "MESH_BEACON_APP"}, {38, "PAGING_APP"}, {64, "SERIAL_APP"},
        {65, "STORE_FORWARD_APP"}, {66, "RANGE_TEST_APP"}, {67, "TELEMETRY_APP"},
        {68, "ZPS_APP"}, {69, "SIMULATOR_APP"}, {70, "TRACEROUTE_APP"},
        {71, "NEIGHBORINFO_APP"}, {72, "ATAK_PLUGIN"}, {73, "MAP_REPORT_APP"},
        {74, "POWERSTRESS_APP"}, {75, "LORAWAN_BRIDGE"}, {76, "RETICULUM_TUNNEL_APP"},
        {77, "CAYENNE_APP"}, {79, "LORA_OTA_APP"}, {112, "GROUPALARM_APP"},
        {256, "PRIVATE_APP"}, {257, "ATAK_FORWARDER"}
    };
    for (size_t i = 0u; i < sizeof(ports) / sizeof(ports[0]); ++i)
        if (ports[i].number == portnum) return ports[i].name;
    return "PORT";
}

typedef struct {
    char *text;
    size_t capacity, used;
} out_t;

static void put(out_t *out, const char *format, ...) {
    if (out->used >= out->capacity) return;
    va_list args;
    va_start(args, format);
    int n = vsnprintf(out->text + out->used, out->capacity - out->used, format, args);
    va_end(args);
    if (n < 0) return;
    out->used += (size_t)n;
    if (out->used >= out->capacity) out->used = out->capacity - 1u;
}

/* Quoted UTF-8 text; control characters and invalid sequences become '?'. */
static void put_text(out_t *out, const uint8_t *text, uint32_t length) {
    put(out, "\"");
    for (uint32_t i = 0u; i < length;) {
        uint8_t c = text[i];
        uint32_t extra = c >= 0xf0u && c <= 0xf4u ? 3u : c >= 0xe0u && c < 0xf0u ? 2u :
                         c >= 0xc2u && c < 0xe0u ? 1u : 0u;
        int valid = c >= 0x20u && c != 0x7fu && (c < 0x80u || extra != 0u) && i + extra < length;
        for (uint32_t j = 1u; valid && j <= extra; ++j)
            valid = (text[i + j] & 0xc0u) == 0x80u;
        if (!valid) { put(out, "?"); ++i; continue; }
        if (c == '"' || c == '\\') put(out, "\\%c", c);
        else put(out, "%.*s", (int)(extra + 1u), (const char *)text + i);
        i += extra + 1u;
    }
    put(out, "\"");
}

static float as_float(uint64_t value) {
    uint32_t bits = (uint32_t)value;
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

static void describe_position(out_t *out, const uint8_t *data, uint32_t length) {
    pb_t pb = {data, length, 0u};
    pb_field_t f;
    int has_lat = 0, has_lon = 0;
    int32_t lat = 0, lon = 0;
    while (pb_next(&pb, &f) == 1) {
        if (f.field == 1u && f.wire == 5u) { lat = (int32_t)(uint32_t)f.value; has_lat = 1; }
        else if (f.field == 2u && f.wire == 5u) { lon = (int32_t)(uint32_t)f.value; has_lon = 1; }
        else if (f.field == 3u && f.wire == 0u) put(out, " alt=%ldm", (long)(int32_t)f.value);
        else if (f.field == 14u && f.wire == 0u) put(out, " sats=%lu", (unsigned long)f.value);
    }
    if (has_lat && has_lon) put(out, " lat=%.5f lon=%.5f", lat * 1e-7, lon * 1e-7);
}

static void describe_user(out_t *out, const uint8_t *data, uint32_t length) {
    pb_t pb = {data, length, 0u};
    pb_field_t f;
    while (pb_next(&pb, &f) == 1) {
        if (f.wire == 2u && f.field >= 1u && f.field <= 3u) {
            put(out, f.field == 1u ? " id=" : f.field == 2u ? " long=" : " short=");
            put_text(out, f.bytes, f.size);
        } else if (f.field == 5u && f.wire == 0u) put(out, " hw=%lu", (unsigned long)f.value);
        else if (f.field == 7u && f.wire == 0u) put(out, " role=%lu", (unsigned long)f.value);
    }
}

static void describe_metrics(out_t *out, uint32_t kind, const uint8_t *data, uint32_t length) {
    static const char *device[] = {0, "battery=%.0f%%", "voltage=%.2fV", "chutil=%.1f%%",
                                   "airtx=%.1f%%", "uptime=%.0fs"};
    static const char *environment[] = {0, "temp=%.1fC", "rh=%.0f%%", "pressure=%.1fhPa"};
    pb_t pb = {data, length, 0u};
    pb_field_t f;
    while (pb_next(&pb, &f) == 1) {
        double value = f.wire == 5u ? (double)as_float(f.value) : (double)f.value;
        if (f.wire != 0u && f.wire != 5u) continue;
        if (kind == 2u && f.field >= 1u && f.field <= 5u) {
            put(out, " ");
            put(out, device[f.field], value);
        } else if (kind == 3u && f.field >= 1u && f.field <= 3u) {
            put(out, " ");
            put(out, environment[f.field], value);
        }
    }
}

static void describe_telemetry(out_t *out, const uint8_t *data, uint32_t length) {
    pb_t pb = {data, length, 0u};
    pb_field_t f;
    while (pb_next(&pb, &f) == 1) {
        if (f.wire != 2u) continue;
        if (f.field == 2u) { put(out, " device:"); describe_metrics(out, 2u, f.bytes, f.size); }
        else if (f.field == 3u) { put(out, " environment:"); describe_metrics(out, 3u, f.bytes, f.size); }
        else put(out, " metrics#%lu", (unsigned long)f.field);
    }
}

static void describe_data(out_t *out, const mesh_data_t *d) {
    put(out, " %s", mesh_port_name(d->portnum));
    if (!strcmp(mesh_port_name(d->portnum), "PORT")) put(out, "#%lu", (unsigned long)d->portnum);
    switch (d->portnum) {
    case 1u: case 10u: case 11u: case 66u:
        put(out, " ");
        put_text(out, d->payload, d->payload_length);
        break;
    case 3u: describe_position(out, d->payload, d->payload_length); break;
    case 4u: describe_user(out, d->payload, d->payload_length); break;
    case 67u: describe_telemetry(out, d->payload, d->payload_length); break;
    default: put(out, " %lu bytes", (unsigned long)d->payload_length); break;
    }
    if (d->request_id) put(out, " request=0x%08lx", (unsigned long)d->request_id);
    if (d->reply_id) put(out, " reply=0x%08lx", (unsigned long)d->reply_id);
    if (d->emoji) put(out, " emoji");
    if (d->want_response) put(out, " want_response");
}

static void put_node(out_t *out, uint32_t node) {
    if (node == MESH_BROADCAST) put(out, "^all");
    else put(out, "!%08lx", (unsigned long)node);
}

int mesh_describe(const uint8_t *frame, uint32_t length,
                  const mesh_channel_t *channels, uint32_t channel_count,
                  char *output, size_t capacity) {
    out_t out = {output, capacity, 0u};
    mesh_header_t h;
    if (!capacity) return 0;
    output[0] = 0;
    if (!mesh_header(frame, length, &h)) {
        put(&out, "short frame (%lu bytes), not a Meshtastic header", (unsigned long)length);
        return 0;
    }
    put_node(&out, h.from);
    put(&out, " -> ");
    put_node(&out, h.to);
    put(&out, " id=0x%08lx hop=%u/%u ch=0x%02x relay=0x%02x", (unsigned long)h.id,
        h.hop_limit, h.hop_start, h.channel, h.relay_node);
    if (h.want_ack) put(&out, " want_ack");
    if (h.via_mqtt) put(&out, " via_mqtt");
    uint32_t size = length - MESH_HEADER_SIZE;
    for (uint32_t i = 0u; i < channel_count; ++i) {
        const mesh_channel_t *c = channels + i;
        uint8_t plain[256];
        mesh_data_t d;
        if (c->hash != h.channel) continue;
        memcpy(plain, frame + MESH_HEADER_SIZE, size);
        if (c->key_length) mesh_ctr(c->key, c->key_length, h.from, h.id, plain, size);
        if (!mesh_data_decode(plain, size, &d)) continue;
        put(&out, " | [%s]", c->name);
        describe_data(&out, &d);
        return 1;
    }
    /* Unknown hash: the channel may use a known key under another name (the
       hash mixes both). Accept only a strict parse on a known port. */
    for (uint32_t i = 0u; h.channel != 0u && i <= channel_count; ++i) {
        const uint8_t *key = i < channel_count ? channels[i].key : default_psk;
        uint32_t key_length = i < channel_count ? channels[i].key_length : 16u;
        uint8_t plain[256];
        mesh_data_t d;
        if (!key_length) continue;
        memcpy(plain, frame + MESH_HEADER_SIZE, size);
        mesh_ctr(key, key_length, h.from, h.id, plain, size);
        if (!mesh_data_decode(plain, size, &d) || !strcmp(mesh_port_name(d.portnum), "PORT"))
            continue;
        if (i < channel_count) put(&out, " | [other channel, key of %s]", channels[i].name);
        else put(&out, " | [other channel, default key]");
        describe_data(&out, &d);
        return 1;
    }
    if (h.channel == 0u && h.to != MESH_BROADCAST)
        put(&out, " | PKI direct message, %lu bytes (not decryptable with a channel key)",
            (unsigned long)size);
    else put(&out, " | encrypted, %lu bytes (no matching channel key)", (unsigned long)size);
    return 0;
}
