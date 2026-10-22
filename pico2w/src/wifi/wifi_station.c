#include "wifi_station.h"
#include "wifi_control.h"
#include "wifi_gspi.h"
#include "wifi_assets_generated.h"

#define RADIO_RAM_SIZE 0x80000u
#define WINDOW_SIZE 0x8000u
#define ARM_WRAPPER 0x18103000u
#define RAM_WRAPPER 0x18104000u
#define BP_BLOCK 64u
#define RX_SLOTS 4u
#define CMD_UP 2u
#define CMD_DOWN 3u
#define CMD_INFRA 20u
#define CMD_AUTH 22u
#define CMD_BSSID 23u
#define CMD_WSEC 134u
#define CMD_WPA_AUTH 165u
#define CMD_GET_VAR 262u
#define CMD_SET_VAR 263u
#define CMD_PMK 268u

static int gspi_bulk(uint8_t write, uint8_t function, uint32_t address,
                     const uint8_t *input, uint8_t *output, uint32_t length);

typedef struct {
    uint8_t frame[WIFI_ETHERNET_MAX];
    uint16_t length;
} rx_frame_t;

typedef struct {
    wifi_wire_t wire;
    uint8_t active;
    uint8_t linked;
    uint8_t sequence;
    uint8_t credit;
    uint8_t flow;
    uint8_t rx_head;
    uint8_t rx_count;
    uint8_t window_valid;
    wifi_station_stage_t stage;
    wifi_station_error_t error;
    uint32_t current_window;
    uint16_t ioctl_id;
    uint32_t last_link_check;
    uint32_t join_started;
    uint32_t controller_status;
    wifi_station_diagnostic_t diagnostic;
    uint8_t tx[WIFI_CONTROL_MAX + 4u];
    uint8_t rx[WIFI_CONTROL_MAX + WIFI_GSPI_BACKPLANE_PAD + 4u];
    uint8_t control_tx[WIFI_CONTROL_MAX];
    uint8_t control_rx[WIFI_CONTROL_MAX];
    uint8_t block[BP_BLOCK];
    uint8_t pmk[68];
    rx_frame_t frames[RX_SLOTS];
} wifi_station_state_t;

static wifi_station_state_t state;
static void (*service_callback)(void *context);
static void *service_context;

void wifi_station_set_service(void (*callback)(void *context), void *context) {
    service_callback = callback;
    service_context = context;
}

static void host_service(void *context) {
    (void)context;
    if (service_callback != 0) service_callback(service_context);
}

static int station_fail(wifi_station_error_t error) {
    state.error = error;
    state.stage = WIFI_STATION_FAILED;
    state.linked = 0u;
    state.active = 0u;
    state.rx_count = 0u;
    wifi_wire_power_off(&state.wire);
    host_service(0);
    return 0;
}

static void bytes_zero(uint8_t *data, uint32_t size) {
    for (uint32_t i = 0u; i < size; ++i) data[i] = 0u;
}

static void bytes_copy(uint8_t *to, const uint8_t *from, uint32_t size) {
    for (uint32_t i = 0u; i < size; ++i) to[i] = from[i];
}

static uint32_t load32(const uint8_t *data) {
    return (uint32_t)data[0] | (uint32_t)data[1] << 8u |
           (uint32_t)data[2] << 16u | (uint32_t)data[3] << 24u;
}

static void store32(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8u);
    data[2] = (uint8_t)(value >> 16u);
    data[3] = (uint8_t)(value >> 24u);
}

static uint32_t align4(uint32_t size) {
    return (size + 3u) & ~3u;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t size) {
    for (uint32_t i = 0u; i < size; ++i) {
        if ((i & 255u) == 0u) host_service(0);
        crc ^= data[i];
        for (uint32_t bit = 0u; bit < 8u; ++bit) {
            crc = (crc >> 1u) ^ ((crc & 1u) ? 0xedb88320u : 0u);
        }
    }
    return crc;
}

static int assets_valid(void) {
    uint32_t crc;
    if ((WIFI_FIRMWARE_SIZE & 3u) != 0u || (WIFI_NVRAM_SIZE & 3u) != 0u ||
        (WIFI_CLM_SIZE & 3u) != 0u || WIFI_FIRMWARE_SIZE < 16u ||
        WIFI_FIRMWARE_SIZE >= RADIO_RAM_SIZE / 2u ||
        WIFI_NVRAM_SIZE < 4u || WIFI_NVRAM_SIZE > 4096u ||
        WIFI_CLM_SIZE == 0u || WIFI_CLM_SIZE > 16384u) return 0;
    crc = ~crc32_update(0xffffffffu, wifi_firmware_data, WIFI_FIRMWARE_SIZE);
    if (crc != WIFI_FIRMWARE_CRC32) return 0;
    crc = ~crc32_update(0xffffffffu, wifi_nvram_data, WIFI_NVRAM_SIZE);
    if (crc != WIFI_NVRAM_CRC32) return 0;
    crc = ~crc32_update(0xffffffffu, wifi_clm_data, WIFI_CLM_SIZE);
    return crc == WIFI_CLM_CRC32;
}

static int read_reg(uint8_t function, uint32_t address, uint8_t size, uint32_t *value) {
    int ok = wifi_gspi_read(&state.wire, function, address, size, WIFI_ORDER_NORMAL32, value);
    state.diagnostic.address = address;
    if (ok) state.diagnostic.actual = *value;
    if (!ok) state.error = WIFI_STATION_ERROR_BUS;
    return ok;
}

