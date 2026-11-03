#include "fs.h"
#include "../src/shared/bytes.h"
#include "../src/tools/diagnostics_logic.h"

typedef struct {
    const fs_bus_t *bus;
    fs_result_t *result;
    uint32_t started, last_sof;
    uint16_t frame;
} host_t;

static int32_t exchange(host_t *h, const fs_tx_t *tx, uint8_t receive, fs_packet_t *packet) {
    if (h->bus->now(h->bus->context) - h->started >= 300000u) return FS_DEADLINE;
    h->result->tx += tx->packets;
    int32_t error = h->bus->exchange(h->bus->context, tx, receive, packet);
    if (error == FS_TIMEOUT) ++h->result->timeouts;
    else if (error) ++h->result->wire_errors;
    else if (receive) {
        ++h->result->rx;
        if (packet->pid == FS_DATA0 || packet->pid == FS_DATA1) ++h->result->tx;
        h->result->last_pid = packet->pid;
    }
    return error;
}

static int32_t delay(host_t *h, uint32_t us) {
    uint32_t start = h->bus->now(h->bus->context);
    for (uint32_t loops = 0u; loops < 1000000u; ++loops) {
        uint32_t now = h->bus->now(h->bus->context);
        if (now - h->last_sof >= 1000u) {
            fs_tx_t tx;
            fs_tx_init(&tx);
            int32_t error = fs_sof(&tx, h->frame++ & 2047u);
            if (!error) error = exchange(h, &tx, 0u, 0);
            if (error) return error;
            h->last_sof = now;
        }
        if (now - start >= us) return 0;
    }
    return FS_DEADLINE;
}

static int32_t transaction(host_t *h, const fs_tx_t *tx, fs_packet_t *packet) {
    uint32_t start = h->bus->now(h->bus->context);
    for (uint32_t attempts = 0u; attempts < 128u; ++attempts) {
        int32_t error = delay(h, 0u);
        if (!error) error = exchange(h, tx, 1u, packet);
        if (error) return error;
        if (packet->pid == FS_STALL_PID) return FS_STALL;
        if (packet->pid != FS_NAK) return 0;
        ++h->result->naks;
        if (h->bus->now(h->bus->context) - start >= 100000u) return FS_DEADLINE;
        error = delay(h, 250u);
        if (error) return error;
    }
    return FS_DEADLINE;
}

static int32_t setup(host_t *h, uint8_t address, const uint8_t bytes[8]) {
    h->result->phase = 1u;
    fs_tx_t tx;
    fs_tx_init(&tx);
    fs_packet_t packet;
    int32_t error = fs_token(&tx, FS_SETUP, address, 0u);
    if (!error) error = fs_data(&tx, FS_DATA0, bytes, 8u);
    if (!error) error = transaction(h, &tx, &packet);
    if (!error && packet.pid != FS_ACK) error = FS_PID;
    return error;
}

static int32_t read_descriptor(host_t *h, uint8_t address, uint8_t *bytes,
                               uint8_t length, uint8_t mps) {
    const uint8_t request[] = {0x80u,6u,0u,1u,0u,0u,length,0u};
    int32_t error = setup(h, address, request);
    uint8_t received = 0u, toggle = FS_DATA1, duplicates = 0u;
    while (!error && received < length) {
        h->result->phase = 2u;
        fs_tx_t tx;
        fs_tx_init(&tx);
        fs_packet_t packet;
        error = fs_token(&tx, FS_IN, address, 0u);
        if (!error) error = transaction(h, &tx, &packet);
        if (error) break;
        if (packet.pid != FS_DATA0 && packet.pid != FS_DATA1) return FS_PID;
        if (packet.pid != toggle) {
            if (!received || ++duplicates > 3u) return FS_TOGGLE;
            continue; /* Valid duplicate was ACKed by the PHY; do not copy it twice. */
        }
        if (packet.length > mps || packet.length > length - received) return FS_LENGTH;
        for (uint8_t i = 0u; i < packet.length; ++i) bytes[received + i] = packet.data[i];
        received += packet.length;
        toggle ^= FS_DATA0 ^ FS_DATA1;
        duplicates = 0u;
        if (packet.length < mps && received != length) return FS_LENGTH;
    }
    if (error) return error;
    if (h->result->stage == 3u && received == 8u && bytes[0] == 18u && bytes[1] == 1u &&
        (bytes[7] == 8u || bytes[7] == 16u || bytes[7] == 32u || bytes[7] == 64u)) {
        badger_bytes_copy(h->result->descriptor, bytes, 8u);
        h->result->descriptor_length = 8u;
        h->result->mps = bytes[7];
    }
    h->result->phase = 3u;
    fs_tx_t tx;
    fs_tx_init(&tx);
    fs_packet_t packet;
    error = fs_token(&tx, FS_OUT, address, 0u);
    if (!error) error = fs_data(&tx, FS_DATA1, 0, 0u);
    if (!error) error = transaction(h, &tx, &packet);
    if (!error && packet.pid != FS_ACK) error = FS_PID;
    return error;
}

