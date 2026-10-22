#include "bluetooth.h"
#include "../src/shared/bytes.h"
#include "../src/tools/diagnostics_logic.h"

#define BT_CONTROL 0x18000c7cu
#define HOST_CONTROL 0x18000d6cu
#define BUFFER_BASE 0x18000d68u
#define BT_PATCH_BASE 0x19000000u
#define BT_POWER_WAKE 0x19640894u
#define BT_TIMEOUT_TICKS 500000u

static int fail(clicker_bt_t *bt, clicker_bt_error_t error) {
    bt->error = error;
    return 0;
}

static int service(clicker_bt_t *bt) {
    if (bt->io.service != 0 && !bt->io.service(bt->io.context)) {
        return fail(bt, CLICKER_BT_CANCELLED);
    }
    return 1;
}

static int read32(clicker_bt_t *bt, uint32_t address, uint32_t *value) {
    bt->address = address;
    if (!bt->io.read32(bt->io.context, address, value)) return fail(bt, CLICKER_BT_IO);
    bt->actual = *value;
    return 1;
}

static int write32(clicker_bt_t *bt, uint32_t address, uint32_t value) {
    bt->address = address;
    bt->expected = value;
    return bt->io.write32(bt->io.context, address, value) || fail(bt, CLICKER_BT_IO);
}

static int memory(clicker_bt_t *bt, uint32_t address, const uint8_t *source,
                    uint8_t *destination, uint32_t length) {
    bt->address = address;
    int ok = source ? bt->io.write(bt->io.context, address, source, length) :
                      bt->io.read(bt->io.context, address, destination, length);
    return ok || fail(bt, CLICKER_BT_IO);
}

static int delay(clicker_bt_t *bt, uint32_t ticks) {
    uint32_t start = bt->io.now(bt->io.context);
    for (uint32_t attempt = 0u; attempt < 1000000u; ++attempt) {
        if (!service(bt)) return 0;
        if (bt->io.now(bt->io.context) - start >= ticks) return 1;
    }
    return fail(bt, CLICKER_BT_TIMEOUT);
}

static int wait_control(clicker_bt_t *bt, uint32_t mask) {
    uint32_t start = bt->io.now(bt->io.context);
    bt->expected = mask;
    for (uint32_t attempt = 0u; attempt < 1000u; ++attempt) {
        uint32_t value;
        if (!service(bt) || !read32(bt, BT_CONTROL, &value)) return 0;
        if (value != 0xffffffffu && (value & mask) == mask) return 1;
        if (bt->io.now(bt->io.context) - start >= BT_TIMEOUT_TICKS) break;
        if (!delay(bt, 1000u)) return 0;
    }
    return fail(bt, CLICKER_BT_TIMEOUT);
}

static int patch_write(clicker_bt_t *bt, uint32_t address,
                        const uint8_t *data, uint32_t length) {
    uint8_t aligned[260];
    uint32_t prefix = address & 3u;
    uint32_t start = address & ~3u;
    uint32_t total = (prefix + length + 3u) & ~3u;
    if (!memory(bt, start, 0, aligned, total)) return 0;
    badger_bytes_copy(aligned + prefix, data, length);
    return memory(bt, start, aligned, 0, total);
}

static int patch_records(clicker_bt_t *bt, const uint8_t *patch,
                           uint32_t length, uint8_t apply) {
    if (length < 8u || patch[0] == 0u || patch[0] > 96u ||
        (uint32_t)patch[0] + 2u > length || patch[patch[0]] != 0u) {
        return fail(bt, CLICKER_BT_PATCH);
    }
    uint32_t offset = (uint32_t)patch[0] + 2u;
    uint32_t records = patch[offset - 1u];
    uint32_t high = 0u;
    for (uint32_t record = 0u; record < records; ++record) {
        if (length - offset < 4u) return fail(bt, CLICKER_BT_PATCH);
        uint32_t size = patch[offset];
        uint32_t address = (uint32_t)patch[offset + 1u] << 8u | patch[offset + 2u];
        uint8_t type = patch[offset + 3u];
        offset += 4u;
        if (size > length - offset) return fail(bt, CLICKER_BT_PATCH);
        if (type == 4u && size == 2u && address == 0u) {
            high = ((uint32_t)patch[offset] << 8u | patch[offset + 1u]) << 16u;
        } else if (type == 0u && size != 0u &&
                   high < 0x300000u && address + size <= 0x10000u) {
            if (apply) {
                if (!service(bt) || !patch_write(bt, BT_PATCH_BASE + high + address,
                                                  patch + offset, size)) return 0;
                bt->patch_bytes += size;
            }
        } else if (type == 1u && size == 0u && address == 0u &&
                   record + 1u == records && offset == length) {
            return 1;
        } else {
            return fail(bt, CLICKER_BT_PATCH);
        }
        offset += size;
    }
    return fail(bt, CLICKER_BT_PATCH);
}

