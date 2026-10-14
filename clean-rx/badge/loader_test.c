#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "loader.h"
#include "../../src/shared/bytes.h"
#include "frames.h"

static uint32_t ticks, calls, blocks, state, base;
static int cancel, mismatch, bus_error;
static uint32_t f2_status;
static uint8_t f2_packet[544];
static uint8_t ram[0x80000u];

static uint32_t now(void *context) {
    (void)context;
    return ticks++;
}

static void write_signal(void *context, wifi_signal_t signal, uint8_t high) {
    (void)context;
    (void)signal;
    (void)high;
}

static void output(void *context, uint8_t enabled) {
    (void)context;
    (void)enabled;
}

static uint8_t read_signal(void *context) {
    (void)context;
    return 0u;
}

static int service(void *context) {
    (void)context;
    return !cancel;
}

static wifi_bus_result_t mock_transfer(const wifi_wire_t *wire, const uint8_t *tx,
                                       uint32_t tx_length, uint8_t *rx, uint32_t rx_length) {
    (void)wire;
    ++calls;
    if (bus_error) return WIFI_BUS_TRANSFER_TIMEOUT;
    uint32_t cmd = badger_bytes_load_u32(tx);
    uint32_t addr = (cmd >> 11u) & 0x1ffffu, count = cmd & 0x7ffu;
    uint32_t function = (cmd >> 28u) & 3u;
    if (function == 0u && addr == 8u) {
        assert(rx_length == 4u);
        badger_bytes_store_u32(rx, f2_status);
        return WIFI_BUS_OK;
    }
    if (function == 2u) {
        assert(tx_length == 4u && rx_length == ((count + 3u) & ~3u));
        assert(rx_length <= sizeof(f2_packet));
        memcpy(rx, f2_packet, rx_length);
        return WIFI_BUS_OK;
    }
    if (addr >= 0x1000au && addr <= 0x1000cu) {
        assert(tx_length == 8u);
        uint32_t shift = 8u + 8u*(addr - 0x1000au);
        base = (base & ~(255u << shift)) | ((uint32_t)tx[4] << shift);
        return WIFI_BUS_OK;
    }
    uint32_t absolute = base | (addr & 0x7fffu);
    if (addr & 0x8000u) {
        assert(rx_length == 4u + WIFI_GSPI_BACKPLANE_PAD);
        memset(rx, 0, rx_length);
        badger_bytes_store_u32(rx + WIFI_GSPI_BACKPLANE_PAD, state);
        return WIFI_BUS_OK;
    }
    assert(count <= 64u && (absolute & 63u) + count <= 64u);
    assert(absolute + count <= sizeof(ram));
    ++blocks;
    if (cmd >> 31u) {
        assert(tx_length == 4u + count && rx == NULL);
        memcpy(ram + absolute, tx + 4u, count);
    } else {
        assert(tx_length == 4u && rx_length == count + WIFI_GSPI_BACKPLANE_PAD);
        memset(rx, 0, rx_length);
        memcpy(rx + WIFI_GSPI_BACKPLANE_PAD, ram + absolute, count);
        if (mismatch) rx[WIFI_GSPI_BACKPLANE_PAD] ^= 1u;
    }
    return WIFI_BUS_OK;
}