static int write_reg(uint8_t function, uint32_t address, uint8_t size, uint32_t value) {
    int ok = wifi_gspi_write(&state.wire, function, address, size, WIFI_ORDER_NORMAL32, value);
    state.diagnostic.address = address;
    state.diagnostic.expected = value;
    if (!ok) state.error = WIFI_STATION_ERROR_BUS;
    return ok;
}

static int wait_reg(uint8_t function, uint32_t address, uint8_t size,
                    uint32_t mask, uint32_t expected, uint32_t timeout) {
    uint32_t start = state.wire.now_us(state.wire.context);
    for (uint32_t attempt = 0u; attempt < 2000u; ++attempt) {
        uint32_t value = 0u;
        if (!read_reg(function, address, size, &value)) return 0;
        state.diagnostic.expected = expected;
        if (value != 0xffffffffu && value != 0xffu && (value & mask) == expected) return 1;
        if (state.wire.now_us(state.wire.context) - start >= timeout) break;
        if (!wifi_wire_delay(&state.wire, 1000u)) return 0;
    }
    state.error = WIFI_STATION_ERROR_TIMEOUT;
    return 0;
}

static int keep_awake(void) {
    uint32_t value = 0u;
    if (!read_reg(1u, 0x1001eu, 1u, &value) || value == 0xffu ||
        !write_reg(1u, 0x1001eu, 1u, value | 2u) ||
        !write_reg(0u, 0xf0u, 1u, 8u) ||
        !write_reg(1u, 0x1000eu, 1u, 2u)) return 0;
    for (uint32_t attempt = 0u; attempt < 64u; ++attempt) {
        if (!write_reg(1u, 0x1001fu, 1u, 1u) ||
            !read_reg(1u, 0x1001fu, 1u, &value)) return 0;
        if (value != 0xffu && (value & 3u) == 3u) {
            return write_reg(1u, 0x1000fu, 1u, 0u) &&
                   write_reg(0u, 4u, 2u, 0x99u);
        }
        if (!wifi_wire_delay(&state.wire, 1000u)) return 0;
    }
    return 0;
}

static int set_window(uint32_t address) {
    uint32_t base = address & ~(WINDOW_SIZE - 1u);
    if (state.window_valid && state.current_window == base) return 1;
    if (!write_reg(1u, 0x1000au, 1u, (base >> 8u) & 0xffu) ||
        !write_reg(1u, 0x1000bu, 1u, (base >> 16u) & 0xffu) ||
        !write_reg(1u, 0x1000cu, 1u, (base >> 24u) & 0xffu)) return 0;
    state.current_window = base;
    state.window_valid = 1u;
    return 1;
}

static int backplane_read(uint32_t address, uint8_t *output, uint32_t length) {
    if (length == 1u || length == 4u) {
        uint32_t value = 0u;
        if (!set_window(address) ||
            !read_reg(1u, (address & 0x7fffu) | 0x8000u, (uint8_t)length, &value)) return 0;
        for (uint32_t i = 0u; i < length; ++i) output[i] = (uint8_t)(value >> (8u * i));
        state.diagnostic.address = address;
        return 1;
    }
    uint32_t left = length;
    while (left != 0u) {
        uint32_t count = left > BP_BLOCK ? BP_BLOCK : left;
        uint32_t offset = address & (WINDOW_SIZE - 1u);
        if (count > WINDOW_SIZE - offset) count = WINDOW_SIZE - offset;
        if (!set_window(address) ||
            !gspi_bulk(0u, 1u, offset, 0, output, count)) return 0;
        address += count;
        output += count;
        left -= count;
    }
    return 1;
}

static int backplane_write(uint32_t address, const uint8_t *input, uint32_t length) {
    if (length == 1u || length == 4u) {
        uint32_t value = 0u;
        for (uint32_t i = 0u; i < length; ++i) value |= (uint32_t)input[i] << (8u * i);
        int ok = set_window(address) &&
                 write_reg(1u, (address & 0x7fffu) | 0x8000u, (uint8_t)length, value);
        state.diagnostic.address = address;
        return ok;
    }
    uint32_t left = length;
    while (left != 0u) {
        uint32_t count = left > BP_BLOCK ? BP_BLOCK : left;
        uint32_t offset = address & (WINDOW_SIZE - 1u);
        if (count > WINDOW_SIZE - offset) count = WINDOW_SIZE - offset;
        if (!set_window(address) ||
            !gspi_bulk(1u, 1u, offset | 0x8000u, input, 0, count)) return 0;
        address += count;
        input += count;
        left -= count;
    }
    return 1;
}

static int gspi_bulk(uint8_t write, uint8_t function, uint32_t address,
                     const uint8_t *input, uint8_t *output, uint32_t length) {
    uint32_t aligned = align4(length);
    uint32_t command = ((uint32_t)write << 31u) | (1u << 30u) |
                       ((uint32_t)function << 28u) | (address << 11u) | length;
    state.tx[0] = (uint8_t)command;
    state.tx[1] = (uint8_t)(command >> 8u);
    state.tx[2] = (uint8_t)(command >> 16u);
    state.tx[3] = (uint8_t)(command >> 24u);
    if (write) {
        bytes_copy(state.tx + 4u, input, length);
        bytes_zero(state.tx + 4u + length, aligned - length);
        return wifi_wire_transfer(&state.wire, state.tx, 4u + aligned, 0, 0u);
    }
    uint32_t padding = function == 1u ? WIFI_GSPI_BACKPLANE_PAD : 0u;
    if (!wifi_wire_transfer(&state.wire, state.tx, 4u, state.rx, padding + aligned)) return 0;
    bytes_copy(output, state.rx + padding, length);
    return 1;
}