int32_t fs_probe(const fs_bus_t *bus, fs_result_t *result) {
    badger_bytes_zero((uint8_t *)result, sizeof(*result));
    host_t h = {.bus = bus, .result = result, .started = bus->now(bus->context)};
    result->stage = 1u;
    int32_t error = bus->reset(bus->context);
    if (error) return result->error = error;
    h.last_sof = bus->now(bus->context);
    result->stage = 2u;
    error = delay(&h, 10000u);
    if (error) return result->error = error;
    uint8_t initial[8];
    result->stage = 3u;
    if (!error) error = read_descriptor(&h, 0u, initial, 8u, 8u);
    if (!error) {
        result->mps = initial[7];
        if (initial[0] != 18u || initial[1] != 1u ||
            (initial[7] != 8u && initial[7] != 16u && initial[7] != 32u && initial[7] != 64u))
            error = FS_DESCRIPTOR;
        else {
            badger_bytes_copy(result->descriptor, initial, 8u);
            result->descriptor_length = 8u;
        }
    }
    if (!error) {
        result->stage = 4u;
        const uint8_t request[] = {0u,5u,1u,0u,0u,0u,0u,0u};
        error = setup(&h, 0u, request);
        fs_tx_t tx;
        fs_tx_init(&tx);
        fs_packet_t packet;
        if (!error) error = fs_token(&tx, FS_IN, 0u, 0u);
        if (!error) result->phase = 4u;
        if (!error) error = transaction(&h, &tx, &packet);
        if (!error && (packet.pid != FS_DATA1 || packet.length)) error = FS_LENGTH;
        if (!error) error = delay(&h, 2000u);
    }
    if (!error) {
        result->stage = 5u;
        uint8_t descriptor[18];
        error = read_descriptor(&h, 1u, descriptor, 18u, result->mps);
        if (!error) for (uint8_t i = 0u; i < 8u; ++i)
            if (descriptor[i] != initial[i]) error = FS_DESCRIPTOR;
        if (!error) {
            badger_bytes_copy(result->descriptor, descriptor, 18u);
            result->descriptor_length = 18u;
            result->stage = 6u;
        }
    }
    result->error = error;
    return error;
}

void fs_report(const fs_result_t *r, uint32_t build, uint8_t line, uint8_t bytes[64]) {
    badger_bytes_zero(bytes, 64u);
    badger_bytes_store_u32(bytes, FS_REPORT_MAGIC);
    badger_bytes_store_u32(bytes + 4u, build);
    badger_bytes_store_u32(bytes + 8u, (uint32_t)r->error);
    badger_bytes_store_u32(bytes + 12u, r->tx);
    badger_bytes_store_u32(bytes + 16u, r->rx);
    badger_bytes_store_u32(bytes + 20u, r->timeouts);
    badger_bytes_store_u32(bytes + 24u, r->naks);
    badger_bytes_store_u32(bytes + 28u, r->wire_errors);
    bytes[32] = line;
    bytes[33] = r->stage;
    bytes[34] = r->descriptor_length;
    bytes[35] = r->mps;
    badger_bytes_copy(bytes + 36u, r->descriptor, 18u);
    bytes[54] = (uint8_t)r->generation;
    bytes[55] = (uint8_t)(r->generation >> 8u);
    bytes[56] = r->last_pid;
    bytes[57] = r->reset_levels;
    bytes[58] = r->clock_ready;
    bytes[59] = r->phase;
    badger_bytes_store_u32(bytes + 60u, diag_crc32(bytes, 60u));
}
