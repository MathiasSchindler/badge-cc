#include "wifi_firmware.h"
#include "wifi_control.h"
#include "../tools/diagnostics_logic.h"
#include "../shared/bytes.h"

#define RADIO_RAM_SIZE 0x80000u
#define WINDOW_SIZE 0x8000u
#define ARM_WRAPPER 0x18103000u
#define RAM_WRAPPER 0x18104000u
#define FW_DEADLINE_TICKS 3000000u
#define FW_ASSET_DEADLINE_TICKS 6000000u
#define FW_STARTUP_DEADLINE_TICKS 20000000u
#define RX_FRAME_SLOTS 4u

typedef struct {
    uint8_t bytes[WIFI_ETHERNET_MAX];
    uint16_t length;
    uint8_t interface;
} received_frame_t;

typedef struct {
    const wifi_wire_t *wire;
    wifi_fw_report_t *report;
    wifi_fw_service_t service;
    void *context;
    uint32_t start;
    uint32_t operation_start;
    uint8_t validating_assets;
    uint8_t starting;
    uint32_t callback_at;
    uint32_t callback_stage;
    uint32_t window;
    uint8_t window_valid;
    uint8_t sequence;
    uint8_t credit;
    uint8_t flow;
    uint16_t ioctl_id;
    uint8_t interface;
    uint8_t ready;
    uint8_t data_enabled;
    uint8_t rx_head;
    uint8_t rx_count;
    uint32_t rx_dropped;
    received_frame_t frames[RX_FRAME_SLOTS];
    uint8_t tx[WIFI_CONTROL_MAX + 4u];
    uint8_t rx[WIFI_CONTROL_MAX + WIFI_GSPI_BACKPLANE_PAD];
    uint8_t control_tx[WIFI_CONTROL_MAX];
    uint8_t control_rx[WIFI_CONTROL_MAX];
    uint8_t chunk[WIFI_FW_BACKPLANE_BLOCK];
    uint8_t clm_payload[WIFI_FW_CHUNK + 24u];
} firmware_state_t;

static firmware_state_t state;

static void store(uint8_t *data, uint32_t value) {
    badger_bytes_store_u32(data, value);
}

static int failed(wifi_fw_error_t error) {
    state.report->error = error;
    return 0;
}

static int service(void) {
    uint32_t now = state.wire->now_us(state.wire->context);
    state.report->elapsed_ticks = now - state.start;
    uint32_t limit = state.validating_assets ? FW_ASSET_DEADLINE_TICKS :
                     state.starting ? FW_STARTUP_DEADLINE_TICKS : FW_DEADLINE_TICKS;
    if (now - state.operation_start >= limit) return failed(WIFI_FW_TIMEOUT);
    if (state.service != 0 &&
        (state.callback_stage != (uint32_t)state.report->stage || now - state.callback_at >= 5000u)) {
        state.callback_at = now;
        state.callback_stage = (uint32_t)state.report->stage;
        int alive = state.service(state.context, state.report);
        state.report->service_ticks += state.wire->now_us(state.wire->context) - now;
        if (!alive) return failed(WIFI_FW_CANCELLED);
    }
    return 1;
}

static int stage(wifi_fw_stage_t next) {
    state.report->stage = next;
    return service();
}

static int readreg(uint8_t function, uint32_t address, uint8_t size, uint32_t *value) {
    state.report->address = address;
    wifi_bus_result_t error = wifi_gspi_read(state.wire, function, address, size,
                                             WIFI_ORDER_NORMAL32, value);
    state.report->actual = error == WIFI_BUS_OK ? *value : (uint32_t)error;
    return error == WIFI_BUS_OK || failed(WIFI_FW_BUS);
}

static int writereg(uint8_t function, uint32_t address, uint8_t size, uint32_t value) {
    state.report->address = address;
    state.report->expected = value;
    wifi_bus_result_t error = wifi_gspi_write(state.wire, function, address, size,
                                              WIFI_ORDER_NORMAL32, value);
    if (error != WIFI_BUS_OK) state.report->actual = (uint32_t)error;
    return error == WIFI_BUS_OK || failed(WIFI_FW_BUS);
}

