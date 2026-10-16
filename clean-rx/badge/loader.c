#include "loader.h"
#include "../../src/shared/bytes.h"
#include "../../src/radio/wifi_probe.h"

static int fail(clean_loader_t *l, uint32_t error) {
    if (l->error == CLEAN_OK) l->error = error;
    return 0;
}

int clean_service(clean_loader_t *l) {
    if (l->error != CLEAN_OK) return 0;
    if (l->wire->now_us(l->wire->context) - l->started >= 6000000u)
        return fail(l, CLEAN_TIMEOUT);
    if (l->service != 0 && !l->service(l->context)) return fail(l, CLEAN_CANCELLED);
    return 1;
}

void clean_loader_init(clean_loader_t *l, const wifi_wire_t *wire,
                       int (*service)(void *), void *context) {
    l->wire = wire;
    l->service = service;
    l->context = context;
    l->started = wire->now_us(wire->context);
    l->window = l->error = l->address = l->actual = l->verified = l->stage = 0u;
    l->window_valid = 0u;
    l->rx_checked = l->started;
    l->rx_resyncs = 0u;
    l->rx_status = 0u;
    l->rx_consumed = 0u;
    l->rx_credit = 0u;
    l->rx_stalled = l->rx_retries = 0u;
}

static int reg_write(clean_loader_t *l, uint8_t fn, uint32_t addr,
                     uint8_t size, uint32_t value) {
    l->address = addr;
    wifi_bus_result_t result = wifi_gspi_write(l->wire, fn, addr, size,
                                               WIFI_ORDER_NORMAL32, value);
    if (result == WIFI_BUS_OK) return 1;
    l->actual = (uint32_t)result;
    return fail(l, CLEAN_BUS);
}

static int reg_read(clean_loader_t *l, uint8_t fn, uint32_t addr,
                    uint8_t size, uint32_t *value) {
    l->address = addr;
    wifi_bus_result_t result = wifi_gspi_read(l->wire, fn, addr, size,
                                              WIFI_ORDER_NORMAL32, value);
    if (result == WIFI_BUS_OK) return 1;
    l->actual = (uint32_t)result;
    return fail(l, CLEAN_BUS);
}

static int window(clean_loader_t *l, uint32_t address) {
    uint32_t base = address & ~0x7fffu;
    if (l->window_valid && l->window == base) return 1;
    for (uint32_t i = 0u; i < 3u; ++i)
        if (!reg_write(l, 1u, 0x1000au + i, 1u, (base >> (8u + 8u*i)) & 255u))
            return 0;
    l->window = base;
    l->window_valid = 1u;
    return 1;
}

int clean_read(clean_loader_t *l, uint32_t address, uint32_t *value) {
    if (!clean_service(l) || !window(l, address)) return 0;
    int ok = reg_read(l, 1u, (address & 0x7fffu) | 0x8000u, 4u, value);
    l->address = address;
    return ok;
}

int clean_write(clean_loader_t *l, uint32_t address, uint32_t value) {
    if (!clean_service(l) || !window(l, address)) return 0;
    int ok = reg_write(l, 1u, (address & 0x7fffu) | 0x8000u, 4u, value);
    l->address = address;
    return ok;
}

int clean_block(clean_loader_t *l, uint32_t address, uint8_t *bytes,
                uint32_t length, uint8_t write) {
    if (bytes == 0 || length == 0u || write > 1u || (address & 3u) ||
        (length & 3u) || address >= 0x80000u || length > 0x80000u - address)
        return fail(l, CLEAN_INPUT);
    for (uint32_t offset = 0u; offset < length;) {
        uint8_t tx[68], rx[64u + WIFI_GSPI_BACKPLANE_PAD];
        uint32_t addr = address + offset;
        uint32_t count = 64u - (addr & 63u);
        if (count > length - offset) count = length - offset;
        if (!clean_service(l) || !window(l, addr)) return 0;
        /* SPI backplane bursts need the same word-access flag as scalar I/O. */
        badger_bytes_store_u32(tx, ((uint32_t)write << 31u) | 0x50000000u |
                               (((addr & 0x7fffu) | 0x8000u) << 11u) | count);
        if (write) badger_bytes_copy(tx + 4u, bytes + offset, count);
        wifi_bus_result_t result = l->wire->transfer(l->wire, tx, write ? count + 4u : 4u,
            write ? 0 : rx, write ? 0u : count + WIFI_GSPI_BACKPLANE_PAD);
        l->address = addr;
        if (result != WIFI_BUS_OK) {
            l->actual = (uint32_t)result;
            return fail(l, CLEAN_BUS);
        }
        if (!write) badger_bytes_copy(bytes + offset, rx + WIFI_GSPI_BACKPLANE_PAD, count);
        offset += count;
    }
    return 1;
}