static int probe(void) {
    uint32_t value = 0u;
    wifi_wire_power_off(&state.wire);
    if (!wifi_wire_delay(&state.wire, 20000u)) return 0;
    state.wire.write(state.wire.context, WIFI_SIGNAL_DATA, 0u);
    state.wire.data_output(state.wire.context, 1u);
    state.wire.write(state.wire.context, WIFI_SIGNAL_ENABLE, 1u);
    if (!wifi_wire_delay(&state.wire, 250000u)) return 0;
    state.wire.data_output(state.wire.context, 0u);
    uint8_t matched = 0u;
    for (uint32_t attempt = 0u; attempt < 10u; ++attempt) {
        if (!wifi_gspi_read(&state.wire, 0u, 0x14u, 4u,
                            WIFI_ORDER_RESET16, &value)) return 0;
        if (value == 0xfeedbeadu) {
            matched = 1u;
            break;
        }
        if (!wifi_wire_delay(&state.wire, 1000u)) return 0;
    }
    if (!matched || !wifi_gspi_write(&state.wire, 0u, 0u, 4u,
                                      WIFI_ORDER_RESET16, 0x000200a3u)) return 0;
    if (!read_reg(0u, 0u, 4u, &value) ||
        (value & 0x00ffff7fu) != (0x000200a3u & 0x00ffff7fu)) return 0;
    for (uint32_t i = 0u; i < 16u; ++i) {
        if (!read_reg(0u, 0x14u, 4u, &value) || value != 0xfeedbeadu) return 0;
    }
    if (!write_reg(0u, 0x1du, 1u, WIFI_GSPI_BACKPLANE_PAD) ||
        !read_reg(0u, 0x1du, 1u, &value) || value != WIFI_GSPI_BACKPLANE_PAD ||
        !write_reg(1u, 0x1000eu, 1u, 8u) ||
        !wait_reg(1u, 0x1000eu, 1u, 0x40u, 0x40u, 100000u)) return 0;
    state.window_valid = 0u;
    uint8_t chip[4];
    if (!backplane_read(0x18000000u, chip, sizeof(chip))) return 0;
    value = load32(chip);
    return value != 0u && value != 0xffffffffu && (value & 0xffffu) == 43439u;
}

static int core_reset(uint32_t base) {
    uint8_t value, zero = 0u;
    if (!backplane_read(base + 0x800u, &value, 1u) || (value & 1u) == 0u) return 0;
    value = 3u;
    if (!backplane_write(base + 0x408u, &value, 1u) ||
        !backplane_read(base + 0x408u, &value, 1u) ||
        !backplane_write(base + 0x800u, &zero, 1u) ||
        !wifi_wire_delay(&state.wire, 1000u)) return 0;
    value = 1u;
    if (!backplane_write(base + 0x408u, &value, 1u) ||
        !backplane_read(base + 0x408u, &value, 1u) || (value & 3u) != 1u ||
        !backplane_read(base + 0x800u, &value, 1u)) return 0;
    return (value & 1u) == 0u;
}

static int load_asset(uint32_t address, const uint8_t *data, uint32_t size,
                      uint8_t verify) {
    state.diagnostic.completed = 0u;
    for (uint32_t offset = 0u; offset < size;) {
        host_service(0);
        uint32_t length = size - offset;
        if (length > BP_BLOCK) length = BP_BLOCK;
        uint32_t window_left = WINDOW_SIZE - ((address + offset) & (WINDOW_SIZE - 1u));
        if (length > window_left) length = window_left;
        if (verify) {
            if (!backplane_read(address + offset, state.block, length)) return 0;
            for (uint32_t i = 0u; i < length; ++i) {
                if (state.block[i] != data[offset + i]) {
                    state.diagnostic.address = address + offset + i;
                    state.diagnostic.expected = data[offset + i];
                    state.diagnostic.actual = state.block[i];
                    return 0;
                }
            }
        } else if (!backplane_write(address + offset, data + offset, length)) return 0;
        offset += length;
        state.diagnostic.completed = offset;
    }
    return 1;
}

static uint32_t cstring(const char *value, uint8_t *output, uint32_t capacity) {
    uint32_t length = 0u;
    if (value == 0) return 0u;
    while (value[length] != '\0') {
        if (length >= capacity) return 0u;
        output[length] = (uint8_t)value[length];
        ++length;
    }
    return length;
}