static int window(uint32_t address) {
    uint32_t base = address & ~(WINDOW_SIZE - 1u);
    if (state.window_valid && state.window == base) return 1;
    const uint32_t registers[] = {0x1000au, 0x1000bu, 0x1000cu};
    for (uint32_t i = 0u; i < 3u; ++i) {
        if (!writereg(1u, registers[i], 1u, (base >> (8u + i*8u)) & 255u)) return 0;
    }
    state.window = base;
    state.window_valid = 1u;
    return 1;
}

static int bp_read(uint32_t address, uint8_t size, uint32_t *value) {
    if (!window(address)) return 0;
    int ok = readreg(1u, (address & 0x7fffu) | 0x8000u, size, value);
    state.report->address = address;
    return ok;
}

static int bp_write(uint32_t address, uint8_t size, uint32_t value) {
    if (!window(address)) return 0;
    int ok = writereg(1u, (address & 0x7fffu) | 0x8000u, size, value);
    state.report->address = address;
    return ok;
}

static int transfer(uint8_t write, uint8_t function, uint32_t address,
                     const uint8_t *data, uint8_t *output, uint32_t length) {
    if (length == 0u || length > WIFI_CONTROL_MAX || function < 1u || function > 2u ||
        address > 0x1ffffu || state.wire->transfer == 0 ||
        (function == 1u && length > WIFI_FW_BACKPLANE_BLOCK) ||
        (write ? data == 0 : output == 0)) return failed(WIFI_FW_PROTOCOL);
    uint32_t aligned = (length + 3u) & ~3u;
    uint32_t command = ((uint32_t)write << 31u) | (1u << 30u) |
                       ((uint32_t)function << 28u) | (address << 11u) | length;
    store(state.tx, command);
    if (write) {
        badger_bytes_copy(state.tx + 4u, data, length);
        badger_bytes_zero(state.tx + 4u + length, aligned - length);
    }
    uint32_t padding = function == 1u ? WIFI_GSPI_BACKPLANE_PAD : 0u;
    uint32_t started = state.wire->now_us(state.wire->context);
    wifi_bus_result_t error = state.wire->transfer(state.wire, state.tx, write ? aligned + 4u : 4u,
                                                   write ? 0 : state.rx, write ? 0u : aligned + padding);
    state.report->transfer_ticks += state.wire->now_us(state.wire->context) - started;
    if (error != WIFI_BUS_OK) {
        state.report->actual = (uint32_t)error;
        return failed(WIFI_FW_BUS);
    }
    if (!write) badger_bytes_copy(output, state.rx + padding, length);
    return 1;
}

static int reset_core(uint32_t base) {
    uint32_t value;
    if (!bp_read(base + 0x800u, 1u, &value)) return 0;
    if ((value & 1u) == 0u) return failed(WIFI_FW_CORE_STATE);
    if (!bp_write(base + 0x408u, 1u, 3u) || !bp_read(base + 0x408u, 1u, &value) ||
        !bp_write(base + 0x800u, 1u, 0u)) return 0;
    if (wifi_wire_delay(state.wire, 1000u) != WIFI_BUS_OK) return failed(WIFI_FW_BUS);
    if (!bp_write(base + 0x408u, 1u, 1u) || !bp_read(base + 0x408u, 1u, &value)) return 0;
    if ((value & 3u) != 1u) return failed(WIFI_FW_CORE_STATE);
    if (!bp_read(base + 0x800u, 1u, &value)) return 0;
    return (value & 1u) == 0u || failed(WIFI_FW_CORE_STATE);
}

static int load_bytes(uint32_t base, const uint8_t *source, uint32_t size, int verify) {
    for (uint32_t offset = 0u; offset < size;) {
        if (!service()) return 0;
        uint32_t address = base + offset;
        uint32_t length = size - offset;
        if (length > WIFI_FW_BACKPLANE_BLOCK) length = WIFI_FW_BACKPLANE_BLOCK;
        if (length > WINDOW_SIZE - (address & 0x7fffu)) length = WINDOW_SIZE - (address & 0x7fffu);
        if (!window(address)) return 0;
        state.report->address = address;
        if (verify) {
            if (!transfer(0u, 1u, address & 0x7fffu, 0, state.chunk, length)) return 0;
            if (!badger_bytes_equal(state.chunk, source + offset, length)) {
                for (uint32_t i = 0u; i < length; ++i) {
                    if (state.chunk[i] != source[offset+i]) {
                        for (uint32_t j = 0u; j < sizeof(state.report->last_rx_prefix); ++j) {
                            state.report->last_rx_prefix[j] = j < length ? state.chunk[j] : 0u;
                        }
                        uint32_t diagnostic;
                        if (!readreg(0u, 4u, 2u, &diagnostic)) return 0;
                        store(state.report->last_rx_prefix + 12u, diagnostic);
                        const uint32_t offsets[] = {0u, length >= 8u ? 4u : 0u, length - 4u};
                        for (uint32_t j = 0u; j < 3u; ++j) {
                            if (!bp_read(address + offsets[j], 4u, &diagnostic)) return 0;
                            store(state.report->last_rx_prefix + 16u + 4u*j, diagnostic);
                        }
                        state.report->address = address+i;
                        state.report->expected = source[offset+i];
                        state.report->actual = state.chunk[i];
                        return failed(WIFI_FW_READBACK);
                    }
                }
            }
            state.report->verified_bytes += length;
        } else if (!transfer(1u, 1u, (address & 0x7fffu) | 0x8000u, source+offset, 0, length)) return 0;
        offset += length;
    }
    return 1;
}

