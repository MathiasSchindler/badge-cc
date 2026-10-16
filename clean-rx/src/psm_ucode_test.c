#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../src/tools/diagnostics_logic.h"
#include "../frame_limits.h"

#define WORD(op,a,b,d) (((uint64_t)(op) << 39u) | ((uint64_t)(a) << 26u) | \
                       ((uint64_t)(b) << 13u) | (d))

extern const uint32_t psm_ucode_byte_count;
extern const uint8_t psm_ucode_bytes[2408];

static uint64_t instruction(uint32_t pc) {
    assert(pc < 301u);
    uint64_t word = 0u;
    for (uint32_t i = 0u; i < 8u; ++i)
        word |= (uint64_t)psm_ucode_bytes[pc * 8u + i] << (8u * i);
    return word;
}

static uint16_t immediate(uint32_t operand) {
    assert(operand >= 0x1800u && operand <= 0x1fffu);
    uint16_t value = (uint16_t)(operand & 0x7ffu);
    return (uint16_t)((value & 0x400u) ? value | 0xf800u : value);
}

static void restore(uint8_t original[2360], uint32_t pc, uint64_t expected, uint64_t old) {
    assert(instruction(pc) == expected);
    for (uint32_t i = 0u; i < 8u; ++i)
        original[pc * 8u + i] = (uint8_t)(old >> (8u * i));
}

static void capture_layout(uint8_t original[2360]) {
    const uint32_t fault_regs[] = {0x100cu,0x102bu,0x100du,0x1002u};
    for (uint32_t i = 0u; i < 4u; ++i)
        assert(instruction(295u + i) == WORD(0x3f0u,fault_regs[i],0x1800u,0x620u + i));
    assert(instruction(299u) == WORD(0x1c0u,0x7abu,0x1801u,0x7abu));
    assert(instruction(300u) == WORD(0x77fu,0x1780u,0x1780u,266u));
    restore(original, 257u, WORD(0x77fu,0x1780u,0x1780u,295u),
            WORD(0x1c0u,0x7abu,0x1801u,0x7abu));
    for (uint32_t pc = 65u; pc <= 285u; pc += 220u) {
        restore(original, pc, WORD(0x3f0u,0x1800u | (CLEAN_SHM_FRAME & 255u),0x1800u,0x1782u),
                WORD(0x3f0u,0x18a0u,0x1800u,0x1782u));
        restore(original, pc + 1u, WORD(0x378u,0x1800u | (CLEAN_SHM_FRAME >> 8u),0x1782u,0x1782u),
                WORD(0x378u,0x1806u,0x1782u,0x1782u));
        restore(original, pc + 3u, WORD(0x3f0u,CLEAN_SHM_CAPTURE_LIMIT,0x1800u,0x1002u),
                WORD(0x3f0u,0x1a00u,0x1800u,0x1002u));
    }
    const uint32_t checks[] = {142u,156u,166u};
    for (uint32_t i = 0u; i < 3u; ++i) {
        uint32_t reg = i == 1u ? 0x1782u : 0x100cu;
        restore(original, checks[i], WORD(0xdcu,reg,CLEAN_SHM_RXE_LIMIT,0x103u),
                WORD(0xdcu,reg,0x19feu,0x103u));
    }
    for (uint32_t i = 0u; i < 3u; ++i) {
        restore(original, 147u + i, WORD(0x3f0u,CLEAN_SHM_FRAME + 1u + i,0x1800u,0x7acu + i),
                WORD(0x3f0u,0x6a1u + i,0x1800u,0x7acu + i));
        restore(original, 225u + i, WORD(0x3f0u,CLEAN_SHM_FRAME + 1u + i,0x1800u,0x7fau + i),
                WORD(0x3f0u,0x6a1u + i,0x1800u,0x7fau + i));
    }
    restore(original, 169u, WORD(0x40eu,CLEAN_SHM_FRAME + 4u,0x1800u,0xbfu),
            WORD(0x40eu,0x6a4u,0x1800u,0xbfu));
    restore(original, 172u, WORD(0x407u,CLEAN_SHM_FRAME + 4u,0x1800u,0xaeu),
            WORD(0x407u,0x6a4u,0x1800u,0xaeu));
    for (uint32_t length = 6u; length <= CLEAN_RXE_BYTES; ++length) {
        uint32_t words = (length + 3u) >> 1u;
        assert(words <= CLEAN_RAW_BYTES / 2u);
        assert(CLEAN_SHM_FRAME + words <= 0x600u);
    }
}