static int signal_controller(clicker_bt_t *bt) {
    bt->host_control ^= 2u;
    return write32(bt, HOST_CONTROL, bt->host_control);
}

int clicker_bt_start(clicker_bt_t *bt, const clicker_bt_io_t *io,
                      const uint8_t *patch, uint32_t length, uint32_t crc) {
    badger_bytes_zero((uint8_t *)bt, sizeof(*bt));
    if (io == 0 || io->read32 == 0 || io->write32 == 0 || io->read == 0 ||
        io->write == 0 || io->now == 0 || patch == 0 || length > 65536u) {
        return fail(bt, CLICKER_BT_ARGUMENT);
    }
    bt->io.context = io->context;
    bt->io.read32 = io->read32;
    bt->io.write32 = io->write32;
    bt->io.read = io->read;
    bt->io.write = io->write;
    bt->io.now = io->now;
    bt->io.service = io->service;
    if (diag_crc32(patch, length) != crc || !patch_records(bt, patch, length, 0u)) {
        return fail(bt, CLICKER_BT_PATCH);
    }
    bt->stage = CLICKER_BT_PATCHING;
    if (!service(bt) || !write32(bt, BT_POWER_WAKE, 3u) ||
        !patch_records(bt, patch, length, 1u)) return 0;
    bt->stage = CLICKER_BT_READY;
    if (!service(bt) || !delay(bt, 150000u) || !wait_control(bt, 1u << 24u)) return 0;
    bt->stage = CLICKER_BT_BUFFERS;
    if (!service(bt) || !read32(bt, BUFFER_BASE, &bt->base)) return 0;
    if ((bt->base & 3u) != 0u || bt->base < 0x10000u ||
        bt->base > 0x80000u - 0x2010u) return fail(bt, CLICKER_BT_POINTER);
    for (uint32_t i = 0u; i < 4u; ++i) {
        if (!write32(bt, bt->base + 0x2000u + i * 4u, 0u)) return 0;
    }
    bt->stage = CLICKER_BT_AWAKE;
    bt->host_control = 1u << 17u;
    if (!service(bt) || !write32(bt, HOST_CONTROL, bt->host_control) ||
        !wait_control(bt, 1u << 8u)) return 0;
    bt->host_control |= 1u << 24u;
    if (!write32(bt, HOST_CONTROL, bt->host_control) || !signal_controller(bt)) return 0;
    bt->credits = 1u;
    bt->started = 1u;
    bt->stage = CLICKER_BT_HCI;
    return service(bt);
}

static int indexes(clicker_bt_t *bt, uint32_t values[4]) {
    uint8_t bytes[16];
    if (!memory(bt, bt->base + 0x2000u, 0, bytes, sizeof(bytes))) return 0;
    for (uint32_t i = 0u; i < 4u; ++i) {
        values[i] = badger_bytes_load_u32(bytes + i * 4u);
        if (values[i] >= CLICKER_BT_RING_SIZE || (values[i] & 3u) != 0u) {
            bt->actual = values[i];
            return fail(bt, CLICKER_BT_POINTER);
        }
    }
    return 1;
}

static int ring_memory(clicker_bt_t *bt, uint32_t base, uint32_t index,
                         const uint8_t *source, uint8_t *destination, uint32_t length) {
    uint32_t first = CLICKER_BT_RING_SIZE - index;
    if (first > length) first = length;
    if (!memory(bt, base + index, source, destination, first)) return 0;
    if (first < length && !memory(bt, base, source ? source + first : 0,
                                  destination ? destination + first : 0, length - first)) return 0;
    return 1;
}

static int packet_valid(uint8_t type, const uint8_t *packet, uint32_t length) {
    if (type == 1u) return length >= 3u && length == 3u + packet[2];
    if (type == 4u) return length >= 2u && length == 2u + packet[1];
    if (type == 2u) return length >= 4u &&
        length == 4u + ((uint32_t)packet[2] | (uint32_t)packet[3] << 8u);
    return 0;
}

int clicker_bt_send(clicker_bt_t *bt, uint8_t type, const uint8_t *packet, uint32_t length) {
    if (bt->error != CLICKER_BT_OK) return -1;
    if (!bt->started || packet == 0 || length > CLICKER_BT_PACKET_MAX ||
        (type != 1u && type != 2u) || !packet_valid(type, packet, length)) {
        fail(bt, CLICKER_BT_ARGUMENT);
        return -1;
    }
    uint32_t values[4];
    if (!service(bt) || !indexes(bt, values)) return -1;
    uint32_t total = (length + 7u) & ~3u;
    uint32_t free = (values[1] - values[0] - 4u) & (CLICKER_BT_RING_SIZE - 1u);
    if (total > free) return 0;
    badger_bytes_zero(bt->frame, total);
    bt->frame[0] = (uint8_t)length;
    bt->frame[1] = (uint8_t)(length >> 8u);
    bt->frame[3] = type;
    badger_bytes_copy(bt->frame + 4u, packet, length);
    if (!ring_memory(bt, bt->base, values[0], bt->frame, 0, total) ||
        !write32(bt, bt->base + 0x2000u, (values[0] + total) & 4095u) ||
        !signal_controller(bt)) return -1;
    ++bt->tx_packets;
    return 1;
}