static int wait_reg(uint8_t function, uint32_t address, uint8_t size,
                     uint32_t mask, uint32_t expected, uint32_t timeout) {
    uint32_t start = state.wire->now_us(state.wire->context);
    state.report->expected = expected;
    for (uint32_t attempt = 0u; attempt < 2000u; ++attempt) {
        uint32_t value;
        if (!service() || !readreg(function, address, size, &value)) return 0;
        if (value != (size == 1u ? 255u : 0xffffffffu) && (value & mask) == expected) return 1;
        if (state.wire->now_us(state.wire->context) - start >= timeout) break;
        if (wifi_wire_delay(state.wire, 1000u) != WIFI_BUS_OK) return failed(WIFI_FW_BUS);
    }
    return failed(WIFI_FW_TIMEOUT);
}

/* Returns -1 on error, 0 with no frame, 1 with a validated frame. */
static int poll_packet(wifi_control_packet_t *packet) {
    uint32_t status;
    if (!readreg(0u, 8u, 4u, &status)) return -1;
    if (status == 0xffffffffu) { failed(WIFI_FW_BUS); return -1; }
    if ((status & 0x100u) == 0u) return 0;
    uint32_t length = (status >> 9u) & 0x7ffu;
    if (length < 12u || length > WIFI_CONTROL_MAX) { failed(WIFI_FW_PROTOCOL); return -1; }
    if (!transfer(0u, 2u, 0u, 0, state.control_rx, length)) return -1;
    ++state.report->rx_packets;
    for (uint32_t i = 0u; i < 28u; ++i) {
        state.report->last_rx_prefix[i] = i < length ? state.control_rx[i] : 0u;
    }
    if (!wifi_control_parse(state.control_rx, length, packet)) {
        uint32_t header = state.control_rx[7];
        state.report->expected = length;
        state.report->actual = usb_report_u32(state.control_rx);
        if (header >= 12u && header + 16u <= length) {
            for (uint32_t i = 0u; i < 16u; ++i) state.report->last_rx_prefix[12u+i] = state.control_rx[header+i];
        }
        failed(WIFI_FW_PROTOCOL); return -1;
    }
    state.flow = packet->flow;
    state.credit = packet->credit;
    if (state.data_enabled && packet->channel == 2u) {
        const uint8_t *frame;
        uint32_t size;
        uint8_t interface;
        if (!wifi_data_decode(packet,&interface,&frame,&size) || state.rx_count == RX_FRAME_SLOTS) {
            ++state.rx_dropped;
        } else {
            uint32_t slot = (state.rx_head + state.rx_count) % RX_FRAME_SLOTS;
            badger_bytes_copy(state.frames[slot].bytes, frame, size);
            state.frames[slot].length = (uint16_t)size;
            state.frames[slot].interface = interface;
            ++state.rx_count;
        }
    }
    return 1;
}

