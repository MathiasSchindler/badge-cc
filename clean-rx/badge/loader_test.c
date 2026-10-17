#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "loader.h"
#include "../../src/shared/bytes.h"
#include "frames.h"
#include "station.h"
#include "../src/unicast.h"
#include "../src/tx_diag.h"
#include "../src/mac_soak.h"
#include "../src/ht20.h"

static uint32_t ticks, calls, blocks, state, base;
static int cancel, mismatch, bus_error, terminate_error, f2_post_status, ack_error;
static uint32_t f2_status;
static uint32_t f2_after_status;
static uint32_t fifo_status, terminations;
static uint8_t f2_packet[CLEAN_RX_PACKET_BYTES];
static uint8_t f2_next[CLEAN_F2_PACKET_BYTES];
static uint32_t f2_next_size, f2_reads;
static uint8_t f2_last[CLEAN_F2_PACKET_BYTES];
static uint32_t f2_last_size;
static int f2_second_error, f2_cancel_next;
static int f2_check_credit;
static uint32_t f2_expected_credit;
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
        if (f2_check_credit)
            assert(badger_bytes_load_u32(ram + CLEAN_MBOX + CLEAN_SDIO_ACK_OFFSET) ==
                   f2_expected_credit);
        if (f2_reads++ == 1u && f2_second_error) return WIFI_BUS_TRANSFER_TIMEOUT;
        assert(tx_length == 4u && rx_length == ((count + 3u) & ~3u));
        assert(cmd == (0x60000000u | count));
        assert(rx_length <= sizeof(f2_packet));
        memcpy(rx, f2_packet, rx_length);
        if (f2_next_size != 0u) {
            memcpy(f2_packet, f2_next, sizeof(f2_next));
            f2_status = 0x100u | (f2_next_size << 9u);
            memcpy(f2_next, f2_last, sizeof(f2_next));
            f2_next_size = f2_last_size;
            f2_last_size = 0u;
        } else if (f2_post_status) f2_status = f2_after_status;
        if (f2_cancel_next) cancel = 1;
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

static uint32_t fragment(uint8_t *packet, uint32_t mpdu, uint32_t offset) {
    uint32_t count = mpdu - offset;
    if (count > CLEAN_F2_FRAGMENT_BYTES) count = CLEAN_F2_FRAGMENT_BYTES;
    uint32_t length = CLEAN_F2_FRAGMENT_HEADER + count;
    memset(packet, 0, (length + 3u) & ~3u);
    badger_bytes_store_u32(packet, length | ((length ^ 0xffffu) << 16u));
    packet[4] = (uint8_t)mpdu;
    packet[5] = 2u;
    packet[7] = 14u;
    packet[14] = 0x21u;
    packet[18] = (uint8_t)mpdu;
    packet[19] = (uint8_t)(mpdu >> 8u);
    packet[20] = (uint8_t)offset;
    packet[21] = (uint8_t)(offset >> 8u);
    for (uint32_t i = 0u; i < count; ++i)
        packet[CLEAN_F2_FRAGMENT_HEADER + i] = (uint8_t)((offset + i) * 37u);
    return length;
}

static void fragments(const wifi_wire_t *wire) {
    clean_loader_t l;
    uint8_t packet[CLEAN_RX_PACKET_BYTES];
    uint32_t length;
    f2_post_status = 1;
    f2_after_status = 0u;
    f2_check_credit = 1;
    f2_expected_credit = 1234u;
    for (uint32_t mpdu = CLEAN_F2_PACKET_BYTES - 17u; mpdu <= CLEAN_MPDU_BYTES; ++mpdu) {
        for (uint32_t rounded = 0u; rounded < 2u; ++rounded) {
            clean_loader_init(&l, wire, service, NULL);
            l.rx_credit = 1u;
            l.rx_consumed = (mpdu & 1u) ? UINT32_MAX : 0u;
            badger_bytes_store_u32(ram + CLEAN_MBOX + CLEAN_SDIO_ACK_OFFSET, 1234u);
            uint32_t first = fragment(f2_packet, mpdu, 0u);
            f2_next_size = fragment(f2_next, mpdu, CLEAN_F2_FRAGMENT_BYTES);
            if (mpdu > 2u * CLEAN_F2_FRAGMENT_BYTES)
                f2_last_size = fragment(f2_last, mpdu, 2u * CLEAN_F2_FRAGMENT_BYTES);
            uint32_t rate = mpdu % CLEAN_CCK_RATES;
            uint8_t meta = clean_rx_phy_metadata(rounded && rate ? 0x80u : 0u, 0u, 1u);
            f2_packet[15] = f2_next[15] = clean_cck_rates[rate].signal;
            f2_packet[16] = f2_next[16] = meta;
            if (f2_last_size) {
                f2_last[15] = f2_next[15];
                f2_last[16] = meta;
            }
            if (rounded) {
                first = (first + 3u) & ~3u;
                f2_next_size = (f2_next_size + 3u) & ~3u;
                if (f2_last_size) f2_last_size = (f2_last_size + 3u) & ~3u;
            }
            f2_status = 0x100u | (first << 9u);
            f2_reads = 0u;
            assert(clean_receive(&l, packet, &length) && length == mpdu + 18u);
            assert(f2_reads == (mpdu + CLEAN_F2_FRAGMENT_BYTES - 1u) /
                               CLEAN_F2_FRAGMENT_BYTES && packet[14] == 0x20u);
            assert(packet[15] == clean_cck_rates[rate].signal && packet[16] == meta);
            assert(badger_bytes_load_u32(packet) ==
                   (length | ((length ^ 0xffffu) << 16u)));
            for (uint32_t i = 0u; i < mpdu; ++i)
                assert(packet[18u + i] == (uint8_t)(i * 37u));
#ifdef CLEAN_RX_STRICT_TEST
            assert(badger_bytes_load_u32(ram + CLEAN_MBOX + CLEAN_SDIO_ACK_OFFSET) == 1234u);
#else
            uint32_t consumed = (mpdu & 1u) ? 0u : 1u;
            assert(l.rx_consumed == consumed);
            assert(badger_bytes_load_u32(ram + CLEAN_MBOX + CLEAN_SDIO_ACK_OFFSET) == consumed);
#endif
        }
    }
    for (uint32_t fault = 0u; fault < (CLEAN_F2_MAX_FRAGMENTS > 2u ? 27u : 19u); ++fault) {
        f2_expected_credit = 0u;
        f2_after_status = 0u;
        clean_loader_init(&l, wire, service, NULL);
        l.rx_credit = 1u;
        badger_bytes_store_u32(ram + CLEAN_MBOX + CLEAN_SDIO_ACK_OFFSET, 0u);
        uint32_t first = fragment(f2_packet, CLEAN_MPDU_BYTES, 0u);
        f2_next_size = fragment(f2_next, CLEAN_MPDU_BYTES, CLEAN_F2_FRAGMENT_BYTES);
        if (CLEAN_F2_MAX_FRAGMENTS > 2u)
            f2_last_size = fragment(f2_last, CLEAN_MPDU_BYTES, 2u * CLEAN_F2_FRAGMENT_BYTES);
        f2_status = 0x100u | (first << 9u);
        f2_reads = 0u;
        if (fault == 0u) f2_packet[2] ^= 1u;
        if (fault == 1u) f2_packet[20] = 1u;
        if (fault == 2u) f2_next[20] ^= 1u;
        if (fault == 3u) f2_next[4] ^= 1u;
        if (fault == 4u) f2_next[18] ^= 1u;
        if (fault == 5u) f2_next[14] = 0x20u;
        if (fault == 6u) f2_second_error = 1;
        if (fault == 7u) f2_next_size = 0u;
        if (fault == 8u) f2_cancel_next = 1;
        if (fault == 9u) { f2_packet[18] = 0xffu; f2_packet[19] = 0xffu; }
        if (fault == 10u) f2_next[2] ^= 1u;
        if (fault == 11u) f2_next[15] ^= 1u;
        if (fault == 12u) {
            uint32_t truncated = CLEAN_F2_FRAGMENT_HEADER - 1u;
            badger_bytes_store_u32(f2_packet,
                                   truncated | ((truncated ^ 0xffffu) << 16u));
        }
        if (fault == 13u || fault == 15u) f2_after_status = 2u;
        if (fault == 14u) f2_after_status = 0xffffffffu;
        if (fault == 15u) l.rx_credit = 0u;
        if (fault == 16u)
            f2_next_size = fragment(f2_next, CLEAN_MPDU_BYTES, 0u);
        if (fault == 17u) f2_next[16] ^= CLEAN_RX_SHORT_PREAMBLE;
        if (fault == 18u) f2_next[17] ^= CLEAN_HT_DATA_VERSION;
        if (fault == 19u) f2_last[20] ^= 1u;
        if (fault == 20u) f2_last[4] ^= 1u;
        if (fault == 21u) f2_last[15] ^= 1u;
        if (fault == 22u) f2_last[16] ^= CLEAN_RX_SHORT_PREAMBLE;
        if (fault == 23u) f2_last[17] ^= CLEAN_HT_DATA_VERSION;
        if (fault == 24u) f2_last[2] ^= 1u;
        if (fault == 25u) f2_last[18] ^= 1u;
        if (fault == 26u) f2_last_size = 0u;
        assert(!clean_receive(&l, packet, &length) && length == 0u);
        assert(l.error == (fault == 6u || fault == 14u ? CLEAN_BUS :
                           fault == 7u || fault == 26u ? CLEAN_TIMEOUT :
                           fault == 8u ? CLEAN_CANCELLED : CLEAN_INPUT));
        assert(l.rx_consumed == 0u && l.rx_resyncs == 0u);
        assert(badger_bytes_load_u32(ram + CLEAN_MBOX + CLEAN_SDIO_ACK_OFFSET) == 0u);
        cancel = f2_second_error = f2_cancel_next = 0;
        f2_next_size = f2_last_size = 0u;
    }
    f2_post_status = 0;
    f2_after_status = 0u;
    f2_check_credit = 0;
    f2_status = 0u;
}

