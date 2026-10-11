#include "wifi_pio.h"
#include "../../shared/bytes.h"
#include "wifi_pio_fifo.h"

#define REG(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define PIO(offset) REG(0x50200000u + (offset))
#define GPIO_CTRL(pin) REG(0x40028004u + (pin) * 8u)

static uint32_t profile[3];
#if defined(__arm__) && defined(BADGER_FAST_BYTES)
static const wifi_pio_fifo_t fifo = {
    .status = &PIO(4u), .tx = &PIO(0x10u), .rx = &PIO(0x20u),
    .timer = &REG(0x400b000cu),
};
#endif

const uint32_t *wifi_pio_profile(void) { return profile; }

#if !defined(__arm__) || !defined(BADGER_FAST_BYTES)
static uint32_t network_word(const uint8_t *bytes) {
    return (uint32_t)bytes[0] << 24u | (uint32_t)bytes[1] << 16u |
           (uint32_t)bytes[2] << 8u | bytes[3];
}
#endif

static int wait_mask(const wifi_wire_t *wire, volatile uint32_t *reg,
                     uint32_t mask, uint32_t expected) {
    uint32_t start = wire->now_us(wire->context);
    for (uint32_t i = 0u; i < 1000000u; ++i) {
        if ((*reg & mask) == expected) return 1;
        if (wire->now_us(wire->context) - start >= 100000u) break;
    }
    return 0;
}

static void observe(const wifi_wire_t *wire, wifi_trace_event_t event,
                     const uint8_t *bytes, uint32_t length) {
    if (wire->observe) wire->observe(wire->context, event, bytes, length);
}