static int command(uint32_t command_id, uint8_t set, const uint8_t *payload,
                    uint32_t size, wifi_control_packet_t *reply) {
    uint32_t start = state.wire->now_us(state.wire->context);
    uint32_t credit_polls = 0u;
    while (state.flow || state.sequence == state.credit) {
        if (!service()) return 0;
        int rc = poll_packet(reply);
        if (rc < 0) return 0;
        if (++credit_polls >= 10000u ||
            state.wire->now_us(state.wire->context) - start >= 500000u) return failed(WIFI_FW_TIMEOUT);
    }
    if (!wait_reg(0u, 8u, 4u, 0x20u, 0x20u, 500000u)) return 0;
    ++state.ioctl_id;
    uint32_t length = wifi_control_request(state.control_tx, state.sequence, state.ioctl_id,
                                           command_id, set, payload, size);
    if (length == 0u) return failed(WIFI_FW_PROTOCOL);
    store(state.control_tx + 20u, usb_report_u32(state.control_tx + 20u) | (uint32_t)state.interface << 12u);
    if (!transfer(1u, 2u, 0u, state.control_tx, 0, length)) return 0;
    ++state.sequence;
    ++state.report->tx_packets;
    start = state.wire->now_us(state.wire->context);
    for (uint32_t loops = 0u; loops < 10000u; ++loops) {
        if (!service()) return 0;
        int rc = poll_packet(reply);
        if (rc < 0) return 0;
        if (rc && reply->channel == 0u && reply->id == state.ioctl_id) {
            state.report->controller_status = reply->status;
            if (reply->status != 0u || (reply->flags & 1u)) return failed(WIFI_FW_CONTROLLER_ERROR);
            /* Successful replies can clear SET and omit the interface field.
               The single outstanding command is still bound by command and ID. */
            uint32_t reply_interface = (reply->flags >> 12u) & 15u;
            if (reply->command != command_id ||
                (reply_interface != 0u && reply_interface != state.interface)) {
                state.report->expected = command_id;
                state.report->actual = reply->command;
                store(state.report->last_rx_prefix+12u,reply->command);
                store(state.report->last_rx_prefix+16u,reply->payload_size);
                store(state.report->last_rx_prefix+20u,reply->flags);
                store(state.report->last_rx_prefix+24u,reply->status);
                return failed(WIFI_FW_PROTOCOL);
            }
            return 1;
        }
        if (state.wire->now_us(state.wire->context) - start >= 500000u) return failed(WIFI_FW_TIMEOUT);
    }
    return failed(WIFI_FW_TIMEOUT);
}

static int asset_crc_valid(const uint8_t *bytes, uint32_t size, uint32_t expected) {
    uint32_t crc = 0xffffffffu;
    for (uint32_t offset = 0u; offset < size;) {
        state.report->address = offset;
        state.report->expected = size;
        state.report->actual = offset;
        if (!service()) return 0;
        uint32_t length = size - offset;
        if (length > 256u) length = 256u;
        crc = diag_crc32_update(crc, bytes + offset, length);
        offset += length;
    }
    state.report->expected = expected;
    state.report->actual = ~crc;
    return ~crc == expected || failed(WIFI_FW_ASSET);
}

static int assets_valid(const wifi_fw_assets_t *assets) {
    if (assets == 0 || assets->firmware == 0 || assets->nvram == 0 || assets->clm == 0 ||
        assets->firmware_size < 16u || assets->firmware_size >= RADIO_RAM_SIZE / 2u ||
        (assets->firmware_size & 3u) || assets->nvram_size < 4u || assets->nvram_size > 4096u ||
        (assets->nvram_size & 3u) || assets->clm_size == 0u || assets->clm_size > 16384u)
        return failed(WIFI_FW_ASSET);
    return asset_crc_valid(assets->firmware, assets->firmware_size, assets->firmware_crc) &&
           asset_crc_valid(assets->nvram, assets->nvram_size, assets->nvram_crc) &&
           asset_crc_valid(assets->clm, assets->clm_size, assets->clm_crc);
}

static int keep_awake(void) {
    uint32_t value;
    if (!readreg(1u, 0x1001eu, 1u, &value)) return 0;
    if (value == 255u) return failed(WIFI_FW_BUS);
    if (!writereg(1u, 0x1001eu, 1u, value | 2u) ||
        !writereg(0u, 0xf0u, 1u, 8u) ||
        !writereg(1u, 0x1000eu, 1u, 2u)) return 0;
    for (uint32_t attempt = 0u; attempt < 64u; ++attempt) {
        if (!service() || !writereg(1u, 0x1001fu, 1u, 1u) ||
            !readreg(1u, 0x1001fu, 1u, &value)) return 0;
        if (value != 255u && (value & 3u) == 3u) {
            return writereg(1u, 0x1000fu, 1u, 0u) && writereg(0u, 4u, 2u, 0x99u);
        }
        if (wifi_wire_delay(state.wire, 1000u) != WIFI_BUS_OK) return failed(WIFI_FW_BUS);
    }
    return failed(WIFI_FW_TIMEOUT);
}