static void layout_tests(const wifi_wire_t *wire) {
    static const uint32_t words[CLEAN_RX_LAYOUT_WORDS] = CLEAN_RX_LAYOUT_VALUES;
    clean_loader_t l;
    for (uint32_t fault = 0u; fault <= CLEAN_RX_LAYOUT_WORDS; ++fault) {
        for (uint32_t i = 0u; i < CLEAN_RX_LAYOUT_WORDS; ++i)
            badger_bytes_store_u32(ram + CLEAN_RX_LAYOUT_ADDR + 4u*i,
                                  words[i] ^ (i == fault ? 1u : 0u));
        clean_loader_init(&l, wire, service, NULL);
        assert(clean_check_layout(&l) == (fault == CLEAN_RX_LAYOUT_WORDS));
        if (fault < CLEAN_RX_LAYOUT_WORDS) {
            assert(l.error == CLEAN_INPUT && l.address == CLEAN_RX_LAYOUT_ADDR + 4u*fault);
            assert(l.actual == (words[fault] ^ 1u));
        }
    }
    clean_loader_init(&l, wire, service, NULL);
    bus_error = 1;
    assert(!clean_check_layout(&l) && l.error == CLEAN_BUS);
    bus_error = 0;
}

static void eapol_tests(uint8_t evidence[2048]);

static void ht_transport(const wifi_wire_t *wire) {
    static const uint16_t sizes[] = {28u,88u,2026u,2027u,2332u,
#if CLEAN_RX_HT_RX
        CLEAN_AMSDU_MPDU_BYTES
#endif
    };
    uint8_t expected[CLEAN_HT_DATA_STORAGE], packet[CLEAN_RX_PACKET_BYTES];
    uint8_t nonce[14] = {1u,2u,3u,4u,5u,6u};
    f2_post_status = 1;
    f2_after_status = 0u;
    for (uint32_t mcs = 0u; mcs < 8u; ++mcs)
        for (uint32_t i = 0u; i < sizeof(sizes)/sizeof(sizes[0]); ++i) {
            uint32_t mpdu = sizes[i], length = 0u;
            nonce[11] = (uint8_t)mpdu; nonce[12] = (uint8_t)(mpdu >> 8u);
            nonce[13] = (uint8_t)mcs;
            assert(clean_ht_data_frame(expected, nonce));
            clean_loader_t l;
            clean_loader_init(&l, wire, service, NULL);
            uint32_t physical = mpdu + 18u;
            if (physical > CLEAN_F2_PACKET_BYTES) {
                physical = fragment(f2_packet, mpdu, 0u);
                f2_next_size = fragment(f2_next, mpdu, CLEAN_F2_FRAGMENT_BYTES);
                memcpy(f2_packet + 22u, expected + 14u, CLEAN_F2_FRAGMENT_BYTES);
                uint32_t remaining = mpdu - CLEAN_F2_FRAGMENT_BYTES;
                memcpy(f2_next + 22u, expected + 14u + CLEAN_F2_FRAGMENT_BYTES,
                       remaining < CLEAN_F2_FRAGMENT_BYTES ? remaining : CLEAN_F2_FRAGMENT_BYTES);
                f2_next[15] = (uint8_t)mcs;
                f2_next[16] = CLEAN_RX_HT20 | CLEAN_RX_UNSUPPORTED_PHY | 1u;
                f2_next[17] = CLEAN_HT_DATA_VERSION;
                if (mpdu > 2u * CLEAN_F2_FRAGMENT_BYTES) {
                    f2_last_size = fragment(f2_last, mpdu, 2u * CLEAN_F2_FRAGMENT_BYTES);
                    memcpy(f2_last + 22u, expected + 14u + 2u * CLEAN_F2_FRAGMENT_BYTES,
                           mpdu - 2u * CLEAN_F2_FRAGMENT_BYTES);
                    memcpy(f2_last + 4u, f2_next + 4u, 14u);
                }
            } else {
                memset(f2_packet, 0, (physical + 3u) & ~3u);
                badger_bytes_store_u32(f2_packet, physical | ((physical ^ 0xffffu) << 16u));
                f2_packet[5] = 2u; f2_packet[7] = 14u; f2_packet[14] = 0x20u;
                memcpy(f2_packet + 18u, expected + 14u, mpdu);
            }
            f2_packet[15] = (uint8_t)mcs;
            f2_packet[16] = CLEAN_RX_HT20 | CLEAN_RX_UNSUPPORTED_PHY | 1u;
            f2_packet[17] = CLEAN_HT_DATA_VERSION;
            f2_status = 0x100u | (physical << 9u);
            f2_reads = 0u;
            assert(clean_receive(&l, packet, &length) && length == mpdu + 18u);
            assert(clean_ht_rx_metadata(packet[15], packet[16], packet[17], mpdu, 1u, mcs));
            assert(clean_ht_data_match(packet + 18u, mpdu, nonce));
            assert(!memcmp(packet + 18u, expected + 14u, mpdu));
        }
    f2_post_status = 0; f2_status = f2_next_size = 0u;
    printf("HT transport: all MCS and up to %u fragments preserve version, PHY evidence, full payload and FCS; NOT RF\n",
           CLEAN_F2_MAX_FRAGMENTS);
}

