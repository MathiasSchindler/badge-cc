#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../src/tools/diagnostics_logic.h"
#include "frame_limits.h"
#include "psm_ack.h"

#define WORD(op,a,b,d) (((uint64_t)(op) << 39u) | ((uint64_t)(a) << 26u) | \
                       ((uint64_t)(b) << 13u) | (d))

extern const uint32_t psm_ucode_byte_count;
extern const uint8_t psm_ucode_bytes[ACK_PROGRAM_END * 8u];

static uint64_t instruction(uint32_t pc) {
    assert(pc < ACK_PROGRAM_END);
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

static void operand_limits(void) {
    for (uint32_t pc = 0u; pc < ACK_PROGRAM_END; ++pc) {
        uint64_t w = instruction(pc);
        uint32_t a = (uint32_t)(w >> 26u) & 0x1fffu;
        uint32_t b = (uint32_t)(w >> 13u) & 0x1fffu;
        int a_memory = a < 0x1000u || (a >= 0x1400u && a < 0x1780u);
        int b_memory = b < 0x1000u || (b >= 0x1400u && b < 0x1780u);
        if (a_memory && b_memory)
            fprintf(stderr, "PSM PC %u has two memory inputs: %04x %04x\n", pc, a, b);
        assert(!(a_memory && b_memory));
    }
}

static void restore(uint8_t original[2360], uint32_t pc, uint64_t expected, uint64_t old) {
    assert(instruction(pc) == expected);
    for (uint32_t i = 0u; i < 8u; ++i)
        original[pc * 8u + i] = (uint8_t)(old >> (8u * i));
}

static void capture_layout(uint8_t original[2360]) {
    restore(original, 197u, WORD(0x77fu,0x1780u,0x1780u,ACK_ENTER),
            WORD(0x3f0u,0x180fu,0x1800u,0x607u));
    restore(original, 191u, WORD(0x77fu,0x1780u,0x1780u,309u),
            WORD(0x606u,0x1780u,0x1780u,194u));
    assert(instruction(309u) == WORD(0x501u,0x1004u,0x1800u,312u));
    assert(instruction(310u) ==
           WORD(0xdau,0x100cu,0x1800u | CLEAN_RX_STREAM_PREFIX_BYTES,312u));
    assert(instruction(311u) == WORD(0x310u,0x1802u,0x1004u,0x1004u));
    assert(instruction(312u) == WORD(0x606u,0x1780u,0x1780u,194u));
    assert(instruction(313u) == WORD(0x77fu,0x1780u,0x1780u,192u));
    const uint32_t fault_regs[] = {0x100cu,0x102bu,0x100du,0x1002u,
                                  0x607u,0x1004u,0x102au,0x7a2u,
                                  CLEAN_SHM_FRAME + 1u,CLEAN_SHM_FRAME + 2u,
                                  CLEAN_SHM_FRAME + 3u,0x100eu};
    for (uint32_t i = 0u; i < CLEAN_RX_FAULT_WORDS; ++i)
        assert(instruction(295u + i) == WORD(0x3f0u,fault_regs[i],0x1800u,0x620u + i));
    assert(instruction(307u) == WORD(0x1c0u,0x7abu,0x1801u,0x7abu));
    assert(instruction(308u) == WORD(0x77fu,0x1780u,0x1780u,266u));
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

static uint16_t stream_prefix(uint16_t rxctl, uint16_t count, int complete,
                              uint32_t *writes, uint32_t *next) {
    uint32_t pc = 191u;
    *writes = 0u;
    for (uint32_t step = 0u; step < 8u; ++step) {
        if (pc == 192u || pc == 194u) {
            *next = pc;
            return rxctl;
        }
        uint64_t w = instruction(pc);
        uint32_t op = (uint32_t)(w >> 39u);
        uint32_t a = (uint32_t)(w >> 26u) & 0x1fffu;
        uint32_t b = (uint32_t)(w >> 13u) & 0x1fffu;
        uint32_t d = (uint32_t)w & 0x1fffu;
        ++pc;
        if (op == 0x77fu) pc = d;
        else if (op == 0x501u) {
            assert(a == 0x1004u && b == 0x1800u);
            if ((rxctl & 2u) != 0u) pc = d;
        } else if (op == 0xdau) {
            assert(a == 0x100cu);
            if (count < immediate(b)) pc = d;
        } else if (op == 0x310u) {
            assert(b == 0x1004u && d == 0x1004u);
            rxctl = (uint16_t)((rxctl & ~3u) | (immediate(a) & 3u));
            ++*writes;
        } else {
            assert(op == 0x606u && a == 0x1780u && b == 0x1780u);
            if (!complete) pc = d;
        }
    }
    assert(!"RX streaming prefix did not terminate");
    return 0u;
}

static void streaming(void) {
    for (uint32_t count = 0u; count <= CLEAN_RXE_BYTES; ++count) {
        for (uint32_t state = 0u; state < 256u; ++state) {
            uint16_t before = (uint16_t)(0x2800u | state);
            int enable = (before & 2u) == 0u && count >= CLEAN_RX_STREAM_PREFIX_BYTES;
            for (int complete = 0; complete <= 1; ++complete) {
                uint32_t writes, next;
                uint16_t after = stream_prefix(before, (uint16_t)count, complete, &writes, &next);
                assert(after == (enable ? (uint16_t)((before & ~3u) | 2u) : before));
                assert(writes == (uint32_t)enable);
                assert(next == (complete ? 192u : 194u));
                uint32_t again;
                assert(stream_prefix(after, (uint16_t)count, complete, &again, &next) == after);
                assert(again == 0u);
            }
        }
    }
}

static uint16_t ack_operand(const uint16_t regs[0x1800], uint32_t operand) {
    return operand >= 0x1800u ? immediate(operand) : regs[operand];
}

static uint32_t ack_run(uint16_t regs[0x1800], int tx_now) {
    uint32_t pc = ACK_ENTER;
    for (uint32_t step = 0u; step < 10000u; ++step) {
        if (pc == 198u || pc == 266u) return pc;
        assert(pc >= ACK_ENTER && pc < ACK_PROGRAM_END);
        uint64_t w = instruction(pc++);
        uint32_t op = (uint32_t)(w >> 39u);
        uint32_t a = (uint32_t)(w >> 26u) & 0x1fffu;
        uint32_t b = (uint32_t)(w >> 13u) & 0x1fffu;
        uint32_t d = (uint32_t)w & 0x1fffu;
        uint16_t av = ack_operand(regs, a), bv = ack_operand(regs, b);
        if (op == 0x3f0u) regs[d] = av;
        else if (op == 0x378u) regs[d] = (uint16_t)((av << 8u) | (bv & 255u));
        else if (op == 0x77fu) pc = d;
        else if (op == 0xd0u) { if (av == bv) pc = d; }
        else if (op == 0xd1u) { if (av != bv) pc = d; }
        else if (op == 0xdau) { if (av < bv) pc = d; }
        else if (op == 0x1c0u) regs[d] = (uint16_t)(av + bv);
        else if (op == 0x1d0u) regs[d] = (uint16_t)(av - bv);
        else if ((op & 0xf00u) == 0x400u || (op & 0xf00u) == 0x500u) {
            if (((av >> (op & 15u)) & 1u) == (op >> 8u == 5u)) pc = d;
        } else if (op == 0x230u || op == 0x270u) {
            regs[d] = (uint16_t)(av & (op == 0x230u ? 15u : 255u));
        } else if (op == 0x304u || op == 0x308u) {
            uint16_t mask = (uint16_t)(1u << (op & 15u));
            regs[d] = (uint16_t)((bv & ~mask) | ((av << (op & 15u)) & mask));
        } else {
            assert(op == 0x7a0u);
            if (tx_now) pc = d;
        }
    }
    assert(!"ACK path failed to terminate");
    return 0u;
}

static void acknowledgements(void) {
    uint16_t regs[0x1800], base_regs[0x1800] = {0};
    base_regs[CLEAN_ACK_ENABLE] = 1u;
    base_regs[0x100c] = 40u;
    for (uint32_t i = 0u; i < 3u; ++i) {
        base_regs[0x612u + i] = (uint16_t)(0x1002u + i);
        base_regs[CLEAN_SHM_FRAME + 6u + i] = base_regs[0x612u + i];
        base_regs[CLEAN_SHM_FRAME + 9u + i] = (uint16_t)(0x2002u + i);
        base_regs[CLEAN_ACK_PEER + i] = base_regs[CLEAN_SHM_FRAME + 9u + i];
    }
    static const uint8_t cck[4] = {10u,20u,55u,110u};
    for (uint32_t rate = 0u; rate < 12u; ++rate) {
        memcpy(regs, base_regs, sizeof(regs));
        uint8_t plcp[6];
        uint16_t phy;
        int response = clean_ack_rate(0xfffu, rate);
        assert(clean_ack_template(plcp, (uint32_t)response, &phy));
        for (uint32_t i = 0u; i < 3u; ++i)
            regs[CLEAN_ACK_TABLE + 4u*rate + i] =
                (uint16_t)(plcp[2u*i] | ((uint16_t)plcp[2u*i + 1u] << 8u));
        regs[CLEAN_ACK_TABLE + 4u*rate + 3u] = phy;
        uint16_t airtime = response >= 4 ?
            (uint16_t)clean_ofdm_airtime_us(14u, (uint32_t)response - 4u) :
            (uint16_t)(192u + plcp[2]);
        regs[CLEAN_ACK_AIRTIME + rate] = airtime;
        regs[CLEAN_SHM_FRAME + 4u] = 0x08b0u;
        regs[0x100e] = rate >= 4u ? 0x2000u : 0u;
        regs[CLEAN_SHM_FRAME + 1u] = rate >= 4u ?
            (uint16_t)(0xf0u | clean_ofdm_rates[rate - 4u].signal) : cck[rate];
        ack_run(regs, 1);
        assert(regs[CLEAN_ACK_SENT] == 1u && !regs[CLEAN_ACK_ERROR]);
        assert(regs[CLEAN_ACK_KINDS] == 1u);
        assert(regs[0x108a] == phy && regs[0x1086] == (rate >= 4u ? 0x41u : 0x40u));
        assert(regs[0x10b5] == 16u && regs[0x10b4] == 0x0826u);
        assert(regs[0x1080] == 0x4021u && regs[0x1084] == 255u);
        assert(regs[0x10e3] == 0xd4u && regs[0x10e4] == 0u);
        for (uint32_t i = 0u; i < 3u; ++i)
            assert(regs[0x10e5u + i] == base_regs[CLEAN_SHM_FRAME + 9u + i]);
        for (uint32_t i = 0u; i < 8u; ++i) assert(regs[0x10c0u + i] == 65535u);
        regs[CLEAN_ACK_SENT] = 0u;
        regs[CLEAN_SHM_FRAME + 4u] |= 0x400u;
        regs[CLEAN_SHM_FRAME + 15u] = 7u;
        regs[CLEAN_SHM_FRAME + 5u] = 1000u;
        ack_run(regs, 1);
        assert(regs[CLEAN_ACK_SENT] == 1u);
        assert(regs[0x10e4u] == 1000u - 10u - airtime);
        regs[CLEAN_SHM_FRAME + 5u] = 1u;
        ack_run(regs, 1);
        assert(regs[0x10e4u] == 0u);
        for (uint32_t short_rx = 0u; short_rx < 2u; ++short_rx) {
            for (uint32_t rts = 0u; rts < 2u; ++rts) {
                regs[CLEAN_RESPONSE_POLICY] = 3u;
                regs[CLEAN_CTS_ALLOWED] = 1u;
                regs[CLEAN_ACK_SENT] = regs[CLEAN_CTS_SENT] = regs[CLEAN_SHORT_SENT] = 0u;
                regs[CLEAN_SHM_FRAME + 4u] = rts ? 0xb4u : 0x04b0u;
                regs[CLEAN_SHM_FRAME + 5u] = 1000u;
                regs[0x100c] = rts ? 26u : 40u;
                regs[0x1009] = short_rx ? 0x80u : 0u;
                uint32_t end = ack_run(regs, 1);
                uint32_t short_tx = short_rx && response > 0 && response < 4;
                assert(end == (rts ? 266u : 198u));
                assert(regs[CLEAN_ACK_SENT] == !rts && regs[CLEAN_CTS_SENT] == rts);
                assert(regs[CLEAN_SHORT_SENT] == short_tx);
                assert(regs[0x1086] == (rate >= 4u ? 0x41u : short_tx ? 0x50u : 0x40u));
                assert(regs[0x10e3] == (rts ? 0xc4u : 0xd4u));
                assert(regs[0x10e4] == 1000u - 10u - airtime + (short_tx ? 96u : 0u));
            }
        }
        if (rate < 4u) {
            uint8_t fallback[6];
            uint16_t fallback_phy;
            assert(clean_ack_template(fallback, 0u, &fallback_phy));
            for (uint32_t i = 0u; i < 3u; ++i)
                regs[CLEAN_ACK_TABLE + 4u*rate + i] =
                    (uint16_t)(fallback[2u*i] | ((uint16_t)fallback[2u*i+1u] << 8u));
            regs[CLEAN_ACK_TABLE + 4u*rate + 3u] = fallback_phy;
            regs[CLEAN_ACK_AIRTIME + rate] = 304u;
            regs[CLEAN_CTS_SENT] = regs[CLEAN_SHORT_SENT] = 0u;
            assert(ack_run(regs, 1) == 266u && regs[CLEAN_CTS_SENT] == 1u);
            assert(regs[0x1086] == 0x40u && regs[0x108a] == 2u && !regs[CLEAN_SHORT_SENT]);
            assert(regs[0x10e4] == 686u);
        }
        for (uint32_t fault = 0u; fault < 7u; ++fault) {
            regs[CLEAN_RESPONSE_POLICY] = fault == 0u ? 0u : 3u;
            regs[CLEAN_CTS_ALLOWED] = fault == 1u ? 0u : 1u;
            regs[CLEAN_ACK_SENT] = regs[CLEAN_CTS_SENT] = 0u;
            regs[CLEAN_SHM_FRAME + 4u] = 0xb4u;
            regs[CLEAN_SHM_FRAME + 5u] = fault == 2u ? 0x8000u : fault == 3u ? 1u : 1000u;
            regs[0x100c] = fault == 4u ? 25u : fault == 5u ? 27u : 26u;
            if (fault == 6u) regs[CLEAN_SHM_FRAME + 9u] ^= 1u;
            assert(ack_run(regs, 1) == 198u);
            assert(!regs[CLEAN_ACK_SENT] && !regs[CLEAN_CTS_SENT]);
            if (fault == 6u) regs[CLEAN_SHM_FRAME + 9u] ^= 1u;
        }
    }
    for (uint32_t kind = 0u; kind < 10u; ++kind) {
        memcpy(regs, base_regs, sizeof(regs));
        regs[CLEAN_SHM_FRAME + 1u] = 10u;
        if (kind == 0u) regs[CLEAN_ACK_ENABLE] = 0u;
        if (kind == 1u) regs[CLEAN_SHM_FRAME + 6u] ^= 1u;
        if (kind == 2u) regs[CLEAN_SHM_FRAME + 4u] = 0xd4u;
        if (kind == 3u) regs[CLEAN_SHM_FRAME + 4u] = 0x88u;
        if (kind == 4u) regs[CLEAN_SHM_FRAME + 4u] = 1u;
        if (kind == 5u) regs[CLEAN_SHM_FRAME + 4u] = 0xb4u;
        if (kind == 6u) regs[0x100c] = 33u;
        if (kind == 7u) regs[CLEAN_SHM_FRAME + 1u] = 0u;
        if (kind == 8u) regs[CLEAN_SHM_FRAME + 4u] = 0xe0u;
        if (kind == 9u) regs[CLEAN_SHM_FRAME + 9u] ^= 1u;
        ack_run(regs, 1);
        assert(!regs[CLEAN_ACK_SENT] && !regs[CLEAN_ACK_SEEN]);
    }
    memcpy(regs, base_regs, sizeof(regs));
    regs[CLEAN_SHM_FRAME + 1u] = 10u;
    ack_run(regs, 0);
    assert(!regs[CLEAN_ACK_SENT] && regs[CLEAN_ACK_ERROR] == 1u && !regs[0x1080]);
    memcpy(regs, base_regs, sizeof(regs));
    regs[CLEAN_SHM_FRAME + 4u] = 0xd4u;
    regs[0x100c] = 20u;
    regs[CLEAN_ACK_WAITING] = 1u;
    ack_run(regs, 1);
    assert(regs[CLEAN_ACK_SEEN] == 1u && !regs[CLEAN_ACK_SENT]);
    const uint8_t frame_controls[] = {0xb0u,0x10u,8u,0x48u,0xb4u};
    for (uint32_t kind = 0u; kind < sizeof(frame_controls); ++kind) {
        for (uint32_t mismatch = 0u; mismatch <= 96u; ++mismatch) {
            memcpy(regs, base_regs, sizeof(regs));
            regs[CLEAN_SHM_FRAME + 1u] = 10u;
            regs[CLEAN_RESPONSE_POLICY] = 3u | CLEAN_RESPONSE_INTERNAL_ACK;
            regs[CLEAN_CTS_ALLOWED] = 1u;
            regs[CLEAN_ACK_AIRTIME] = 304u;
            regs[CLEAN_SHM_FRAME + 5u] = 1000u;
            regs[CLEAN_SHM_FRAME + 4u] = frame_controls[kind];
            regs[0x100c] = kind == 4u ? 26u : 40u;
            if (mismatch)
                regs[CLEAN_SHM_FRAME + 6u + (mismatch - 1u) / 16u] ^=
                    (uint16_t)(1u << ((mismatch - 1u) & 15u));
            ack_run(regs, 1);
            assert(regs[CLEAN_ACK_SENT] == (uint16_t)(!mismatch && kind != 4u));
            assert(regs[CLEAN_CTS_SENT] == (uint16_t)(!mismatch && kind == 4u));
            for (uint32_t i = 0u; i < 4u; ++i)
                assert(regs[CLEAN_ACK_KINDS + i] ==
                       (uint16_t)(!mismatch && kind != 4u && kind == i));
        }
    }
    for (uint32_t waiting = 0u; waiting < 2u; ++waiting) {
        for (uint32_t fault = 0u; fault < 7u; ++fault) {
            memcpy(regs, base_regs, sizeof(regs));
            regs[CLEAN_RESPONSE_POLICY] = 3u | CLEAN_RESPONSE_INTERNAL_ACK;
            regs[CLEAN_SHM_FRAME + 4u] = 0xd4u;
            regs[0x100c] = fault == 1u ? 19u : fault == 2u ? 21u : 20u;
            regs[CLEAN_ACK_WAITING] = (uint16_t)waiting;
            regs[CLEAN_ACK_CONSUMED] = 65535u;
            if (fault >= 3u && fault <= 5u) regs[CLEAN_SHM_FRAME + 3u + fault] ^= 1u;
            if (fault == 6u) regs[CLEAN_ACK_ENABLE] = 0u;
            uint32_t end = ack_run(regs, 1);
            int consumed = fault == 0u;
            assert(end == (consumed ? 266u : 198u));
            assert(regs[CLEAN_ACK_CONSUMED] == (consumed ? 0u : 65535u));
            assert(regs[CLEAN_ACK_SEEN] == (uint16_t)(consumed && waiting));
            assert(!regs[CLEAN_ACK_SENT]);
            /* The short ACK must never enter DMA/capture publication. */
            assert(!regs[0x1007] && !regs[0x1008] && !regs[0x1004]);
        }
    }
    memcpy(regs, base_regs, sizeof(regs));
    regs[CLEAN_SHM_FRAME + 4u] = 0xd4u;
    regs[0x100c] = 20u;
    regs[CLEAN_RESPONSE_POLICY] = 3u | CLEAN_RESPONSE_INTERNAL_ACK;
    for (uint32_t i = 0u; i < 100000u; ++i) {
        regs[CLEAN_ACK_WAITING] = (uint16_t)(i & 1u);
        regs[CLEAN_ACK_SEEN] = 0u;
        assert(ack_run(regs, 1) == 266u);
        assert(regs[CLEAN_ACK_SEEN] == (i & 1u));
        assert(!regs[0x1004] && !regs[0x1007] && !regs[0x1008]);
    }
    assert(regs[CLEAN_ACK_CONSUMED] == (uint16_t)100000u);
}

int main(void) {
    assert(psm_ucode_byte_count == sizeof(psm_ucode_bytes));
    operand_limits();
    assert(instruction(118u) == WORD(0x3f0u,0x1803u,0x1800u,0x601u));
    assert(instruction(193u) == WORD(0x703u,0x1780u,0x1780u,197u));
    assert(instruction(264u) == WORD(0x1c0u,0x7a8u,0x1801u,0x7a8u));
    assert(instruction(260u) == WORD(0x1c0u,0x7a9u,0x1801u,0x7a9u));
    assert(instruction(81u) == instruction(105u));
    uint32_t limit = immediate((uint32_t)(instruction(81u) >> 26u) & 0x1fffu);
    assert(limit == 65535u);
    uint8_t original[2360];
    memcpy(original, psm_ucode_bytes, sizeof(original));
    static const uint8_t old_counter[8] = {0x80,0x17,0x00,0xff,0x63,0xf8,0x01,0x00};
    memcpy(original + 81u * 8u, old_counter, sizeof(old_counter));
    capture_layout(original);
    assert(diag_crc32(original, sizeof(original)) == 0x227ce2bfu);
    streaming();
    acknowledgements();
    for (uint16_t bits = 0u; bits < 32u; ++bits) {
        uint32_t delays[] = {0u, 254u, 255u, 256u, 4096u, 65534u, 65535u, 65536u};
        for (uint32_t i = 0u; i < sizeof(delays) / sizeof(delays[0]); ++i) {
            assert(flush(delays[i], bits, limit) ==
                   ((bits & 0x14u) == 0u || delays[i] < limit));
            assert(flush(delays[i], bits, 255u) ==
                   ((bits & 0x14u) == 0u || delays[i] < 255u));
        }
    }
    puts("PSM: image identity, capture bounds, RX streaming and startup flush OK");
    puts("PSM: single-SHM inputs, pinned ACK/CTS addresses and 100000 native ACK rearms OK; NOT RF timing");
    return 0;
}