static void initialize(const wifi_wire_t *wire, const wifi_fw_assets_t *assets,
                        wifi_fw_report_t *report, wifi_fw_service_t callback, void *context,
                        int keep_power) {
    wifi_firmware_stop();
    state.ready = 0u;
    report->stage = WIFI_FW_IDLE;
    report->error = WIFI_FW_OK;
    report->address = report->expected = report->actual = report->verified_bytes = 0u;
    report->elapsed_ticks = report->controller_status = report->tx_packets = report->rx_packets = 0u;
    report->service_ticks = report->transfer_ticks = 0u;
    badger_bytes_zero(report->last_rx_prefix, sizeof(report->last_rx_prefix));
    badger_bytes_zero((uint8_t *)report->version, sizeof(report->version));
    if (wire == 0 || wire->transfer == 0 || wire->write == 0 || wire->data_output == 0 ||
        wire->read_data == 0 || wire->now_us == 0) { report->error = WIFI_FW_BUS; return; }
    state.wire = wire;
    state.report = report;
    state.service = callback;
    state.context = context;
    state.start = wire->now_us(wire->context);
    state.operation_start = state.start;
    state.callback_at = state.start - 5000u;
    state.callback_stage = 0xffffffffu;
    state.validating_assets = 1u;
    state.starting = 1u;
    state.window_valid = state.sequence = state.flow = 0u;
    state.credit = 1u;
    state.ioctl_id = 0u;
    state.interface = 0u;
    state.data_enabled = state.rx_head = state.rx_count = 0u;
    state.rx_dropped = 0u;
    uint32_t value = 0u;
    wifi_probe_report_t probe;
    wifi_control_packet_t reply;
    if (!assets_valid(assets)) goto done;
    state.validating_assets = 0u;
    state.operation_start = wire->now_us(wire->context);
    if (!stage(WIFI_FW_PROBE)) goto done;
    if (wifi_probe_run(wire, &probe, 0, 0) != WIFI_BUS_OK) {
        report->actual = probe.result; failed(WIFI_FW_BUS); goto done;
    }
    if ((probe.chip_id & 0xffffu) != 43439u) { report->actual = probe.chip_id; failed(WIFI_FW_CHIP); goto done; }
    if (!stage(WIFI_FW_CORES)) goto done;
    if (!writereg(1u, 0x1000eu, 1u, 8u) || !wait_reg(1u, 0x1000eu, 1u, 0x40u, 0x40u, 100000u)) goto done;
    if (!bp_read(ARM_WRAPPER + 0x800u, 1u, &value)) goto done;
    if ((value & 1u) == 0u) { failed(WIFI_FW_CORE_STATE); goto done; }
    if (!reset_core(RAM_WRAPPER) || !bp_write(0x18004010u, 4u, 3u) || !bp_write(0x18004044u, 4u, 0u)) goto done;
    if (!stage(WIFI_FW_DOWNLOAD)) goto done;
    for (uint32_t address = 0u; address < 16u; address += 4u) {
        uint32_t expected = usb_report_u32(assets->firmware + address);
        if (!bp_write(address, 4u, expected) || !bp_read(address, 4u, &value)) goto done;
        if (value != expected) { failed(WIFI_FW_READBACK); goto done; }
    }
    if (!load_bytes(0u, assets->firmware, assets->firmware_size, 0)) goto done;
    if (!stage(WIFI_FW_VERIFY) || !load_bytes(0u, assets->firmware, assets->firmware_size, 1)) goto done;
    if (!stage(WIFI_FW_NVRAM)) goto done;
    uint32_t nvram_address = RADIO_RAM_SIZE - 4u - assets->nvram_size;
    if (!load_bytes(nvram_address, assets->nvram, assets->nvram_size, 0) ||
        !load_bytes(nvram_address, assets->nvram, assets->nvram_size, 1)) goto done;
    uint32_t words = assets->nvram_size / 4u;
    uint32_t token = ((~words & 0xffffu) << 16u) | words;
    if (!bp_write(RADIO_RAM_SIZE - 4u, 4u, token) ||
        !bp_read(RADIO_RAM_SIZE - 4u, 4u, &value)) goto done;
    if (value != token) { failed(WIFI_FW_READBACK); goto done; }
    if (!stage(WIFI_FW_START) || !reset_core(ARM_WRAPPER)) goto done;
    if (!stage(WIFI_FW_HT) || !wait_reg(1u, 0x1000eu, 1u, 0x80u, 0x80u, 500000u)) goto done;
    if (!bp_write(0x18002024u, 4u, 0xf0u) || !writereg(1u, 0x10008u, 1u, 32u)) goto done;
    if (!stage(WIFI_FW_F2) || !wait_reg(0u, 8u, 4u, 0x20u, 0x20u, 500000u)) goto done;
    if (!keep_awake()) goto done;
    if (!stage(WIFI_FW_VERSION)) goto done;
    badger_bytes_zero(state.clm_payload, 192u);
    state.clm_payload[0] = 'v'; state.clm_payload[1] = 'e'; state.clm_payload[2] = 'r';
    if (!command(262u, 0u, state.clm_payload, 192u, &reply)) goto done;
    if (!wifi_control_version(&reply, state.ioctl_id, report->version)) {
        report->expected = reply.payload_size;
        report->actual = reply.flags;
        for (uint32_t i = 0u; i < 28u; ++i) report->last_rx_prefix[i] = i < reply.payload_size ? reply.payload[i] : 0u;
        failed(WIFI_FW_PROTOCOL); goto done;
    }
    if (!stage(WIFI_FW_CLM)) goto done;
    for (uint32_t offset = 0u; offset < assets->clm_size;) {
        uint32_t length = assets->clm_size - offset;
        if (length > WIFI_FW_CHUNK) length = WIFI_FW_CHUNK;
        const char name[] = "clmload";
        for (uint32_t i = 0u; i < 8u; ++i) state.clm_payload[i] = (uint8_t)name[i];
        uint32_t flags = 0x1000u | (offset == 0u ? 2u : 0u) |
                         (offset + length == assets->clm_size ? 4u : 0u);
        store(state.clm_payload + 8u, flags | (2u << 16u));
        store(state.clm_payload + 12u, length);
        store(state.clm_payload + 16u, 0u);
        badger_bytes_copy(state.clm_payload + 20u, assets->clm + offset, length);
        uint32_t padded = (length + 27u) & ~7u;
        badger_bytes_zero(state.clm_payload + length + 20u, padded - length - 20u);
        if (!command(263u, 1u, state.clm_payload, padded, &reply)) goto done;
        offset += length;
    }
    const uint8_t clm_status[19] = "clmload_status";
    if (!command(262u, 0u, clm_status, sizeof(clm_status), &reply)) goto done;
    if (reply.payload_size < 4u || usb_report_u32(reply.payload) != 0u) {
        if (reply.payload_size >= 4u) report->controller_status = usb_report_u32(reply.payload);
        failed(WIFI_FW_CONTROLLER_ERROR); goto done;
    }
    stage(WIFI_FW_DONE);
done:
    state.starting = state.validating_assets = 0u;
    report->elapsed_ticks = wire->now_us(wire->context) - state.start;
    if (keep_power && report->error == WIFI_FW_OK && report->stage == WIFI_FW_DONE) state.ready = 1u;
    else wifi_wire_power_off(wire);
}