static void cck_rates(void) {
    clean_tx_tail_t tail = {0};
    assert(!clean_tx_tail_poll(&tail, 0) && !tail.ended);
    assert(!clean_tx_tail_poll(&tail, 1) && tail.active && !tail.ended);
    assert(!clean_tx_tail_poll(&tail, 1));
    assert(clean_tx_tail_poll(&tail, 0) && tail.ended);
    assert(!clean_tx_tail_poll(&tail, 0) && !clean_tx_tail_poll(&tail, 1));
    static const uint8_t golden[4][6] = {
        {0x0au,0x04u,0x70u,0x00u,0xd5u,0xb3u},
        {0x14u,0x04u,0x38u,0x00u,0x90u,0x56u},
        {0x37u,0x04u,0x15u,0x00u,0x45u,0x6fu},
        {0x6eu,0x84u,0x0bu,0x00u,0x5du,0x40u}
    };
    uint8_t plcp[6], packet[CLEAN_RX_PACKET_BYTES];
    for (uint32_t rate = 0u; rate < CLEAN_CCK_RATES; ++rate) {
        assert(clean_cck_plcp_rate(plcp, 14u, rate) && !memcmp(plcp, golden[rate], 6u));
        assert(clean_cck_rate_index(plcp[0]) == (int)rate);
        assert(clean_cck_preamble_valid(rate, 0u));
        assert(clean_cck_preamble_valid(rate, 1u) == (rate != 0u));
        assert(!clean_cck_preamble_valid(rate, 2u));
        for (uint32_t length = 14u; length <= CLEAN_MPDU_BYTES; ++length) {
            assert(clean_cck_plcp_rate(plcp, length, rate));
            assert(clean_cck_plcp_valid(plcp, length));
            uint32_t duration = clean_cck_duration_us(length, rate);
            uint32_t half_mbps = clean_cck_rates[rate].half_mbps;
            assert(duration * half_mbps >= length * 16u);
            assert((duration - 1u) * half_mbps < length * 16u);
            assert(clean_cck_airtime_us(length, rate, 0u) == duration + 192u);
            assert(clean_cck_airtime_us(length, rate, 1u) ==
                   (rate ? duration + 96u : 0u));
            assert(plcp[1] == (4u | (rate == 3u && duration * 11u - length * 8u >= 8u ? 128u : 0u)));
            assert(!clean_cck_plcp_valid(plcp, length + 1u));
            for (uint32_t bit = 0u; bit < 48u; ++bit) {
                plcp[bit / 8u] ^= (uint8_t)(1u << (bit & 7u));
                assert(!clean_cck_plcp_valid(plcp, length));
                plcp[bit / 8u] ^= (uint8_t)(1u << (bit & 7u));
            }
        }
        assert(!clean_cck_plcp_rate(plcp, 13u, rate));
        assert(!clean_cck_plcp_rate(plcp, CLEAN_MPDU_BYTES + 1u, rate));
        assert(!clean_cck_duration_us(UINT32_MAX, rate));
    }
    assert(!clean_cck_plcp_rate(plcp, 14u, CLEAN_CCK_RATES));
    assert(!clean_cck_airtime_us(14u, CLEAN_CCK_RATES, 0u));
    assert(clean_cck_rate_index(0u) == -1 && clean_cck_rate_index(111u) == -1);
    assert(clean_cck_airtime_us(2332u, 0u, 0u) == 18848u);
    assert(clean_cck_airtime_us(2332u, 1u, 0u) == 9520u);
    assert(clean_cck_airtime_us(2332u, 2u, 0u) == 3584u);
    assert(clean_cck_airtime_us(2332u, 3u, 0u) == 1888u);
    assert(clean_rx_phy_metadata(0u, 0u, 1u) == 1u);
    assert(clean_rx_phy_metadata(0x80u, 0u, 1u) == (1u | CLEAN_RX_SHORT_PREAMBLE));
    assert(clean_rx_phy_metadata(1u, 0x440u, 1u) == (1u | CLEAN_RX_OFDM));
    assert(clean_rx_phy_metadata(0u, 0x2000u, 1u) == (1u | CLEAN_RX_OFDM));
    assert(clean_rx_phy_metadata(2u, 0u, 1u) == (1u | CLEAN_RX_UNSUPPORTED_PHY));
    assert(clean_rx_phy_metadata(3u, 0u, 1u) == (1u | CLEAN_RX_UNSUPPORTED_PHY));
    static const uint8_t long_vectors[2][6] = {
        {0x0au,0x04u,0xe0u,0x32u,0x19u,0xb8u},
        {0x0au,0x04u,0xe0u,0x48u,0xc4u,0x64u}
    };
    assert(clean_cck_plcp_rate(plcp, 1628u, 0u) && !memcmp(plcp, long_vectors[0], 6u));
    assert(clean_cck_plcp_rate(plcp, 2332u, 0u) && !memcmp(plcp, long_vectors[1], 6u));
    uint8_t invalid_nonce[14] = {0};
    invalid_nonce[13] = CLEAN_SOAK_PHYS * CLEAN_SOAK_PAYLOADS;
    assert(!clean_soak_frame(packet, invalid_nonce, 1u, 0u));
    assert(!clean_soak_payload_size(invalid_nonce, 0u));
    for (uint32_t phy = 0u; phy < CLEAN_SOAK_PHYS * CLEAN_SOAK_PAYLOADS; ++phy) {
        uint8_t nonce[14] = {1u,2u,3u,4u};
        nonce[13] = (uint8_t)phy;
        uint32_t rate = clean_soak_rate(nonce);
        uint8_t flags = rate == 4u ? CLEAN_RX_OFDM :
            clean_soak_short(nonce) ? CLEAN_RX_SHORT_PREAMBLE : 0u;
        for (uint32_t fault = 0u; fault < 5u; ++fault) {
            clean_soak_status_t s = {.target = 10u};
            for (uint32_t index = 0u; index < 10u; ++index) {
                uint32_t length = clean_soak_frame(packet + 12u, nonce, 1u, index) + 12u;
                packet[15] = rate == 4u ? 0x8bu : clean_cck_rates[rate].signal;
                packet[16] = 1u | flags;
                if (fault == 1u) packet[15] ^= 1u;
                if (fault == 2u) packet[16] ^= CLEAN_RX_SHORT_PREAMBLE;
                if (fault == 3u) packet[16] ^= CLEAN_RX_OFDM;
                if (fault == 4u) packet[16] |= CLEAN_RX_UNSUPPORTED_PHY;
                int result = clean_soak_receive_packet(&s, packet, length, nonce, 2u);
                assert(result == (fault ? -1 : 1));
                if (fault) {
                    assert(s.failure == 6u && s.rx_phy_mismatch == 1u && !s.rx_phy_checked);
                    break;
                }
            }
            if (!fault) assert(s.received == 10u && s.rx_phy_checked == 10u && !s.failure);
        }
    }
    puts("CCK: four rates, all lengths/CRC bits, seven legal preambles, RX PHY evidence and mismatch rejection pass");
}