int clean_wait_state(clean_loader_t *l, uint32_t expected, uint32_t timeout) {
    uint32_t started = l->wire->now_us(l->wire->context);
    do {
        uint32_t state;
        if (!clean_read(l, CLEAN_MBOX + 4u, &state)) return 0;
        l->actual = state;
        if (state == expected) return 1;
        if (state == 255u) return fail(l, CLEAN_CONTROLLER);
    } while (l->wire->now_us(l->wire->context) - started < timeout);
    return fail(l, CLEAN_TIMEOUT);
}

static int receive_resync(clean_loader_t *l, uint32_t actual) {
    l->actual = actual;
    if (l->rx_credit) return fail(l, CLEAN_INPUT);
#ifdef CLEAN_RX_STRICT_TEST
    return fail(l, CLEAN_INPUT);
#endif
    if (l->rx_retries >= 3u) return fail(l, CLEAN_INPUT);
    /* Terminate the incomplete read frame, not the running DMA descriptor ring. */
    if (!reg_write(l, 1u, 0x1000du, 1u, 1u) ||
        !reg_write(l, 0u, 4u, 2u, 0x23u)) return 0;
    ++l->rx_retries;
    ++l->rx_resyncs;
    l->rx_stalled = 0u;
    l->rx_checked = l->wire->now_us(l->wire->context);
    return 1;
}