void wifi_firmware_run(const wifi_wire_t *wire, const wifi_fw_assets_t *assets,
                        wifi_fw_report_t *report, wifi_fw_service_t callback, void *context) {
    initialize(wire, assets, report, callback, context, 0);
}

int wifi_firmware_begin(const wifi_wire_t *wire, const wifi_fw_assets_t *assets,
                         wifi_fw_report_t *report, wifi_fw_service_t callback, void *context) {
    initialize(wire, assets, report, callback, context, 1);
    return state.ready;
}

int wifi_firmware_control(uint32_t command_id, uint8_t set, uint8_t interface,
                           const uint8_t *request, uint32_t request_size,
                           uint8_t *output, uint32_t capacity, uint32_t *output_size) {
    if (output_size != 0) *output_size = 0u;
    if (!state.ready) return 0;
    state.report->error = WIFI_FW_OK;
    state.report->controller_status = 0u;
    state.operation_start = state.wire->now_us(state.wire->context);
    state.callback_at = state.operation_start - 5000u;
    if (interface > 1u || set > 1u || output_size == 0 || (capacity != 0u && output == 0)) {
        return failed(WIFI_FW_PROTOCOL);
    }
    state.interface = interface;
    wifi_control_packet_t reply;
    if (!command(command_id, set, request, request_size, &reply)) return 0;
    if (!set && reply.payload_size > capacity) return failed(WIFI_FW_PROTOCOL);
    uint32_t length = reply.payload_size;
    if (length > capacity) length = capacity;
    badger_bytes_copy(output, reply.payload, length);
    *output_size = length;
    return 1;
}