static int gspi_transfer(uint8_t write, uint8_t function, uint32_t address,
                         const uint8_t *input, uint8_t *output, uint32_t length) {
    uint32_t aligned = align4(length);
    uint32_t command = ((uint32_t)write << 31u) | (1u << 30u) |
                       ((uint32_t)function << 28u) | (address << 11u) | length;
    if (length == 0u || aligned + 4u > sizeof(state.tx) ||
        (write && input == 0) || (!write && output == 0)) return 0;
    state.tx[0] = (uint8_t)command;
    state.tx[1] = (uint8_t)(command >> 8u);
    state.tx[2] = (uint8_t)(command >> 16u);
    state.tx[3] = (uint8_t)(command >> 24u);
    if (write) {
        bytes_copy(state.tx + 4u, input, length);
        bytes_zero(state.tx + 4u + length, aligned - length);
        return wifi_wire_transfer(&state.wire, state.tx, aligned + 4u, 0, 0u);
    }
    if (!wifi_wire_transfer(&state.wire, state.tx, 4u, state.rx, aligned)) return 0;
    bytes_copy(output, state.rx, length);
    return 1;
}

static int poll_packet(wifi_control_packet_t *packet) {
    uint32_t status;
    if (!read_reg(0u, 8u, 4u, &status) || status == 0xffffffffu) {
        state.error = WIFI_STATION_ERROR_BUS;
        return -1;
    }
    if ((status & 0x100u) == 0u) return 0;
    uint32_t length = (status >> 9u) & 0x7ffu;
    if (length < 12u || length > WIFI_CONTROL_MAX ||
        !gspi_transfer(0u, 2u, 0u, 0, state.control_rx, length)) {
        state.error = WIFI_STATION_ERROR_PROTOCOL;
        return -1;
    }
    if (!wifi_control_parse(state.control_rx, length, packet)) {
        state.error = WIFI_STATION_ERROR_PROTOCOL;
        return -1;
    }
    state.flow = packet->flow;
    state.credit = packet->credit;
    if (packet->channel == 1u) {
        uint32_t type, status, reason;
        if (wifi_control_event(packet, &type, &status, &reason)) {
            state.diagnostic.event_type = type;
            state.diagnostic.event_status = status;
            state.diagnostic.event_reason = reason;
        }
    }
    if (packet->channel == 2u) {
        const uint8_t *frame;
        uint32_t frame_length;
        uint8_t interface;
        if (wifi_data_decode(packet, &interface, &frame, &frame_length) &&
            interface == 0u) {
            if (state.rx_count < RX_SLOTS) {
                uint32_t slot = (state.rx_head + state.rx_count) % RX_SLOTS;
                bytes_copy(state.frames[slot].frame, frame, frame_length);
                state.frames[slot].length = (uint16_t)frame_length;
                ++state.rx_count;
            }
        }
    }
    return 1;
}

static int control(uint32_t command, uint8_t set, uint8_t interface,
                   const uint8_t *request, uint32_t request_size,
                   uint8_t *reply_data, uint32_t capacity, uint32_t *reply_size) {
    uint32_t started = state.wire.now_us(state.wire.context);
    wifi_control_packet_t packet;
    state.controller_status = 0u;
    state.diagnostic.command = command;
    if (reply_size != 0) *reply_size = 0u;
    if (!state.active || interface > 1u || set > 1u || reply_size == 0 ||
        (request_size != 0u && request == 0) || (!set && capacity != 0u && reply_data == 0))
        return 0;
    while (state.flow || state.sequence == state.credit) {
        host_service(0);
        int polled = poll_packet(&packet);
        if (polled < 0) return 0;
        if (state.wire.now_us(state.wire.context) - started > 500000u) {
            state.error = WIFI_STATION_ERROR_TIMEOUT;
            return 0;
        }
        if (!polled && !wifi_wire_delay(&state.wire, 1000u)) {
            state.error = WIFI_STATION_ERROR_BUS;
            return 0;
        }
    }
    if (!wait_reg(0u, 8u, 4u, 0x20u, 0x20u, 500000u)) return 0;
    ++state.ioctl_id;
    if (state.ioctl_id == 0u) ++state.ioctl_id;
    uint32_t packet_size = wifi_control_request(state.control_tx, state.sequence,
        state.ioctl_id, command, set, request, request_size);
    if (packet_size == 0u) {
        state.error = WIFI_STATION_ERROR_PROTOCOL;
        return 0;
    }
    store32(state.control_tx + 20u, load32(state.control_tx + 20u) |
            ((uint32_t)interface << 12u));
    int sent = gspi_transfer(1u, 2u, 0u, state.control_tx, 0, packet_size);
    bytes_zero(state.control_tx, sizeof(state.control_tx));
    if (!sent) {
        state.error = WIFI_STATION_ERROR_BUS;
        return 0;
    }
    ++state.sequence;
    started = state.wire.now_us(state.wire.context);
    for (uint32_t attempt = 0u; attempt < 10000u; ++attempt) {
        host_service(0);
        int polled = poll_packet(&packet);
        if (polled < 0) return 0;
        if (state.wire.now_us(state.wire.context) - started > 500000u) {
            state.error = WIFI_STATION_ERROR_TIMEOUT;
            return 0;
        }
        if (!polled) {
            if (!wifi_wire_delay(&state.wire, 1000u)) {
                state.error = WIFI_STATION_ERROR_BUS;
                return 0;
            }
            continue;
        }
        if (packet.channel == 0u && packet.id == state.ioctl_id) {
            state.controller_status = packet.status;
            if (packet.command != command) {
                state.error = WIFI_STATION_ERROR_PROTOCOL;
                return 0;
            }
            if (packet.status != 0u || (packet.flags & 1u)) {
                state.error = WIFI_STATION_ERROR_CONTROLLER;
                return 0;
            }
            if (!set) {
                if (packet.payload_size > capacity) {
                    state.error = WIFI_STATION_ERROR_PROTOCOL;
                    return 0;
                }
                bytes_copy(reply_data, packet.payload, packet.payload_size);
                *reply_size = packet.payload_size;
            }
            return 1;
        }
    }
    state.error = WIFI_STATION_ERROR_TIMEOUT;
    return 0;
}

