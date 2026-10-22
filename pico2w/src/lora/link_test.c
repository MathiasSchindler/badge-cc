#include "link_test.h"
#include "hardware.h"
#include "usb/report.h"

static int32_t command(sx1262_receiver_t *rx, const uint8_t *out, uint8_t *in, uint32_t n) {
    rx->last_command = out[0];
    return sx1262_transfer(out, in, n, 0, 0u, rx->service);
}

int32_t link_test_configure(sx1262_receiver_t *rx) {
    sx1262_rx_config_t config;
    sx1262_rx_lorawan_config(&config);
    config.frequency_hz = LINK_FREQUENCY_HZ;
    int32_t error = sx1262_receiver_configure(rx, &config);
    static const uint8_t standby[] = {0x80u, 0u};
    static const uint8_t sync[] = {0x0du, 0x07u, 0x40u, 0x14u, 0x24u};
    static const uint8_t receive[] = {0x82u, 0xffu, 0xffu, 0xffu};
    uint8_t in[5];
    if (!error) error = command(rx, standby, in, sizeof(standby));
    if (!error) error = command(rx, sync, in, sizeof(sync));
    if (!error) error = command(rx, receive, in, sizeof(receive));
    if (error) {
        sx1262_hw_release();
        rx->state = SX1262_RX_FAULT;
        rx->last_error = error;
    }
    return error;
}

void link_test_payload(uint8_t *payload, const uint32_t chip_id[2],
                        uint32_t sequence, const uint8_t nonce[16]) {
    usb_store_u32(payload, LINK_PACKET_MAGIC);
    usb_store_u32(payload + 4u, chip_id[0]);
    usb_store_u32(payload + 8u, chip_id[1]);
    usb_store_u32(payload + 12u, sequence);
    for (uint32_t i = 0u; i < 16u; ++i) payload[16u + i] = nonce[i];
    usb_store_u32(payload + 32u, usb_crc32(payload, 32u));
}

static int32_t transmit(sx1262_receiver_t *rx, const uint8_t *payload) {
    static const uint8_t standby[] = {0x80u, 0u};
    static const uint8_t read_clamp[] = {0x1du, 0x08u, 0xd8u, 0u, 0u};
    static const uint8_t pa[] = {0x95u, 0x04u, 0x07u, 0x00u, 0x01u};
    static const uint8_t power[] = {0x8eu, 0x00u, 0x04u}; /* 0 dBm, 200 us ramp */
    static const uint8_t params[] = {0x8cu, 0u, 8u, 0u, LINK_PACKET_SIZE, 1u, 0u};
    static const uint8_t irq[] = {0x08u, 0x02u, 0x01u, 0u, 0u, 0u, 0u, 0u, 0u};
    static const uint8_t clear[] = {0x02u, 0xffu, 0xffu};
    static const uint8_t tx[] = {0x83u, 0x00u, 0x25u, 0x80u}; /* hard 150 ms timeout */
    static const uint8_t get_irq[] = {0x12u, 0u, 0u, 0u};
    static const uint8_t errors[] = {0x17u, 0u, 0u, 0u};
    uint8_t in[LINK_PACKET_SIZE + 2u], out[LINK_PACKET_SIZE + 2u];
    int32_t error = command(rx, standby, in, sizeof(standby));
    if (!error) error = command(rx, read_clamp, in, sizeof(read_clamp));
    if (!error) {
        out[0] = 0x0du; out[1] = 0x08u; out[2] = 0xd8u;
        out[3] = in[4] | 0x1eu; /* SX1262 PA clamping erratum */
        error = command(rx, out, in, 4u);
    }
    if (!error) error = command(rx, pa, in, sizeof(pa));
    if (!error) error = command(rx, power, in, sizeof(power));
    if (!error) error = command(rx, params, in, sizeof(params));
    if (!error) {
        out[0] = 0x0eu; out[1] = 0u;
        for (uint32_t i = 0u; i < LINK_PACKET_SIZE; ++i) out[i + 2u] = payload[i];
        error = command(rx, out, in, sizeof(out));
    }
    if (!error) error = command(rx, irq, in, sizeof(irq));
    if (!error) error = command(rx, clear, in, sizeof(clear));
    if (!error) error = command(rx, errors, in, sizeof(errors));
    if (!error && (in[2] || in[3])) error = SX1262_DEVICE_ERROR;
    if (!error) error = command(rx, tx, in, sizeof(tx));
    uint32_t start = sx1262_hw_time_us();
    for (uint32_t loops = 0u; !error && loops < 100000u; ++loops) {
        error = command(rx, get_irq, in, sizeof(get_irq));
        if (error) break;
        if (in[2] & 0x02u) return LINK_TX_TIMEOUT;
        if (in[3] & 0x01u) return 0;
        if (sx1262_hw_time_us() - start >= 200000u) return LINK_TX_TIMEOUT;
    }
    return error ? error : LINK_TX_TIMEOUT;
}

int32_t link_test_send(link_test_t *test, sx1262_receiver_t *rx, uint32_t now_ms,
                       const uint32_t chip_id[2], const uint8_t nonce[16]) {
    if (test->attempts >= LINK_MAX_ATTEMPTS || now_ms - test->last_attempt_ms < LINK_INTERVAL_MS)
        return test->error = LINK_LIMIT_ERROR;
    if (rx->state != SX1262_RX_RUNNING) return test->error = SX1262_RX_STATE_ERROR;
    uint8_t payload[LINK_PACKET_SIZE];
    ++test->attempts;
    test->last_attempt_ms = now_ms;
    link_test_payload(payload, chip_id, test->attempts, nonce);
    test->error = transmit(rx, payload);
    if (!test->error) ++test->transmitted;
    test->restore_error = link_test_configure(rx);
    return test->error ? test->error : test->restore_error;
}