void wifi_firmware_stop(void) {
    if (state.ready) wifi_wire_power_off(state.wire);
    state.ready = 0u;
    state.data_enabled = 0u;
}

int wifi_firmware_data_enable(void) {
    if (!state.ready) return 0;
    state.data_enabled = 1u;
    state.rx_head = state.rx_count = 0u;
    state.rx_dropped = 0u;
    return 1;
}

int wifi_firmware_send_frame(uint8_t interface, const uint8_t *frame, uint32_t length) {
    if (!state.ready || !state.data_enabled) return 0;
    state.report->error = WIFI_FW_OK;
    state.operation_start = state.wire->now_us(state.wire->context);
    state.callback_at = state.operation_start - 5000u;
    uint32_t started = state.operation_start;
    wifi_control_packet_t incoming;
    for (uint32_t attempt = 0u; state.flow || state.sequence == state.credit; ++attempt) {
        if (!service() || poll_packet(&incoming) < 0) return 0;
        if (attempt >= 10000u || state.wire->now_us(state.wire->context)-started >= 500000u) return failed(WIFI_FW_TIMEOUT);
    }
    if (!wait_reg(0u,8u,4u,0x20u,0x20u,500000u)) return 0;
    uint32_t size = wifi_data_frame(state.control_tx,state.sequence,interface,frame,length);
    if (size == 0u) return failed(WIFI_FW_PROTOCOL);
    if (!transfer(1u,2u,0u,state.control_tx,0,size)) return 0;
    ++state.sequence;
    ++state.report->tx_packets;
    return 1;
}

int wifi_firmware_read_frame(uint8_t frame[1514], uint32_t *length, uint8_t *interface) {
    if (length != 0) *length = 0u;
    if (!state.ready || !state.data_enabled) return -1;
    if (frame == 0 || length == 0 || interface == 0) { failed(WIFI_FW_PROTOCOL); return -1; }
    state.report->error = WIFI_FW_OK;
    state.operation_start = state.wire->now_us(state.wire->context);
    state.callback_at = state.operation_start - 5000u;
    if (state.rx_count == 0u) {
        wifi_control_packet_t packet;
        if (!service() || poll_packet(&packet) < 0) return -1;
        if (state.rx_count == 0u) return 0;
    }
    received_frame_t *slot = &state.frames[state.rx_head];
    *length = slot->length;
    *interface = slot->interface;
    badger_bytes_copy(frame, slot->bytes, slot->length);
    state.rx_head = (uint8_t)((state.rx_head + 1u) % RX_FRAME_SLOTS);
    --state.rx_count;
    return 1;
}

uint32_t wifi_firmware_rx_dropped(void) {
    return state.rx_dropped;
}

void wifi_firmware_report(uint8_t wire[64], uint32_t build_id,
                           const uint8_t nonce[16], const wifi_fw_report_t *report) {
    usb_report_header(wire, 0x46363255u, build_id, nonce, WIFI_FW_TEST_ID);
    store(wire + 32u, (uint32_t)report->stage | (uint32_t)report->error << 8u);
    store(wire + 36u, report->address);
    store(wire + 40u, report->expected);
    store(wire + 44u, report->actual);
    store(wire + 48u, report->verified_bytes);
    store(wire + 52u, report->elapsed_ticks);
    store(wire + 56u, report->controller_status);
    store(wire + 60u, diag_crc32(wire, 60u));
}

void wifi_firmware_detail(uint8_t bytes[WIFI_TRACE_SIZE], const wifi_fw_assets_t *assets,
                           const wifi_fw_report_t *report) {
    for (uint32_t i = 0u; i < WIFI_TRACE_SIZE; ++i) bytes[i] = 0u;
    store(bytes, 0x31574657u);
    store(bytes+4u, assets->firmware_size);
    store(bytes+8u, assets->firmware_crc);
    store(bytes+12u, assets->nvram_size);
    store(bytes+16u, assets->nvram_crc);
    store(bytes+20u, assets->clm_size);
    store(bytes+24u, assets->clm_crc);
    store(bytes+28u, report->tx_packets);
    store(bytes+32u, report->rx_packets);
    for (uint32_t i = 0u; i < sizeof(report->last_rx_prefix); ++i) bytes[36u+i] = report->last_rx_prefix[i];
    for (uint32_t i = 0u; i < sizeof(report->version); ++i) bytes[64u+i] = (uint8_t)report->version[i];
}