static int flush(uint32_t busy_polls, uint16_t busy_bits, uint32_t limit) {
    uint32_t pc = 81u, polls = 0u, fault = 0u;
    uint16_t counter = 0u, rxctl = 0u;
    for (uint32_t step = 0u; step < 400000u; ++step) {
        if (pc == 88u) return 1;
        if (pc == 290u) {
            assert(fault == 3u);
            return 0;
        }
        assert(pc >= 81u && pc <= 87u);
        uint64_t word = instruction(pc);
        uint32_t op = (uint32_t)(word >> 39u);
        uint32_t a = (uint32_t)(word >> 26u) & 0x1fffu;
        uint32_t b = (uint32_t)(word >> 13u) & 0x1fffu;
        uint32_t d = (uint32_t)word & 0x1fffu;
        if (pc == 82u) rxctl = (uint16_t)(0x0800u | (polls++ < busy_polls ? busy_bits : 0u));
        uint32_t next = pc + 1u;
        if (op == 0x3f0u) {
            assert(b == 0x1800u);
            if (d == 0x1780u) counter = (uint16_t)limit;
            else {
                assert(d == 0x602u);
                fault = immediate(a);
            }
        } else if (op == 0x502u || op == 0x404u) {
            assert(a == 0x1004u && b == 0x1800u);
            uint32_t bit = (rxctl >> (op & 15u)) & 1u;
            if (bit == (op >> 8u == 5u)) next = d;
        } else if (op == 0x1d0u) {
            assert(a == 0x1780u && b == 0x1801u && d == 0x1780u);
            --counter;
        } else if (op == 0xd1u) {
            assert(a == 0x1780u && b == 0x1800u);
            if (counter != 0u) next = d;
        } else {
            assert(op == 0x77fu && a == 0x1780u && b == 0x1780u);
            next = d;
        }
        pc = next;
    }
    assert(!"PSM flush did not terminate");
    return 0;
}

int main(void) {
    assert(psm_ucode_byte_count == sizeof(psm_ucode_bytes));
    assert(instruction(81u) == instruction(105u));
    uint32_t limit = immediate((uint32_t)(instruction(81u) >> 26u) & 0x1fffu);
    assert(limit == 65535u);
    uint8_t original[2360];
    memcpy(original, psm_ucode_bytes, sizeof(original));
    static const uint8_t old_counter[8] = {0x80,0x17,0x00,0xff,0x63,0xf8,0x01,0x00};
    memcpy(original + 81u * 8u, old_counter, sizeof(old_counter));
    capture_layout(original);
    assert(diag_crc32(original, sizeof(original)) == 0x227ce2bfu);
    for (uint16_t bits = 0u; bits < 32u; ++bits) {
        uint32_t delays[] = {0u, 254u, 255u, 256u, 4096u, 65534u, 65535u, 65536u};
        for (uint32_t i = 0u; i < sizeof(delays) / sizeof(delays[0]); ++i) {
            assert(flush(delays[i], bits, limit) ==
                   ((bits & 0x14u) == 0u || delays[i] < limit));
            assert(flush(delays[i], bits, 255u) ==
                   ((bits & 0x14u) == 0u || delays[i] < 255u));
        }
    }
    puts("PSM: image identity, relocated capture, SHM length operands and startup flush OK");
    return 0;
}