static void ht_probe_tests(void) {
    for (uint32_t word = 0u; word < 3u; ++word)
        for (uint32_t bit = 0u; bit < 32u; ++bit) {
            uint32_t input = 1u << bit;
            uint32_t expected = word == 1u && bit >= 2u && bit < 10u ? 0u : input;
            assert(clean_ht_probe_template_word(input, word, 1u, 1u) == expected);
        }
    static const uint8_t golden[5][6] = {
        {0u,0x58u,0u,7u,0x24u,0u}, {7u,0x58u,0u,7u,0x80u,3u},
        {0u,0x1cu,9u,7u,0x88u,3u}, {7u,0x1cu,9u,7u,0x2cu,0u},
        {0u,14u,0u,7u,0xe4u,3u}
    };
    static const uint16_t lengths[5] = {88u,88u,2332u,2332u,14u};
    static const uint16_t airtimes[5] = {154u,54u,2918u,330u,66u};
    static const uint16_t lsig_lengths[5] = {93u,18u,2166u,225u,27u};
    static const uint16_t phyctl1[8] = {0x0002u,0x0802u,0x0a02u,0x1002u,
                                      0x1202u,0x1902u,0x1a02u,0x1c02u};
    uint8_t sig[6], lsig[6], frame[96];
    clean_ht_tx_t controls;
    assert(clean_ht20_tx_controls(&controls, 88u, 0u));
    assert(controls.phyctl == 0x42u && controls.phyctl1 == 2u &&
           controls.mmplcp[0] == 0xbu && controls.mmplcp[1] == 93u);
    assert(clean_ht20_tx_controls(&controls, 88u, 7u));
    assert(controls.phyctl == 0x42u && controls.phyctl1 == 0x1c02u &&
           controls.mmplcp[0] == 0xbu && controls.mmplcp[1] == 18u);
    static const uint8_t observed[6] = {1u,57u,0u,6u,0x14u,2u};
    assert(clean_ht20_rx_sig_valid(observed, 57u) && !clean_ht20_sig_valid(observed, 57u));
    for (uint32_t i = 0u; i < 5u; ++i) {
        assert(clean_ht20_sig(sig, lengths[i], golden[i][0]) && !memcmp(sig, golden[i], 6u));
        assert(clean_ht20_airtime_us(lengths[i], golden[i][0]) == airtimes[i]);
        assert(clean_ht20_lsig_length(lengths[i], golden[i][0]) == lsig_lengths[i]);
    }
    for (uint32_t mcs = 0u; mcs < 8u; ++mcs) {
        for (uint32_t length = 14u; length <= CLEAN_MPDU_BYTES; ++length) {
            assert(clean_ht20_sig(sig, length, mcs) && clean_ht20_sig_valid(sig, length));
            assert(clean_ht20_rx_sig_valid(sig, length));
            uint32_t symbols = clean_ht20_symbols(length, mcs);
            uint32_t n = clean_ht20_rates[mcs].ndbps;
            assert(symbols * n >= 22u + 8u*length && (symbols - 1u)*n < 22u + 8u*length);
            assert(clean_ht20_lsig(lsig, length, mcs));
            assert(clean_ht20_tx_controls(&controls, length, mcs));
            assert(controls.phyctl == 0x42u && controls.phyctl1 == phyctl1[mcs]);
            assert(controls.mmplcp[0] == 0xbu);
            assert(controls.mmplcp[1] == 3u * symbols + 9u);
            uint32_t signal = lsig[0] | ((uint32_t)lsig[1] << 8u) | ((uint32_t)lsig[2] << 16u);
            assert((signal & 31u) == 0xbu && (signal >> 18u) == 0u);
            assert(((signal >> 5u) & 4095u) == 3u * symbols + 9u);
            uint32_t ones = 0u;
            for (uint32_t bits = signal; bits; bits >>= 1u) ones += bits & 1u;
            assert(!(ones & 1u) && !lsig[3] && !lsig[4] && !lsig[5]);
            for (uint32_t bit = 0u; bit < 48u; ++bit) {
                sig[bit/8u] ^= (uint8_t)(1u << (bit & 7u));
                assert(!clean_ht20_sig_valid(sig, length));
                assert(!clean_ht20_rx_sig_valid(sig, length));
                sig[bit/8u] ^= (uint8_t)(1u << (bit & 7u));
            }
        }
        assert(!clean_ht20_symbols(13u, mcs) && !clean_ht20_symbols(UINT32_MAX, mcs));
        assert(!clean_ht20_sig(sig, CLEAN_MPDU_BYTES + 1u, mcs));
        uint8_t nonce[14] = {1u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,12u,13u};
        nonce[13] = (uint8_t)mcs;
        assert(clean_ht_probe_frame(frame, nonce));
        assert(clean_frame_valid(frame + 6u, CLEAN_HT_PROBE_MPDU));
        assert(clean_ht_probe_match(frame + 6u, CLEAN_HT_PROBE_MPDU, nonce));
        clean_ht_rx_t rx = {.magic=CLEAN_HT_RX_MAGIC,.captures=1u,
                            .rxe_length=CLEAN_HT_PROBE_BYTES,.phy={2u,0u,0u,0u}};
        memcpy(rx.sig, frame, 6u);
        memcpy(rx.mpdu, frame + 6u, CLEAN_HT_PROBE_MPDU);
        assert(clean_ht_probe_rx_valid(&rx, nonce));
        for (uint32_t format = 0u; format < 4u; ++format) {
            rx.phy[0] = (uint16_t)(0x100u | format);
            assert(clean_ht_probe_rx_valid(&rx, nonce) == (format == 2u));
        }
        rx.phy[0] = 2u; rx.captures = 0u;
        assert(!clean_ht_probe_rx_valid(&rx, nonce));
        rx.captures = 1u; --rx.rxe_length;
        assert(!clean_ht_probe_rx_valid(&rx, nonce));
        ++rx.rxe_length; rx.magic ^= 1u;
        assert(!clean_ht_probe_rx_valid(&rx, nonce));
        rx.magic ^= 1u;
        for (uint32_t bit = 0u; bit < CLEAN_HT_PROBE_BYTES * 8u; ++bit) {
            uint8_t *part = bit < 48u ? rx.sig : rx.mpdu;
            uint32_t offset = bit < 48u ? bit : bit - 48u;
            part[offset / 8u] ^= (uint8_t)(1u << (offset & 7u));
            assert(!clean_ht_probe_rx_valid(&rx, nonce));
            part[offset / 8u] ^= (uint8_t)(1u << (offset & 7u));
        }
        rx.sig[3] &= 254u; rx.sig[4] &= 3u; rx.sig[5] = 0u;
        uint8_t rx_crc = clean_ht20_crc(rx.sig);
        for (uint32_t bit = 0u; bit < 8u; ++bit)
            rx.sig[(34u+bit)/8u] |= (uint8_t)(((rx_crc >> (7u-bit)) & 1u) << ((34u+bit)&7u));
        assert(clean_ht_probe_rx_valid(&rx, nonce));
        assert(!clean_ht20_sig_valid(rx.sig, CLEAN_HT_PROBE_MPDU));
        uint8_t masked[96];
        for (uint32_t word = 0u; word < 24u; ++word) {
            uint32_t w = (uint32_t)frame[4u*word] | ((uint32_t)frame[4u*word+1u] << 8u) |
                         ((uint32_t)frame[4u*word+2u] << 16u) | ((uint32_t)frame[4u*word+3u] << 24u);
            assert(clean_ht_probe_template_word(w, word, 0u, 1u) == w);
            assert(clean_ht_probe_template_word(w, word, 1u, 0u) == w);
            uint32_t actual = clean_ht_probe_template_word(w, word, 1u, 1u);
            assert(actual == (word == 1u ? w & 0xfffffc03u : w));
            for (uint32_t byte = 0u; byte < 4u; ++byte)
                masked[4u*word+byte] = (uint8_t)(actual >> (8u*byte));
        }
        assert(!clean_ht20_sig_valid(masked, CLEAN_HT_PROBE_MPDU));
        assert(!memcmp(masked + 6u, frame + 6u, 90u));
        uint32_t cmd = CLEAN_HT_PROBE_CMD | clean_tx_length_bits(CLEAN_HT_PROBE_BYTES) | (1u << 19u);
        assert(clean_ht_probe_command_valid(cmd, 1u, 0u, frame));
        assert(!clean_ht_probe_command_valid(cmd, 1u, 0u, masked));
        assert(!clean_ht_probe_command_valid(cmd, 0u, 0u, NULL));
        assert(!clean_ht_probe_command_valid(cmd, 1u, 1u, NULL));
        for (uint32_t bit = 0u; bit < 32u; ++bit)
            assert(!clean_ht_probe_command_valid(cmd ^ (1u << bit), 1u, 0u, frame));
        for (uint32_t bit = 0u; bit < CLEAN_HT_PROBE_BYTES * 8u; ++bit) {
            frame[bit/8u] ^= (uint8_t)(1u << (bit & 7u));
            assert(!clean_ht_probe_command_valid(cmd, 1u, 0u, frame));
            frame[bit/8u] ^= (uint8_t)(1u << (bit & 7u));
        }
        nonce[0] ^= 1u;
        assert(!clean_ht_probe_match(frame + 6u, CLEAN_HT_PROBE_MPDU, nonce));
    }
    assert(!clean_ht20_sig(sig, 88u, 8u) && !clean_ht20_airtime_us(88u, UINT32_MAX));
    assert(!clean_ht20_lsig_length(13u, 0u));
    memset(&controls, 0xa5, sizeof(controls));
    const uint8_t *unchanged = (const uint8_t *)&controls;
    assert(!clean_ht20_tx_controls(&controls, 13u, 0u));
    assert(!clean_ht20_tx_controls(&controls, CLEAN_MPDU_BYTES + 1u, 0u));
    assert(!clean_ht20_tx_controls(&controls, 88u, 8u));
    assert(!clean_ht20_tx_controls(&controls, UINT32_MAX, UINT32_MAX));
    for (uint32_t i = 0u; i < sizeof(controls); ++i) assert(unchanged[i] == 0xa5u);
    puts("HT probe: CRC/timing/control vectors, all lengths/MCS, trace matching, CRC-only template experiment and gates pass; NOT RF");
}

