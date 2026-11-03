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
    0x0032u, /* 15: jmp !x, 18 */
    0x4022u, /* 16: in x, 2 */
    0x078du, /* 17: jmp y--, 13 [7] */
    0xc001u, /* 18: irq 1; first observed SE0 */
    0x8020u, /* 19: push block; partial byte, possibly empty */
    0xa0c2u, /* 20: mov isr, y */
    0x8020u, /* 21: push block; nonzero low half marks bit-count trailer */
    0xa442u, /* 22: nop [4] */
    0xa0e0u, /* 23: mov osr, pins; next bit must also be SE0 */
    0x6022u, /* 24: out x, 2 */
    0x003bu, /* 25: jmp !x, 27 */
    0xc023u, /* 26: irq wait 3; malformed EOP */
    0x37a0u, /* 27: wait 1 pin 0 [23]; minimum ACK interpacket gap */
    0xc002u, /* 28: irq 2; ACK may now start */
    0x001du, /* 29: jmp 29 */
    0xe083u, /* 30: set pindirs, 3; preloaded ACK entry */
    0x0004u, /* 31: jmp 4 */
};

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
    WRITE(PIO(0xd8u), 0xe080u);
    for (uint32_t pin = LOGI_DP; pin <= LOGI_DM; ++pin)
        WRITE(0x40038004u + pin * 4u, 0xc6u);
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
    WRITE(PIO(0u), 0u);
    for (uint32_t i = 0u; i < 32u; ++i) WRITE(PIO(0x48u + i * 4u), fs_pio_program[i]);
    WRITE(PIO(0xc8u), 1u << 16u);
    WRITE(PIO(0xccu), 9u << 12u);
    WRITE(PIO(0xd0u), TX_SHIFT);
    WRITE(PIO(0xdcu), (2u << 26u) | (2u << 20u) | (LOGI_DP << 5u) | LOGI_DP);
    WRITE(PIO(0xe0u), 1u << 16u);
    WRITE(PIO(0xe4u), (29u << 12u) | (10u << 7u) | (LOGI_DP << 24u));
    WRITE(PIO(0xe8u), RX_SHIFT);
    WRITE(PIO(0xf4u), LOGI_DP << 15u);
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

int32_t fs_pio_exchange(void *context, const fs_tx_t *tx, uint8_t receive, fs_packet_t *packet) {
    (void)context;
    fs_tx_t ack;
    fs_tx_init(&ack);
    (void)fs_handshake(&ack, FS_ACK);
    fs_rx_t rx;
    fs_rx_init(&rx);
    int32_t error = transmit(tx, receive);
    if (error || !receive) {
        WRITE(PIO(0u), 0u);
        WRITE(PIO(0xd8u), 0xe080u);
        return error;
    }
    prepare_tx(&ack, 1u);
    WRITE(PIO(0x10u), ack.words[0]);
    WRITE(PIO(0x10u), ack.words[1]);
    WRITE(PIO(0x10u), 0u);
    WRITE(PIO(0xd8u), 0x80a0u);
    WRITE(PIO(0xd8u), 0x6020u);
    WRITE(PIO(0xd8u), 0xe001u);
    WRITE(PIO(0xd8u), 30u);
    uint32_t pending = 0u, words = 0u, start = fs_pio_now(0);
    uint8_t finished = 0u, pending_valid = 0u;
    for (uint32_t loops = 0u; loops < 100000u; ++loops) {
        if (!(READ(PIO(4u)) & (1u << 9u))) {
            uint32_t word = READ(PIO(0x24u));
            if (word & 0xffffu) {
                if (!words || word > RX_LIMIT || RX_LIMIT - word > FS_RX_BITS) { error = FS_LENGTH; break; }
                uint32_t count = RX_LIMIT - word;
                if ((count / 8u) != words - 1u) { error = FS_LENGTH; break; }
                if (count % 8u) {
                    if (!pending_valid) { error = FS_LENGTH; break; }
                    error = fs_rx_feed(&rx, (uint16_t)(pending >> (32u - 2u * (count % 8u))),
                                       (uint8_t)(count % 8u));
                }
                finished = 1u;
                break;
            }
            ++words;
            if (pending_valid) {
                error = fs_rx_feed(&rx, (uint16_t)(pending >> 16u), 8u);
                if (error) break;
            }
            pending = word;
            pending_valid = 1u;
            /* Before SE0, every pushed word is full: decode CRC bytes without a word of lag. */
            if (!(READ(PIO(0x30u)) & 2u)) {
                error = fs_rx_feed(&rx, (uint16_t)(word >> 16u), 8u);
                pending_valid = 0u;
                if (error) break;
            }
            continue;
        }
        if (fs_pio_now(0) - start >= 150u) { error = FS_TIMEOUT; break; }
    }
    if (!error && !finished) error = FS_TIMEOUT;
    if (!error) error = fs_rx_validate(&rx, packet);
    if (!error && (READ(PIO(8u)) & 0x03030303u)) error = FS_FIFO;
    if (!error) {
        uint32_t flags = READ(PIO(0x30u));
        if (!(flags & 12u)) {
            uint32_t eop = fs_pio_now(0);
            do {
                flags = READ(PIO(0x30u));
                if (fs_pio_now(0) - eop >= 2u) { error = FS_ENCODING; break; }
            } while (!(flags & 12u));
        }
        if (flags & 8u) error = FS_ENCODING;
        if (!error && line() != 1u) error = FS_LINE;
        if (!error && (packet->pid == FS_DATA0 || packet->pid == FS_DATA1)) {
            /* The ACK is already encoded and queued; validation precedes this one enable write. */
            WRITE(PIO(0u), 1u);
            uint32_t began = fs_pio_now(0);
            while (!(READ(PIO(0x30u)) & 1u))
                if (fs_pio_now(0) - began >= 10u) { error = FS_TIMEOUT; break; }
        }
    }
    if (!error) error = fs_rx_finish(&rx, packet);
    WRITE(PIO(0u), 0u);
    WRITE(PIO(0xd8u), 0xe080u);
    return error;
}