int main(void) {
    wifi_wire_t wire = {.now_us = now, .transfer = mock_transfer,
        .write = write_signal, .data_output = output, .read_data = read_signal};
    clean_loader_t l;
    uint8_t bytes[128], readback[128];
    for (unsigned i = 0u; i < sizeof(bytes); ++i) bytes[i] = (uint8_t)i;
    clean_loader_init(&l, &wire, service, NULL);
    assert(clean_block(&l, 0x7ffcu, bytes, sizeof(bytes), 1u));
    assert(blocks == 3u);
    assert(clean_block(&l, 0x7ffcu, readback, sizeof(readback), 0u));
    assert(memcmp(bytes, readback, sizeof(bytes)) == 0);
    assert(blocks == 6u);
    clean_loader_init(&l, &wire, service, NULL);
    unsigned before = calls;
    assert(!clean_block(&l, 1u, bytes, sizeof(bytes), 1u));
    assert(l.error == CLEAN_INPUT && calls == before);
    clean_loader_init(&l, &wire, service, NULL);
    assert(!clean_block(&l, 0x7fffcu, bytes, 8u, 1u));
    assert(l.error == CLEAN_INPUT);
    clean_loader_init(&l, &wire, service, NULL);
    cancel = 1;
    assert(!clean_block(&l, 0u, bytes, 4u, 1u));
    assert(l.error == CLEAN_CANCELLED);
    cancel = 0;
    clean_loader_init(&l, &wire, service, NULL);
    ticks += 6000000u;
    assert(!clean_service(&l) && l.error == CLEAN_TIMEOUT);
    clean_loader_init(&l, &wire, service, NULL);
    state = 8u;
    assert(clean_wait_state(&l, 8u, 20u));
    clean_loader_init(&l, &wire, service, NULL);
    state = 255u;
    assert(!clean_wait_state(&l, 8u, 20u) && l.error == CLEAN_CONTROLLER);
    clean_loader_init(&l, &wire, service, NULL);
    state = 7u;
    assert(!clean_wait_state(&l, 8u, 20u) && l.error == CLEAN_TIMEOUT);
    clean_loader_init(&l, &wire, service, NULL);
    assert(!clean_boot(&l, bytes, sizeof(bytes), 1u, 1u, 0u) && l.error == CLEAN_INPUT);
    uint8_t packet[544];
    uint32_t packet_length;
    clean_loader_init(&l, &wire, service, NULL);
    assert(clean_receive(&l, packet, &packet_length) && packet_length == 0u);
    f2_status = 0x100u | (524u << 9u);
    f2_packet[0] = 10u; f2_packet[1] = 2u;
    f2_packet[2] = 245u; f2_packet[3] = 253u;
    f2_packet[5] = 2u; f2_packet[7] = 14u; f2_packet[14] = 0x20u;
    assert(clean_receive(&l, packet, &packet_length) && packet_length == 522u);
    f2_packet[2] ^= 1u;
    assert(!clean_receive(&l, packet, &packet_length) && l.error == CLEAN_INPUT);
    clean_loader_init(&l, &wire, service, NULL);
    f2_status = 0x100u | (525u << 9u);
    assert(!clean_receive(&l, packet, &packet_length) && l.error == CLEAN_INPUT);
    clean_loader_init(&l, &wire, service, NULL);
    bus_error = 1;
    assert(!clean_receive(&l, packet, &packet_length) && l.error == CLEAN_BUS);
    assert(l.actual == WIFI_BUS_TRANSFER_TIMEOUT);
    bus_error = 0;
    uint8_t probe[96];
    assert(clean_probe_request(probe, 0x123u, NULL, 0u));
    assert(clean_frame_valid(probe + 6u, 88u));
    assert(probe[0] == 0x6eu && probe[1] == 4u && probe[2] == 64u && probe[3] == 0u);
    assert(probe[6] == 0x40u && probe[28] == 0x30u && probe[29] == 0x12u);
    assert(probe[30] == 0u && probe[31] == 0u);
    assert(clean_probe_request(probe, 0x123u, bytes, 32u));
    assert(probe[31] == 32u && memcmp(probe + 32u, bytes, 32u) == 0);
    assert(clean_frame_valid(probe + 6u, 88u));
    assert(!clean_probe_request(probe, 0u, bytes, 33u));
    assert(!clean_probe_request(probe, 0u, NULL, 1u));
    uint8_t frame[48] = {0x80u}, entry[40];
    frame[36] = 0u;
    frame[37] = 3u;
    memcpy(frame + 38u, "abc", 3u);
    badger_bytes_store_u32(frame + 44u, diag_crc32(frame, 44u));
    assert(clean_frame_valid(frame, sizeof(frame)));
    assert(clean_beacon_info(frame, sizeof(frame), entry, 6u));
    assert(entry[6] == 3u && entry[7] == 6u && memcmp(entry + 8u, "abc", 3u) == 0);
    frame[38] ^= 1u;
    assert(!clean_frame_valid(frame, sizeof(frame)));
    assert(!clean_beacon_info(frame, sizeof(frame), entry, 6u));
    frame[37] = 255u;
    badger_bytes_store_u32(frame + 44u, diag_crc32(frame, 44u));
    assert(!clean_beacon_info(frame, sizeof(frame), entry, 6u));
    assert(!clean_frame_valid(frame, 505u));
    puts("clean-rx loader: boundary splitting, readback, invalid inputs, cancellation, deadlines and states pass");
    return 0;
}