static void ht_boundaries(void) {
    for (uint32_t status = 0u; status <= UINT16_MAX; ++status) {
        if ((status & 3u) < 2u) continue;
        static const uint8_t channels[] = {1u,6u,14u};
        for (uint32_t i = 0u; i < sizeof(channels); ++i)
            for (uint32_t legacy = 0u; legacy < 2u; ++legacy)
                assert(clean_rx_phy_metadata((uint16_t)status,
                    legacy ? 0x2000u : 0u, channels[i]) ==
                    (channels[i] | CLEAN_RX_UNSUPPORTED_PHY));
    }
    static const uint8_t ht_cap[28] = {45u,26u,0x62u,0u,0x1bu,0xffu};
    static const uint8_t ht_operation[24] = {61u,22u,1u};
    static const uint8_t wmm[26] = {
        221u,24u,0u,0x50u,0xf2u,2u,1u,1u,0u,0u,
        0x03u,0xa4u,0u,0u,0x27u,0xa4u,0u,0u,
        0x42u,0x43u,0x5eu,0u,0x62u,0x32u,0x2fu,0u
    };
    static const uint8_t own[6] = {2u,1u,2u,3u,4u,5u};
    for (uint32_t secured = 0u; secured < 2u; ++secured) {
        clean_station_ap_t ap = {0}, parsed;
        ap.bssid[0] = 2u; ap.bssid[5] = 6u;
        ap.ssid_length = sizeof(ap.ssid);
        memset(ap.ssid, 'n', sizeof(ap.ssid));
        ap.rates = 0xfffu; ap.basic_rates = 0x15fu;
        ap.capability = (uint16_t)(0xffedu | (secured ? 16u : 0u));
        ap.secured = (uint8_t)secured;
        uint8_t frame[160], fallback[160], beacon[256] = {0};
        uint32_t length = clean_station_associate(frame, 0x123u, own, &ap);
        assert(clean_frame_valid(frame + 6u, length - 6u));
        assert(clean_station_u16(frame + 30u) == (0x421u | (secured ? 16u : 0u)));
        uint32_t seen = 0u;
        for (uint32_t at = 34u; at < length - 4u;) {
            assert(length - 4u - at >= 2u);
            uint32_t id = frame[at], size = frame[at + 1u];
            at += 2u;
            assert(size <= length - 4u - at);
            assert(id == 0u || id == 1u || id == 50u || (secured && id == 48u));
            uint32_t bit = id == 0u ? 1u : id == 1u ? 2u : id == 50u ? 4u : 8u;
            assert(!(seen & bit));
            seen |= bit;
            at += size;
        }
        assert(seen == (secured ? 15u : 7u));
        beacon[0] = 0x80u;
        memset(beacon + 4u, 255, 6u);
        memcpy(beacon + 10u, ap.bssid, 6u);
        memcpy(beacon + 16u, ap.bssid, 6u);
        beacon[34] = (uint8_t)ap.capability;
        beacon[35] = (uint8_t)(ap.capability >> 8u);
        uint32_t ies = length - 38u, at = 36u + ies;
        memcpy(beacon + 36u, frame + 34u, ies);
        memcpy(beacon + at, ht_cap, sizeof(ht_cap)); at += sizeof(ht_cap);
        memcpy(beacon + at, ht_operation, sizeof(ht_operation)); at += sizeof(ht_operation);
        memcpy(beacon + at, wmm, sizeof(wmm)); at += sizeof(wmm);
        assert(at + 4u <= sizeof(beacon));
        badger_bytes_store_u32(beacon + at, diag_crc32(beacon, at));
        assert(clean_station_beacon(beacon, at + 4u, ap.bssid, &parsed) ==
               (secured ? CLEAN_AP_PSK_CCMP : CLEAN_AP_OPEN));
        assert(parsed.ht.capabilities && parsed.ht.operation && parsed.ht.wmm.present);
        assert(clean_ht_peer_fallback(&parsed.ht, 1u) == CLEAN_HT_FALLBACK_MAC);
        assert(clean_station_associate(fallback, 0x123u, own, &parsed) == length);
        assert(!memcmp(frame, fallback, sizeof(frame)));
        beacon[at - sizeof(wmm) + 8u] ^= 1u;
        badger_bytes_store_u32(beacon + at, diag_crc32(beacon, at));
        assert(clean_station_refresh(&parsed, beacon, at + 4u) == 0);
        assert(parsed.ht.wmm.parameter_set == 1u);
        assert(clean_station_associate(fallback, 0x123u, own, &parsed) == length);
        assert(!memcmp(frame, fallback, sizeof(frame)));
        memcpy(beacon + at, ht_cap, sizeof(ht_cap));
        at += sizeof(ht_cap);
        assert(at + 4u <= sizeof(beacon));
        badger_bytes_store_u32(beacon + at, diag_crc32(beacon, at));
        assert(clean_station_beacon(beacon, at + 4u, ap.bssid, &parsed) ==
               (secured ? CLEAN_AP_PSK_CCMP : CLEAN_AP_OPEN));
        assert(parsed.ht.invalid);
        assert(clean_ht_peer_fallback(&parsed.ht, 1u) & CLEAN_HT_FALLBACK_MALFORMED);
        assert(clean_station_associate(fallback, 0x123u, own, &parsed) == length);
        assert(!memcmp(frame, fallback, sizeof(frame)));
    }
    puts("HT boundary: nonlegacy RX stays unsupported; HT/WMM peers do not enable advertisements; NOT HT RF");
}

