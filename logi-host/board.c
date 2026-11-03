#include "board.h"
#include "../src/shared/bytes.h"
#include "../src/tools/diagnostics_logic.h"

static void write(logi_board_t *b, uint32_t address, uint32_t value) {
    b->io.write(b->io.context, address, value);
}

static uint32_t read(const logi_board_t *b, uint32_t address) {
    return b->io.read(b->io.context, address);
}

static uint32_t now(logi_board_t *b) { return b->io.now(b->io.context); }

static void fail(logi_board_t *b, int32_t error) {
    b->error = error;
    ++b->errors;
    b->pending = 0u;
    write(b, LOGI_PIO, 0u);
}

int32_t logi_board_init(logi_board_t *b, const logi_io_t *io) {
    badger_bytes_zero((uint8_t *)b, sizeof(*b));
    badger_bytes_copy((uint8_t *)&b->io, (const uint8_t *)io, sizeof(*io));
    write(b, 0x40022000u, 1u << 11u);
    write(b, 0x40023000u, 1u << 11u);
    uint32_t started = now(b);
    for (uint32_t loops = 0u; !(read(b, 0x40020008u) & (1u << 11u)); ++loops) {
        if (now(b) - started >= 100000u || loops >= 1000000u) {
            b->error = -4401;
            ++b->errors;
            return b->error;
        }
    }
    write(b, LOGI_PIO, 0u);
    for (uint32_t pin = LOGI_DP; pin <= LOGI_DM; ++pin) {
        /* Physical output disable, input enable, weak pull-down and Schmitt input. */
        write(b, 0x40038004u + pin * 4u, 0xc6u);
        write(b, 0x40028004u + pin * 8u, 6u);
    }
    static const uint16_t program[] = {
        0x4002u, /* in pins, 2 */
        0x0040u, /* jmp x--, 0 */
        0x8020u, /* push block */
        0x0003u, /* jmp 3 */
    };
    for (uint32_t i = 0u; i < sizeof(program) / sizeof(program[0]); ++i)
        write(b, LOGI_PIO + 0x48u + i * 4u, program[i]);
    write(b, LOGI_PIO + 0xc8u, 1u << 16u);
    write(b, LOGI_PIO + 0xccu, 3u << 12u);
    write(b, LOGI_PIO + 0xd0u, 1u << 18u);
    write(b, LOGI_PIO + 0xdcu, LOGI_DP << 15u);
    write(b, LOGI_PIO + 0xd8u, 0xe080u);
    b->previous = 4u;
    return 0;
}

void logi_board_poll(logi_board_t *b) {
    if (b->error) return;
    if (b->captures == 0xffffffffu || b->samples > 0xffffffffu - LOGI_SAMPLE_COUNT) {
        fail(b, -4404);
        return;
    }
    if (!b->pending) {
        write(b, LOGI_PIO, 0u);
        write(b, LOGI_PIO, (1u << 4u) | (1u << 8u));
        write(b, LOGI_PIO + 0xd8u, 0xe02fu);
        write(b, LOGI_PIO + 0xd8u, 0x0000u);
        b->started = now(b);
        b->pending = 1u;
        write(b, LOGI_PIO, 1u);
        return;
    }
    if (read(b, LOGI_PIO + 4u) & (1u << 8u)) {
        if (now(b) - b->started >= 10000u) fail(b, -4402);
        return;
    }
    uint32_t word = read(b, LOGI_PIO + 0x20u);
    write(b, LOGI_PIO, 0u);
    b->pending = 0u;
    b->last_word = word;
    ++b->captures;
    for (uint32_t i = 0u; i < LOGI_SAMPLE_COUNT; ++i) {
        uint8_t state = (uint8_t)((word >> (2u * i)) & 3u);
        if (b->previous != 4u && state != b->previous) ++b->changes;
        b->previous = state;
        ++b->histogram[state];
        ++b->samples;
    }
    if (read(b, LOGI_PIO + 8u) & ((1u << 24u) | (1u << 16u) | (1u << 8u) | 1u))
        fail(b, -4403);
}

void logi_board_status(const logi_board_t *b, uint8_t status[64],
                       uint32_t build, uint32_t uptime_ms, uint32_t usb_errors) {
    badger_bytes_zero(status, 64u);
    badger_bytes_store_u32(status, LOGI_STATUS_MAGIC);
    badger_bytes_store_u32(status + 4u, build);
    badger_bytes_store_u32(status + 8u, uptime_ms);
    badger_bytes_store_u32(status + 12u, b->captures);
    badger_bytes_store_u32(status + 16u, b->samples);
    badger_bytes_store_u32(status + 20u, b->changes);
    for (uint32_t i = 0u; i < 4u; ++i)
        badger_bytes_store_u32(status + 24u + i * 4u, b->histogram[i]);
    badger_bytes_store_u32(status + 40u, b->last_word);
    badger_bytes_store_u32(status + 44u, (uint32_t)b->error);
    badger_bytes_store_u32(status + 48u, usb_errors);
    uint32_t dp = (read(b, 0x40028000u + LOGI_DP * 8u) >> 17u) & 1u;
    uint32_t dm = (read(b, 0x40028000u + LOGI_DM * 8u) >> 17u) & 1u;
    status[52] = (uint8_t)(dp | dm << 1u);
    status[53] = LOGI_MODE_PASSIVE;
    status[54] = LOGI_DP;
    status[55] = LOGI_DM;
    badger_bytes_store_u32(status + 56u, b->errors);
    badger_bytes_store_u32(status + 60u, diag_crc32(status, 60u));
}

const char *logi_line_name(uint32_t state) {
    switch (state) {
    case 0u: return "SE0 (both low; no pull-up detected)";
    case 1u: return "D+ high (full-speed idle OR board R13)";
    case 2u: return "D- high (low-speed idle candidate)";
    case 3u: return "SE1 (both high; check mouse pull-up and R13)";
    default: return "invalid line state";
    }
}