int clean_receive(clean_loader_t *l, uint8_t packet[CLEAN_RX_PACKET_BYTES], uint32_t *length) {
    *length = 0u;
    uint32_t status;
    if (!clean_service(l) || !reg_read(l, 0u, 8u, 4u, &status)) return 0;
    l->rx_status = status;
    if (status == 0xffffffffu) return fail(l, CLEAN_BUS);
    if (l->rx_credit && (status & 2u)) return receive_resync(l, status);
#ifdef CLEAN_RX_STRICT_TEST
    if (status & 2u) return receive_resync(l, status);
#endif
    if ((status & 0x100u) == 0u) {
        uint32_t now = l->wire->now_us(l->wire->context);
        if (now - l->rx_checked < 100000u) return 1;
        l->rx_checked = now;
        uint32_t pending;
        if (l->rx_credit) {
            uint32_t posted;
            if (!clean_read(l, CLEAN_MBOX + CLEAN_SDIO_POSTED_OFFSET, &posted)) return 0;
            pending = posted - l->rx_consumed;
            if (pending > l->rx_credit) {
                l->actual = posted;
                return fail(l, CLEAN_CONTROLLER);
            }
        } else {
            uint32_t fifo;
            if (!clean_read(l, 0x18002020u, &fifo)) return 0;
            pending = fifo & 0x00800000u;
        }
        if (pending == 0u) l->rx_stalled = 0u;
        else if (l->rx_stalled) return receive_resync(l, status);
        else l->rx_stalled = 1u;
        return 1;
    }
    l->rx_stalled = 0u;
    uint32_t assembled = 0u, total = 0u, sdpcm = 0u;
    for (uint32_t part = 0u; part < 2u; ++part) {
        if (!clean_service(l)) return 0;
        uint32_t size = (status >> 9u) & 0x7ffu;
        if (size < 18u || size > CLEAN_F2_PACKET_BYTES) {
            if (part == 0u) return receive_resync(l, size);
            l->actual = size;
            return fail(l, CLEAN_INPUT);
        }
        if (status & 2u) {
            if (part != 0u) {
                l->actual = status;
                return fail(l, CLEAN_INPUT);
            }
            return receive_resync(l, status);
        }
        uint8_t physical[CLEAN_F2_PACKET_BYTES];
        uint8_t command[4];
        badger_bytes_store_u32(command, 0x60000000u | size);
        wifi_bus_result_t result = l->wire->transfer(l->wire, command, 4u,
                                                    physical, (size + 3u) & ~3u);
        if (result != WIFI_BUS_OK) {
            l->actual = (uint32_t)result;
            return fail(l, CLEAN_BUS);
        }
        if (part == 0u) badger_bytes_copy(packet, physical, (size + 3u) & ~3u);
#ifdef CLEAN_RX_STRICT_TEST
        const int check_status = 1;
#else
        const int check_status = l->rx_credit || physical[14] == 0x21u;
#endif
        if (check_status) {
            if (!reg_read(l, 0u, 8u, 4u, &status)) return 0;
            if (status == 0xffffffffu) {
                l->actual = status;
                return fail(l, CLEAN_BUS);
            }
            if (status & 2u) {
                if (part != 0u || physical[14] == 0x21u) {
                    l->actual = status;
                    return fail(l, CLEAN_INPUT);
                }
                return receive_resync(l, status);
            }
        }
        uint32_t physical_len = (uint32_t)physical[0] | ((uint32_t)physical[1] << 8u);
        uint32_t inverse = (uint32_t)physical[2] | ((uint32_t)physical[3] << 8u);
        if ((physical_len ^ inverse) != 0xffffu || physical_len < 18u ||
            physical_len > size || size > ((physical_len + 3u) & ~3u) ||
            physical[5] != 2u || physical[7] != 14u ||
            (physical[14] != 0x20u && physical[14] != 0x21u)) {
            l->actual = badger_bytes_load_u32(physical);
            return fail(l, CLEAN_INPUT);
        }
        if (physical[14] == 0x20u) {
            if (part != 0u) {
                l->actual = physical[14];
                return fail(l, CLEAN_INPUT);
            }
            badger_bytes_copy(packet, physical, physical_len);
            sdpcm = physical_len;
            break;
        }
        if (physical_len < CLEAN_F2_FRAGMENT_HEADER) {
            l->actual = physical_len;
            return fail(l, CLEAN_INPUT);
        }
        uint32_t mpdu = (uint32_t)physical[18] | ((uint32_t)physical[19] << 8u);
        uint32_t offset = (uint32_t)physical[20] | ((uint32_t)physical[21] << 8u);
        if (mpdu + 18u <= CLEAN_F2_PACKET_BYTES || mpdu > CLEAN_MPDU_BYTES ||
            offset != assembled || offset >= mpdu ||
            (part != 0u && (total != mpdu ||
             !badger_bytes_equal(packet + 4u, physical + 4u, 14u)))) {
            l->actual = mpdu | (offset << 16u);
            return fail(l, CLEAN_INPUT);
        }
        uint32_t payload = mpdu - offset;
        if (payload > CLEAN_F2_FRAGMENT_BYTES) payload = CLEAN_F2_FRAGMENT_BYTES;
        if (physical_len != CLEAN_F2_FRAGMENT_HEADER + payload) {
            l->actual = physical_len;
            return fail(l, CLEAN_INPUT);
        }
        if (part == 0u) badger_bytes_copy(packet, physical, 18u);
        total = mpdu;
        badger_bytes_copy(packet + 18u + offset, physical + CLEAN_F2_FRAGMENT_HEADER, payload);
        assembled += payload;
        if (assembled == total) {
            sdpcm = 18u + total;
            badger_bytes_store_u32(packet, sdpcm | ((sdpcm ^ 0xffffu) << 16u));
            packet[14] = 0x20u;
            break;
        }
        uint32_t started = l->wire->now_us(l->wire->context);
        while ((status & 0x100u) == 0u) {
            if (!clean_service(l) || !reg_read(l, 0u, 8u, 4u, &status)) return 0;
            if (status == 0xffffffffu) {
                l->actual = status;
                return fail(l, CLEAN_BUS);
            }
            if (status & 2u) {
                l->actual = status;
                return fail(l, CLEAN_INPUT);
            }
            if (l->wire->now_us(l->wire->context) - started >= 100000u) {
                l->actual = status;
                return fail(l, CLEAN_TIMEOUT);
            }
        }
    }
    if (sdpcm == 0u) {
        l->actual = assembled;
        return fail(l, CLEAN_INPUT);
    }
#ifndef CLEAN_RX_STRICT_TEST
    if (l->rx_credit) {
        uint32_t consumed = l->rx_consumed + 1u;
        if (!clean_write(l, CLEAN_MBOX + CLEAN_SDIO_ACK_OFFSET, consumed)) return 0;
        l->rx_consumed = consumed;
    }
#endif
    l->rx_retries = 0u;
    l->rx_checked = l->wire->now_us(l->wire->context);
    *length = sdpcm;
    return 1;
}

