#include "fs.h"

#ifdef LOGI_FS_TEST
extern uint32_t fs_test_read(uint32_t address);
extern void fs_test_write(uint32_t address, uint32_t value);
#define READ(address) fs_test_read(address)
#define WRITE(address, value) fs_test_write(address, value)
#else
#define READ(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define WRITE(address, value) (*(volatile uint32_t *)(uintptr_t)(address) = (value))
#endif

#define PIO(offset) (LOGI_PIO + (offset))
#define TRACE(offset) (0x50300000u + (offset))
#define RX_LIMIT 1023u
#define TX_SHIFT ((1u << 30u) | (1u << 19u) | (1u << 17u))
#define RX_SHIFT ((1u << 31u) | (16u << 20u) | (1u << 19u) | (1u << 18u) | (1u << 16u))

/* TX starts only when explicitly enabled. RX waits for the TX release IRQ. */
const uint16_t fs_pio_program[32] = {
    0x80a0u, /*  0: pull block */
    0x6020u, /*  1: out x, 32 */
    0xe001u, /*  2: set pins, 1 */
    0xf783u, /*  3: set pindirs, 3 [23]; two bit-times of J */
    0x6a02u, /*  4: out pins, 2 [10] */
    0x0044u, /*  5: jmp x--, 4 */
    0xe080u, /*  6: set pindirs, 0 */
    0xc004u, /*  7: irq 4; release RX before announcing TX complete */
    0xc000u, /*  8: irq 0 */
    0x0009u, /*  9: jmp 9 */
    0x20c4u, /* 10: wait 1 irq 4 */
    0x2020u, /* 11: wait 0 pin 0 */
    0xa442u, /* 12: nop [4]; sample near middle of first K */
    0xa0e0u, /* 13: mov osr, pins */
    0x6022u, /* 14: out x, 2 */
    0x0033u, /* 15: jmp !x, 19 */
    0x4022u, /* 16: in x, 2 */
    0x078du, /* 17: jmp y--, 13 [7] */
    0x0013u, /* 18: unused; jump to EOP trailer */
    0x4850u, /* 19: in y, 16 [8]; tagged partial samples + count, autopush */
    0xa0e0u, /* 20: mov osr, pins; next bit must also be SE0 */
    0x6022u, /* 21: out x, 2 */
    0x0038u, /* 22: jmp !x, 24 */
    0xc023u, /* 23: irq wait 3; malformed EOP */
    0x37a0u, /* 24: wait 1 pin 0 [23]; minimum ACK interpacket gap */
    0x00d7u, /* 25: jmp pin, 23; D- must be low at idle J */
    0xc002u, /* 26: irq 2; validated EOP, ACK may now start */
    0x001bu, /* 27: jmp 27 */
    0x001bu, 0x001bu,
    0xe083u, /* 30: set pindirs, 3; preloaded ACK entry */
    0x0004u, /* 31: jmp 4 */
};
const uint16_t fs_trace_program[6] = {0xa0e0u, 0x6022u, 0x0024u, 0x0000u, 0x4402u, 0x0004u};

uint32_t fs_pio_now(void *context) { (void)context; return READ(0x400b000cu); }

static int32_t wait_mask(uint32_t address, uint32_t mask, uint32_t expected) {
    uint32_t start = fs_pio_now(0);
    for (uint32_t loops = 0u; loops < 1000000u; ++loops) {
        if ((READ(address) & mask) == expected) return 0;
        if (fs_pio_now(0) - start >= 100000u) break;
    }
    return FS_CLOCK;
}