static void station_tests(void) {
    const uint8_t mac[6] = {2u,1u,2u,3u,4u,5u};
    const uint8_t peer[6] = {2u,6u,7u,8u,9u,10u};
    for (uint32_t rate = 0u; rate < 12u; ++rate) {
        assert(clean_ack_rate(0xfffu, rate) == (int)rate);
        int fallback = clean_ack_rate(1u, rate);
        assert(fallback >= 0 && fallback <= (int)rate);
        if (rate >= 4u) assert(fallback == 4 || fallback == 6 || fallback == 8);
        uint8_t plcp[6], expected[6];
        uint16_t phy;
        assert(clean_ack_template(plcp, rate, &phy));
        if (rate >= 4u) assert(clean_ofdm_plcp_valid(plcp, 14u));
        else {
            static const uint8_t signal[4] = {10u,20u,55u,110u};
            static const uint16_t duration[4] = {112u,56u,21u,11u};
            clean_cck_plcp_service(expected, signal[rate], duration[rate],
                                   (uint8_t)(rate == 3u ? 0x84u : 4u));
            assert(!memcmp(plcp, expected, 6u));
        }
    }
    assert(clean_ack_rate(0u, 0u) == -1 && clean_ack_rate(0x8000u, 0u) == -1);
    assert(clean_ack_rate(1u, 12u) == -1);
    clean_station_ap_t ap = {0}, parsed;
    memcpy(ap.bssid, peer, 6u);
    memcpy(ap.ssid, "test", 4u);
    ap.ssid_length = 4u; ap.secured = 1u;
    ap.rates = 0xfffu; ap.basic_rates = 0x15fu; ap.capability = 0x431u;
    uint8_t frame[160], beacon[200] = {0};
    clean_station_request_t request = {.sequence = 4095u};
    uint8_t original[160];
    uint32_t length = clean_station_request_prepare(&request, frame, mac, &ap, 0u);
    memcpy(original, frame, sizeof(original));
    assert(clean_station_u16(frame + 28u) == 0xfff0u && !(frame[7] & 8u));
    clean_station_request_sent(&request, 0u, 0u);
    assert(clean_station_request_prepare(&request, frame, mac, &ap, 0u) == length);
    assert(!memcmp(original, frame, sizeof(frame)) && request.sequence == 4096u);
    clean_station_request_sent(&request, 1u, 0u);
    assert(clean_station_request_prepare(&request, frame, mac, &ap, 0u) == length);
    assert(frame[7] & 8u);
    assert(clean_station_u16(frame + 28u) == 0xfff0u &&
           clean_frame_valid(frame + 6u, length - 6u));
    assert(!memcmp(frame + 8u, original + 8u, length - 12u));
    clean_station_request_sent(&request, 0u, 0u);
    assert(request.pending && request.submitted && request.sequence == 4096u);
    clean_station_request_sent(&request, 1u, 1u);
    assert(!request.pending);
    assert(clean_station_request_prepare(&request, frame, mac, &ap, 0u) == length);
    assert(!clean_station_u16(frame + 28u) && !(frame[7] & 8u));
    assert(request.sequence == 4097u && clean_frame_valid(frame + 6u, length - 6u));
    clean_station_request_sent(&request, 1u, 0u);
    frame[10] ^= 1u;
    assert(!clean_station_request_prepare(&request, frame, mac, &ap, 0u));
    /* A valid response may arrive without its preceding request ACK. */
    request.pending = 0u;
    length = clean_station_request_prepare(&request, frame, mac, &ap, 1u);
    assert(frame[6] == 0u && clean_station_u16(frame + 28u) == 16u &&
           !(frame[7] & 8u) && clean_frame_valid(frame + 6u, length - 6u));
    uint32_t auth_len = clean_station_auth(frame, 0x123u, mac, &ap);
    assert(auth_len == 40u && clean_frame_valid(frame + 6u, 34u));
    assert(clean_ofdm_plcp_valid(frame, 34u));
    assert(frame[28] == 0x30u && frame[29] == 0x12u && frame[32] == 1u);
    uint32_t total = clean_station_associate(frame, 0x124u, mac, &ap);
    assert(total <= sizeof(frame) && clean_frame_valid(frame + 6u, total - 6u));
    assert(clean_ofdm_plcp_valid(frame, total - 6u));
    assert(clean_station_u16(frame + 30u) == 0x431u && frame[32] == 10u);
    uint32_t ies = total - 10u - 28u;
    beacon[0] = 0x80u;
    memset(beacon + 4u, 255, 6u);
    memcpy(beacon + 10u, peer, 6u); memcpy(beacon + 16u, peer, 6u);
    beacon[34] = 0x31u; beacon[35] = 4u;
    memcpy(beacon + 36u, frame + 34u, ies);
    uint32_t n = 40u + ies;
    badger_bytes_store_u32(beacon + n - 4u, diag_crc32(beacon, n - 4u));
    assert(clean_station_beacon(beacon, n, peer, &parsed) == CLEAN_AP_PSK_CCMP);
    assert(parsed.basic_rates == ap.basic_rates && parsed.rates == ap.rates);
    assert(parsed.ssid_length == 4u && !memcmp(parsed.ssid, "test", 4u));
    uint8_t discovery[218] = {0};
    memcpy(discovery + 18u, beacon, n);
    clean_station_ap_t discovered = {0};
    int discovery_status = 0;
    clean_station_discover(discovery, n + 18u, peer, &discovered, &discovery_status);
    assert(discovery_status == CLEAN_AP_PSK_CCMP && !memcmp(&discovered, &parsed, sizeof(parsed)));
    const uint8_t non_beacons[] = {0x50u,8u,0xb0u,0x10u};
    for (uint32_t i = 0u; i < sizeof(non_beacons); ++i) {
        discovery[18] = non_beacons[i];
        badger_bytes_store_u32(discovery + 18u + n - 4u, diag_crc32(discovery + 18u, n - 4u));
        clean_station_discover(discovery, n + 18u, peer, &discovered, &discovery_status);
        assert(discovery_status == CLEAN_AP_PSK_CCMP &&
               !memcmp(&discovered, &parsed, sizeof(parsed)));
    }
    discovery[18] = 0x80u;
    clean_station_discover(discovery, 57u, peer, &discovered, &discovery_status);
    assert(discovery_status == CLEAN_AP_PSK_CCMP);
    clean_station_discover(discovery, n + 18u, peer, &discovered, &discovery_status);
    assert(discovery_status == CLEAN_AP_INVALID);
    clean_station_ap_t refreshed = parsed;
    assert(clean_station_refresh(&refreshed, beacon, n) == 0);
    beacon[n - 4u] = 42u; beacon[n - 3u] = 1u; beacon[n - 2u] = 2u;
    badger_bytes_store_u32(beacon + n - 1u, diag_crc32(beacon, n - 1u));
    assert(clean_station_beacon(beacon, n + 3u, peer, &parsed) == CLEAN_AP_PSK_CCMP);
    assert(parsed.erp == 2u);
    assert(clean_station_refresh(&refreshed, beacon, n + 3u) == 1 && refreshed.erp == 2u);
    beacon[35] ^= 4u;
    beacon[44] &= 127u;
    badger_bytes_store_u32(beacon + n - 1u, diag_crc32(beacon, n - 1u));
    assert(clean_station_refresh(&refreshed, beacon, n + 3u) == 1);
    assert(!(refreshed.capability & 1024u) && !(refreshed.basic_rates & 1u));
    beacon[44] = 12u;
    badger_bytes_store_u32(beacon + n - 1u, diag_crc32(beacon, n - 1u));
    assert(clean_station_refresh(&refreshed, beacon, n + 3u) == CLEAN_AP_UNSUPPORTED);
    beacon[44] = 130u;
    beacon[35] ^= 4u;
    beacon[n - 2u] = 0u;
    badger_bytes_store_u32(beacon + n - 1u, diag_crc32(beacon, n - 1u));
    assert(clean_station_beacon(beacon, n + 3u, peer, &parsed) == CLEAN_AP_PSK_CCMP);
    assert(clean_station_refresh(&refreshed, beacon, n + 3u) == 1 && refreshed.erp == 0u);
    beacon[n] ^= 1u;
    assert(clean_station_refresh(&refreshed, beacon, n + 3u) == 0);
    beacon[n] ^= 1u;
    badger_bytes_store_u32(beacon + n - 4u, diag_crc32(beacon, n - 4u));
    for (uint32_t truncated = 0u; truncated < n; ++truncated)
        assert(clean_station_beacon(beacon, truncated, peer, &parsed) == CLEAN_AP_INVALID);
    uint8_t rsn[24] = {1u,0u,0u,15u,172u,4u,1u,0u,0u,15u,172u,4u,
                       1u,0u,0u,15u,172u,2u,0u,0u};
    assert(clean_station_rsn(rsn, 18u) == CLEAN_AP_PSK_CCMP);
    assert(clean_station_rsn(rsn, 20u) == CLEAN_AP_PSK_CCMP);
    for (uint32_t truncated = 0u; truncated < 18u; ++truncated)
        assert(clean_station_rsn(rsn, truncated) == CLEAN_AP_INVALID);
    assert(clean_station_rsn(rsn, 19u) == CLEAN_AP_INVALID);
    rsn[18] = 64u;
    assert(clean_station_rsn(rsn, 20u) == CLEAN_AP_UNSUPPORTED);
    rsn[18] = 0u; rsn[17] = 8u;
    assert(clean_station_rsn(rsn, 20u) == CLEAN_AP_UNSUPPORTED);
    rsn[17] = 2u; rsn[20] = 1u;
    assert(clean_station_rsn(rsn, 22u) == CLEAN_AP_INVALID);
    uint8_t response[40] = {0};
    memcpy(response + 4u, mac, 6u); memcpy(response + 10u, peer, 6u);
    memcpy(response + 16u, peer, 6u);
    response[0] = 0xb0u; response[26] = 2u;
    badger_bytes_store_u32(response + 30u, diag_crc32(response, 30u));
    uint16_t aid = 0u;
    assert(clean_station_response(response, 34u, mac, peer, 0u, &aid) == 0);
    response[28] = 13u;
    badger_bytes_store_u32(response + 30u, diag_crc32(response, 30u));
    assert(clean_station_response(response, 34u, mac, peer, 0u, &aid) == 13);
    response[0] = 0x10u; response[26] = 0u; response[28] = 38u; response[29] = 0xc0u;
    response[24] = 1u; response[30] = 1u; response[31] = 2u;
    response[32] = 0x82u; response[33] = 0x84u;
    badger_bytes_store_u32(response + 34u, diag_crc32(response, 34u));
    assert(clean_station_response(response, 38u, mac, peer, 1u, &aid) == 0 && aid == 38u);
    uint8_t evidence[2048] = {0};
    evidence[2000] = 16u;
    badger_bytes_store_u32(evidence + 4u, 8u);
    badger_bytes_store_u32(evidence + 520u, 0x31545341u);
    badger_bytes_store_u32(evidence + 524u, 4u);
    badger_bytes_store_u32(evidence + 536u, 38u);
    badger_bytes_store_u32(evidence + 596u, 34u);
    badger_bytes_store_u32(evidence + 796u, 38u);
    memcpy(evidence + 1972u, mac, 6u); memcpy(evidence + 1980u, peer, 6u);
    memcpy(evidence + 800u, response, 38u);
    response[0] = 0xb0u; response[24] = 0u; response[26] = 2u;
    response[28] = response[29] = 0u;
    badger_bytes_store_u32(response + 30u, diag_crc32(response, 30u));
    memcpy(evidence + 600u, response, 34u);
    assert(clean_station_evidence(evidence));
    clean_ht_peer_report(&ap.ht, 1u, evidence);
    assert(clean_station_evidence(evidence));
    assert(badger_bytes_load_u32(evidence + CLEAN_HT_PEER_DETAIL) == CLEAN_HT_PEER_MAGIC);
    eapol_tests(evidence);
    evidence[800u + 4u] ^= 1u;
    assert(!clean_station_evidence(evidence));
    response[0] = 0x10u; response[26] = 0u; response[28] = 0xd8u; response[29] = 0xc7u;
    badger_bytes_store_u32(response + 30u, diag_crc32(response, 30u));
    assert(clean_station_response(response, 34u, mac, peer, 1u, &aid) == -1);
    response[28] = 38u; response[29] = 0u;
    response[24] = 1u; response[25] = response[26] = response[27] = 0u;
    badger_bytes_store_u32(response + 30u, diag_crc32(response, 30u));
    /* Reserved upper AID bits vary between APs; range/IE checks still apply. */
    response[30] = 1u; response[31] = 2u; response[32] = 0x82u; response[33] = 0x84u;
    for (uint32_t upper = 0u; upper < 4u; ++upper) {
        response[29] = (uint8_t)(upper << 6u);
        badger_bytes_store_u32(response + 34u, diag_crc32(response, 34u));
        assert(clean_station_response(response, 38u, mac, peer, 1u, &aid) == 0 && aid == 38u);
    }
    response[28] = response[29] = 0u;
    badger_bytes_store_u32(response + 34u, diag_crc32(response, 34u));
    assert(clean_station_response(response, 38u, mac, peer, 1u, &aid) == -1);
}