static int scalar(uint32_t command, uint32_t value) {
    uint8_t request[4];
    store32(request, value);
    uint32_t size = 0u;
    return control(command, 1u, 0u, request, sizeof(request), 0, 0u, &size);
}

static uint32_t var_name(const char *name, uint8_t *output) {
    uint32_t size = 0u;
    while (name[size] != '\0') {
        output[size] = (uint8_t)name[size];
        ++size;
    }
    output[size] = 0u;
    return size + 1u;
}

static int set_variable(const char *name, const uint8_t *value, uint32_t size) {
    uint32_t name_size = var_name(name, state.tx);
    bytes_copy(state.tx + name_size, value, size);
    uint32_t reply_size = 0u;
    return control(CMD_SET_VAR, 1u, 0u, state.tx, name_size + size,
                   0, 0u, &reply_size);
}

static int set_var_scalar(const char *name, uint32_t value) {
    uint8_t scalar_value[4];
    store32(scalar_value, value);
    return set_variable(name, scalar_value, sizeof(scalar_value));
}

static int set_var_pair(const char *name, uint32_t index, uint32_t value) {
    uint8_t pair[8];
    store32(pair, index);
    store32(pair + 4u, value);
    return set_variable(name, pair, sizeof(pair));
}

static int set_passphrase(const uint8_t *passphrase, uint32_t length) {
    bytes_zero(state.pmk, sizeof(state.pmk));
    if (length == 64u) {
        for (uint32_t i = 0u; i < 32u; ++i) {
            uint8_t high = passphrase[i * 2u];
            uint8_t low = passphrase[i * 2u + 1u];
            high = high >= '0' && high <= '9' ? (uint8_t)(high - '0') :
                   high >= 'a' && high <= 'f' ? (uint8_t)(high - 'a' + 10u) :
                   high >= 'A' && high <= 'F' ? (uint8_t)(high - 'A' + 10u) : 0xffu;
            low = low >= '0' && low <= '9' ? (uint8_t)(low - '0') :
                  low >= 'a' && low <= 'f' ? (uint8_t)(low - 'a' + 10u) :
                  low >= 'A' && low <= 'F' ? (uint8_t)(low - 'A' + 10u) : 0xffu;
            if (high > 15u || low > 15u) return 0;
            state.pmk[4u + i] = (uint8_t)((high << 4u) | low);
        }
        state.pmk[0] = 32u;
    } else {
        if (length > 63u) return 0;
        state.pmk[0] = (uint8_t)length;
        state.pmk[2] = 1u;
        bytes_copy(state.pmk + 4u, passphrase, length);
    }
    uint32_t reply_size = 0u;
    int ok = control(CMD_PMK, 1u, 0u, state.pmk, sizeof(state.pmk), 0, 0u, &reply_size);
    bytes_zero(state.pmk, sizeof(state.pmk));
    return ok;
}

static int command_empty(uint32_t command) {
    uint32_t reply_size = 0u;
    return control(command, 1u, 0u, 0, 0u, 0, 0u, &reply_size);
}

static int station_configure(const uint8_t *ssid, uint32_t ssid_length,
                             const uint8_t *passphrase, uint32_t passphrase_length) {
    uint8_t country[12];
    uint8_t request[128];
    uint32_t reply_size = 0u;
    state.stage = WIFI_STATION_CONFIGURE;
    if (!scalar(CMD_DOWN, 1u) || !set_var_scalar("bus:txglom", 0u)) return 0;
    bytes_zero(country, sizeof(country));
    country[0] = 'D'; country[1] = 'E';
    store32(country + 4u, 0xffffffffu);
    country[8] = 'D'; country[9] = 'E';
    uint8_t events[23];
    bytes_zero(events, sizeof(events));
    events[4] = 0xebu;
    events[5] = 0x18u;
    events[6] = 1u;
    events[9] = 0x40u;
    if (!set_variable("country", country, sizeof(country)) ||
        !set_variable("bsscfg:event_msgs", events, sizeof(events)) ||
        !command_empty(CMD_UP) || !scalar(86u, 0u) ||
        !scalar(CMD_WSEC, 4u) ||
        !set_var_pair("bsscfg:sup_wpa", 0u, 1u) ||
        !set_var_pair("bsscfg:sup_wpa2_eapver", 0u, 0xffffffffu) ||
        !set_var_pair("bsscfg:sup_wpa_tmo", 0u, 5000u) ||
        !set_passphrase(passphrase, passphrase_length) ||
        !scalar(CMD_INFRA, 1u) || !scalar(CMD_AUTH, 0u) ||
        !set_var_scalar("mfp", 1u) || !scalar(CMD_WPA_AUTH, 0x80u)) return 0;
    state.stage = WIFI_STATION_JOIN;
    uint32_t name_length = var_name("join", request);
    uint8_t *join = request + name_length;
    bytes_zero(join, 70u);
    store32(join, ssid_length);
    bytes_copy(join + 4u, ssid, ssid_length);
    for (uint32_t i = 40u; i < 56u; i += 4u) store32(join + i, 0xffffffffu);
    for (uint32_t i = 56u; i < 62u; ++i) join[i] = 0xffu;
    /* Zero chanspec count requests a legal-country scan, not channel-1 only. */
    store32(join + 64u, 0u);
    return control(CMD_SET_VAR, 1u, 0u, request, name_length + 68u,
                   0, 0u, &reply_size);
}

