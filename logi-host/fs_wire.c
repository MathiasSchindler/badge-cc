#include "fs.h"
#include "../src/shared/bytes.h"

static uint32_t decode[14][511];
static uint16_t crc_table[256];

static uint16_t crc_byte(uint16_t crc, uint8_t byte) {
    return (uint16_t)((crc >> 8u) ^ crc_table[(crc ^ byte) & 255u]);
}

uint8_t fs_crc5(uint16_t bits) {
    uint8_t crc = 31u;
    for (uint8_t i = 0u; i < 11u; ++i) {
        uint8_t feedback = (uint8_t)((crc ^ (bits >> i)) & 1u);
        crc >>= 1u;
        if (feedback) crc ^= 0x14u;
    }
    return crc ^ 31u;
}

uint16_t fs_crc16(const uint8_t *bytes, uint16_t length) {
    uint16_t crc = 0xffffu;
    for (uint16_t i = 0u; i < length; ++i) crc = crc_byte(crc, bytes[i]);
    return (uint16_t)~crc;
}

void fs_wire_init(void) {
    for (uint32_t i = 0u; i < 256u; ++i) {
        uint16_t crc = (uint16_t)i;
        for (uint8_t j = 0u; j < 8u; ++j) crc = (uint16_t)((crc >> 1u) ^ ((crc & 1u) ? 0xa001u : 0u));
        crc_table[i] = crc;
    }
    for (uint8_t state = 0u; state < 14u; ++state)
    for (uint8_t size = 1u; size <= 8u; ++size)
    for (uint32_t sample = 0u; sample < (1u << size); ++sample) {
        uint8_t previous = state & 1u, ones = state >> 1u, count = 0u, byte = 0u;
        uint32_t error = 0u;
        for (uint8_t i = 0u; i < size; ++i) {
            uint8_t pin = (uint8_t)(sample >> i) & 1u, bit = pin == previous;
            previous = pin;
            if (ones == 6u) {
                if (bit) error = 0x10000u;
                ones = 0u;
            } else {
                byte |= bit << count++;
                ones = bit ? (uint8_t)(ones + 1u) : 0u;
            }
        }
        decode[state][(1u << size) - 1u + sample] = byte | (uint32_t)count << 8u |
                                 (uint32_t)((ones << 1u) | previous) << 12u | error;
    }
}

static int32_t symbol(fs_tx_t *tx, uint8_t state) {
    if (tx->count >= FS_TX_WORDS * 16u) return FS_OVERFLOW;
    uint16_t word = tx->count / 16u, shift = 2u * (tx->count % 16u);
    if (!shift) tx->words[word] = 0u;
    tx->words[word] |= (uint32_t)state << shift;
    ++tx->count;
    return 0;
}

static int32_t encode(fs_tx_t *tx, const uint8_t *bytes, uint8_t length) {
    if (!length || tx->count > FS_TX_WORDS * 16u) return FS_LENGTH;
    /* Reject before writing, including worst-case stuffing and an interpacket gap. */
    uint32_t bound = ((uint32_t)length + 1u) * 8u;
    bound += bound / 6u + 4u + (tx->count ? 2u : 0u);
    if (tx->count + bound > FS_TX_WORDS * 16u) return FS_OVERFLOW;
    if (tx->count) { (void)symbol(tx, 1u); (void)symbol(tx, 1u); }
    uint8_t line = 1u, ones = 0u;
    for (uint8_t byte = 0u; byte <= length; ++byte) {
        uint8_t value = byte ? bytes[byte - 1u] : 0x80u;
        for (uint8_t bit = 0u; bit < 8u; ++bit) {
            if (!((value >> bit) & 1u)) { line ^= 3u; ones = 0u; }
            else ++ones;
            (void)symbol(tx, line);
            if (ones == 6u) { line ^= 3u; (void)symbol(tx, line); ones = 0u; }
        }
    }
    (void)symbol(tx, 0u); (void)symbol(tx, 0u); (void)symbol(tx, 1u);
    ++tx->packets;
    return 0;
}

int32_t fs_token(fs_tx_t *tx, uint8_t pid, uint8_t address, uint8_t endpoint) {
    if ((pid != FS_OUT && pid != FS_IN && pid != FS_SETUP) || address > 127u || endpoint > 15u)
        return FS_PID;
    uint16_t bits = (uint16_t)(address | (uint16_t)endpoint << 7u);
    uint8_t bytes[] = {pid, (uint8_t)bits, (uint8_t)((bits >> 8u) | fs_crc5(bits) << 3u)};
    return encode(tx, bytes, 3u);
}

