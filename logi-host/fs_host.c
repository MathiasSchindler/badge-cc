#include "fs.h"
#include "../src/shared/bytes.h"
#include "../src/tools/diagnostics_logic.h"

typedef fs_host_t host_t;

static int32_t exchange(host_t *h, const fs_tx_t *tx, uint8_t receive, fs_packet_t *packet) {
    if (h->bus->now(h->bus->context) - h->started >= h->budget) return FS_DEADLINE;
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

static int32_t control_read(host_t *h, uint8_t address, const uint8_t request[8],
                             uint8_t *bytes, uint16_t length, uint8_t mps) {
    int32_t error = setup(h, address, request);
    uint16_t received = 0u;
    uint8_t toggle = FS_DATA1, duplicates = 0u;
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

static int32_t read_descriptor(host_t *h, uint8_t address, uint8_t *bytes,
                               uint8_t length, uint8_t mps) {
    const uint8_t request[] = {0x80u,6u,0u,1u,0u,0u,length,0u};
    return control_read(h, address, request, bytes, length, mps);
}

static int32_t control_write(host_t *h, uint8_t address, const uint8_t request[8]) {
    int32_t error = setup(h, address, request);
    fs_tx_t tx;
    fs_tx_init(&tx);
    fs_packet_t packet;
    if (!error) error = fs_token(&tx, FS_IN, address, 0u);
    if (!error) h->result->phase = 4u;
    if (!error) error = transaction(h, &tx, &packet);
    if (!error && (packet.pid != FS_DATA1 || packet.length)) error = FS_LENGTH;
    return error;
}

static int32_t probe(host_t *h) {
    const fs_bus_t *bus = h->bus;
    fs_result_t *result = h->result;
    result->stage = 1u;
    int32_t error = bus->reset(bus->context);
    if (error) return result->error = error;
    h->last_sof = bus->now(bus->context);
    result->stage = 2u;
    error = delay(h, 10000u);
    if (error) return result->error = error;
    uint8_t initial[8];
    result->stage = 3u;
    if (!error) error = read_descriptor(h, 0u, initial, 8u, 8u);
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
        error = control_write(h, 0u, request);
        if (!error) error = delay(h, 2000u);
    }
    if (!error) {
        result->stage = 5u;
        uint8_t descriptor[18];
        error = read_descriptor(h, 1u, descriptor, 18u, result->mps);
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

int32_t fs_probe(const fs_bus_t *bus, fs_result_t *result) {
    badger_bytes_zero((uint8_t *)result, sizeof(*result));
    host_t h = {.bus = bus, .result = result, .started = bus->now(bus->context), .budget = 300000u};
    return probe(&h);
}

int32_t fs_keypad_start(const fs_bus_t *bus, fs_result_t *result, fs_keypad_t *k) {
    badger_bytes_zero((uint8_t *)k, sizeof(*k));
    badger_bytes_zero((uint8_t *)result, sizeof(*result));
    k->host.bus = bus;
    k->host.result = result;
    k->host.started = bus->now(bus->context);
    k->host.budget = 300000u;
    k->stage = k->phase = 1u;
    int32_t error = probe(&k->host);
    if (!error && (keypad_u16(result->descriptor + 8u) != 0x046du ||
        keypad_u16(result->descriptor + 10u) != 0xc354u || result->descriptor[17] != 1u))
        error = FS_DESCRIPTOR;
    uint8_t bytes[512], request[] = {0x80u,6u,0u,2u,0u,0u,9u,0u};
    if (!error) {
        k->phase = 2u;
        error = control_read(&k->host, 1u, request, bytes, 9u, result->mps);
    }
    if (!error) {
        uint16_t length = keypad_u16(bytes + 2u);
        if (bytes[0] != 9u || bytes[1] != 2u || length < 9u || length > sizeof(bytes))
            error = FS_DESCRIPTOR;
        else {
            k->phase = 3u;
            request[6] = (uint8_t)length; request[7] = (uint8_t)(length >> 8u);
            error = control_read(&k->host, 1u, request, bytes, length, result->mps);
            if (!error && !keypad_configuration(bytes, length, &k->endpoints)) error = FS_DESCRIPTOR;
        }
    }
    if (!error) {
        k->phase = 4u;
        const uint8_t configure[] = {0u,9u,k->endpoints.configuration,0u,0u,0u,0u,0u};
        error = control_write(&k->host, 1u, configure);
        if (!error) k->configured = 1u;
    }
    if (!error) {
        k->phase = 5u;
        const uint8_t hid[] = {0x81u,6u,0u,0x22u,k->endpoints.interface,0u,140u,0u};
        error = control_read(&k->host, 1u, hid, bytes, 140u, result->mps);
        if (!error && !keypad_known_hid(bytes, 140u)) error = FS_DESCRIPTOR;
        if (!error) k->report_crc = diag_crc32(bytes, 140u);
    }
    k->error = error;
    if (!error) {
        k->phase = 6u;
        k->stage = 2u;
        k->query_started = bus->now(bus->context);
        k->input_pid = FS_DATA0;
        k->last_input = k->last_output = k->query_started;
        k->host.started = k->query_started;
        k->host.budget = 31000000u;
    }
    return error;
}

static const uint8_t feature_query[20] = {0x11u,0xffu,0u,1u,0x1bu,4u};

int32_t fs_keypad_step(fs_keypad_t *k, fs_packet_t *packet) {
    host_t *h = &k->host;
    if (k->stage != 2u) return FS_DESCRIPTOR;
    int32_t error = delay(h, 0u);
    if (error) return error;
    uint32_t now = h->bus->now(h->bus->context);
    fs_tx_t tx;
    fs_tx_init(&tx);
    if (!k->query_sent && now - k->last_output >= (uint32_t)k->endpoints.output_interval * 1000u) {
        error = fs_token(&tx, FS_OUT, 1u, k->endpoints.output & 15u);
        if (!error) error = fs_data(&tx, FS_DATA0, feature_query, 20u);
        if (!error) error = exchange(h, &tx, 1u, packet);
        k->last_output = h->bus->now(h->bus->context);
        if (error) return error;
        if (packet->pid == FS_NAK) ++h->result->naks;
        else if (packet->pid == FS_STALL_PID) return FS_STALL;
        else if (packet->pid != FS_ACK) return FS_PID;
        else k->query_sent = 1u;
    }
    now = h->bus->now(h->bus->context);
    if (!k->query_done && now - k->query_started >= 1000000u) return FS_DEADLINE;
    if (now - k->last_input < (uint32_t)k->endpoints.input_interval * 1000u) return 0;
    fs_tx_init(&tx);
    error = fs_token(&tx, FS_IN, 1u, k->endpoints.input & 15u);
    if (!error) error = exchange(h, &tx, 1u, packet);
    k->last_input = h->bus->now(h->bus->context);
    ++k->polls;
    if (error) return error;
    if (packet->pid == FS_NAK) { ++h->result->naks; return 0; }
    if (packet->pid == FS_STALL_PID) return FS_STALL;
    if (packet->pid != FS_DATA0 && packet->pid != FS_DATA1) return FS_PID;
    if (packet->length > k->endpoints.input_size) return FS_LENGTH;
    if (packet->pid != k->input_pid) {
        ++k->duplicates;
        if (++k->repeated > 3u) return FS_TOGGLE;
        return 0;
    }
    k->input_pid ^= FS_DATA0 ^ FS_DATA1;
    k->repeated = 0u;
    if (!packet->length) return 0;
    ++k->reports;
    if (decode_button(packet->data[0], packet->data, packet->length) >= 0) ++k->buttons;
    if (k->query_sent && !k->query_done) {
        int match = match_hidpp_reply(feature_query, packet->data[0], packet->data, packet->length);
        if (match < 0) k->error = FS_HID_REPLY;
        if (match) {
            if (match > 0 && packet->data[4] && packet->data[4] != 0xffu) {
                k->feature = packet->data[4];
                k->query_done = 1u;
                k->phase = 7u;
            } else k->error = FS_HID_REPLY;
        }
    }
    return 1;
}

int32_t fs_keypad_stop(fs_keypad_t *k) {
    if (!k->error && !k->query_done) k->error = FS_DEADLINE;
    if (k->configured) {
        const uint8_t request[] = {0u,9u,0u,0u,0u,0u,0u,0u};
        k->host.started = k->host.bus->now(k->host.bus->context);
        k->host.budget = 300000u;
        k->cleanup_error = control_write(&k->host, 1u, request);
        if (!k->cleanup_error) k->configured = 0u;
    }
    if (!k->error) k->error = k->cleanup_error;
    if (k->host.result) k->naks = k->host.result->naks;
    k->stage = k->error ? 4u : 3u;
    return k->error;
}

void fs_keypad_status(const fs_keypad_t *k, uint32_t build, uint16_t generation, uint8_t bytes[64]) {
    badger_bytes_zero(bytes, 64u);
    badger_bytes_store_u32(bytes, FS_KEYPAD_MAGIC);
    badger_bytes_store_u32(bytes + 4u, build);
    badger_bytes_store_u32(bytes + 8u, (uint32_t)k->error);
    bytes[12] = (uint8_t)generation; bytes[13] = (uint8_t)(generation >> 8u);
    bytes[14] = k->stage; bytes[15] = k->phase;
    badger_bytes_store_u32(bytes + 16u, k->reports);
    badger_bytes_store_u32(bytes + 20u, k->polls);
    badger_bytes_store_u32(bytes + 24u,
                          k->stage == 2u && k->host.result ? k->host.result->naks : k->naks);
    badger_bytes_store_u32(bytes + 28u, k->duplicates);
    badger_bytes_store_u32(bytes + 32u, k->report_crc);
    bytes[36] = k->endpoints.input; bytes[37] = k->endpoints.input_size;
    bytes[38] = k->endpoints.input_interval; bytes[39] = k->endpoints.output;
    bytes[40] = k->endpoints.output_size; bytes[41] = k->endpoints.output_interval;
    bytes[42] = k->endpoints.interface; bytes[43] = k->feature;
    badger_bytes_store_u32(bytes + 44u, (uint32_t)k->cleanup_error);
    bytes[48] = (uint8_t)k->endpoints.max_power_ma;
    bytes[49] = (uint8_t)(k->endpoints.max_power_ma >> 8u);
    badger_bytes_store_u32(bytes + 52u, k->buttons);
    bytes[56] = k->query_done; bytes[57] = k->configured;
    badger_bytes_store_u32(bytes + 60u, diag_crc32(bytes, 60u));
}

void fs_keypad_event(const fs_keypad_t *k, const fs_packet_t *p, uint32_t build,
                    uint16_t generation, uint8_t bytes[64]) {
    badger_bytes_zero(bytes, 64u);
    badger_bytes_store_u32(bytes, FS_KEYPAD_EVENT_MAGIC);
    badger_bytes_store_u32(bytes + 4u, build);
    badger_bytes_store_u32(bytes + 8u, k->reports);
    bytes[12] = (uint8_t)generation; bytes[13] = (uint8_t)(generation >> 8u);
    bytes[14] = p->length; bytes[15] = k->feature;
    badger_bytes_copy(bytes + 16u, p->data, p->length);
    badger_bytes_store_u32(bytes + 48u, k->host.bus->now(k->host.bus->context) - k->query_started);
    badger_bytes_store_u32(bytes + 60u, diag_crc32(bytes, 60u));
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

void fs_trace_report(const fs_result_t *r, uint32_t build, uint8_t bytes[64]) {
    badger_bytes_zero(bytes, 64u);
    badger_bytes_store_u32(bytes, FS_TRACE_MAGIC);
    badger_bytes_store_u32(bytes + 4u, build);
    bytes[8] = r->trace_count;
    bytes[9] = r->trace_pid;
    bytes[10] = (uint8_t)r->generation;
    bytes[11] = (uint8_t)(r->generation >> 8u);
    for (uint8_t i = 0u; i < r->trace_count && i < 8u; ++i)
        badger_bytes_store_u32(bytes + 16u + 4u * i, r->trace_words[i]);
    badger_bytes_store_u32(bytes + 60u, diag_crc32(bytes, 60u));
}