static int get_variable(const char *name, uint8_t *output, uint32_t capacity,
                        uint32_t *size) {
    uint8_t request[64];
    if (capacity > sizeof(request)) return 0;
    bytes_zero(request, sizeof(request));
    uint32_t name_length = var_name(name, request);
    return control(CMD_GET_VAR, 0u, 0u, request, capacity,
                   output, capacity, size) && name_length <= capacity;
}

static int get_bssid(uint8_t bssid[6]) {
    uint32_t size = 0u;
    const uint8_t request[6] = {0u};
    if (!control(CMD_BSSID, 0u, 0u, request, sizeof(request), bssid, 6u, &size)) return 0;
    if (size != 6u) {
        state.error = WIFI_STATION_ERROR_PROTOCOL;
        return 0;
    }
    return 1;
}

static int station_authorized(void) {
    uint8_t bssid[6];
    uint8_t request[512];
    uint32_t size = 0u;
    if (!get_bssid(bssid)) return 0;
    uint8_t nonzero = 0u;
    for (uint32_t i = 0u; i < sizeof(bssid); ++i) nonzero |= bssid[i];
    if (nonzero == 0u) return 0;
    bytes_zero(request, sizeof(request));
    uint32_t name_length = var_name("sta_info", request);
    bytes_copy(request + name_length, bssid, sizeof(bssid));
    if (!control(CMD_GET_VAR, 0u, 0u, request, sizeof(request),
                 state.rx, sizeof(request), &size)) return 0;
    if (size < 84u) {
        state.error = WIFI_STATION_ERROR_PROTOCOL;
        return 0;
    }
    uint32_t version = (uint32_t)state.rx[0] | (uint32_t)state.rx[1] << 8u;
    uint32_t declared = (uint32_t)state.rx[2] | (uint32_t)state.rx[3] << 8u;
    if (version < 3u || version > 7u || declared < 84u || declared > size) {
        state.error = WIFI_STATION_ERROR_PROTOCOL;
        return 0;
    }
    for (uint32_t i = 0u; i < sizeof(bssid); ++i) {
        if (state.rx[16u + i] != bssid[i]) {
            state.error = WIFI_STATION_ERROR_PROTOCOL;
            return 0;
        }
    }
    uint32_t flags = load32(state.rx + 8u);
    return (flags & 0x30u) == 0x30u;
}

static int service_rx(void) {
    wifi_control_packet_t packet;
    return poll_packet(&packet) >= 0;
}