int32_t fs_sof(fs_tx_t *tx, uint16_t frame) {
    if (frame > 2047u) return FS_LENGTH;
    uint8_t bytes[] = {FS_SOF, (uint8_t)frame, (uint8_t)((frame >> 8u) | fs_crc5(frame) << 3u)};
    return encode(tx, bytes, 3u);
}

int32_t fs_data(fs_tx_t *tx, uint8_t pid, const uint8_t *bytes, uint8_t length) {
    if ((pid != FS_DATA0 && pid != FS_DATA1) || length > 64u || (length && !bytes)) return FS_LENGTH;
    uint8_t packet[67];
    packet[0] = pid;
    for (uint8_t i = 0u; i < length; ++i) packet[1u + i] = bytes[i];
    uint16_t crc = fs_crc16(bytes, length);
    packet[1u + length] = (uint8_t)crc;
    packet[2u + length] = (uint8_t)(crc >> 8u);
    return encode(tx, packet, (uint8_t)(length + 3u));
}

int32_t fs_handshake(fs_tx_t *tx, uint8_t pid) {
    if (pid != FS_ACK && pid != FS_NAK && pid != FS_STALL_PID) return FS_PID;
    return encode(tx, &pid, 1u);
}

void fs_rx_init(fs_rx_t *rx) {
    badger_bytes_zero((uint8_t *)rx, sizeof(*rx));
    rx->crc = 0xffffu;
    rx->state = 1u;
}

int32_t fs_rx_feed(fs_rx_t *rx, uint16_t samples, uint8_t count) {
    if (rx->error) return rx->error;
    if (!count || count > 8u) return rx->error = FS_LENGTH;
    uint16_t mask = (uint16_t)(0xffffu >> (16u - 2u * count));
    samples &= mask;
    if (((samples ^ (samples >> 1u)) & 0x5555u & mask) != (0x5555u & mask))
        return rx->error = FS_LINE;
    uint32_t dp = samples & 0x5555u;
    dp = (dp | dp >> 1u) & 0x3333u;
    dp = (dp | dp >> 2u) & 0x0f0fu;
    dp = (dp | dp >> 4u) & 0xffu;
    uint32_t decoded = decode[rx->state][(1u << count) - 1u + dp];
    if (decoded & 0x10000u) return rx->error = FS_ENCODING;
    rx->state = (uint8_t)(decoded >> 12u) & 15u;
    rx->pending |= (decoded & 255u) << rx->bits;
    rx->bits += (uint8_t)(decoded >> 8u) & 15u;
    if (rx->bits >= 8u) {
        if (rx->length == FS_PACKET_BYTES) return rx->error = FS_OVERFLOW;
        uint8_t byte = (uint8_t)rx->pending;
        rx->bytes[rx->length++] = byte;
        if (rx->length > 2u) rx->crc = crc_byte(rx->crc, byte);
        rx->pending >>= 8u;
        rx->bits -= 8u;
    }
    return 0;
}

int32_t fs_rx_validate(const fs_rx_t *rx, fs_packet_t *packet) {
    if (rx->error) return rx->error;
    if (rx->bits || rx->state >> 1u == 6u || rx->length < 2u || rx->bytes[0] != 0x80u)
        return FS_ENCODING;
    uint8_t pid = rx->bytes[1];
    if (((pid ^ (pid >> 4u)) & 15u) != 15u) return FS_PID;
    packet->pid = pid;
    packet->length = 0u;
    if (pid == FS_DATA0 || pid == FS_DATA1) {
        if (rx->length < 4u) return FS_LENGTH;
        if (rx->crc != 0xb001u) return FS_CRC;
        packet->length = (uint8_t)(rx->length - 4u);
    } else if ((pid != FS_ACK && pid != FS_NAK && pid != FS_STALL_PID) || rx->length != 2u)
        return FS_PID;
    return 0;
}

int32_t fs_rx_finish(fs_rx_t *rx, fs_packet_t *packet) {
    int32_t error = fs_rx_validate(rx, packet);
    if (!error) for (uint8_t i = 0u; i < packet->length; ++i) packet->data[i] = rx->bytes[i + 2u];
    return error;
}
