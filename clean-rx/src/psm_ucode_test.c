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
    uint32_t phy_latch = 0u, held_capture = 0u;
    for (uint32_t pc = 198u; pc < 260u; ++pc) {
        if (instruction(pc) == WORD(0x3f0u,0x1009u,0x1800u,CLEAN_SHM_PHY_STATUS0))
            phy_latch = pc;
        if (instruction(pc) == WORD(0x3f0u,0x180eu,0x1800u,0x601u))
            held_capture = pc;
    }
    assert(phy_latch > 198u && phy_latch < 225u && phy_latch < held_capture);
    restore(original, 262u, WORD(0x77fu,0x1780u,0x1780u,RX_TRACE_BAD),
            WORD(0x3f0u,0x100cu,0x1800u,0x7a5u));
    restore(original, 197u, WORD(0x77fu,0x1780u,0x1780u,HT_TRACE_GOOD),
            WORD(0x3f0u,0x180fu,0x1800u,0x607u));
    restore(original, 123u, WORD(0x77fu,0x1780u,0x1780u,DCF_IDLE),
            WORD(0x78eu,0x1780u,0x1780u,130u));
    restore(original, 128u, WORD(0x77fu,0x1780u,0x1780u,DCF_IDLE_EXPIRED),
            WORD(0x3f0u,0x1806u,0x1800u,0x602u));
    restore(original, 264u, WORD(0x77fu,0x1780u,0x1780u,DCF_BAD),
            WORD(0x1c0u,0x7a8u,0x1801u,0x7a8u));
    restore(original, 260u, WORD(0x77fu,0x1780u,0x1780u,DCF_BAD_LENGTH),
            WORD(0x1c0u,0x7a9u,0x1801u,0x7a9u));
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
    assert(instruction(308u) == WORD(0x77fu,0x1780u,0x1780u,
                                   CLEAN_RX_HT_RESPONSE ? HT_FAIL_ABORT : 266u));
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
        restore(original, 147u + i, i == 0u ? WORD(0x77fu,0x1780u,0x1780u,HT_TRACE_ENTER) :
                WORD(0x3f0u,CLEAN_SHM_FRAME + 1u + i,0x1800u,0x7acu + i),
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
        assert(CLEAN_SHM_FRAME + words <= CLEAN_SHM_FRAME_END);
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

static uint32_t psm_run(uint16_t regs[0x1800], int events, uint32_t pc) {
    for (uint32_t step = 0u; step < 10000u; ++step) {
        if (pc == 198u || pc == 266u || pc == 124u || pc == 130u ||
            pc == 118u || pc == 129u ||
            pc == 148u || pc == 263u) return pc;
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
        else if (op == 0xdcu) { if (av > bv) pc = d; }
        else if (op == 0x1c0u) regs[d] = (uint16_t)(av + bv);
        else if (op == 0x1d0u) regs[d] = (uint16_t)(av - bv);
        else if (op == 0x278u) regs[d] = av >> 8u;
        else if (op == 0x140u) regs[d] = av & bv;
        else if (op == 0x78eu) { if (events & 2u) pc = d; }
        else if ((op & 0xf00u) == 0x400u || (op & 0xf00u) == 0x500u) {
            if (((av >> (op & 15u)) & 1u) == (op >> 8u == 5u)) pc = d;
        } else if (op == 0x230u || op == 0x270u) {
            regs[d] = (uint16_t)(av & (op == 0x230u ? 15u : 255u));
        } else if (op == 0x304u || op == 0x308u) {
            uint16_t mask = (uint16_t)(1u << (op & 15u));
            regs[d] = (uint16_t)((bv & ~mask) | ((av << (op & 15u)) & mask));
        } else {
            assert(op == 0x7a0u);
            if (events & 1u) pc = d;
        }
    }
    assert(!"ACK path failed to terminate");
    return 0u;
}

static uint32_t ack_run(uint16_t regs[0x1800], int tx_now) {
    return psm_run(regs, tx_now, ACK_ENTER);
}

static void idle_watchdog(void) {
    uint16_t r[0x1800];
    const uint32_t old_poll_budget = 64u * 65535u;
    for (uint32_t enabled = 0u; enabled < 2u; ++enabled) {
        memset(r, 0, sizeof(r));
        r[CLEAN_DCF_ENABLE] = (uint16_t)enabled;
        r[CLEAN_DCF_SLOT_US] = 9u;
        r[CLEAN_DCF_DIFS_US] = 28u;
        r[0x1148u] = 15u;
        uint32_t pc = 118u, polls = 0u;
        while (pc != 130u && pc != 290u && polls <= old_poll_budget) {
            if (pc == DCF_IDLE) {
                r[0x1119u] = (uint16_t)++polls;
                pc = psm_run(r, 0, pc);
                continue;
            }
            if (pc == DCF_IDLE_EXPIRED) {
                pc = psm_run(r, 0, pc);
                continue;
            }
            assert(pc >= 118u && pc <= 129u);
            uint64_t w = instruction(pc++);
            uint32_t op = (uint32_t)(w >> 39u);
            uint32_t a = (uint32_t)(w >> 26u) & 0x1fffu;
            uint32_t b = (uint32_t)(w >> 13u) & 0x1fffu;
            uint32_t d = (uint32_t)w & 0x1fffu;
            uint16_t av = ack_operand(r, a), bv = ack_operand(r, b);
            if (op == 0x3f0u) r[d] = av;
            else if (op == 0x1d0u) r[d] = (uint16_t)(av - bv);
            else if (op == 0xd1u) { if (av != bv) pc = d; }
            else if (op == 0x77fu) pc = d;
            else assert(op == 0x78du || op == 0x785u);
        }
        if (enabled) {
            assert(polls == old_poll_budget + 1u && r[0x601u] == 3u && !r[0x602u]);
            assert(!r[0x1081u] && !r[CLEAN_DCF_REQUEST]);
            assert(psm_run(r, 2, DCF_IDLE) == 130u);
        } else {
            assert(pc == 290u && polls == old_poll_budget && r[0x602u] == 6u);
        }
    }
    puts("PSM idle: full outer watchdog expires for one-shot RX, stays armed for live DCF and still accepts RX; NOT RF");
}

static void qos_ack_base(uint16_t r[0x1800], uint32_t mcs, uint32_t length) {
    memset(r, 0, 0x1800u * sizeof(*r));
    r[CLEAN_ACK_ENABLE] = 1u;
    r[CLEAN_QOS_RESPONSE_POLICY] = 3u;
    r[CLEAN_RESPONSE_POLICY] = 3u;
    r[0x1009u] = 2u;
    r[0x100cu] = (uint16_t)(length + 6u);
    r[CLEAN_SHM_FRAME + 1u] = (uint16_t)(mcs | (length << 8u));
    r[CLEAN_SHM_FRAME + 2u] = (uint16_t)(((length & 1u) ? 0x0600u : 0x0700u) |
                                                    (length >> 8u));
    r[CLEAN_SHM_FRAME + 4u] = 0x88u;
    for (uint32_t i = 0u; i < 3u; ++i) {
        r[CLEAN_SHM_FRAME + 6u + i] = r[0x612u + i] = (uint16_t)(0x1102u + i);
        r[CLEAN_SHM_FRAME + 9u + i] = r[CLEAN_ACK_PEER + i] = (uint16_t)(0x2202u + i);
    }
}

static void ht_qos_acknowledgements(void) {
    uint16_t r[0x1800], base[0x1800];
    static const uint8_t rates[] = {2u,4u,11u,22u,12u,18u,24u,36u,48u,72u,96u,108u};
    static const uint8_t reference[] = {12u,24u,36u,48u,72u,96u,108u,108u};
    assert(clean_ht_ack_rate(0u, 0u) < 0 && clean_ht_ack_rate(0x1000u, 0u) < 0);
    assert(clean_ht_ack_rate(1u, 8u) < 0);
    for (uint32_t profile = 0u; profile < 5u; ++profile)
        for (uint32_t policy = 0u; policy < 256u; ++policy) {
            uint32_t command = CLEAN_HT_RESPONSE_CMD | (policy << 8u);
            assert(clean_ht_response_policy(command, 1u, profile) ==
                   (profile == 3u && policy < 4u ? (int)policy : -1));
            assert(clean_ht_response_policy(command, 0u, profile) < 0);
            assert(clean_ht_response_policy(command | (1u << 24u), 1u, profile) < 0);
            assert(clean_ht_response_policy(command ^ 1u, 1u, profile) < 0);
        }
    for (uint32_t mcs = 0u; mcs < 8u; ++mcs) {
        qos_ack_base(base, mcs, 60u);
        for (uint32_t basic = 1u; basic < 4096u; ++basic) {
            int rate = clean_ht_ack_rate((uint16_t)basic, mcs), expected = -1;
            for (uint32_t speed = 1u; speed <= reference[mcs]; ++speed)
                for (uint32_t i = 0u; i < 12u; ++i)
                    if ((basic & (1u << i)) && rates[i] == speed) expected = (int)i;
            if (expected < 0) expected = mcs == 0u ? 4 : mcs < 3u ? 6 : 8;
            assert(rate == expected);
            uint8_t plcp[6];
            uint16_t phy;
            assert(clean_ack_template(plcp, (uint32_t)rate, &phy));
            uint32_t at = CLEAN_HT_ACK_TABLE + 6u*mcs;
            memcpy(r, base, sizeof(r));
            for (uint32_t i = 0u; i < 3u; ++i)
                r[at + i] = (uint16_t)(plcp[2u*i] | ((uint16_t)plcp[2u*i+1u] << 8u));
            r[at + 3u] = phy;
            r[at + 4u] = rate < 4 ? 0x40u : 0x41u;
            r[at + 5u] = (uint16_t)(rate < 4 ?
                clean_cck_airtime_us(14u, (uint32_t)rate, 0u) :
                clean_ofdm_airtime_us(14u, (uint32_t)rate - 4u));
            assert(ack_run(r, 1) == 198u && r[CLEAN_ACK_SENT] == 1u);
            assert(r[0x108au] == phy && r[0x1086u] == r[at + 4u]);
            assert(!r[CLEAN_SHORT_SENT] && r[0x10e3u] == 0xd4u);
            for (uint32_t i = 0u; i < 3u; ++i)
                assert(r[0x10e0u + i] == r[at + i] &&
                       r[0x10e5u + i] == r[CLEAN_ACK_PEER + i]);
        }
        for (uint32_t length = 30u; length <= CLEAN_MPDU_BYTES; ++length) {
            qos_ack_base(r, mcs, length);
            assert(ack_run(r, 1) == 198u && r[CLEAN_ACK_SENT] == 1u);
            r[CLEAN_ACK_SENT] = 0u;
            r[0x100cu] ^= 1u;
            assert(ack_run(r, 1) == 198u && !r[CLEAN_ACK_SENT]);
        }
    }
    qos_ack_base(base, 0u, 60u);
    for (uint32_t fault = 0u; fault < 19u; ++fault) {
        memcpy(r, base, sizeof(r));
        if (fault < 2u) r[CLEAN_QOS_RESPONSE_POLICY] ^= (uint16_t)(1u << fault);
        if (fault == 2u) r[0x1009u] = 3u;
        if (fault == 3u) r[CLEAN_SHM_FRAME + 1u] |= 8u;
        if (fault == 4u) r[CLEAN_SHM_FRAME + 1u] |= 128u;
        if (fault >= 5u && fault <= 11u)
            r[CLEAN_SHM_FRAME + 2u] ^= (uint16_t)(1u << (fault + 4u));
        if (fault == 12u) r[CLEAN_SHM_FRAME + 3u] |= 1u;
        if (fault == 13u) r[CLEAN_SHM_FRAME + 3u] |= 2u;
        if (fault == 14u) r[CLEAN_SHM_FRAME + 3u] |= 0xfc00u;
        if (fault == 15u) r[CLEAN_SHM_FRAME + 4u] |= 0x300u;
        if (fault == 16u) r[CLEAN_SHM_FRAME + 4u] |= 0x8000u;
        if (fault == 17u) r[CLEAN_SHM_FRAME + 6u] ^= 1u;
        if (fault == 18u) r[CLEAN_SHM_FRAME + 9u] ^= 1u;
        assert(ack_run(r, 1) == 198u && !r[CLEAN_ACK_SENT] && !r[CLEAN_CTS_SENT]);
    }
    for (uint32_t format = 0u; format < 3u; ++format)
        for (uint32_t subtype = 0x88u; subtype <= 0xf8u; subtype += 16u)
            for (uint32_t qos = 0u; qos < 256u; ++qos) {
                memcpy(r, base, sizeof(r));
                r[0x1009u] = (uint16_t)format;
                if (format < 2u) r[CLEAN_SHM_FRAME + 1u] = format ? 0xbu : 10u;
                r[CLEAN_SHM_FRAME + 4u] = (uint16_t)subtype;
                r[CLEAN_SHM_FRAME + 16u] = (uint16_t)qos;
                assert(ack_run(r, 1) == 198u);
                assert(r[CLEAN_ACK_SENT] == (uint16_t)(subtype == 0x88u && !(qos & 0x60u)));
            }
    for (uint32_t format = 0u; format < 3u; ++format) {
        qos_ack_base(r, 0u, 30u);
        r[0x1009u] = (uint16_t)format;
        if (format < 2u) r[CLEAN_SHM_FRAME + 1u] = format ? 0xbu : 10u;
        r[CLEAN_SHM_FRAME + 4u] = 0xc8u;
        assert(ack_run(r, 1) == 198u && r[CLEAN_ACK_SENT] == 1u);
    }
    puts("PSM QoS/HT ACK: all basic sets/MCS, lengths, TIDs/policies, profile and address gates OK; NOT RF timing");
}

static void ack_capture_evidence(void) {
    uint16_t r[0x1800], base[0x1800];
    qos_ack_base(base, 0u, 60u);
    base[CLEAN_SHM_FRAME + 4u] = 0xd4u;
    base[CLEAN_SHM_FRAME + 5u] = 0u;
    base[CLEAN_SHM_FRAME + 9u] = 0x1234u;
    base[CLEAN_SHM_FRAME + 10u] = 0x5678u;
    base[CLEAN_ACK_WAITING] = 1u;
    base[CLEAN_RESPONSE_POLICY] |= CLEAN_RESPONSE_INTERNAL_ACK;
    base[0x100cu] = 20u;
    base[0x1009u] = 1u;
    base[0x1119u] = 0xfffeu;
    base[0x111au] = 0x1234u;
    memcpy(r, base, sizeof(r));
    assert(ack_run(r, 1) == 266u && r[CLEAN_ACK_SEEN]);
    assert(r[CLEAN_ACK_RX_SHM] == 1u);
    assert(r[CLEAN_ACK_RX_SHM + 1u] == 0xfffeu &&
           r[CLEAN_ACK_RX_SHM + 2u] == 0x1234u &&
           r[CLEAN_ACK_RX_SHM + 3u] == 1u);
    for (uint32_t i = 0u; i < 7u; ++i)
        assert(r[CLEAN_ACK_RX_SHM + 4u+i] == base[CLEAN_SHM_FRAME + 4u+i]);
    for (uint32_t i = 0u; i < 3u; ++i)
        assert(r[CLEAN_ACK_RX_SHM + 11u+i] == base[CLEAN_SHM_FRAME + 1u+i]);
    for (uint32_t fault = 0u; fault < 6u; ++fault) {
        memcpy(r, base, sizeof(r));
        if (fault == 0u) r[CLEAN_ACK_WAITING] = 0u;
        if (fault == 1u) r[CLEAN_ACK_ENABLE] = 0u;
        if (fault == 2u) r[CLEAN_SHM_FRAME + 6u] ^= 1u;
        if (fault == 3u) r[0x100cu] = 19u;
        if (fault == 4u) r[0x100cu] = 21u;
        if (fault == 5u) r[CLEAN_QOS_RESPONSE_POLICY] = 0u;
        (void)ack_run(r, 1);
        assert(!r[CLEAN_ACK_RX_SHM]);
    }
    memcpy(r, base, sizeof(r));
    r[CLEAN_DCF_ENABLE] = 1u;
    r[CLEAN_DCF_ACK_UNTIL] = base[0x1119u];
    assert(psm_run(r, 1, DCF_GOOD) == 266u);
    assert(!r[CLEAN_ACK_RX_SHM] && !r[CLEAN_ACK_SEEN]);
    puts("PSM ACK evidence: exact bytes/PHY/TSF, expected-ACK/address/length/policy/deadline gates pass; NOT RF");
}

static void receive_trace(void) {
    uint16_t r[0x1800] = {0};
    r[0x1780u] = 123u; r[0x1781u] = 456u; r[0x1782u] = 789u;
    r[CLEAN_SHM_FRAME + 1u] = 0x040au;
    r[CLEAN_SHM_FRAME + 2u] = 18656u;
    r[CLEAN_SHM_FRAME + 3u] = 25796u;
    r[0x100cu] = 6u; r[0x100du] = 0x1800u;
    r[0x102bu] = 0x8000u; r[0x100eu] = 64u; r[0x1119u] = 65530u;
    assert(psm_run(r, 0, RX_TRACE_ENTER) == 148u);
    assert(r[CLEAN_RX_TRACE_SHM] == 0x040au);
    assert(r[CLEAN_RX_TRACE_SHM + 1u] == 18656u);
    assert(r[CLEAN_RX_TRACE_SHM + 2u] == 25796u);
    assert(r[CLEAN_RX_TRACE_SHM + 7u] == 65530u && r[0x7acu] == 0x040au);
    r[CLEAN_SHM_FRAME + 2u] = 12224u;
    assert(psm_run(r, 0, RX_TRACE_ENTER) == 148u);
    assert(r[CLEAN_RX_TRACE_SHM + 1u] == 18656u);
    assert(psm_run(r, 0, RX_TRACE_BAD) == 263u);
    assert(!r[CLEAN_RX_TRACE_SHM + 8u]);
    r[CLEAN_SHM_FRAME + 2u] = 18656u;
    r[0x1119u] = 12u; r[0x100cu] = 1590u;
    assert(psm_run(r, 0, RX_TRACE_BAD) == 263u);
    assert(r[CLEAN_RX_TRACE_SHM + 8u] == 18656u);
    assert(r[CLEAN_RX_TRACE_SHM + 9u] == 12u);
    assert(r[CLEAN_RX_TRACE_SHM + 10u] == 1590u && r[0x7a5u] == 1590u);
    r[0x1119u] = 20u;
    assert(psm_run(r, 0, RX_TRACE_ENTER) == 148u);
    assert(r[CLEAN_RX_TRACE_SHM + 7u] == 20u && !r[CLEAN_RX_TRACE_SHM + 8u]);
    r[CLEAN_SHM_FRAME + 1u] = 0x840eu;
    assert(psm_run(r, 0, RX_TRACE_ENTER) == 148u);
    assert(r[0x7acu] == 0x840eu && r[CLEAN_RX_TRACE_SHM] == 0x040au);
    assert(r[0x1780u] == 123u && r[0x1781u] == 456u && r[0x1782u] == 789u);
}

static void ht_receive_trace(void) {
    uint16_t r[0x1800] = {0};
    for (uint32_t phy = 0u; phy <= UINT16_MAX; ++phy) {
        r[0x1009u] = (uint16_t)phy;
        r[0x100cu] = 6u;
        r[CLEAN_SHM_FRAME + 1u] = 0x5800u;
        r[CLEAN_SHM_FRAME + 2u] = 0x0700u;
        r[CLEAN_SHM_FRAME + 3u] = 0x0024u;
        r[CLEAN_HT_EARLY_SHM] = UINT16_MAX;
        r[CLEAN_HT_EARLY_SHM + 9u] = 99u;
        assert(psm_run(r, 0, HT_TRACE_ENTER) == 148u);
        assert(!r[CLEAN_HT_EARLY_SHM] && r[CLEAN_HT_EARLY_SHM + 1u] == phy);
        assert(r[CLEAN_HT_EARLY_SHM + 2u] == 0x5800u);
        assert(r[CLEAN_HT_EARLY_SHM + 3u] == 0x0700u);
        assert(r[CLEAN_HT_EARLY_SHM + 4u] == 0x0024u);
        assert(r[CLEAN_HT_EARLY_SHM + 5u] == 6u);
        for (uint32_t i = 6u; i < CLEAN_HT_EARLY_WORDS; ++i)
            assert(!r[CLEAN_HT_EARLY_SHM + i]);
        r[0x100cu] = 94u; r[0x100du] = 0x1234u; r[0x100eu] = 0x5678u;
        if ((phy & 3u) < 2u) {
            assert(psm_run(r, 0, HT_TRACE_GOOD) == 198u);
            assert(!r[CLEAN_HT_EARLY_SHM + 9u]);
            assert(psm_run(r, 0, HT_TRACE_BAD) == 263u);
            assert(!r[CLEAN_HT_EARLY_SHM + 9u]);
        }
        r[0x1009u] = (uint16_t)((phy & ~3u) | 2u);
        assert(psm_run(r, 0, HT_TRACE_GOOD) == 198u);
        assert(r[CLEAN_HT_EARLY_SHM + 6u] == 94u);
        assert(r[CLEAN_HT_EARLY_SHM + 7u] == 0x1234u);
        assert(r[CLEAN_HT_EARLY_SHM + 8u] == 0x5678u);
        assert(r[CLEAN_HT_EARLY_SHM + 9u] == 1u);
        assert(psm_run(r, 0, HT_TRACE_BAD) == 263u);
        assert(r[CLEAN_HT_EARLY_SHM + 9u] == 1u);
        assert(psm_run(r, 0, HT_TRACE_ENTER) == 148u);
        for (uint32_t i = 1u; i <= 3u; ++i) {
            r[CLEAN_SHM_FRAME + i] ^= 1u;
            assert(psm_run(r, 0, HT_TRACE_BAD) == 263u);
            assert(!r[CLEAN_HT_EARLY_SHM + 9u]);
            assert(psm_run(r, 0, HT_TRACE_GOOD) == 198u);
            assert(!r[CLEAN_HT_EARLY_SHM + 9u]);
            r[CLEAN_SHM_FRAME + i] ^= 1u;
        }
        r[0x100cu] = 35u;
        assert(psm_run(r, 0, HT_TRACE_BAD) == 263u);
        assert(r[CLEAN_HT_EARLY_SHM + 6u] == 35u);
        assert(r[CLEAN_HT_EARLY_SHM + 9u] == 2u);
        assert(!r[0x1080u] && !r[0x1081u] && !r[0x10b4u]);
    }
    for (uint32_t first = 0u; first < 256u; ++first) {
        r[CLEAN_SHM_FRAME + 1u] = (uint16_t)(0x5800u | first);
        r[CLEAN_HT_EARLY_SHM] = 100u;
        r[CLEAN_HT_EARLY_SHM + 9u] = 99u;
        assert(psm_run(r, 0, HT_TRACE_ENTER) == 148u);
        assert(r[CLEAN_HT_EARLY_SHM] == (first < 8u ? 101u : 100u));
        assert(r[CLEAN_HT_EARLY_SHM + 9u] == (first < 8u ? 0u : 99u));
    }
    puts("PSM: early PHY is not a format decision; HT prefix filtering, terminal evidence, stale rejection and wrap pass; NOT RF");
}

static void ht_failure_capture(void) {
    uint16_t r[0x1800];
    for (uint32_t policy = 0u; policy < 4u; ++policy) {
        for (uint32_t cause = 2u; cause <= 3u; ++cause) {
            for (uint32_t length = 0u; length <= 67u; ++length) {
                memset(r, 0, sizeof(r));
                r[CLEAN_QOS_RESPONSE_POLICY] = (uint16_t)policy;
                for (uint32_t i = 0u; i < 33u; ++i)
                    r[CLEAN_SHM_FRAME + 1u + i] = (uint16_t)(0xa000u + i);
                const uint16_t sig[] = {0x3c07u,0x0700u,0x03fcu};
                for (uint32_t i = 0u; i < 3u; ++i)
                    r[CLEAN_HT_EARLY_SHM + 2u + i] = r[CLEAN_SHM_FRAME + 1u + i] = sig[i];
                r[0x100cu] = (uint16_t)length;
                r[0x100du] = 0x4062u; r[0x100eu] = 0x0840u; r[0x1009u] = 2u;
                r[0x1119u] = 65535u; r[0x111au] = 123u;
                uint32_t entry = cause == 2u ? HT_FAIL_FCS : HT_FAIL_ABORT;
                assert(psm_run(r, 0, entry) == (cause == 2u ? 263u : 266u));
                int saved = policy == 3u && length <= 66u;
                assert(r[CLEAN_HT_FAILURE_SHM] == saved);
                if (!saved) continue;
                const uint16_t meta[] = {(uint16_t)cause,65535u,123u,(uint16_t)length,
                                         0x4062u,0x0840u,2u};
                for (uint32_t i = 0u; i < 7u; ++i)
                    assert(r[CLEAN_HT_FAILURE_SHM + 1u + i] == meta[i]);
                for (uint32_t i = 0u; i < 33u; ++i) {
                    assert(r[CLEAN_HT_FAILURE_SHM + 8u + i] == r[CLEAN_SHM_FRAME + 1u + i]);
                    assert(r[CLEAN_HT_FAILURE_SHM + 41u + i] == r[CLEAN_SHM_FRAME + 1u + i]);
                    uint32_t pc = HT_FAIL_LOAD_0 + 3u * i;
                    assert(instruction(pc) == WORD(0x3f0u,CLEAN_SHM_FRAME + 1u + i,0x1800u,0x1782u));
                    assert(instruction(pc + 1u) == WORD(0x3f0u,0x1119u,0x1800u,0x1781u));
                    assert(instruction(pc + 2u) == WORD(0x3f0u,0x1782u,0x1800u,
                                                      CLEAN_HT_FAILURE_SHM + 41u + i));
                }
                const uint16_t final_meta[] = {(uint16_t)length,0x4062u,0x0840u,2u};
                for (uint32_t i = 0u; i < 4u; ++i)
                    assert(r[CLEAN_HT_FAILURE_SHM + 74u + i] == final_meta[i]);
                for (uint32_t i = 0u; i < 3u; ++i) {
                    r[CLEAN_SHM_FRAME + 1u + i] ^= 1u;
                    assert(psm_run(r, 0, entry) == (cause == 2u ? 263u : 266u));
                    assert(r[CLEAN_HT_FAILURE_SHM] == 1u);
                    r[CLEAN_SHM_FRAME + 1u + i] ^= 1u;
                }
            }
        }
    }
    const uint16_t declared_lengths[] = {0u,59u,60u,61u,255u,256u,316u,0xff3cu};
    for (uint32_t i = 0u; i < sizeof(declared_lengths) / sizeof(declared_lengths[0]); ++i) {
        for (uint32_t cause = 2u; cause <= 3u; ++cause) {
            memset(r, 0, sizeof(r));
            r[CLEAN_QOS_RESPONSE_POLICY] = 3u;
            r[0x100cu] = 66u;
            r[CLEAN_SHM_FRAME + 1u] = (uint16_t)((declared_lengths[i] << 8u) | 7u);
            r[CLEAN_SHM_FRAME + 2u] = (uint16_t)(0x0700u | (declared_lengths[i] >> 8u));
            r[CLEAN_SHM_FRAME + 3u] = 0x03fcu;
            for (uint32_t j = 0u; j < 3u; ++j)
                r[CLEAN_HT_EARLY_SHM + 2u + j] = r[CLEAN_SHM_FRAME + 1u + j];
            assert(psm_run(r, 0, cause == 2u ? HT_FAIL_FCS : HT_FAIL_ABORT) ==
                   (cause == 2u ? 263u : 266u));
            assert(r[CLEAN_HT_FAILURE_SHM] == (declared_lengths[i] == 60u));
        }
    }
    assert(instruction(RX_TRACE_BAD_CONTINUE) ==
           WORD(0x77fu,0x1780u,0x1780u,CLEAN_RX_HT_RESPONSE ? HT_FAIL_FCS : HT_TRACE_BAD));
    assert(instruction(HT_TRACE_BAD_RETURN) == WORD(0x77fu,0x1780u,0x1780u,263u));
    for (uint32_t phy = 0u; phy < 4u; ++phy) {
        memset(r, 0, sizeof(r));
        r[CLEAN_QOS_RESPONSE_POLICY] = 3u;
        r[0x100cu] = 66u;
        r[0x1009u] = (uint16_t)phy;
        const uint16_t sig[] = {0x3c07u,0x0700u,0x03fcu};
        for (uint32_t j = 0u; j < 3u; ++j)
            r[CLEAN_HT_EARLY_SHM + 2u + j] = r[CLEAN_SHM_FRAME + 1u + j] = sig[j];
        assert(psm_run(r, 0, RX_TRACE_BAD) == 263u);
        assert(r[CLEAN_HT_FAILURE_SHM] == CLEAN_RX_HT_RESPONSE);
        if (CLEAN_RX_HT_RESPONSE) {
            assert(r[CLEAN_HT_FAILURE_SHM + 7u] == phy);
            assert(r[CLEAN_HT_FAILURE_SHM + 77u] == phy);
        }
        assert(r[CLEAN_HT_EARLY_SHM + 9u] == (phy >= 2u ? 2u : 0u));
    }
    puts("HT failure capture: policy/SIG/length gates, independent repeated copies, terminal PHY 0..3 and abort return paths pass; NOT RF");
}

static void contention(void) {
    uint16_t r[0x1800];
    for (uint32_t aifsn = 2u; aifsn <= 15u; ++aifsn) {
        for (uint32_t slot = 9u; slot <= 20u; slot += 11u) {
            memset(r, 0, sizeof(r));
            r[CLEAN_DCF_ENABLE] = r[CLEAN_DCF_REQUEST] = 1u;
            r[CLEAN_DCF_SLOT_US] = (uint16_t)slot;
            r[CLEAN_DCF_DIFS_US] = (uint16_t)(10u + aifsn * slot);
            r[CLEAN_DCF_SLOTS] = 2u;
            r[0x1148u] = 3u;
            uint32_t end = 10u + aifsn * slot + 2u * slot;
            for (uint32_t elapsed = 0u; elapsed <= end; ++elapsed) {
                r[0x1119u] = (uint16_t)(65500u + elapsed);
                assert(psm_run(r, 0, DCF_IDLE) == 124u);
                assert((r[CLEAN_DCF_REQUEST] == CLEAN_DCF_FIRED) == (elapsed == end));
            }
            assert(!r[CLEAN_DCF_SLOTS]);
        }
    }
    for (uint32_t profile = 0u; profile < 5u; ++profile) {
        assert(clean_edca_command_valid(15u, 1u, profile) == (profile == 3u));
        assert(!clean_edca_command_valid(15u, 0u, profile));
        for (uint32_t bit = 0u; bit < 32u; ++bit)
            assert(!clean_edca_command_valid(15u ^ (1u << bit), 1u, profile));
    }
    assert(instruction(DCF_SLOTS_CHECK) ==
           WORD(0x3f0u,CLEAN_DCF_SLOTS,0x1800u,0x1783u));
    assert(DCF_COUNT_CLOCK == DCF_SLOTS_CHECK + 1u &&
           DCF_SLOTS_RANGE == DCF_COUNT_CLOCK + 1u);
    assert(instruction(DCF_COUNT_CLOCK) ==
           WORD(0x3f0u,0x1119u,0x1800u,CLEAN_DCF_TRACE_SHM + 3u));
    assert(instruction(DCF_SLOT_COUNT) == WORD(0x1d0u,0x1783u,0x1801u,0x1783u));
    assert(instruction(DCF_SLOT_STORE) == WORD(0x3f0u,0x1783u,0x1800u,CLEAN_DCF_SLOTS));
    assert(instruction(DCF_SLOT_MORE) == WORD(0xd1u,0x1783u,0x1800u,124u));
    for (uint32_t status = 0u; status < 4096u; ++status) {
        memset(r, 0, sizeof(r));
        r[CLEAN_DCF_ENABLE] = 1u;
        r[CLEAN_DCF_REQUEST] = 1u;
        r[CLEAN_DCF_ACK_US] = 203u;
        r[CLEAN_DCF_DIFS_US] = 50u;
        r[0x1119u] = 65500u;
        r[0x111au] = 19u;
        r[0x1148u] = (uint16_t)status;
        r[0x1780u] = 123u; r[0x1781u] = 456u;
        assert(psm_run(r, 0, DCF_IDLE) == 124u);
        assert(!r[0x1081u] && r[CLEAN_DCF_REQUEST] == 1u);
        r[0x1119u] = 13u; r[0x111au] = 20u;
        assert(psm_run(r, 0, DCF_IDLE) == 124u && !r[0x1081u]);
        r[0x1119u] = 14u;
        assert(psm_run(r, 0, DCF_IDLE) == 124u);
        int ready = (status & 0xf03u) == 3u;
        assert((r[CLEAN_DCF_REQUEST] == CLEAN_DCF_FIRED) == ready);
        assert(r[0x1081u] == (ready ? 0xd0u : 0u));
        assert(r[0x1780u] == 123u && r[0x1781u] == 456u);
        if (ready) {
            assert(r[CLEAN_ACK_WAITING] == 1u);
            assert(r[CLEAN_DCF_TX_LO] == 14u && r[CLEAN_DCF_TX_HI] == 20u);
            assert(r[CLEAN_DCF_ACK_UNTIL] == 217u);
            assert(r[CLEAN_DCF_TX_PENDING] == 1u);
            r[0x1119u] = 44u; r[0x111au] = 20u;
            assert(psm_run(r, 0, DCF_IDLE) == 124u && r[CLEAN_DCF_TX_PENDING]);
            r[0x1148u] |= 0x100u;
            assert(psm_run(r, 0, DCF_IDLE) == 124u && !r[CLEAN_DCF_TX_PENDING]);
            assert(r[CLEAN_DCF_TX_LO] == 44u && r[CLEAN_DCF_TX_HI] == 20u);
            assert(r[CLEAN_DCF_ACK_UNTIL] == 247u);
            assert(psm_run(r, 0, DCF_IDLE) == 124u && r[CLEAN_DCF_REQUEST] == 2u);
        }
    }
    memset(r, 0, sizeof(r));
    r[CLEAN_RESPONSE_PENDING] = 2u;
    r[0x1148u] = 0x10fu;
    assert(psm_run(r, 0, DCF_IDLE) == 124u && r[CLEAN_RESPONSE_PENDING] == 2u);
    r[0x1148u] = 15u;
    assert(psm_run(r, 0, DCF_IDLE) == 124u && !r[CLEAN_RESPONSE_PENDING]);
    r[CLEAN_DCF_ENABLE] = 1u; r[CLEAN_DCF_REQUEST] = 1u;
    r[0x1148u] = 0x8fu;
    assert(psm_run(r, 2, DCF_IDLE) == 130u && !r[0x1081u]);
    r[CLEAN_DCF_ENABLE] = 0u;
    assert(psm_run(r, 0, DCF_IDLE) == 124u && !r[0x1081u]);
    r[CLEAN_DCF_ENABLE] = 1u;
    r[CLEAN_DCF_EIFS_US] = 364u;
    r[0x1119u] = 65400u; r[CLEAN_DCF_SLOTS] = 7u;
    assert(psm_run(r, 0, DCF_BAD) == 266u && r[0x7a8u] == 1u);
    assert(r[CLEAN_DCF_REQUEST] == 3u && r[CLEAN_DCF_EIFS_UNTIL] == 228u);
    assert(r[CLEAN_DCF_EIFS] && r[CLEAN_DCF_SLOTS] == 7u);
    r[CLEAN_DCF_EIFS] = 0u;
    assert(psm_run(r, 0, DCF_BAD_LENGTH) == 266u && r[0x7a9u] == 1u);
    assert(r[CLEAN_DCF_EIFS] && r[CLEAN_DCF_SLOTS] == 7u);
    r[CLEAN_DCF_REQUEST] = 1u;
    for (uint32_t elapsed = 0u; elapsed < 364u; ++elapsed) {
        r[0x1119u] = (uint16_t)(65400u + elapsed);
        r[0x1145u] = 0u;
        assert(psm_run(r, 0, DCF_IDLE) == 124u);
        assert(r[CLEAN_DCF_SLOTS] == 7u && !r[0x1081u] && r[CLEAN_DCF_EIFS]);
    }
    r[0x1119u] = 228u; r[0x1148u] = 15u;
    assert(psm_run(r, 0, DCF_IDLE) == 124u && !r[CLEAN_DCF_EIFS] && !r[0x1081u]);
    for (uint32_t late = 0u; late < 3u; ++late) {
        r[CLEAN_DCF_REQUEST] = 1u;
        r[CLEAN_DCF_EIFS] = 1u;
        r[CLEAN_ACK_ENABLE] = 0u;
        r[CLEAN_ACK_WAITING] = 1u;
        r[CLEAN_DCF_ACK_UNTIL] = 3u;
        r[0x1119u] = (uint16_t)(2u + late);
        assert(psm_run(r, 0, DCF_GOOD) == 198u);
        assert(!r[CLEAN_DCF_EIFS] && r[CLEAN_DCF_REQUEST] == 3u);
        assert(r[CLEAN_ACK_WAITING] == !late);
        assert(r[CLEAN_DCF_RX_LO] == 2u + late);
    }
    const uint16_t draws[] = {0u,1u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,12u,13u,14u,
                              15u,31u,63u,127u,255u,511u,1023u};
    for (uint32_t slot = 9u; slot <= 20u; slot += 11u) {
        for (uint32_t d = 0u; d < sizeof(draws) / sizeof(draws[0]); ++d) {
            memset(r, 0, sizeof(r));
            r[CLEAN_DCF_ENABLE] = r[CLEAN_DCF_REQUEST] = 1u;
            r[CLEAN_DCF_SLOT_US] = (uint16_t)slot;
            r[CLEAN_DCF_DIFS_US] = (uint16_t)(10u + 2u * slot);
            r[CLEAN_DCF_SLOTS] = draws[d];
            r[0x1148u] = 15u;
            uint32_t access = 10u + 2u * slot + slot * draws[d];
            for (uint32_t elapsed = 0u; elapsed <= access; ++elapsed) {
                r[0x1119u] = (uint16_t)(65500u + elapsed);
                assert(psm_run(r, 0, DCF_IDLE) == 124u);
                assert((r[CLEAN_DCF_REQUEST] == 2u) == (elapsed == access));
                assert(r[CLEAN_DCF_SLOTS] <= draws[d] && !r[CLEAN_ACK_ERROR]);
                if (r[CLEAN_DCF_TRACE_SHM + 1u])
                    assert(r[CLEAN_DCF_TRACE_SHM + 2u] + 1u ==
                           r[CLEAN_DCF_TRACE_SHM + 1u]);
            }
        }
        for (uint32_t draw = 0u; draw <= 15u; ++draw) {
            memset(r, 0, sizeof(r));
            r[CLEAN_DCF_ENABLE] = 1u;
            r[CLEAN_DCF_SLOTS] = (uint16_t)draw;
            r[CLEAN_DCF_SLOT_US] = 9u;
            r[CLEAN_DCF_DIFS_US] = 28u;
            r[0x1148u] = 15u;
            for (uint32_t elapsed = 0u; elapsed <= 700000u; elapsed += 7u) {
                r[0x1119u] = (uint16_t)(65500u + elapsed);
                assert(psm_run(r, 0, DCF_IDLE) == 124u);
                assert(r[CLEAN_DCF_SLOTS] <= draw && !r[CLEAN_ACK_ERROR] && !r[0x1081u]);
            }
            assert(!r[CLEAN_DCF_SLOTS]);
            r[CLEAN_DCF_REQUEST] = 1u;
            assert(psm_run(r, 0, DCF_IDLE) == 124u && r[CLEAN_DCF_REQUEST] == CLEAN_DCF_FIRED);
        }
        const uint16_t corrupt[] = {1024u,15574u,18000u,21300u,23297u,35046u,60025u,65535u};
        for (uint32_t i = 0u; i < sizeof(corrupt) / sizeof(corrupt[0]); ++i) {
            memset(r, 0, sizeof(r));
            r[CLEAN_DCF_ENABLE] = r[CLEAN_DCF_REQUEST] = 1u;
            r[CLEAN_DCF_IDLE_VALID] = 2u;
            r[CLEAN_DCF_SLOTS] = corrupt[i];
            r[0x1148u] = 15u;
            r[0x1119u] = 123u;
            assert(psm_run(r, 0, DCF_IDLE) == 124u);
            assert(!r[CLEAN_DCF_ENABLE] && !r[0x1081u] && !r[CLEAN_ACK_WAITING]);
            assert(r[CLEAN_DCF_REQUEST] == CLEAN_DCF_DEFERRED && r[CLEAN_ACK_ERROR] == 1u);
            assert(r[CLEAN_DCF_TRACE_SHM + 1u] == corrupt[i]);
            assert(r[CLEAN_DCF_TRACE_SHM + 2u] == corrupt[i]);
            assert(r[CLEAN_DCF_TRACE_SHM + 3u] == 123u && r[CLEAN_DCF_TRACE_SHM + 4u] == 1u);
            assert(psm_run(r, 0, DCF_IDLE) == 124u && !r[0x1081u]);
        }
        puts("PSM backoff: register-staged transitions, all CWmin draws, 700-ms idle/wrap and corrupt-counter fail-closed gates pass; NOT RF");
        memset(r, 0, sizeof(r));
        r[CLEAN_DCF_ENABLE] = r[CLEAN_DCF_REQUEST] = 1u;
        r[CLEAN_DCF_SLOT_US] = (uint16_t)slot;
        r[CLEAN_DCF_DIFS_US] = (uint16_t)(10u + 2u * slot);
        r[CLEAN_DCF_SLOTS] = 3u;
        for (uint32_t elapsed = 0u; elapsed <= 17u + 5u * slot; ++elapsed) {
            r[0x1119u] = (uint16_t)elapsed;
            r[0x1148u] = elapsed == 5u || elapsed == 6u ? 0u : 15u;
            assert(psm_run(r, 0, DCF_IDLE) == 124u);
            assert((r[CLEAN_DCF_REQUEST] == 2u) == (elapsed == 17u + 5u * slot));
        }
    }
    puts("PSM: hardware-IFS TX gate, RX arbitration, EIFS freeze/wrap and ACK deadlines OK; NOT RF timing");
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
        regs[CLEAN_TEST_ACK_REMAINING] = CLEAN_TEST_ACK_MAX;
        regs[CLEAN_SHM_FRAME + 9u] ^= 1u;
        assert(ack_run(regs, 1) == 198u && !regs[CLEAN_TEST_ACK_DROPPED]);
        assert(regs[CLEAN_TEST_ACK_REMAINING] == CLEAN_TEST_ACK_MAX);
        regs[CLEAN_SHM_FRAME + 9u] ^= 1u;
        for (uint32_t dropped = 1u; dropped <= CLEAN_TEST_ACK_MAX; ++dropped) {
            assert(ack_run(regs, 1) == 198u && !regs[CLEAN_ACK_SENT]);
            assert(regs[CLEAN_TEST_ACK_REMAINING] == CLEAN_TEST_ACK_MAX - dropped);
            assert(regs[CLEAN_TEST_ACK_DROPPED] == dropped);
            assert(!regs[0x1081u] && !regs[CLEAN_RESPONSE_PENDING]);
        }
        regs[CLEAN_TEST_ACK_DROPPED] = 0u;
        ack_run(regs, 1);
        assert(regs[CLEAN_ACK_SENT] == 1u && !regs[CLEAN_ACK_ERROR]);
        assert(regs[CLEAN_ACK_KINDS] == 1u);
        assert(regs[0x108a] == phy && regs[0x1086] == (rate >= 4u ? 0x41u : 0x40u));
        assert(regs[0x10b5] == 16u && regs[0x10b4] == 0x0826u);
        assert(regs[0x1080] == 0x4021u && regs[0x1084] == 255u);
        assert(regs[0x10e3] == 0xd4u && regs[0x10e4] == 0u);
        assert(regs[0x1081u] == 0xd0u);
        assert(regs[CLEAN_RESPONSE_PENDING] == 2u);
        regs[CLEAN_RESPONSE_POLICY] = 1u;
        regs[CLEAN_CTS_ALLOWED] = 1u;
        regs[CLEAN_TEST_ACK_REMAINING] = 1u;
        regs[CLEAN_SHM_FRAME + 4u] = 0xb4u;
        regs[CLEAN_SHM_FRAME + 5u] = 1000u;
        regs[0x100cu] = 26u;
        assert(ack_run(regs, 1) == 266u && regs[CLEAN_CTS_SENT] == 1u);
        assert(regs[CLEAN_TEST_ACK_REMAINING] == 1u && !regs[CLEAN_TEST_ACK_DROPPED]);
        regs[CLEAN_CTS_SENT] = regs[CLEAN_TEST_ACK_REMAINING] = 0u;
        regs[CLEAN_RESPONSE_POLICY] = 0u;
        regs[CLEAN_SHM_FRAME + 4u] = 0x08b0u;
        regs[CLEAN_SHM_FRAME + 5u] = 0u;
        regs[0x100cu] = 40u;
        if (rate >= 4u) {
            regs[0x100eu] = 0x440u;
            regs[0x1009u] = 1u;
            ack_run(regs, 1);
            assert(regs[CLEAN_ACK_SENT] == 2u && regs[0x108au] == phy);
            regs[0x1009u] = 0u;
            regs[0x100eu] = 0x2000u;
        }
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
    for (uint32_t format = 2u; format < 4u; ++format) {
        for (uint32_t flags = 0u; flags < 4u; ++flags) {
            memcpy(regs, base_regs, sizeof(regs));
            regs[0x1009u] = (uint16_t)(format | ((flags & 1u) ? 0x80u : 0u));
            regs[0x100eu] = (flags & 2u) ? 0x2000u : 0u;
            regs[CLEAN_SHM_FRAME + 1u] = 0xbu;
            regs[CLEAN_SHM_FRAME + 4u] = 8u;
            assert(ack_run(regs, 1) == 198u && !regs[CLEAN_ACK_SENT] &&
                   !regs[CLEAN_CTS_SENT] && !regs[0x1080u]);
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
    ht_qos_acknowledgements();
    ack_capture_evidence();
    assert(psm_ucode_byte_count == sizeof(psm_ucode_bytes));
    operand_limits();
    assert(ACK_TA2 < ACK_CTL && ACK_SELECT < ACK_CTL && ACK_CRYPTO < ACK_CTL);
    assert(ACK_CTL < ACK_TRIGGER && ACK_TRIGGER < ACK_WAIT_NOW);
    assert(instruction(118u) == WORD(0x3f0u,0x1803u,0x1800u,0x601u));
    assert(instruction(193u) == WORD(0x703u,0x1780u,0x1780u,197u));
    assert(instruction(264u) == WORD(0x77fu,0x1780u,0x1780u,DCF_BAD));
    assert(instruction(260u) == WORD(0x77fu,0x1780u,0x1780u,DCF_BAD_LENGTH));
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
    receive_trace();
    ht_receive_trace();
    ht_failure_capture();
    idle_watchdog();
    acknowledgements();
    contention();
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
    puts("PSM: single-SHM inputs, pinned ACK/CTS addresses, bounded ACK suppression at all 12 rates and 100000 native ACK rearms OK; NOT RF timing");
    return 0;
}