int wifi_station_init(const char *ssid, const char *passphrase) {
    uint8_t ssid_bytes[32];
    uint8_t passphrase_bytes[64];
    bytes_zero(ssid_bytes, sizeof(ssid_bytes));
    bytes_zero(passphrase_bytes, sizeof(passphrase_bytes));
    uint32_t ssid_length = cstring(ssid, ssid_bytes, sizeof(ssid_bytes));
    uint32_t passphrase_length = cstring(passphrase, passphrase_bytes,
                                         sizeof(passphrase_bytes));
    if (ssid_length == 0u || ssid_length > 32u ||
        passphrase_length < 8u || passphrase_length > 64u) {
        state.stage = WIFI_STATION_FAILED;
        state.error = WIFI_STATION_ERROR_ARGUMENT;
        bytes_zero(ssid_bytes, sizeof(ssid_bytes));
        bytes_zero(passphrase_bytes, sizeof(passphrase_bytes));
        return 0;
    }
    if (passphrase_length == 64u) {
        for (uint32_t i = 0u; i < passphrase_length; ++i) {
            uint8_t c = passphrase_bytes[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                  (c >= 'A' && c <= 'F'))) {
                state.stage = WIFI_STATION_FAILED;
                state.error = WIFI_STATION_ERROR_ARGUMENT;
                bytes_zero(ssid_bytes, sizeof(ssid_bytes));
                bytes_zero(passphrase_bytes, sizeof(passphrase_bytes));
                return 0;
            }
        }
    }
    if (state.active) wifi_wire_power_off(&state.wire);
    bytes_zero((uint8_t *)&state, sizeof(state));
    state.diagnostic.event_type = 0xffffffffu;
    wifi_wire_init(&state.wire);
    state.wire.service = host_service;
    state.credit = 1u;
    state.stage = WIFI_STATION_ASSET_CHECK;
    state.error = WIFI_STATION_ERROR_NONE;
    if (!assets_valid()) {
        state.error = WIFI_STATION_ERROR_ASSET;
        goto failed;
    }
    state.stage = WIFI_STATION_BUS_PROBE;
    if (!probe()) {
        state.error = WIFI_STATION_ERROR_BUS;
        goto failed;
    }
    uint32_t value = 0u;
    if (!read_reg(1u, 0x1000eu, 1u, &value) ||
        !write_reg(1u, 0x1000eu, 1u, 8u) ||
        !wait_reg(1u, 0x1000eu, 1u, 0x40u, 0x40u, 100000u)) {
        state.error = WIFI_STATION_ERROR_BUS;
        goto failed;
    }
    state.stage = WIFI_STATION_FIRMWARE_LOAD;
    state.diagnostic.step = 1u;
    uint8_t core = 0u;
    if (!backplane_read(ARM_WRAPPER + 0x800u, &core, 1u) || !(core & 1u) ||
        !core_reset(RAM_WRAPPER)) {
        state.error = WIFI_STATION_ERROR_BUS;
        goto failed;
    }
    uint8_t setup[4];
    state.diagnostic.step = 2u;
    store32(setup, 3u);
    if (!backplane_write(0x18004010u, setup, 4u)) goto failed;
    bytes_zero(setup, sizeof(setup));
    if (!backplane_write(0x18004044u, setup, 4u)) goto failed;
    state.diagnostic.step = 3u;
    if (!load_asset(0u, wifi_firmware_data, WIFI_FIRMWARE_SIZE, 0u)) goto failed;
    state.diagnostic.step = 4u;
    if (!load_asset(0u, wifi_firmware_data, WIFI_FIRMWARE_SIZE, 1u)) goto failed;
    uint32_t nvram_address = RADIO_RAM_SIZE - 4u - WIFI_NVRAM_SIZE;
    state.diagnostic.step = 5u;
    if (!load_asset(nvram_address, wifi_nvram_data, WIFI_NVRAM_SIZE, 0u)) goto failed;
    state.diagnostic.step = 6u;
    if (!load_asset(nvram_address, wifi_nvram_data, WIFI_NVRAM_SIZE, 1u)) goto failed;
    uint32_t words = WIFI_NVRAM_SIZE / 4u;
    uint32_t token = ((~words & 0xffffu) << 16u) | words;
    store32(setup, token);
    state.diagnostic.step = 7u;
    if (!backplane_write(RADIO_RAM_SIZE - 4u, setup, 4u) ||
        !backplane_read(RADIO_RAM_SIZE - 4u, setup, 4u) || load32(setup) != token) goto failed;
    state.diagnostic.step = 8u;
    if (!core_reset(ARM_WRAPPER)) goto failed;
    state.diagnostic.step = 9u;
    if (!wait_reg(1u, 0x1000eu, 1u, 0x80u, 0x80u, 500000u)) goto failed;
    state.diagnostic.step = 10u;
    store32(setup, 0xf0u);
    if (!backplane_write(0x18002024u, setup, 4u) ||
        !write_reg(1u, 0x10008u, 1u, 32u) ||
        !wait_reg(0u, 8u, 4u, 0x20u, 0x20u, 500000u) ||
        !keep_awake()) goto failed;
    state.active = 1u;
    state.stage = WIFI_STATION_REGULATORY;
    state.diagnostic.step = 11u;
    bytes_zero(state.tx, sizeof(state.tx));
    state.tx[0] = 'v'; state.tx[1] = 'e'; state.tx[2] = 'r';
    if (!control(CMD_GET_VAR, 0u, 0u, state.tx, 192u,
                 state.rx, 192u, &value) || value < 2u) goto failed;
    char version[192];
    if (!wifi_version_string(state.rx, value, version)) {
        state.error = WIFI_STATION_ERROR_PROTOCOL;
        goto failed;
    }
    state.diagnostic.step = 12u;
    for (uint32_t offset = 0u; offset < WIFI_CLM_SIZE;) {
        uint32_t length = WIFI_CLM_SIZE - offset;
        if (length > 512u) length = 512u;
        bytes_zero(state.tx, sizeof(state.tx));
        bytes_copy(state.tx, (const uint8_t *)"clmload", 8u);
        uint32_t flags = 0x1000u | (offset == 0u ? 2u : 0u) |
                         (offset + length == WIFI_CLM_SIZE ? 4u : 0u);
        store32(state.tx + 8u, flags | (2u << 16u));
        store32(state.tx + 12u, length);
        store32(state.tx + 16u, 0u);
        bytes_copy(state.tx + 20u, wifi_clm_data + offset, length);
        uint32_t padded = (length + 27u) & ~7u;
        if (!control(CMD_SET_VAR, 1u, 0u, state.tx, padded,
                     0, 0u, &value)) goto failed;
        offset += length;
    }
    bytes_zero(state.tx, sizeof(state.tx));
    bytes_copy(state.tx, (const uint8_t *)"clmload_status", 15u);
    if (!control(CMD_GET_VAR, 0u, 0u, state.tx, 32u, state.rx,
                 sizeof(state.rx), &value) || value < 4u || load32(state.rx) != 0u) {
        state.error = WIFI_STATION_ERROR_CONTROLLER;
        goto failed;
    }
    state.diagnostic.step = 13u;
    if (!station_configure(ssid_bytes, ssid_length, passphrase_bytes,
                           passphrase_length)) goto failed;
    state.last_link_check = state.wire.now_us(state.wire.context);
    state.join_started = state.last_link_check;
    state.linked = 0u;
    state.stage = WIFI_STATION_JOIN;
    state.diagnostic.step = 14u;
    state.error = WIFI_STATION_ERROR_NONE;
    bytes_zero(passphrase_bytes, sizeof(passphrase_bytes));
    return 1;