static void eapol_tests(uint8_t evidence[2048]) {
    const uint8_t mac[6] = {2u,1u,2u,3u,4u,5u}, peer[6] = {2u,6u,7u,8u,9u,10u};
    for (uint32_t qos = 0u; qos < 2u; ++qos) {
        uint8_t frame[160] = {0};
        uint32_t header = qos ? 26u : 24u, length = header + 111u;
        frame[0] = qos ? 0x88u : 8u; frame[1] = 2u;
        memcpy(frame + 4u, mac, 6u);
        memcpy(frame + 10u, peer, 6u); memcpy(frame + 16u, peer, 6u);
        const uint8_t snap[8] = {170u,170u,3u,0u,0u,0u,136u,142u};
        memcpy(frame + header, snap, sizeof(snap));
        uint8_t *e = frame + header + 8u;
        e[0] = 2u; e[1] = 3u; e[3] = 95u; e[4] = 2u; e[6] = 0x8au; e[8] = 16u;
        e[16] = 1u; e[17] = 2u;
        badger_bytes_store_u32(frame + length - 4u, diag_crc32(frame, length - 4u));
        assert(clean_station_eapol_m1(frame, length, mac, peer));
        memcpy(evidence + 1122u, frame, length);
        badger_bytes_store_u32(evidence + 1088u, length + 18u);
        badger_bytes_store_u32(evidence + 572u, CLEAN_AP_PSK_CCMP);
        badger_bytes_store_u32(evidence + 576u, 1u);
        assert(clean_station_eapol_evidence(evidence));
        badger_bytes_store_u32(evidence + 576u, 0u);
        assert(!clean_station_eapol_evidence(evidence));
        badger_bytes_store_u32(evidence + 576u, 1u);
        badger_bytes_store_u32(evidence + 572u, CLEAN_AP_OPEN);
        assert(!clean_station_eapol_evidence(evidence));
        badger_bytes_store_u32(evidence + 572u, CLEAN_AP_PSK_CCMP);
        badger_bytes_store_u32(evidence + 1088u, 529u);
        assert(!clean_station_eapol_evidence(evidence));
        badger_bytes_store_u32(evidence + 1088u, length + 18u);
        badger_bytes_store_u32(evidence + 524u, 5u);
        assert(!clean_station_eapol_evidence(evidence));
        badger_bytes_store_u32(evidence + 524u, 4u);
        evidence[1122u + length - 1u] ^= 1u;
        assert(!clean_station_eapol_evidence(evidence));
        evidence[1122u + length - 1u] ^= 1u;
        for (uint32_t bit = 0u; bit < 16u; ++bit) {
            uint32_t at = header + (bit < 8u ? 14u : 13u);
            frame[at] ^= (uint8_t)(1u << (bit & 7u));
            badger_bytes_store_u32(frame + length - 4u, diag_crc32(frame, length - 4u));
            assert(!clean_station_eapol_m1(frame, length, mac, peer));
            frame[at] ^= (uint8_t)(1u << (bit & 7u));
        }
        badger_bytes_store_u32(frame + length - 4u, diag_crc32(frame, length - 4u));
        for (uint32_t n = 0u; n < length; ++n) assert(!clean_station_eapol_m1(frame, n, mac, peer));
        const uint32_t invalid[] = {4u,10u,16u,header,header+9u,header+11u,
                                    header+12u,header+14u,header+16u,header+89u,header+105u};
        for (uint32_t i = 0u; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
            frame[invalid[i]] ^= 1u;
            badger_bytes_store_u32(frame + length - 4u, diag_crc32(frame, length - 4u));
            assert(!clean_station_eapol_m1(frame, length, mac, peer));
            frame[invalid[i]] ^= 1u;
        }
        badger_bytes_store_u32(frame + length - 4u, diag_crc32(frame, length - 4u));
        frame[length - 1u] ^= 1u;
        assert(!clean_station_eapol_m1(frame, length, mac, peer));
    }
}

