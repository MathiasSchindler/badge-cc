#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "loader.h"
#include "../../src/shared/bytes.h"
#include "frames.h"
#include "../tx_diag.h"

static uint32_t ticks, calls, blocks, state, base;
static int cancel, mismatch, bus_error, terminate_error, f2_post_status, ack_error;
static uint32_t f2_status;
static uint32_t f2_after_status;
static uint32_t fifo_status, terminations;
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
        assert(cmd == (0x60000000u | count));
        assert(rx_length <= sizeof(f2_packet));
        memcpy(rx, f2_packet, rx_length);
        if (f2_post_status) f2_status = f2_after_status;
        return WIFI_BUS_OK;
    }
    if (function == 1u && addr == 0x1000du) {
        assert(tx_length == 8u && tx[4] == 1u);
        if (terminate_error) return WIFI_BUS_TRANSFER_TIMEOUT;
        ++terminations;
        return WIFI_BUS_OK;
    }
    if (function == 0u && addr == 4u) {
        assert(tx_length == 8u && tx[4] == 0x23u);
        return WIFI_BUS_OK;
    }
    if (addr >= 0x1000au && addr <= 0x1000cu) {
        assert(tx_length == 8u);
        uint32_t shift = 8u + 8u*(addr - 0x1000au);
        base = (base & ~(255u << shift)) | ((uint32_t)tx[4] << shift);
        return WIFI_BUS_OK;
    }
    uint32_t absolute = base | (addr & 0x7fffu);
    assert(addr & 0x8000u);
    if (ack_error && (cmd >> 31u) && absolute == CLEAN_MBOX + CLEAN_SDIO_ACK_OFFSET)
        return WIFI_BUS_TRANSFER_TIMEOUT;
    if (!(cmd >> 31u) && count == 4u &&
        (absolute == CLEAN_MBOX + 4u || absolute == 0x18002020u)) {
        assert(rx_length == 4u + WIFI_GSPI_BACKPLANE_PAD);
        memset(rx, 0, rx_length);
        badger_bytes_store_u32(rx + WIFI_GSPI_BACKPLANE_PAD,
                              absolute == 0x18002020u ? fifo_status : state);
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
    for (uint32_t event = 0u; event < 256u; ++event) {
        bytes[0] = (uint8_t)event;
        assert(clean_block(&l, 0x38400u, bytes, 96u, 1u));
        assert(clean_write(&l, CLEAN_MBOX + 8u, 7u));
        uint32_t conditions;
        assert(clean_read(&l, CLEAN_MBOX + 8u, &conditions) && conditions == 7u);
        assert(clean_block(&l, 0x38400u, readback, 96u, 0u));
        assert(memcmp(bytes, readback, 96u) == 0);
    }
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
    assert(l.rx_status == f2_status);
    f2_status = 0x100u | (524u << 9u);
    f2_packet[0] = 10u; f2_packet[1] = 2u;
    f2_packet[2] = 245u; f2_packet[3] = 253u;
    f2_packet[5] = 2u; f2_packet[7] = 14u; f2_packet[14] = 0x20u;
    assert(clean_receive(&l, packet, &packet_length) && packet_length == 522u);
    assert(l.rx_status == f2_status);
    static const uint32_t sizes[] = {18u, 26u, 63u, 100u, 106u, 108u, 137u, 522u};
    for (uint32_t i = 0u; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        uint32_t size = sizes[i];
        f2_status = 0x100u | (size << 9u);
        badger_bytes_store_u32(f2_packet, size | ((size ^ 0xffffu) << 16u));
        assert(clean_receive(&l, packet, &packet_length) && packet_length == size);
        assert(l.rx_status == f2_status && memcmp(packet, f2_packet, size) == 0);
    }
    f2_packet[2] ^= 1u;
    assert(!clean_receive(&l, packet, &packet_length) && l.error == CLEAN_INPUT);
    for (uint32_t i = 0u; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        clean_loader_init(&l, &wire, service, NULL);
        l.rx_credit = 1u;
        uint32_t size = sizes[i];
        badger_bytes_store_u32(ram + CLEAN_MBOX + CLEAN_SDIO_ACK_OFFSET, 0u);
        f2_status = 0x100u | (size << 9u);
        badger_bytes_store_u32(f2_packet, size | ((size ^ 0xffffu) << 16u));
        assert(clean_receive(&l, packet, &packet_length) && packet_length == size);
#ifdef CLEAN_RX_STRICT_TEST
        assert(l.rx_consumed == 0u);
        assert(badger_bytes_load_u32(ram + CLEAN_MBOX + CLEAN_SDIO_ACK_OFFSET) == 0u);
#else
        assert(l.rx_consumed == 1u);
        assert(badger_bytes_load_u32(ram + CLEAN_MBOX + CLEAN_SDIO_ACK_OFFSET) == 1u);
#endif
    }
    clean_loader_init(&l, &wire, service, NULL);
    l.rx_credit = 1u;
    f2_status = 0u;
    fifo_status = 0x00800000u;
    badger_bytes_store_u32(ram + CLEAN_MBOX + CLEAN_SDIO_POSTED_OFFSET, 0u);
    for (uint32_t i = 0u; i < 3u; ++i) {
        ticks += 100001u;
        assert(clean_receive(&l, packet, &packet_length) && packet_length == 0u);
        assert(!l.rx_stalled && l.rx_resyncs == 0u);
    }
    badger_bytes_store_u32(ram + CLEAN_MBOX + CLEAN_SDIO_POSTED_OFFSET, 1u);
    ticks += 100001u;
    assert(clean_receive(&l, packet, &packet_length) && l.rx_stalled);
    ticks += 100001u;
    assert(!clean_receive(&l, packet, &packet_length) && l.error == CLEAN_INPUT);
    assert(l.rx_resyncs == 0u && terminations == 0u);
    clean_loader_init(&l, &wire, service, NULL);
    l.rx_credit = 1u;
    badger_bytes_store_u32(ram + CLEAN_MBOX + CLEAN_SDIO_POSTED_OFFSET, 2u);
    ticks += 100001u;
    assert(!clean_receive(&l, packet, &packet_length) && l.error == CLEAN_CONTROLLER);
    assert(l.actual == 2u && terminations == 0u);
    clean_loader_init(&l, &wire, service, NULL);
    l.rx_credit = 1u;
    f2_status = 2u;
    assert(!clean_receive(&l, packet, &packet_length) && l.error == CLEAN_INPUT);
    assert(l.rx_consumed == 0u && l.rx_resyncs == 0u && terminations == 0u);
    static const uint32_t credit_postread[] = {2u, 0xffffffffu};
    for (uint32_t i = 0u; i < 2u; ++i) {
        clean_loader_init(&l, &wire, service, NULL);
        l.rx_credit = 1u;
        f2_status = 0x100u | (63u << 9u);
        badger_bytes_store_u32(f2_packet, 63u | ((63u ^ 0xffffu) << 16u));
        badger_bytes_store_u32(ram + CLEAN_MBOX + CLEAN_SDIO_ACK_OFFSET, 0u);
        f2_post_status = 1;
        f2_after_status = credit_postread[i];
        assert(!clean_receive(&l, packet, &packet_length));
        assert(l.error == (i == 0u ? CLEAN_INPUT : CLEAN_BUS));
        assert(l.actual == credit_postread[i] && l.rx_consumed == 0u);
        assert(badger_bytes_load_u32(ram + CLEAN_MBOX + CLEAN_SDIO_ACK_OFFSET) == 0u);
        assert(l.rx_resyncs == 0u && terminations == 0u);
    }
    f2_post_status = 0;
    clean_loader_init(&l, &wire, service, NULL);
    l.rx_credit = 1u;
    f2_status = 0x100u | (63u << 9u);
    f2_packet[2] ^= 1u;
    assert(!clean_receive(&l, packet, &packet_length) && l.error == CLEAN_INPUT);
    assert(l.rx_consumed == 0u);
    assert(badger_bytes_load_u32(ram + CLEAN_MBOX + CLEAN_SDIO_ACK_OFFSET) == 0u);
    f2_packet[2] ^= 1u;
#ifndef CLEAN_RX_STRICT_TEST
    clean_loader_init(&l, &wire, service, NULL);
    l.rx_credit = 1u;
    l.rx_consumed = 0xffffffffu;
    f2_status = 0x100u | (63u << 9u);
    assert(clean_receive(&l, packet, &packet_length) && l.rx_consumed == 0u);
    assert(badger_bytes_load_u32(ram + CLEAN_MBOX + CLEAN_SDIO_ACK_OFFSET) == 0u);
    clean_loader_init(&l, &wire, service, NULL);
    l.rx_credit = 1u;
    ack_error = 1;
    assert(!clean_receive(&l, packet, &packet_length) && l.error == CLEAN_BUS);
    assert(l.rx_consumed == 0u);
    ack_error = 0;
#endif
    badger_bytes_store_u32(f2_packet, 522u | ((522u ^ 0xffffu) << 16u));
    f2_packet[2] ^= 1u;
#ifdef CLEAN_RX_STRICT_TEST
    static const uint32_t underflows[] = {2u, 0x102u, 0x100u | (63u << 9u) | 2u};
    for (uint32_t i = 0u; i < sizeof(underflows) / sizeof(underflows[0]); ++i) {
        clean_loader_init(&l, &wire, service, NULL);
        f2_status = underflows[i];
        uint32_t before = calls;
        assert(!clean_receive(&l, packet, &packet_length) && l.error == CLEAN_INPUT);
        assert(l.rx_status == f2_status && l.actual == f2_status && packet_length == 0u);
        assert(calls == before + 1u && l.rx_resyncs == 0u && terminations == 0u);
    }
    static const uint32_t postread[] = {0u, 2u, 0xffffffffu};
    for (uint32_t i = 0u; i < sizeof(postread) / sizeof(postread[0]); ++i) {
        clean_loader_init(&l, &wire, service, NULL);
        f2_status = 0x100u | (63u << 9u);
        badger_bytes_store_u32(f2_packet, 63u | ((63u ^ 0xffffu) << 16u));
        f2_post_status = 1;
        f2_after_status = postread[i];
        uint32_t before_postread = calls;
        int received = clean_receive(&l, packet, &packet_length);
        if (postread[i] == 0u) {
            assert(received && packet_length == 63u && l.error == CLEAN_OK);
            assert(l.rx_status == (0x100u | (63u << 9u)));
        } else {
            assert(!received && packet_length == 0u && l.actual == postread[i]);
            assert(l.rx_status == (0x100u | (63u << 9u)));
            assert(l.error == (postread[i] == 2u ? CLEAN_INPUT : CLEAN_BUS));
        }
        assert(calls == before_postread + 3u && memcmp(packet, f2_packet, 63u) == 0);
        assert(l.rx_resyncs == 0u && terminations == 0u);
    }
    f2_post_status = 0;
    clean_loader_init(&l, &wire, service, NULL);
    f2_status = 0x100u | (525u << 9u);
    assert(!clean_receive(&l, packet, &packet_length) && l.error == CLEAN_INPUT);
    assert(l.rx_status == f2_status && l.actual == 525u);
    assert(l.rx_resyncs == 0u && terminations == 0u);
    clean_loader_init(&l, &wire, service, NULL);
    f2_status = 0u;
    fifo_status = 0x00800000u;
    ticks += 100000u;
    assert(clean_receive(&l, packet, &packet_length) && l.rx_stalled);
    ticks += 100000u;
    assert(!clean_receive(&l, packet, &packet_length) && l.error == CLEAN_INPUT);
    assert(l.rx_status == 0u && l.rx_resyncs == 0u && terminations == 0u);
    puts("clean-rx strict test: first FIFO fault preserved without termination or retries");
    return 0;
#endif
    clean_loader_init(&l, &wire, service, NULL);
    f2_status = 0x100u | (525u << 9u);
    for (uint32_t retry = 1u; retry <= 3u; ++retry) {
        assert(clean_receive(&l, packet, &packet_length) && packet_length == 0u);
        assert(l.rx_resyncs == retry && terminations == retry && l.error == CLEAN_OK);
    }
    assert(!clean_receive(&l, packet, &packet_length) && l.error == CLEAN_INPUT);
    assert(l.actual == 525u && terminations == 3u);
    clean_loader_init(&l, &wire, service, NULL);
    f2_status = 0x100u;
    assert(clean_receive(&l, packet, &packet_length) && l.rx_resyncs == 1u);
    assert(l.actual == 0u);
    f2_status = 0x100u | (0x551u << 9u);
    assert(clean_receive(&l, packet, &packet_length) && l.rx_resyncs == 2u);
    assert(l.actual == 0x551u);
    f2_status = 0x100u | (524u << 9u);
    f2_packet[2] ^= 1u;
    assert(clean_receive(&l, packet, &packet_length) && packet_length == 522u);
    assert(l.rx_retries == 0u && l.rx_resyncs == 2u);
    f2_status |= 2u;
    assert(clean_receive(&l, packet, &packet_length) && l.rx_resyncs == 3u);
    clean_loader_init(&l, &wire, service, NULL);
    f2_status = 0u;
    fifo_status = 0x00800000u;
    ticks += 100000u;
    assert(clean_receive(&l, packet, &packet_length) && l.rx_stalled && l.rx_resyncs == 0u);
    ticks += 100000u;
    assert(clean_receive(&l, packet, &packet_length) && !l.rx_stalled && l.rx_resyncs == 1u);
    fifo_status = 0u;
    ticks += 100000u;
    assert(clean_receive(&l, packet, &packet_length) && !l.rx_stalled);
    clean_loader_init(&l, &wire, service, NULL);
    f2_status = 0x100u;
    bus_error = 1;
    assert(!clean_receive(&l, packet, &packet_length) && l.rx_resyncs == 0u);
    bus_error = 0;
    clean_loader_init(&l, &wire, service, NULL);
    terminate_error = 1;
    assert(!clean_receive(&l, packet, &packet_length) && l.error == CLEAN_BUS);
    assert(l.actual == WIFI_BUS_TRANSFER_TIMEOUT && l.rx_resyncs == 0u);
    terminate_error = 0;
    clean_loader_init(&l, &wire, service, NULL);
    cancel = 1;
    assert(!clean_receive(&l, packet, &packet_length) && l.error == CLEAN_CANCELLED);
    assert(l.rx_resyncs == 0u);
    cancel = 0;
    ticks = UINT32_MAX - 50000u;
    clean_loader_init(&l, &wire, service, NULL);
    f2_status = 0u;
    fifo_status = 0x00800000u;
    ticks += 100000u;
    assert(clean_receive(&l, packet, &packet_length) && l.rx_stalled && l.rx_resyncs == 0u);
    fifo_status = 0u;
    ticks += 100000u;
    assert(clean_receive(&l, packet, &packet_length) && !l.rx_stalled && l.rx_resyncs == 0u);
    clean_loader_init(&l, &wire, service, NULL);
    bus_error = 1;
    assert(!clean_receive(&l, packet, &packet_length) && l.error == CLEAN_BUS);
    assert(l.actual == WIFI_BUS_TRANSFER_TIMEOUT);
    bus_error = 0;
    uint8_t probe[96];
    assert(clean_probe_request(probe, 0x123u, NULL, 0u));
    assert(clean_frame_valid(probe + 6u, 88u));
    assert(probe[0] == 0x6eu && probe[1] == 4u && probe[2] == 64u && probe[3] == 0u);
    assert(probe[4] == 0x7fu && probe[5] == 0xeeu);
    uint8_t plcp[6];
    clean_cck_plcp(plcp, 0x0au, 704u);
    assert(plcp[0] == 10u && plcp[1] == 4u && plcp[2] == 0xc0u && plcp[3] == 2u);
    clean_ofdm_plcp(plcp);
    uint32_t signal = (uint32_t)plcp[0] | ((uint32_t)plcp[1] << 8u) | ((uint32_t)plcp[2] << 16u);
    assert((signal & 15u) == 11u && ((signal >> 5u) & 0xfffu) == 88u);
    unsigned parity = 0u;
    for (unsigned bit = 0u; bit < 18u; ++bit) parity ^= (signal >> bit) & 1u;
    assert(parity == 0u && (signal >> 18u) == 0u);
    assert(probe[6] == 0x40u && probe[28] == 0x30u && probe[29] == 0x12u);
    assert(probe[30] == 0u && probe[31] == 0u);
    assert(clean_probe_request(probe, 0x123u, bytes, 32u));
    assert(probe[31] == 32u && memcmp(probe + 32u, bytes, 32u) == 0);
    assert(clean_frame_valid(probe + 6u, 88u));
    assert(!clean_probe_request(probe, 0u, bytes, 33u));
    assert(!clean_probe_request(probe, 0u, NULL, 1u));
    const uint8_t ap[6] = {2u,3u,4u,5u,6u,7u}, station[6] = {2u,8u,9u,10u,11u,12u};
    uint8_t compact[96];
    assert(clean_compact_probe_request(compact, 0x123u, NULL, 0u, ap) == 52u);
    assert(clean_frame_valid(compact + 6u, 46u));
    assert(compact[0] == 10u && compact[2] == 0x70u && compact[3] == 1u);
    assert(compact[6] == 0x40u && compact[42] == 50u && compact[43] == 4u);
    for (unsigned i = 52u; i < sizeof(compact); ++i) assert(compact[i] == 0u);
    assert(memcmp(compact + 10u, ap, 6u) == 0 && memcmp(compact + 22u, ap, 6u) == 0);
    assert(clean_compact_probe_request(compact, 0x123u, bytes, 32u, ap) == 84u);
    assert(clean_frame_valid(compact + 6u, 78u));
    assert(compact[31] == 32u && memcmp(compact + 32u, bytes, 32u) == 0);
    assert(!clean_compact_probe_request(compact, 0u, bytes, 33u, ap));
    static const uint8_t broadcast[6] = {255u,255u,255u,255u,255u,255u};
    assert(clean_compact_probe_request(compact, 0x123u, bytes, 7u, broadcast) == 59u);
    assert(clean_frame_valid(compact + 6u, 53u));
    assert(memcmp(compact + 10u, broadcast, 6u) == 0);
    assert(memcmp(compact + 22u, broadcast, 6u) == 0);
    assert(compact[0] == 10u && compact[2] == 0xa8u && compact[3] == 1u);
    uint8_t short_auth[96];
    assert(clean_auth_request(short_auth, 0x123u, ap, 1u) == 40u);
    assert(clean_frame_valid(short_auth + 6u, 34u));
    assert(short_auth[0] == 10u && short_auth[1] == 4u &&
           short_auth[2] == 0x10u && short_auth[3] == 1u);
    assert(short_auth[6] == 0xb0u && short_auth[32] == 1u);
    for (unsigned i = 40u; i < sizeof(short_auth); ++i) assert(short_auth[i] == 0u);
    assert(clean_auth_request(probe, 0x123u, ap, 0u) == 94u);
    assert(clean_frame_valid(probe + 6u, 88u));
    assert(probe[6] == 0xb0u && probe[30] == 0u && probe[32] == 1u);
    assert(memcmp(probe + 10u, ap, 6u) == 0 && memcmp(probe + 22u, ap, 6u) == 0);
    assert(!clean_ap_reply(probe + 6u, 88u, station, ap, 1u));
    memcpy(probe + 10u, station, 6u);
    memcpy(probe + 16u, ap, 6u);
    probe[32] = 2u;
    badger_bytes_store_u32(probe + 90u, diag_crc32(probe + 6u, 84u));
    assert(clean_ap_reply(probe + 6u, 88u, station, ap, 1u));
    probe[7] = 0x08u;
    badger_bytes_store_u32(probe + 90u, diag_crc32(probe + 6u, 84u));
    assert(clean_ap_reply(probe + 6u, 88u, station, ap, 1u));
    probe[7] = 0x38u;
    badger_bytes_store_u32(probe + 90u, diag_crc32(probe + 6u, 84u));
    assert(clean_ap_reply(probe + 6u, 88u, station, ap, 1u));
    probe[7] = 0x40u;
    badger_bytes_store_u32(probe + 90u, diag_crc32(probe + 6u, 84u));
    assert(!clean_ap_reply(probe + 6u, 88u, station, ap, 1u));
    probe[7] = 0u;
    badger_bytes_store_u32(probe + 90u, diag_crc32(probe + 6u, 84u));
    assert(!clean_ap_reply(probe + 6u, 88u, station, ap, 0u));
    assert(!clean_ap_reply(probe + 6u, 88u, ap, ap, 1u));
    uint8_t rejection[30];
    memcpy(rejection, probe + 6u, 26u);
    rejection[0] = 0xc0u;
    rejection[24] = 1u;
    badger_bytes_store_u32(rejection + 26u, diag_crc32(rejection, 26u));
    assert(clean_ap_reply(rejection, sizeof(rejection), station, ap, 1u));
    assert(!clean_ap_reply(rejection, sizeof(rejection), ap, ap, 1u));
    rejection[0] = 0xa0u;
    badger_bytes_store_u32(rejection + 26u, diag_crc32(rejection, 26u));
    assert(clean_ap_reply(rejection, sizeof(rejection), station, ap, 0u));
    uint8_t ack[14] = {0xd4u};
    memcpy(ack + 4u, station, 6u);
    badger_bytes_store_u32(ack + 10u, diag_crc32(ack, 10u));
    assert(clean_ack_reply(ack, sizeof(ack), station));
    assert(!clean_ack_reply(ack, sizeof(ack), ap));
    assert(!clean_ap_reply(ack, sizeof(ack), station, ap, 1u));
    ack[10] ^= 1u;
    assert(!clean_ack_reply(ack, sizeof(ack), station));
    static uint8_t evidence[2048];
    badger_bytes_store_u32(evidence + 4u, 8u);
    badger_bytes_store_u32(evidence + 32u, 0x4f505258u);
    badger_bytes_store_u32(evidence + 36u, 10u);
    badger_bytes_store_u32(evidence + 40u, 7u);
    badger_bytes_store_u32(evidence + 76u, 0x55550028u);
    badger_bytes_store_u32(evidence + 1088u, 32u);
    badger_bytes_store_u32(evidence + 1092u, 1u);
    badger_bytes_store_u32(evidence + 1096u, 1u);
    badger_bytes_store_u32(evidence + 1988u, 40u);
    memcpy(evidence + 1972u, station, 6u);
    ack[10] ^= 1u;
    memcpy(evidence + 1122u, ack, sizeof(ack));
    badger_bytes_store_u32(evidence + 2004u, 1u);
    badger_bytes_store_u32(evidence + 2008u, 1u);
    assert(!clean_tx_confirmed(evidence));
    uint8_t challenge[14] = {1u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,12u,13u,14u};
    uint8_t peer_frame[96];
    clean_peer_request(peer_frame, challenge, 0u);
    assert(clean_peer_reply(peer_frame + 6u, 88u, challenge));
    assert(peer_frame[0] == 0x6eu);
    clean_peer_request(peer_frame, challenge, 1u);
    assert(peer_frame[0] == 0x0au && peer_frame[2] == 0xc0u && peer_frame[3] == 2u);
    assert(clean_peer_reply(peer_frame + 6u, 88u, challenge));
    assert(!clean_peer_reply(peer_frame + 6u, 87u, challenge));
    challenge[13] ^= 1u;
    assert(!clean_peer_reply(peer_frame + 6u, 88u, challenge));
    challenge[13] ^= 1u;
    peer_frame[60] ^= 1u;
    assert(!clean_peer_reply(peer_frame + 6u, 88u, challenge));
    clean_peer_request(peer_frame, challenge, 0u);
    static uint8_t peer_evidence[2048];
    badger_bytes_store_u32(peer_evidence + 4u, 8u);
    badger_bytes_store_u32(peer_evidence + 32u, 0x4f505258u);
    badger_bytes_store_u32(peer_evidence + 36u, 10u);
    badger_bytes_store_u32(peer_evidence + 1088u, 106u);
    memcpy(peer_evidence + 1122u, peer_frame + 6u, 88u);
    assert(!clean_peer_received(peer_evidence, challenge));
    badger_bytes_store_u32(peer_evidence + 1996u, 1u);
    assert(clean_peer_received(peer_evidence, challenge));
    badger_bytes_store_u32(peer_evidence + 1092u, 1u);
    assert(!clean_peer_received(peer_evidence, challenge));
    badger_bytes_store_u32(peer_evidence + 1092u, 0u);
    badger_bytes_store_u32(peer_evidence + 44u, 0x84u);
    assert(!clean_peer_received(peer_evidence, challenge));
    badger_bytes_store_u32(evidence + 1992u, 1u);
    assert(clean_tx_confirmed(evidence));
    static const uint32_t lengths[] = {40u,51u,52u,84u,85u,94u};
    for (unsigned i = 0u; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
        badger_bytes_store_u32(evidence + 1988u, lengths[i]);
        badger_bytes_store_u32(evidence + 76u, 0x55550000u | lengths[i]);
        assert(clean_tx_confirmed(evidence) == (lengths[i] != 51u && lengths[i] != 85u));
    }
    badger_bytes_store_u32(evidence + 1988u, 40u);
    badger_bytes_store_u32(evidence + 76u, 0x55550028u);
    badger_bytes_store_u32(evidence + 44u, 0x82u);
    assert(!clean_tx_confirmed(evidence));
    badger_bytes_store_u32(evidence + 44u, 0x84u);
    assert(!clean_tx_confirmed(evidence));
    badger_bytes_store_u32(evidence + 44u, 0x85u);
    assert(!clean_tx_confirmed(evidence));
    badger_bytes_store_u32(evidence + 44u, 0u);
    evidence[1122u + 10u] ^= 1u;
    assert(!clean_tx_confirmed(evidence));
    evidence[1122u + 10u] ^= 1u;
    evidence[1972u] ^= 1u;
    assert(!clean_tx_confirmed(evidence));
    evidence[1972u] ^= 1u;
    badger_bytes_store_u32(evidence + 40u, 6u);
    assert(!clean_tx_confirmed(evidence));
    badger_bytes_store_u32(evidence + 40u, 7u);
    badger_bytes_store_u32(evidence + 1992u, 0u);
    badger_bytes_store_u32(evidence + 1960u, 1u);
    badger_bytes_store_u32(evidence + 1088u, 106u);
    memcpy(evidence + 1980u, ap, 6u);
    memcpy(evidence + 1122u, probe + 6u, 88u);
    assert(clean_tx_confirmed(evidence));
    badger_bytes_store_u32(evidence + 8u, 5u);
    assert(!clean_tx_confirmed(evidence));
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
    uint32_t command = 5u | (40u << 8u) | (1u << 19u) |
                       CLEAN_TX_REARM_RX | CLEAN_TX_MATCH_SOURCE;
    assert((command & 0xffu) == 5u);
    assert(((command >> 8u) & 0x03ffu) == 40u);
    assert(((command >> 18u) & 1u) == 0u);
    assert(((command >> 19u) & 0xffu) == 1u);
    uint8_t phase_packet[66] = {0};
    badger_bytes_zero(evidence, sizeof(evidence));
    memcpy(evidence + 1980u, ap, 6u);
    frame[37] = 3u;
    memcpy(frame + 10u, ap, 6u);
    memcpy(frame + 16u, ap, 6u);
    badger_bytes_store_u32(frame + 44u, diag_crc32(frame, 44u));
    memcpy(phase_packet + 18u, frame, sizeof(frame));
    assert(clean_rx_phase(phase_packet, sizeof(phase_packet)) == -1);
    assert(clean_rx_phase(phase_packet, 17u) == -1);
    phase_packet[12] = (uint8_t)CLEAN_RX_PHASE_MAGIC;
    phase_packet[13] = (uint8_t)(CLEAN_RX_PHASE_MAGIC >> 8u);
    assert(clean_rx_phase(phase_packet, sizeof(phase_packet)) == 0);
    clean_record_post_tx(evidence, phase_packet, sizeof(phase_packet), 100u);
    assert(badger_bytes_load_u32(evidence + 2020u) == 0u);
    phase_packet[12] |= 0x80u;
    clean_record_post_tx(evidence, phase_packet, sizeof(phase_packet), 200u);
    assert(badger_bytes_load_u32(evidence + 2020u) == 1u);
    assert(badger_bytes_load_u32(evidence + 2024u) == 1u);
    assert(badger_bytes_load_u32(evidence + 2028u) == 200u);
    assert(badger_bytes_load_u32(evidence + 2032u) == 1u);
    assert(badger_bytes_load_u32(evidence + 2036u) == 1u);
    phase_packet[65] ^= 1u;
    clean_record_post_tx(evidence, phase_packet, sizeof(phase_packet), 300u);
    assert(badger_bytes_load_u32(evidence + 2020u) == 2u);
    assert(badger_bytes_load_u32(evidence + 2024u) == 1u);
    phase_packet[65] ^= 1u;
    phase_packet[28] ^= 1u;
    badger_bytes_store_u32(phase_packet + 62u, diag_crc32(phase_packet + 18u, 44u));
    clean_record_post_tx(evidence, phase_packet, sizeof(phase_packet), 400u);
    assert(badger_bytes_load_u32(evidence + 2024u) == 2u);
    assert(badger_bytes_load_u32(evidence + 2032u) == 1u);
    phase_packet[12] ^= 1u;
    clean_record_post_tx(evidence, phase_packet, sizeof(phase_packet), 500u);
    assert(badger_bytes_load_u32(evidence + 2020u) == 3u);
    assert(badger_bytes_load_u32(evidence + 2040u) == 5u);
    assert(badger_bytes_load_u32(evidence + 2028u) == 200u);
    phase_packet[12] = (uint8_t)CLEAN_RX_PHASE_MAGIC | 0x80u;
    memcpy(phase_packet + 18u, ack, sizeof(ack));
    clean_record_post_tx(evidence, phase_packet, 32u, 600u);
    assert(badger_bytes_load_u32(evidence + 2020u) == 4u);
    assert(badger_bytes_load_u32(evidence + 2024u) == 3u);
    assert(badger_bytes_load_u32(evidence + 2032u) == 1u);
    assert(!clean_tx_confirmed(evidence));
    puts("clean-rx loader: boundary splitting, readback, invalid inputs, cancellation, deadlines and states pass");
    return 0;
}