int32_t fs_pio_clock(void) {
    WRITE(0x4001003cu, READ(0x4001003cu) & ~1u);
    if (wait_mask(0x40010044u, 3u, 1u)) return FS_CLOCK;
    WRITE(0x40022000u, 1u << 14u);
    WRITE(0x40023000u, 1u << 14u);
    if (wait_mask(0x40020008u, 1u << 14u, 1u << 14u)) return FS_CLOCK;
    WRITE(0x40050000u, 1u);
    WRITE(0x40050008u, 120u);
    WRITE(0x40050004u, 0x0cu);
    if (wait_mask(0x40050000u, 1u << 31u, 1u << 31u)) return FS_CLOCK;
    WRITE(0x4005000cu, (5u << 16u) | (2u << 12u));
    WRITE(0x40050004u, 4u);
    WRITE(0x40010040u, 0x10000u);
    WRITE(0x4001003cu, (READ(0x4001003cu) & ~(7u << 5u)) | 1u);
    return wait_mask(0x40010044u, 3u, 2u);
}

static uint8_t line(void) {
    return (uint8_t)(READ(0xd0000004u) >> LOGI_DP) & 3u;
}

void fs_pio_stop(void) {
    WRITE(PIO(0u), 0u);
    WRITE(TRACE(0u), 0u);
    WRITE(PIO(0xd8u), 0xe080u);
    for (uint32_t pin = LOGI_DP; pin <= LOGI_DM; ++pin)
        WRITE(0x40038004u + pin * 4u, 0xc6u);
}

static int32_t trace_init(void) {
    WRITE(0x40022000u, 1u << 12u);
    WRITE(0x40023000u, 1u << 12u);
    if (wait_mask(0x40020008u, 1u << 12u, 1u << 12u)) return FS_CLOCK;
    /* Input-only PIO1: detect receive SE0, then sample both lines every USB bit. */
    for (uint32_t i = 0u; i < 6u; ++i) WRITE(TRACE(0x48u + i * 4u), fs_trace_program[i]);
    WRITE(TRACE(0xc8u), 1u << 16u);
    WRITE(TRACE(0xccu), (5u << 12u) | (4u << 7u));
    WRITE(TRACE(0xdcu), LOGI_DP << 15u);
    return 0;
}

static void trace_prepare(void) {
    WRITE(TRACE(0u), (1u << 4u) | (1u << 8u));
    WRITE(TRACE(0xd0u), 0u);
    WRITE(TRACE(0xd0u), (1u << 31u) | (1u << 19u) | (1u << 18u) | (1u << 16u));
    WRITE(TRACE(0xd8u), 0u);
}

void fs_pio_trace_finish(fs_result_t *result) {
    WRITE(TRACE(0u), 0u);
    result->trace_count = 0u;
    while (result->trace_count < 8u && !(READ(TRACE(4u)) & (1u << 8u)))
        result->trace_words[result->trace_count++] = READ(TRACE(0x20u));
}

static int32_t pause(uint32_t us) {
    uint32_t start = fs_pio_now(0);
    for (uint32_t loops = 0u; loops < 10000000u; ++loops)
        if (fs_pio_now(0) - start >= us) return 0;
    return FS_DEADLINE;
}