int clean_boot(clean_loader_t *l, const uint8_t *image, uint32_t length,
               uint32_t channel, uint32_t generation, uint8_t tx_enabled) {
    if (image == 0 || length < 256u || length > 0x14000u || (length & 3u) ||
        channel < 1u || channel > 11u || generation == 0u || tx_enabled > 1u)
        return fail(l, CLEAN_INPUT);
    wifi_probe_report_t probe;
    l->stage = 1u;
    if (wifi_probe_run(l->wire, &probe, 0, 0) != WIFI_BUS_OK) {
        l->actual = (uint32_t)probe.result;
        return fail(l, CLEAN_BUS);
    }
    if ((probe.chip_id & 0xffffu) != 43439u) {
        l->actual = probe.chip_id;
        return fail(l, CLEAN_INPUT);
    }
    l->window_valid = 0u;
    l->stage = 2u;
    if (!reg_write(l, 0u, 4u, 2u, 4u) || !reg_write(l, 0u, 6u, 2u, 0xf7u) ||
        !clean_write(l, 0x18103800u, 1u) || !clean_write(l, 0x18103408u, 3u) ||
        !clean_write(l, 0x18104800u, 1u) || !clean_write(l, 0x18104408u, 3u) ||
        !clean_write(l, 0x18104800u, 0u) || !clean_write(l, 0x18104408u, 1u) ||
        !clean_write(l, 0x18004010u, 0u) || !clean_write(l, 0x18004044u, 0u)) return 0;
    l->stage = 3u;
    uint8_t chunk[64];
    /* Cold ownership: no retained executable, descriptors or mailbox contents. */
    badger_bytes_zero(chunk, sizeof(chunk));
    for (uint32_t addr = 0u; addr < 0x80000u; addr += sizeof(chunk))
        if (!clean_block(l, addr, chunk, sizeof(chunk), 1u)) return 0;
    for (uint32_t offset = 0u; offset < length;) {
        uint32_t count = length - offset;
        if (count > sizeof(chunk)) count = sizeof(chunk);
        badger_bytes_copy(chunk, image + offset, count);
        if (!clean_block(l, offset, chunk, count, 1u) ||
            !clean_block(l, offset, chunk, count, 0u)) return 0;
        if (!badger_bytes_equal(chunk, image + offset, count)) {
            l->actual = badger_bytes_load_u32(chunk);
            return fail(l, CLEAN_READBACK);
        }
        l->verified += count;
        offset += count;
    }
    if (!clean_write(l, CLEAN_MBOX + 28u, 0xbfu | (channel << 16u) | ((uint32_t)tx_enabled << 8u)) ||
        !clean_write(l, CLEAN_MBOX + 32u, generation) ||
        !clean_write(l, CLEAN_MULTI + 4u, 0xffffffffu)) return 0;
    l->stage = 4u;
    if (!clean_write(l, 0x18103408u, 3u) || !clean_write(l, 0x18103800u, 0u) ||
        !clean_write(l, 0x18103408u, 1u) ||
        !clean_wait_state(l, 3u, 1000000u)) return 0;
    l->stage = 5u;
    if (!reg_write(l, 1u, 0x1000eu, 1u, 0x10u)) return 0;
    uint32_t started = l->wire->now_us(l->wire->context), value;
    do {
        if (!clean_service(l) || !reg_read(l, 1u, 0x1000eu, 1u, &value)) return 0;
        if ((value & 0x90u) == 0x90u) break;
        if (l->wire->now_us(l->wire->context) - started >= 500000u)
            return fail(l, CLEAN_TIMEOUT);
    } while (1);
    if (!clean_read(l, 0x180001e0u, &value)) return 0;
    if ((value & 0x30000u) != 0x30000u) {
        l->actual = value;
        return fail(l, CLEAN_CONTROLLER);
    }
    if (!clean_write(l, CLEAN_MBOX + 24u, 1u) ||
        !clean_wait_state(l, 8u, 2000000u)) return 0;
    l->stage = 6u;
    l->rx_consumed = 0u;
    l->rx_credit = CLEAN_SDIO_WINDOW;
    return 1;
}