wifi_bus_result_t wifi_pio_transfer(const wifi_wire_t *wire, const uint8_t *tx,
                                    uint32_t tx_length, uint8_t *rx, uint32_t rx_length) {
    if (wire == 0 || wire->write == 0 || wire->data_output == 0 || wire->now_us == 0) return WIFI_BUS_BAD_ARGUMENT;
    if (tx == 0 || tx_length < 4u || tx_length > 2044u || (tx_length & 3u) != 0u ||
        rx_length > 2056u || (rx_length & 3u) != 0u || (rx_length != 0u && rx == 0) ||
        (rx_length != 0u && tx_length != 4u)) {
        wifi_wire_idle(wire);
        return WIFI_BUS_BAD_ARGUMENT;
    }
    uint32_t setup_start = wire->now_us(wire->context);
    badger_bytes_zero(rx, rx_length);
    wifi_wire_idle(wire);
    observe(wire, WIFI_TRACE_IDLE, tx, tx_length);

    /* This standalone application owns PIO0. No other PIO users are linked. */
    REG(0x40022000u) = 1u << 11u;
    REG(0x40023000u) = 1u << 11u;
    if (!wait_mask(wire, &REG(0x40020008u), 1u << 11u, 1u << 11u)) {
        return WIFI_BUS_TRANSFER_TIMEOUT;
    }
#if defined(__arm__) && defined(BADGER_FAST_BYTES)
    wifi_pio_prepare_hw(&PIO(0u), &REG(0x40028000u), tx_length, rx_length);
#else
    static const uint16_t program[] = {
        0x6001u, /* out pins,1 side 0 */
        0x1040u, /* jmp x--,0 side 1 */
        0xe080u, /* set pindirs,0 side 0 */
        0x5001u, /* in pins,1 side 1 */
        0x0083u, /* jmp y--,3 side 0 */
        0xc000u, /* irq 0 side 0 */
        0x0006u, /* jmp 6 side 0 */
    };
    for (uint32_t i = 0u; i < sizeof(program)/sizeof(program[0]); ++i) PIO(0x48u+i*4u) = program[i];
    if (rx_length == 0u) {
        PIO(0x54u) = 0xc000u;
        PIO(0x58u) = 0x0004u;
    }
    PIO(0xc8u) = 8u << 16u;
    PIO(0xccu) = 6u << 12u;
    PIO(0xd0u) = 0u;
    /* Set only the clock's direction first; data remains SIO input. */
    PIO(0xdcu) = (1u << 26u) | (29u << 5u);
    PIO(0xd8u) = 0xe000u;
    PIO(0xd8u) = 0xe081u;
    GPIO_CTRL(29u) = 6u;
    PIO(0xdcu) = (1u << 29u) | (1u << 26u) | (1u << 20u) |
                 (24u << 15u) | (29u << 10u) | (24u << 5u) | 24u;
    PIO(0xd8u) = 0xe000u;
    PIO(0x10u) = tx_length * 8u - 1u;
    PIO(0xd8u) = 0x80a0u;
    PIO(0xd8u) = 0x6020u;
    PIO(0x10u) = rx_length != 0u ? rx_length * 8u - 1u : 0u;
    PIO(0xd8u) = 0x80a0u;
    PIO(0xd8u) = 0x6040u;
    PIO(0xd0u) = (1u << 17u) | (1u << 16u);
    PIO(0x38u) = 1u << 24u;
    PIO(0xd8u) = 0u;
#endif
    uint32_t queued = 0u;
    uint32_t transfer_start = wire->now_us(wire->context);
    uint32_t cleanup_start;
#if defined(__arm__) && defined(BADGER_FAST_BYTES)
    queued = tx_length < 16u ? tx_length : 16u;
    wifi_bus_result_t result = WIFI_BUS_OK;
    if (!wifi_pio_write_words(&fifo, tx, queued / 4u, transfer_start)) {
        result = WIFI_BUS_TRANSFER_TIMEOUT;
        goto finished;
    }
#else
    while (queued < tx_length && queued < 16u) {
        PIO(0x10u) = network_word(tx + queued);
        queued += 4u;
    }
#endif
    wire->write(wire->context, WIFI_SIGNAL_SELECT, 0u);
    observe(wire, WIFI_TRACE_SELECTED, 0, 0u);
    PIO(0xd8u) = 0xe081u;
    GPIO_CTRL(24u) = 6u;
    observe(wire, WIFI_TRACE_DRIVING, 0, 0u);
    PIO(0u) = 1u;
#if !defined(__arm__) || !defined(BADGER_FAST_BYTES)
    wifi_bus_result_t result = WIFI_BUS_OK;
#endif
    transfer_start = wire->now_us(wire->context);
    profile[0] += transfer_start - setup_start;
#if defined(__arm__) && defined(BADGER_FAST_BYTES)
    if (!wifi_pio_write_words(&fifo, tx + queued, (tx_length - queued) / 4u, transfer_start) ||
        !wifi_pio_read_words(&fifo, rx, rx_length / 4u, transfer_start)) {
        result = WIFI_BUS_TRANSFER_TIMEOUT;
        goto finished;
    }
#else
    while (queued < tx_length) {
        if (wire->now_us(wire->context) - transfer_start >= 100000u ||
            !wait_mask(wire, &PIO(4u), 1u << 16u, 0u)) {
            result = WIFI_BUS_TRANSFER_TIMEOUT;
            goto finished;
        }
        PIO(0x10u) = network_word(tx + queued);
        queued += 4u;
    }
    for (uint32_t i = 0u; i < rx_length; i += 4u) {
        if (wire->now_us(wire->context) - transfer_start >= 100000u ||
            !wait_mask(wire, &PIO(4u), 1u << 8u, 0u)) {
            result = WIFI_BUS_TRANSFER_TIMEOUT;
            goto finished;
        }
        uint32_t value = PIO(0x20u);
        for (uint32_t b = 0u; b < 4u; ++b) rx[i+b] = (uint8_t)(value >> (24u-b*8u));
    }
#endif
    if (!wait_mask(wire, &PIO(0x30u), 1u, 1u)) result = WIFI_BUS_TRANSFER_TIMEOUT;
finished:
    cleanup_start = wire->now_us(wire->context);
    profile[1] += cleanup_start - transfer_start;
#if defined(__arm__) && defined(BADGER_FAST_BYTES)
    wifi_pio_finish_hw(&PIO(0u), &REG(0xd0000000u), &REG(0x40028000u));
#else
    PIO(0u) = 0u;
    PIO(0xd8u) = 0xe080u;
    wire->data_output(wire->context, 0u);
    wire->write(wire->context, WIFI_SIGNAL_CLOCK, 0u);
    GPIO_CTRL(24u) = 5u;
    GPIO_CTRL(29u) = 5u;
    wifi_wire_idle(wire);
#endif
    observe(wire, WIFI_TRACE_FINISHED, rx, rx_length);
    profile[2] += wire->now_us(wire->context) - cleanup_start;
    return result;
}