int main(void) {
    cck_rates();
    ht_probe_tests();
    ht_boundaries();
    station_tests();
    wifi_wire_t wire = {.now_us = now, .transfer = mock_transfer,
        .write = write_signal, .data_output = output, .read_data = read_signal};
    fragments(&wire);
    ht_transport(&wire);
    layout_tests(&wire);
    calls = blocks = 0u;
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
    l.budget_us = 3505000000u;
    ticks += 8000000u;
    assert(clean_service(&l));
    cancel = 1;
    assert(!clean_service(&l) && l.error == CLEAN_CANCELLED);
    cancel = 0;
    ticks = UINT32_MAX - 1000000u;
    clean_loader_init(&l, &wire, service, NULL);
    l.budget_us = 3505000000u;
    ticks += 3505000000u;
    assert(!clean_service(&l) && l.error == CLEAN_TIMEOUT);
    ticks = UINT32_MAX - 5u;
    clean_loader_init(&l, &wire, service, NULL);
    l.budget_us = CLEAN_SOAK_MAX_US;
    ticks += 3000000000u;
    assert(clean_service(&l) && l.elapsed_us == 3000000001ULL);
    ticks += 3000000000u;
    assert(clean_service(&l) && l.elapsed_us == 6000000002ULL);
    ticks += 1200000000u - 4u;
    assert(clean_service(&l) && l.elapsed_us == CLEAN_SOAK_MAX_US - 1u);
    assert(!clean_service(&l) && l.error == CLEAN_TIMEOUT &&
           l.elapsed_us == CLEAN_SOAK_MAX_US);
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
    uint8_t packet[CLEAN_RX_PACKET_BYTES];
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
    static const uint32_t sizes[] = {
        18u,26u,63u,100u,106u,108u,137u,522u,523u,524u,525u,
        1023u,1024u,1025u,0x551u,1500u,1535u,1536u,1537u,
        CLEAN_F2_PACKET_BYTES - 1u,CLEAN_F2_PACKET_BYTES
    };
    for (uint32_t i = 0u; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        uint32_t size = sizes[i];
        f2_status = 0x100u | (size << 9u);
        badger_bytes_store_u32(f2_packet, size | ((size ^ 0xffffu) << 16u));
        assert(clean_receive(&l, packet, &packet_length) && packet_length == size);
        assert(l.rx_status == f2_status && memcmp(packet, f2_packet, size) == 0);
    }
    f2_packet[2] ^= 1u;
    assert(!clean_receive(&l, packet, &packet_length) && l.error == CLEAN_INPUT);
    for (uint32_t size = 18u; size <= CLEAN_F2_PACKET_BYTES; ++size) {
        for (uint32_t rounded = 0u; rounded < 2u; ++rounded) {
            clean_loader_init(&l, &wire, service, NULL);
            uint32_t advertised = rounded ? (size + 3u) & ~3u : size;
            f2_status = 0x100u | (advertised << 9u);
            badger_bytes_store_u32(f2_packet, size | ((size ^ 0xffffu) << 16u));
            assert(clean_receive(&l, packet, &packet_length) && packet_length == size);
            assert(memcmp(packet, f2_packet, size) == 0);
        }
    }
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
    f2_status = 0x100u | ((CLEAN_F2_PACKET_BYTES + 1u) << 9u);
    assert(!clean_receive(&l, packet, &packet_length) && l.error == CLEAN_INPUT);
    assert(l.rx_status == f2_status && l.actual == CLEAN_F2_PACKET_BYTES + 1u);
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
    f2_status = 0x100u | ((CLEAN_F2_PACKET_BYTES + 1u) << 9u);
    for (uint32_t retry = 1u; retry <= 3u; ++retry) {
        assert(clean_receive(&l, packet, &packet_length) && packet_length == 0u);
        assert(l.rx_resyncs == retry && terminations == retry && l.error == CLEAN_OK);
    }
    assert(!clean_receive(&l, packet, &packet_length) && l.error == CLEAN_INPUT);
    assert(l.actual == CLEAN_F2_PACKET_BYTES + 1u && terminations == 3u);
    clean_loader_init(&l, &wire, service, NULL);
    f2_status = 0x100u;
    assert(clean_receive(&l, packet, &packet_length) && l.rx_resyncs == 1u);
    assert(l.actual == 0u);
    f2_status = 0x100u | (0x7ffu << 9u);
    assert(clean_receive(&l, packet, &packet_length) && l.rx_resyncs == 2u);
    assert(l.actual == 0x7ffu);
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
    uint8_t entries[320] = {0};
    memcpy(entries + 7u * 40u, ap, 6u);
    assert(clean_beacon_find(entries, 8u, ap) == 7);
    assert(clean_beacon_find(entries, 7u, ap) == -1);
    assert(clean_beacon_find(entries, 0u, ap) == -1);
    assert(clean_beacon_find(entries, 9u, ap) == -1);
    assert(clean_beacon_find(entries, 8u, station) == -1);
    assert(clean_beacon_find(entries, 8u, entries) == -1);
    entries[0] = 1u;
    assert(clean_beacon_find(entries, 8u, entries) == -1);
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
    assert(clean_ofdm_plcp_rate(short_auth, 34u, 0u));
    assert(clean_ofdm_plcp_valid(short_auth, 34u));
    assert(short_auth[0] == 0x4bu && short_auth[1] == 4u && short_auth[2] == 2u);
    assert(clean_frame_valid(short_auth + 6u, 34u));
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
    assert(clean_auth_response_status(evidence) == -1);
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
    assert(clean_auth_response_status(evidence) == 0);
    evidence[1122u + 84u] ^= 1u;
    assert(clean_auth_response_status(evidence) == -1);
    evidence[1122u + 84u] ^= 1u;
    evidence[1980u] ^= 1u;
    assert(clean_auth_response_status(evidence) == -1);
    evidence[1980u] ^= 1u;
    evidence[1122u + 28u] = 13u;
    badger_bytes_store_u32(evidence + 1122u + 84u, diag_crc32(evidence + 1122u, 84u));
    assert(clean_auth_response_status(evidence) == 13);
    evidence[1122u + 26u] = 1u;
    badger_bytes_store_u32(evidence + 1122u + 84u, diag_crc32(evidence + 1122u, 84u));
    assert(clean_auth_response_status(evidence) == -1);
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
    assert(!clean_frame_valid(frame, CLEAN_MPDU_BYTES + 1u));
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
    puts("clean-rx loader: fragments, credits, faults, boundary splitting, readback, cancellation, deadlines and states pass");
    return 0;
}