int32_t fs_pio_reset(void *context) {
    fs_result_t *result = context;
    uint8_t before = line();
    if (result) result->reset_levels = before;
    if (before != 1u) return FS_LINE;
    if (result) {
        int32_t error = trace_init();
        if (error) return error;
    }
    WRITE(PIO(0u), 0u);
    for (uint32_t i = 0u; i < 32u; ++i) WRITE(PIO(0x48u + i * 4u), fs_pio_program[i]);
    WRITE(PIO(0xc8u), 1u << 16u);
    WRITE(PIO(0xccu), 9u << 12u);
    WRITE(PIO(0xd0u), TX_SHIFT);
    WRITE(PIO(0xdcu), (2u << 26u) | (2u << 20u) | (LOGI_DP << 5u) | LOGI_DP);
    WRITE(PIO(0xe0u), 1u << 16u);
    WRITE(PIO(0xe4u), (27u << 12u) | (10u << 7u) | (LOGI_DM << 24u));
    WRITE(PIO(0xe8u), RX_SHIFT);
    WRITE(PIO(0xf4u), LOGI_DP << 15u);
    WRITE(PIO(0xf8u), 1u << 16u);
    WRITE(PIO(0xfcu), (30u << 12u) | (4u << 7u));
    WRITE(PIO(0x10cu), (2u << 26u) | (2u << 20u) | (LOGI_DP << 5u) | LOGI_DP);
    WRITE(PIO(8u), 0xffffffffu);
    WRITE(PIO(0x30u), 0xffu);
    WRITE(PIO(0xd8u), 0xe080u);
    WRITE(PIO(0xd8u), 0xe000u);
    for (uint32_t pin = LOGI_DP; pin <= LOGI_DM; ++pin) {
        WRITE(0x40028004u + pin * 8u, 6u);
        WRITE(0x40038004u + pin * 4u, 0x67u);
    }
    WRITE(PIO(0xd8u), 0xe083u);
    if (result) result->reset_levels |= 0x80u;
    int32_t error = pause(1000u);
    uint8_t during = line();
    if (result) result->reset_levels |= during << 2u;
    if (!error && during != 0u) error = FS_LINE;
    if (!error) error = pause(19000u);
    WRITE(PIO(0xd8u), 0xe080u);
    uint8_t after = 0u;
    uint32_t released = fs_pio_now(0);
    for (uint32_t loops = 0u; loops < 10000u; ++loops) {
        after = line();
        if (after == 1u) break;
        if (fs_pio_now(0) - released >= 10u) break;
    }
    if (result) result->reset_levels |= (uint8_t)(0x40u | after << 4u);
    if (!error && after != 1u) error = FS_LINE;
    if (error) fs_pio_stop();
    return error;
}

static void prepare_tx(const fs_tx_t *tx, uint8_t rx_enabled) {
    WRITE(PIO(0u), rx_enabled ? 2u : 0u);
    WRITE(PIO(0u), (rx_enabled ? 2u : 0u) | (1u << 4u) | (1u << 8u));
    WRITE(PIO(0xd0u), 0u);
    WRITE(PIO(0xd0u), TX_SHIFT);
    WRITE(PIO(0xd8u), 0u);
    WRITE(PIO(0x30u), 1u);
    WRITE(PIO(0x10u), tx->count - 1u);
}

static int32_t transmit(const fs_tx_t *tx, uint8_t receive) {
    if (!tx->count || tx->count > FS_TX_WORDS * 16u) return FS_LENGTH;
    WRITE(PIO(0u), 0u);
    WRITE(PIO(0x30u), 0xffu);
    if (receive) {
        WRITE(PIO(0u), (1u << 5u) | (1u << 9u));
        WRITE(PIO(0xe8u), 0u);
        WRITE(PIO(0xe8u), RX_SHIFT);
        /* RX FIFO join disables its TX FIFO; inject the counter without a pull. */
        WRITE(PIO(0xf0u), 0xa0ebu);
        WRITE(PIO(0xf0u), 0x604au);
        WRITE(PIO(0xf0u), 0xa0c3u);
        WRITE(PIO(0xf0u), 10u);
    }
    prepare_tx(tx, receive);
    uint32_t words = (tx->count + 15u) / 16u, sent = 0u;
    /* A spare word avoids an empty autopull at an exact 32-bit boundary. */
    while (sent < words + 1u && !(READ(PIO(4u)) & (1u << 16u))) {
        WRITE(PIO(0x10u), sent < words ? tx->words[sent] : 0u);
        ++sent;
    }
    WRITE(PIO(0u), receive ? 3u : 1u);
    uint32_t start = fs_pio_now(0);
    for (uint32_t loops = 0u; loops < 100000u; ++loops) {
        if (READ(PIO(0x30u)) & 1u) return 0;
        if (sent < words + 1u && !(READ(PIO(4u)) & (1u << 16u))) {
            WRITE(PIO(0x10u), sent < words ? tx->words[sent] : 0u);
            ++sent;
        }
        if (fs_pio_now(0) - start >= 150u) return FS_TIMEOUT;
    }
    return FS_TIMEOUT;
}