int clicker_bt_receive(clicker_bt_t *bt, uint8_t *type, uint8_t *packet,
                        uint32_t capacity, uint32_t *length) {
    if (length != 0) *length = 0u;
    if (bt->error != CLICKER_BT_OK) return -1;
    if (!bt->started || type == 0 || packet == 0 || length == 0) {
        fail(bt, CLICKER_BT_ARGUMENT);
        return -1;
    }
    uint32_t values[4];
    if (!service(bt) || !indexes(bt, values)) return -1;
    uint32_t available = (values[2] - values[3]) & 4095u;
    if (available == 0u) return 0;
    if (!ring_memory(bt, bt->base + 0x1000u, values[3], 0, bt->frame, 4u)) return -1;
    uint32_t size = (uint32_t)bt->frame[0] | (uint32_t)bt->frame[1] << 8u |
                    (uint32_t)bt->frame[2] << 16u;
    if (size > CLICKER_BT_PACKET_MAX || size > capacity || size == 0u) {
        fail(bt, CLICKER_BT_PACKET);
        return -1;
    }
    uint32_t total = (size + 7u) & ~3u;
    if (available < total) return 0;
    uint32_t payload_at = (values[3] + 4u) & 4095u;
    if (!ring_memory(bt, bt->base + 0x1000u, payload_at, 0, bt->frame + 4u,
                       total - 4u)) return -1;
    if ((bt->frame[3] != 2u && bt->frame[3] != 4u) ||
        !packet_valid(bt->frame[3], bt->frame + 4u, size)) {
        fail(bt, CLICKER_BT_PACKET);
        return -1;
    }
    if (!write32(bt, bt->base + 0x200cu, (values[3] + total) & 4095u) ||
        !signal_controller(bt)) return -1;
    *type = bt->frame[3];
    *length = size;
    badger_bytes_copy(packet, bt->frame + 4u, size);
    ++bt->rx_packets;
    return 1;
}

int clicker_bt_command(clicker_bt_t *bt, uint16_t opcode,
                        const uint8_t *parameters, uint8_t parameter_length,
                        uint8_t *reply, uint32_t capacity, uint32_t *reply_length) {
    uint8_t packet[260];
    if (reply_length != 0) *reply_length = 0u;
    if (bt->error != CLICKER_BT_OK) return 0;
    if (reply == 0 || reply_length == 0 || bt->credits == 0u ||
        (parameter_length != 0u && parameters == 0)) return fail(bt, CLICKER_BT_ARGUMENT);
    bt->opcode = opcode;
    packet[0] = (uint8_t)opcode;
    packet[1] = (uint8_t)(opcode >> 8u);
    packet[2] = parameter_length;
    badger_bytes_copy(packet + 3u, parameters, parameter_length);
    uint32_t start = bt->io.now(bt->io.context);
    uint8_t sent = 0u;
    for (uint32_t attempt = 0u; attempt < 100000u; ++attempt) {
        if (!sent) {
            int result = clicker_bt_send(bt, 1u, packet, 3u + parameter_length);
            if (result < 0) return 0;
            if (result > 0) { sent = 1u; --bt->credits; }
        }
        if (sent) {
            uint8_t type;
            uint32_t length;
            int result = clicker_bt_receive(bt, &type, packet, sizeof(packet), &length);
            if (result < 0) return 0;
            if (result > 0) {
                if (type != 4u) return fail(bt, CLICKER_BT_PACKET);
                bt->event = packet[0];
                if (packet[0] == 0x0eu) {
                    if (length < 6u || ((uint32_t)packet[3] | (uint32_t)packet[4] << 8u) != opcode) {
                        return fail(bt, CLICKER_BT_COMMAND);
                    }
                    bt->credits = packet[2];
                    bt->status = packet[5];
                    if (packet[5] != 0u) return fail(bt, CLICKER_BT_COMMAND);
                    if (length - 5u > capacity) return fail(bt, CLICKER_BT_PACKET);
                    *reply_length = length - 5u;
                    badger_bytes_copy(reply, packet + 5u, *reply_length);
                    return 1;
                }
                if (packet[0] == 0x0fu || packet[0] == 0x10u) {
                    if (length > 2u) bt->status = packet[2];
                    return fail(bt, CLICKER_BT_COMMAND);
                }
            }
        }
        if (bt->io.now(bt->io.context) - start >= BT_TIMEOUT_TICKS) break;
    }
    return fail(bt, CLICKER_BT_TIMEOUT);
}