usb_record_result_t wifi_firmware_decode(const uint8_t *wire, uint32_t length,
                                         const uint8_t request[64], uint32_t build_id,
                                         wifi_fw_report_t *report) {
    usb_record_result_t result = usb_report_check_request_for_test(request, 64u, build_id, WIFI_FW_TEST_ID);
    if (result != USB_RECORD_OK) return result;
    if (wire == 0 || report == 0 || length != 64u || wire[6] != 64u || wire[7] != 0u) return USB_RECORD_LENGTH;
    if (usb_report_u32(wire) != 0x46363255u) return USB_RECORD_MAGIC;
    if (wire[4] != 1u || wire[5] != 0u) return USB_RECORD_VERSION;
    if (usb_report_u32(wire+60u) != diag_crc32(wire,60u)) return USB_RECORD_CRC;
    if (usb_report_u32(wire+8u) != build_id) return USB_RECORD_BUILD;
    if (usb_report_u32(wire+12u) != WIFI_FW_TEST_ID) return USB_RECORD_TEST;
    for (uint32_t i = 16u; i < 32u; ++i) if (wire[i] != request[i]) return USB_RECORD_NONCE;
    uint32_t flags = usb_report_u32(wire+32u);
    if ((flags & 255u) > WIFI_FW_DONE || (flags >> 8u) > WIFI_FW_CANCELLED ||
        ((flags >> 8u) == 0u && (flags & 255u) != WIFI_FW_DONE)) return USB_RECORD_PAYLOAD;
    report->stage = (wifi_fw_stage_t)(flags & 255u);
    report->error = (wifi_fw_error_t)(flags >> 8u);
    report->address = usb_report_u32(wire+36u);
    report->expected = usb_report_u32(wire+40u);
    report->actual = usb_report_u32(wire+44u);
    report->verified_bytes = usb_report_u32(wire+48u);
    report->elapsed_ticks = usb_report_u32(wire+52u);
    report->controller_status = usb_report_u32(wire+56u);
    return USB_RECORD_OK;
}

const char *wifi_fw_stage_name(wifi_fw_stage_t stage_value) {
    switch (stage_value) {
        case WIFI_FW_IDLE: return "ASSET CHECK";
        case WIFI_FW_PROBE: return "REGISTER PROBE";
        case WIFI_FW_CORES: return "CORE RESET";
        case WIFI_FW_DOWNLOAD: return "FIRMWARE DOWNLOAD";
        case WIFI_FW_VERIFY: return "FIRMWARE READBACK";
        case WIFI_FW_NVRAM: return "NVRAM DOWNLOAD";
        case WIFI_FW_START: return "START RADIO CPU";
        case WIFI_FW_HT: return "HT CLOCK";
        case WIFI_FW_F2: return "FUNCTION 2 READY";
        case WIFI_FW_VERSION: return "VERSION QUERY";
        case WIFI_FW_CLM: return "REGULATORY DATA";
        case WIFI_FW_DONE: return "FIRMWARE READY";
    }
    return "INVALID STAGE";
}

const char *wifi_fw_error_name(wifi_fw_error_t error) {
    switch (error) {
        case WIFI_FW_OK: return "OK";
        case WIFI_FW_BUS: return "BUS ERROR";
        case WIFI_FW_CHIP: return "WRONG CHIP";
        case WIFI_FW_CORE_STATE: return "CORE STATE ERROR";
        case WIFI_FW_ASSET: return "ASSET CHECK FAILED";
        case WIFI_FW_READBACK: return "DOWNLOAD MISMATCH";
        case WIFI_FW_TIMEOUT: return "TIMEOUT";
        case WIFI_FW_PROTOCOL: return "PROTOCOL ERROR";
        case WIFI_FW_CONTROLLER_ERROR: return "CONTROLLER ERROR";
        case WIFI_FW_CANCELLED: return "CANCELLED";
    }
    return "INVALID ERROR";
}