static void prepare_ack(const fs_tx_t *ack) {
    WRITE(PIO(0u), (1u << 6u) | (1u << 10u));
    WRITE(PIO(0x100u), 0u);
    WRITE(PIO(0x100u), TX_SHIFT);
    WRITE(PIO(0x18u), ack->count - 1u);
    WRITE(PIO(0x18u), ack->words[0]);
    WRITE(PIO(0x18u), ack->words[1]);
    WRITE(PIO(0x18u), 0u);
    WRITE(PIO(0x108u), 0x80a0u);
    WRITE(PIO(0x108u), 0x6020u);
    WRITE(PIO(0x108u), 0xe001u);
    WRITE(PIO(0x108u), 30u);
}

static FS_CRITICAL __attribute__((noinline)) int32_t receive_packet(fs_packet_t *packet) {
    fs_rx_t rx;
    fs_rx_init(&rx);
    uint32_t words = 0u, start = fs_pio_now(0);
    for (uint32_t loops = 0u; loops < 100000u; ++loops) {
        if (!(READ(PIO(4u)) & (1u << 9u))) {
            uint32_t word = READ(PIO(0x24u));
            if ((word >> 16u) <= RX_LIMIT) {
                uint32_t count = RX_LIMIT - (word >> 16u);
                if (__builtin_expect(count > FS_RX_BITS || (count / 8u) != words, 0))
                    return FS_LENGTH;
                if (count % 8u) {
                    int32_t error = fs_rx_feed(&rx, (uint16_t)word >> (16u - 2u * (count % 8u)),
                                              (uint8_t)(count % 8u));
                    if (error) return error;
                }
                goto received;
            }
            ++words;
            int32_t error = fs_rx_feed(&rx, (uint16_t)(word >> 16u), 8u);
            if (error) return error;
            continue;
        }
        if (fs_pio_now(0) - start >= 150u) return FS_TIMEOUT;
    }
    return FS_TIMEOUT;
received:
    if (READ(PIO(8u)) & 0x07070707u) return FS_FIFO;
    uint32_t flags = READ(PIO(0x30u));
    if (!(flags & 12u)) {
        uint32_t eop = fs_pio_now(0);
        do {
            flags = READ(PIO(0x30u));
            if (!(flags & 12u) && fs_pio_now(0) - eop >= 2u) return FS_ENCODING;
        } while (!(flags & 12u));
    }
    if (flags & 8u) return FS_ENCODING;
    if (rx.data_pid) {
        uint32_t tail = rx.tail;
        if ((tail & 0x00ffffffu) != 0xb001u)
            return tail & 0x00ff0000u ? FS_ENCODING : FS_CRC;
        if (tail >> 25u == 6u || rx.length < 4u) return FS_ENCODING;
        /* The ACK is already encoded and queued; validation precedes this one enable write. */
        WRITE(PIO(0u), 4u);
        uint32_t began = fs_pio_now(0);
        while (!(READ(PIO(0x30u)) & 1u))
            if (fs_pio_now(0) - began >= 10u) return FS_TIMEOUT;
    }
    return fs_rx_finish(&rx, packet);
}

int32_t fs_pio_exchange(void *context, const fs_tx_t *tx, uint8_t receive, fs_packet_t *packet) {
    fs_result_t *result = context;
    uint8_t trace = result && receive && !result->trace_pid;
    if (trace) trace_prepare();
    fs_tx_t ack;
    fs_tx_init(&ack);
    (void)fs_handshake(&ack, FS_ACK);
    if (receive) prepare_ack(&ack);
    int32_t error = transmit(tx, receive);
    if (!error && receive) {
        if (trace) WRITE(TRACE(0u), 1u);
        WRITE(PIO(0x30u), 1u);
        error = receive_packet(packet);
    }
    if (!error && trace && (packet->pid == FS_DATA0 || packet->pid == FS_DATA1))
        result->trace_pid = packet->pid;
    WRITE(PIO(0u), 0u);
    WRITE(PIO(0xd8u), 0xe080u);
    return error;
}