failed:
    bytes_zero(state.pmk, sizeof(state.pmk));
    bytes_zero(passphrase_bytes, sizeof(passphrase_bytes));
    bytes_zero(state.control_tx, sizeof(state.control_tx));
    return station_fail(state.error == WIFI_STATION_ERROR_NONE ?
                        WIFI_STATION_ERROR_CONTROLLER : state.error);
}

void wifi_station_service(void) {
    if (!state.active) return;
    if (!service_rx()) {
        station_fail(state.error == WIFI_STATION_ERROR_NONE ?
                     WIFI_STATION_ERROR_PROTOCOL : state.error);
        return;
    }
    uint32_t now = state.wire.now_us(state.wire.context);
    if (!state.linked && now - state.join_started >= 30000000u) {
        station_fail(WIFI_STATION_ERROR_TIMEOUT);
        return;
    }
    if ((state.last_link_check == 0u || now - state.last_link_check >= 500000u) &&
        !(state.flow || state.sequence == state.credit)) {
        state.last_link_check = now;
        uint8_t was_linked = state.linked;
        state.error = WIFI_STATION_ERROR_NONE;
        state.linked = station_authorized();
        if (!state.linked && was_linked) {
            station_fail(state.error == WIFI_STATION_ERROR_NONE ?
                         WIFI_STATION_ERROR_DISCONNECTED : state.error);
            return;
        }
        if (!state.linked && state.error != WIFI_STATION_ERROR_NONE &&
            !(state.error == WIFI_STATION_ERROR_CONTROLLER &&
              (state.controller_status == 0xffffffefu ||
               state.controller_status == 0xffffffe2u))) {
            station_fail(state.error);
            return;
        }
        if (!state.linked && now - state.join_started >= 30000000u) {
            station_fail(WIFI_STATION_ERROR_TIMEOUT);
            return;
        }
        state.error = WIFI_STATION_ERROR_NONE;
        state.stage = state.linked ? WIFI_STATION_CONNECTED : WIFI_STATION_JOIN;
    }
}

int wifi_station_link_up(void) {
    return state.active && state.linked;
}

wifi_station_stage_t wifi_station_stage(void) {
    return state.stage;
}

wifi_station_error_t wifi_station_error(void) {
    return state.error;
}

uint32_t wifi_station_controller_status(void) {
    return state.controller_status;
}

const wifi_station_diagnostic_t *wifi_station_diagnostic(void) {
    return &state.diagnostic;
}

int wifi_station_get_mac(unsigned char mac[6]) {
    uint8_t output[16];
    uint32_t length = 0u;
    if (!state.active || mac == 0) {
        if (state.error == WIFI_STATION_ERROR_NONE) state.error = WIFI_STATION_ERROR_ARGUMENT;
        return 0;
    }
    if (!get_variable("cur_etheraddr", output, sizeof(output), &length))
        return station_fail(state.error == WIFI_STATION_ERROR_NONE ?
                            WIFI_STATION_ERROR_CONTROLLER : state.error);
    if (length < 6u) return station_fail(WIFI_STATION_ERROR_PROTOCOL);
    bytes_copy(mac, output, 6u);
    return 1;
}

int wifi_station_read(void *buffer, unsigned int capacity, unsigned int *length) {
    if (length != 0) *length = 0u;
    if (!state.active || buffer == 0 || length == 0) {
        if (state.error == WIFI_STATION_ERROR_NONE) state.error = WIFI_STATION_ERROR_ARGUMENT;
        return -1;
    }
    wifi_station_service();
    if (!state.active) return -1;
    if (state.rx_count == 0u) return 0;
    rx_frame_t *frame = &state.frames[state.rx_head];
    if (capacity < frame->length) {
        state.error = WIFI_STATION_ERROR_ARGUMENT;
        return -1;
    }
    bytes_copy((uint8_t *)buffer, frame->frame, frame->length);
    *length = frame->length;
    state.rx_head = (uint8_t)((state.rx_head + 1u) % RX_SLOTS);
    --state.rx_count;
    return 1;
}

int wifi_station_send(const void *buffer, unsigned int length) {
    if (!state.active || !state.linked || buffer == 0 || length < 14u ||
        length > WIFI_ETHERNET_MAX) {
        if (state.error == WIFI_STATION_ERROR_NONE) state.error = WIFI_STATION_ERROR_ARGUMENT;
        return 0;
    }
    uint32_t start = state.wire.now_us(state.wire.context);
    wifi_control_packet_t packet;
    while (state.flow || state.sequence == state.credit) {
        int result = poll_packet(&packet);
        if (result < 0) return station_fail(state.error);
        if (state.wire.now_us(state.wire.context) - start > 500000u)
            return station_fail(WIFI_STATION_ERROR_TIMEOUT);
        if (!result && !wifi_wire_delay(&state.wire, 1000u))
            return station_fail(WIFI_STATION_ERROR_BUS);
    }
    uint32_t size = wifi_data_frame(state.control_tx, state.sequence, 0u,
                                    (const uint8_t *)buffer, length);
    if (size == 0u) return station_fail(WIFI_STATION_ERROR_PROTOCOL);
    int sent = gspi_transfer(1u, 2u, 0u, state.control_tx, 0, size);
    bytes_zero(state.control_tx, sizeof(state.control_tx));
    if (!sent) return station_fail(WIFI_STATION_ERROR_BUS);
    ++state.sequence;
    return 1;
}
