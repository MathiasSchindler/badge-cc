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
#if CLEAN_RX_HT_REARM_WAIT
    restore(original,275u,WORD(0x3f0u,0x1fffu,0x1800u,0x1780u),
            WORD(0x3f0u,0x18ffu,0x1800u,0x1780u));
    restore(original,280u,WORD(0x77fu,0x1780u,0x1780u,OBSERVER_REARM_VERIFY),
            WORD(0x783u,0x1780u,0x1780u,281u));
#endif
#if CLEAN_RX_HT_OBSERVER
    restore(original,196u,WORD(0x77fu,0x1780u,0x1780u,OBSERVER_EXIT_TIMEOUT),
            WORD(0x77fu,0x1780u,0x1780u,262u));
#endif
#if CLEAN_RX_AMPDU_DMA
    restore(original,219u,WORD(0x77fu,0x1780u,0x1780u,AMPDU_DMA_LENGTH),
            WORD(0x3f0u,0x1781u,0x1800u,0x7f4u));
    restore(original,220u,WORD(0x3f0u,CLEAN_AMPDU_DMA_PENDING,0x1800u,0x7f5u),
            WORD(0x3f0u,0x1800u,0x1800u,0x7f5u));
    restore(original,228u,WORD(0x3f0u,CLEAN_AMPDU_PROBE_SHM,0x1800u,0x7fdu),
            WORD(0x3f0u,0x1800u,0x1800u,0x7fdu));
    restore(original,252u,WORD(0x77fu,0x1780u,0x1780u,AMPDU_DMA_DONE),
            WORD(0x3f0u,0x180eu,0x1800u,0x601u));
#endif
#if CLEAN_RX_AMPDU_HANDOFF == 1
    restore(original,153u,WORD(0x77fu,0x1780u,0x1780u,AMPDU_HDR_ENTER),
            WORD(0x3f0u,0x1782u,0x1800u,0x7a1u));
#endif
#if CLEAN_RX_AMPDU_INTERFACE == 2
    restore(original,162u,WORD(0x77fu,0x1780u,0x1780u,AMPDU_IF_BODY),
            WORD(0x3f0u,0x1fffu,0x1800u,0x1780u));
#endif
    restore(original, 138u, WORD(0x785u,0x1780u,0x1780u,139u),
            WORD(0x785u,0x1780u,0x1780u,257u));
    restore(original, 143u, WORD(0x77fu,0x1780u,0x1780u,RX_PREFIX_EOI),
            WORD(0x77fu,0x1780u,0x1780u,147u));
    restore(original, 164u, WORD(0x785u,0x1780u,0x1780u,165u),
            WORD(0x785u,0x1780u,0x1780u,257u));
    uint32_t phy_latch = 0u, held_capture = CLEAN_RX_AMPDU_DMA ? 252u : 0u;
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
    assert(instruction(312u) == WORD(0x77fu,0x1780u,0x1780u,RX_COMPLETE));
    assert(instruction(313u) == WORD(0x77fu,0x1780u,0x1780u,192u));
    const uint32_t fault_regs[] = {0x100cu,0x102bu,0x100du,0x1002u,
                                  0x607u,0x1004u,0x102au,0x7a2u,
                                  CLEAN_SHM_FRAME + 1u,CLEAN_SHM_FRAME + 2u,
                                  CLEAN_SHM_FRAME + 3u,0x100eu};
    for (uint32_t i = 0u; i < CLEAN_RX_FAULT_WORDS; ++i)
        assert(instruction(295u + i) == WORD(0x3f0u,fault_regs[i],0x1800u,0x620u + i));
    assert(instruction(307u) == WORD(0x1c0u,0x7abu,0x1801u,0x7abu));
    assert(instruction(308u) == WORD(0x77fu,0x1780u,0x1780u,
                                   OBSERVER_ABORT_TARGET));
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

#if CLEAN_RX_HT_REARM_WAIT
static int rearm_flush(uint32_t busy_polls, uint16_t busy_bits, uint32_t events,
                       int clear_after_latch, int retry_clears) {
    uint32_t pc = 275u, polls = 0u, retries = 0u;
    uint16_t r[0x1800] = {0};
    uint32_t initial_events = events;
    for (uint32_t step = 0u; step < 800300u; ++step) {
        if (pc == 281u || pc == 290u) {
            int success = pc == 281u;
            assert(r[CLEAN_OBSERVER_REARM_SHM] == 2u*(1u+retries));
            assert(r[CLEAN_OBSERVER_REARM_SHM+10u] == (success ? 1u : 2u));
            assert(r[CLEAN_OBSERVER_REARM_SHM+12u] ==
                (retries ? 0u : !!(initial_events&8u)));
            assert(success == !(r[CLEAN_OBSERVER_REARM_SHM+1u]&0x14u));
            if (clear_after_latch) assert(r[CLEAN_OBSERVER_REARM_SHM+11u] == 0x800u);
            assert(events == (retries ? initial_events&~0x6868u :
                success ? initial_events&~8u : initial_events));
#if CLEAN_RX_HT_REARM_RECOVER
            assert(retries <= 1u);
            assert(!!r[CLEAN_OBSERVER_REARM_SHM+16u] == !!retries);
            if (retries) {
                assert(r[CLEAN_OBSERVER_REARM_SHM+16u] == 2u);
                assert(r[CLEAN_OBSERVER_REARM_SHM+17u]&0x14u);
                assert(r[CLEAN_OBSERVER_REARM_SHM+26u] == 2u);
                assert(r[CLEAN_OBSERVER_REARM_SHM+28u] == !!(initial_events&8u));
            }
#else
            assert(!retries);
#endif
            if (!success) assert(r[0x602u] == 3u && r[0x1780u] == 0u);
            return success;
        }
        uint64_t word = instruction(pc);
        uint32_t op = (uint32_t)(word >> 39u), a = (uint32_t)(word >> 26u)&0x1fffu;
        uint32_t b = (uint32_t)(word >> 13u)&0x1fffu, d = (uint32_t)word&0x1fffu;
        if (pc == 276u)
            r[0x1004u] = (uint16_t)(0x800u |
                (retries && retry_clears ? 0u : polls++ < busy_polls ? busy_bits : 0u));
        if (pc == OBSERVER_REARM_REMAINING && clear_after_latch) r[0x1004u] = 0x800u;
        uint16_t av = a >= 0x1800u ? immediate(a) : r[a];
        uint16_t bv = b >= 0x1800u ? immediate(b) : r[b];
        uint32_t next = pc+1u;
        if (op == 0x3f0u) {
            assert(b == 0x1800u);
            r[d] = av;
            if (d == 0x1004u) {
                assert(pc == 274u && av == 0x14u && retries == 0u);
                ++retries;
                polls = 0u;
            }
        } else if (op == 0x502u || op == 0x504u || op == 0x404u) {
            assert(b == 0x1800u);
            if (((av >> (op&15u))&1u) == (op >> 8u == 5u)) next = d;
        } else if (op == 0x1d0u || op == 0x1c0u) {
            r[d] = op == 0x1d0u ? (uint16_t)(av-bv) : (uint16_t)(av+bv);
        } else if (op == 0xd1u) {
            if (av != bv) next = d;
        } else if (op == 0x703u || (op&0x7e0u) == 0x780u) {
            assert(a == 0x1780u && b == 0x1780u);
            uint32_t event = 1u << (op&31u);
            if (events&event) next = d;
            if (op&0x80u) events &= ~event;
        } else {
            assert(op == 0x77fu && a == 0x1780u && b == 0x1780u);
            next = d;
        }
        pc = next;
    }
    assert(!"Bounded rearm flush did not terminate");
    return 0;
}
#endif

static uint16_t stream_prefix(uint16_t rxctl, uint16_t count,
                              uint32_t *writes, uint32_t *next) {
    uint32_t pc = 191u;
    *writes = 0u;
    for (uint32_t step = 0u; step < 8u; ++step) {
        if (pc == RX_COMPLETE) {
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
        } else assert(!"Unexpected streaming instruction");
    }
    assert(!"RX streaming prefix did not terminate");
    return 0u;
}

static void streaming(void) {
    for (uint32_t count = 0u; count <= CLEAN_RXE_BYTES; ++count) {
        for (uint32_t state = 0u; state < 256u; ++state) {
            uint16_t before = (uint16_t)(0x2800u | state);
            int enable = (before & 2u) == 0u && count >= CLEAN_RX_STREAM_PREFIX_BYTES;
            uint32_t writes, next;
            uint16_t after = stream_prefix(before, (uint16_t)count, &writes, &next);
            assert(after == (enable ? (uint16_t)((before & ~3u) | 2u) : before));
            assert(writes == (uint32_t)enable && next == RX_COMPLETE);
            uint32_t again;
            assert(stream_prefix(after, (uint16_t)count, &again, &next) == after);
            assert(again == 0u);
        }
    }
}

static uint16_t ack_operand(const uint16_t regs[0x1800], uint32_t operand) {
    return operand >= 0x1800u ? immediate(operand) : regs[operand];
}

#if CLEAN_RX_HT_EARLY_SAVE
static uint32_t early_copy_fault;
static uint32_t early_status_after_hint;
#endif
#if CLEAN_RX_HT_ADVANCE
static uint32_t handoff_state_after_arm;
#endif
static uint32_t receive_run(uint16_t r[0x1800], uint32_t *events,
                            uint32_t pc, uint32_t *steps) {
    int body = pc == RX_COMPLETE;
    for (*steps = 0u; *steps < 1000000u; ++*steps) {
#if CLEAN_RX_HT_ADVANCE
        if (pc == 163u && r[CLEAN_OBSERVER_ADVANCE]) return pc;
        if (pc == OBSERVER_HANDOFF_2_0 && handoff_state_after_arm) {
            r[0x100cu] = 0u;
            r[0x100du] &= ~0x100u;
            handoff_state_after_arm = 0u;
        }
#endif
#if CLEAN_RX_HT_EARLY_SAVE
        if (pc == OBSERVER_EARLY && early_status_after_hint) {
            r[0x100du] |= 0x4000u;
            early_status_after_hint = 0u;
        }
        if (pc == OBSERVER_EARLY_VERIFY && early_copy_fault) {
            uint32_t target = early_copy_fault == 1u ? 0x100cu :
                early_copy_fault == 2u ? CLEAN_OBSERVER_EPOCH :
                CLEAN_SHM_FRAME+early_copy_fault-2u;
            r[target] ^= 1u;
            early_copy_fault = 0u;
        }
#endif
#if CLEAN_RX_AMPDU_INTERFACE >= 3
        if (pc == AMPDU_IF_GOOD) return 197u;
#endif
#if CLEAN_RX_AMPDU_HANDOFF == 2
        if (pc == AMPDU_IF_BAD) return 262u;
#endif
        if (body && pc == 167u) pc = RX_COMPLETE;
        if (pc == 147u || pc == 167u || pc == 197u ||
            pc == 257u || pc == 259u || pc == 262u || pc == 263u) return pc;
        uint64_t w = instruction(pc++);
        uint32_t op = (uint32_t)(w >> 39u);
        uint32_t a = (uint32_t)(w >> 26u) & 0x1fffu;
        uint32_t b = (uint32_t)(w >> 13u) & 0x1fffu;
        uint32_t d = (uint32_t)w & 0x1fffu;
        uint16_t av = ack_operand(r, a), bv = ack_operand(r, b);
        if (op == 0x77fu) pc = d;
        else if (op == 0xd0u) { if (av == bv) pc = d; }
        else if (op == 0xdau) { if (av < bv) pc = d; }
        else if (op == 0xdcu) { if (av > bv) pc = d; }
        else if (op == 0xdbu) { if (av >= bv) pc = d; }
        else if (op == 0xd1u) { if (av != bv) pc = d; }
        else if (op == 0x3f0u) r[d] = av;
        else if (op == 0x1c0u) r[d] = (uint16_t)(av + bv);
        else if (op == 0x1d0u) r[d] = (uint16_t)(av - bv);
        else if (op == 0x378u) r[d] = (uint16_t)((av << 8u) | (bv & 255u));
        else if (op == 0x376u) r[d] = (uint16_t)((bv & ~0x3fc0u) | ((av << 6u)&0x3fc0u));
        else if (op == 0x307u) r[d] = (uint16_t)((bv & ~0x80u) | ((av << 7u)&0x80u));
        else if (op == 0x278u) r[d] = av >> 8u;
        else if (op == 0x270u) r[d] = av & 255u;
        else if (op == 0x140u) r[d] = av & bv;
        else if (op == 0x606u) { if (!(*events & (1u << 6u))) pc = d; }
        else if ((op & 0x7e0u) == 0x780u || op == 0x703u) {
            uint32_t event = 1u << (op & 31u);
            if (*events & event) pc = d;
            if (op & 0x80u) *events &= ~event;
        } else {
            assert((op & 0xf00u) == 0x400u || (op & 0xf00u) == 0x500u);
            if (((av >> (op & 15u)) & 1u) == (op >> 8u == 5u)) pc = d;
        }
    }
    assert(!"RX completion exceeded bounded wait");
    return 0u;
}

static void receive_completion(void) {
    uint16_t r[0x1800];
    for (uint32_t event = 0u; event < 4u; ++event) {
        memset(r, 0, sizeof(r));
        uint32_t events = ((event & 1u) ? 1u << 5u : 0u) |
                          ((event & 2u) ? 1u << 6u : 0u);
        uint32_t steps;
        r[0x100du] = 0x1000u;
        r[0x100cu] = 6u;
        r[CLEAN_SHM_RXE_LIMIT] = CLEAN_RXE_BYTES;
        assert(receive_run(r, &events, 138u, &steps) == 147u);
        assert(!(events & ((1u << 5u) | (1u << 6u))) && steps < 12u);
        /* An ext5 edge alone is not a terminal abort. Explicit RX abort
         * and FIFO-full checks must still reject before processing a body. */
        for (uint32_t fault = 0u; fault < 4u; ++fault) {
            events = 1u << 5u;
            r[0x100du] = fault == 1u ? 0x8000u : 0x4440u;
            if (fault == 2u) events |= 1u << 13u;
            r[0x100cu] = fault == 3u ? CLEAN_RXE_BYTES + 1u : 66u;
            assert(receive_run(r, &events, 163u, &steps) ==
                   (fault == 1u || fault == 2u ? 257u :
                    fault == 3u ? 259u : 167u));
        }
    }
    for (uint32_t count = 0u; count <= CLEAN_RXE_BYTES; ++count) {
        for (uint32_t flags = 0u; flags < 8u; ++flags) {
            memset(r, 0, sizeof(r));
            uint32_t events = ((flags & 1u) ? 1u << 6u : 0u) |
                              ((flags & 2u) ? 1u << 3u : 0u);
            uint32_t steps;
            r[0x100du] = flags & 4u ? 0x4440u : 0x1000u;
            r[0x100cu] = (uint16_t)count;
            r[0x1780u] = 1u;
            uint32_t next = receive_run(r, &events, RX_COMPLETE, &steps);
            int complete = (flags & 5u) == 5u;
            assert(next == (!complete ? 262u : count < 14u ? 263u :
                            flags & 2u ? 197u : 262u));
            assert(steps < (CLEAN_RX_HT_OBSERVER ? 20u : 10u));
            assert(r[0x1780u] == (complete ? 1u : 0u));
        }
    }
    /* An ended bad frame must not spend the original 65535-poll budget.
     * A genuinely incomplete frame still has that bounded watchdog. */
    for (uint32_t flags = 0u; flags < 4u; ++flags) {
        memset(r, 0, sizeof(r));
        uint32_t events = flags & 1u ? 1u << 6u : 0u, steps;
        r[0x100du] = flags & 2u ? 0x4440u : 0x1000u;
        r[0x100cu] = 66u;
        r[CLEAN_SHM_RXE_LIMIT] = CLEAN_RXE_BYTES;
        r[0x1780u] = 65535u;
        assert(receive_run(r, &events, RX_COMPLETE, &steps) == 262u);
        assert(flags == 3u ? steps < (CLEAN_RX_HT_OBSERVER ? 20u : 10u) && r[0x1780u] == 65535u :
                            steps > 300000u && r[0x1780u] == 0u);
    }
    puts("PSM RX: prefix EOI, terminal GoodFCS/bad-FCS, ext5/explicit abort, size and incomplete watchdog gates pass; NOT RF");
}

static uint32_t slot_reads;
static uint32_t phy_writes;
static uint16_t psm_clock_step;

static uint32_t psm_run(uint16_t regs[0x1800], int events, uint32_t pc) {
    phy_writes = 0u;
    for (uint32_t step = 0u; step < 10000u; ++step) {
#if CLEAN_RX_AMPDU_BA
        if ((pc == AMPDU_BA_EOF_WAIT && !(regs[0x100du] & 0x4000u)) ||
            (pc == AMPDU_BA_START_WAIT && !(regs[0x1148u] & 0x100u)) ||
            (pc == AMPDU_BA_END_WAIT && (regs[0x1148u] & 0x100u))) return pc;
#endif
#if CLEAN_RX_AMPDU_SHM
        if (pc == AMPDU_SHM_WAIT && regs[0x601u]) return pc;
#endif
        if (pc == 198u || pc == 266u || pc == 124u || pc == 130u ||
            (CLEAN_RX_AMPDU_DMA && (pc == 253u || pc == 220u)) ||
            pc == 118u || pc == 129u || pc == 163u ||
            pc == 148u || pc == 154u || pc == 262u || pc == 263u) return pc;
        assert(pc >= ACK_ENTER && pc < ACK_PROGRAM_END);
        uint64_t w = instruction(pc++);
        uint32_t op = (uint32_t)(w >> 39u);
        uint32_t a = (uint32_t)(w >> 26u) & 0x1fffu;
        uint32_t b = (uint32_t)(w >> 13u) & 0x1fffu;
        uint32_t d = (uint32_t)w & 0x1fffu;
        if (a == CLEAN_DCF_SLOTS) ++slot_reads;
        if (a == 0x1119u) regs[a] = (uint16_t)(regs[a] + psm_clock_step);
        uint16_t av = ack_operand(regs, a), bv = ack_operand(regs, b);
        if (op == 0x3f0u) regs[d] = av;
        else if (op == 0x378u) regs[d] = (uint16_t)((av << 8u) | (bv & 255u));
        else if (op == 0x376u) regs[d] =
            (uint16_t)((bv & ~0x3fc0u) | ((av << 6u) & 0x3fc0u));
        else if (op == 0x310u) regs[d] = (uint16_t)((bv & ~3u) | (av & 3u));
        else if (op == 0x77fu) pc = d;
        else if (op == 0xd0u) { if (av == bv) pc = d; }
        else if (op == 0xd1u) { if (av != bv) pc = d; }
        else if (op == 0xdau) { if (av < bv) pc = d; }
        else if (op == 0xdbu) { if (av >= bv) pc = d; }
        else if (op == 0xdcu) { if (av > bv) pc = d; }
        else if (op == 0x1c0u) regs[d] = (uint16_t)(av + bv);
        else if (op == 0x1d0u) regs[d] = (uint16_t)(av - bv);
        else if (op == 0x278u) regs[d] = av >> 8u;
        else if (op == 0x140u) regs[d] = av & bv;
        else if (op == 0x78eu) { if (events & 2u) pc = d; }
        else if ((op & 0x7e0u) == 0x700u) {
            if ((uint32_t)events & (1u << (op & 31u))) pc = d;
        }
        else if ((op & 0xf00u) == 0x400u || (op & 0xf00u) == 0x500u) {
            if (((av >> (op & 15u)) & 1u) == (op >> 8u == 5u)) pc = d;
        } else if (op == 0x230u || op == 0x270u) {
            regs[d] = (uint16_t)(av & (op == 0x230u ? 15u : 255u));
        } else if ((op & 0xff0u) == 0x300u) {
            uint16_t mask = (uint16_t)(1u << (op & 15u));
            regs[d] = (uint16_t)((bv & ~mask) | ((av << (op & 15u)) & mask));
        } else {
            assert(op == 0x7a0u);
            if (events & 1u) pc = d;
        }
        if (op < 0x400u && d == 0x1049u) ++phy_writes;
    }
    assert(!"ACK path failed to terminate");
    return 0u;
}

static uint32_t ack_run(uint16_t regs[0x1800], int tx_now) {
    return psm_run(regs, tx_now, ACK_ENTER);
}

static void authentication_failure_capture(void) {
    uint16_t r[0x1800], base[0x1800];
    memset(base, 0, sizeof(base));
    base[CLEAN_ACK_ENABLE] = 1u;
    base[0x100cu] = 40u;
    base[0x1009u] = 0x1234u;
    base[0x100du] = 0x4321u;
    base[0x100eu] = 0x5678u;
    base[0x1119u] = 0xabcdu;
    base[0x111au] = 0x6789u;
    for (uint32_t i = 0u; i < 20u; ++i)
        base[CLEAN_SHM_FRAME + 1u + i] = (uint16_t)(0x1020u + i);
    base[CLEAN_SHM_FRAME + 4u] = 0x08b0u;
    for (uint32_t i = 0u; i < 3u; ++i) {
        base[0x612u + i] = base[CLEAN_SHM_FRAME + 6u + i];
        base[CLEAN_ACK_PEER + i] = base[CLEAN_SHM_FRAME + 9u + i];
    }
    for (uint32_t fault = 0u; fault < 14u; ++fault) {
        memcpy(r, base, sizeof(r));
        if (fault == 1u) r[CLEAN_ACK_ENABLE] = 0u;
        if (fault == 2u) r[0x100cu] = 39u;
        if (fault == 3u) r[0x100cu] = 41u;
        if (fault == 4u) r[CLEAN_SHM_FRAME + 4u] = 0x80u;
        if (fault == 5u) r[CLEAN_SHM_FRAME + 4u] = 0xb0u;
        if (fault >= 6u && fault < 9u) r[0x612u + fault - 6u] ^= 1u;
        if (fault >= 9u && fault < 12u) r[CLEAN_ACK_PEER + fault - 9u] ^= 1u;
        if (fault >= 12u) {
            r[0x612u + fault - 12u] ^= 1u;
            r[CLEAN_ACK_PEER + fault - 12u] ^= 1u;
        }
        assert(psm_run(r, 0, DCF_BAD) == 266u);
        int captured = fault == 0u || (fault >= 5u && fault < 12u);
        assert(r[CLEAN_AUTH_FAILURE_SHM] == captured);
        assert(r[0x7a8u] == 1u && !r[CLEAN_ACK_SENT] && !r[CLEAN_ACK_SEEN]);
        if (captured) {
            assert(r[CLEAN_AUTH_FAILURE_SHM + 1u] == 40u);
            assert(r[CLEAN_AUTH_FAILURE_SHM + 2u] == 0x1234u);
            assert(r[CLEAN_AUTH_FAILURE_SHM + 3u] == 0x4321u);
            assert(r[CLEAN_AUTH_FAILURE_SHM + 4u] == 0x5678u);
            assert(r[CLEAN_AUTH_FAILURE_SHM + 5u] == 0xabcdu);
            assert(r[CLEAN_AUTH_FAILURE_SHM + 6u] == 0x6789u);
            for (uint32_t i = 0u; i < 20u; ++i)
                assert(r[CLEAN_AUTH_FAILURE_SHM + 8u + i] == r[CLEAN_SHM_FRAME + 1u + i]);
        }
    }
    memcpy(r, base, sizeof(r));
    r[CLEAN_DCF_ENABLE] = 1u;
    r[CLEAN_DCF_EIFS_US] = 94u;
    r[CLEAN_DCF_REQUEST] = 1u;
    assert(psm_run(r, 0, DCF_BAD) == 266u);
    assert(r[CLEAN_DCF_EIFS] && r[CLEAN_DCF_REQUEST] == CLEAN_DCF_DEFERRED);
    assert(r[CLEAN_AUTH_FAILURE_SHM] == 1u);
    puts("PSM bad authentication: bounded raw evidence, either bound address, no ACK/delivery, EIFS retained; NOT RF");
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
    assert(instruction(HT_TRACE_BAD_RETURN) == WORD(0x77fu,0x1780u,0x1780u,AMPDU_BAD_HOOK));
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
    r[0x1087u] = 0x400u;
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
            assert(r[CLEAN_DCF_IDLE_VALID] == 3u);
            slot_reads = 0u;
            for (uint32_t poll = 0u; poll < 1000u; ++poll)
                assert(psm_run(r, 0, DCF_IDLE) == 124u);
            assert(!slot_reads && !r[CLEAN_ACK_ERROR] && !r[0x1081u]);
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

static void transmit_cleanup(void) {
    uint16_t r[0x1800];
    for (uint32_t fault = 0u; fault < 5u; ++fault) {
        memset(r, 0, sizeof(r));
        r[CLEAN_TX_CLEANUP_REQUEST] = 1u;
        r[CLEAN_RESPONSE_PENDING] = 2u;
        r[0x1081u] = 0xd0u; r[0x10b4u] = 6u;
        r[0x11e0u] = 0x100u; r[0x11e8u] = 0x280u;
        r[0x1087u] = 0x400u;
        r[0x1049u] = 0xa5f2u;
        if (fault == 2u) r[0x100du] = 0x1000u;
        if (fault == 3u) r[0x1087u] |= 0x80u;
        if (fault == 4u) r[0x1087u] = 0u;
        if (fault == 1u) r[0x1148u] = 0x100u;
        assert(psm_run(r, 0, TX_CLEANUP_ENTER) == 124u);
        assert(r[0x1049u] == 0xa5f2u);
        if (fault) {
            assert(!phy_writes);
            assert(r[CLEAN_TX_CLEANUP_REQUEST] == 1u && r[CLEAN_RESPONSE_PENDING] == 2u);
            assert(!r[CLEAN_TX_CLEANUP_COUNT] && r[0x1081u] == 0xd0u &&
                   r[0x10b4u] == 6u && r[0x11e0u] == 0x100u && r[0x11e8u] == 0x280u);
        } else {
            assert(!phy_writes);
            assert(!r[CLEAN_TX_CLEANUP_REQUEST] && !r[CLEAN_RESPONSE_PENDING]);
            assert(r[CLEAN_TX_CLEANUP_COUNT] == 1u && !r[0x1081u] && !r[0x10b4u]);
            assert(r[0x11e0u] == 0x8300u && r[0x11e8u] == 0x300u);
        }
    }
    memset(r, 0, sizeof(r));
    r[CLEAN_TX_CLEANUP_REQUEST] = 1u;
    r[0x1081u] = 0xd0u;
    assert(psm_run(r, 2, TX_CLEANUP_ENTER) == 130u);
    assert(r[0x1081u] == 0xd0u && r[CLEAN_TX_CLEANUP_REQUEST] &&
           !r[CLEAN_TX_CLEANUP_COUNT]);
    memset(r, 0, sizeof(r));
    r[CLEAN_TX_CLEANUP_REQUEST] = 1u;
    r[0x1087u] = 0x400u;
    assert(psm_run(r, 0, DCF_IDLE) == 124u);
    assert(!r[CLEAN_TX_CLEANUP_REQUEST] && r[CLEAN_TX_CLEANUP_COUNT] == 1u);
    assert(psm_run(r, 0, DCF_IDLE) == 124u);
    assert(r[CLEAN_TX_CLEANUP_COUNT] == 1u);
    r[CLEAN_RESPONSE_PENDING] = 1u;
    r[CLEAN_TX_CLEANUP_REQUEST] = 1u;
    assert(psm_run(r, 0, DCF_IDLE) == 124u && r[CLEAN_TX_CLEANUP_REQUEST] &&
           r[CLEAN_RESPONSE_PENDING] == 1u);
    r[CLEAN_RESPONSE_PENDING] = 2u;
    r[CLEAN_TX_CLEANUP_COUNT] = 65535u;
    assert(psm_run(r, 0, DCF_IDLE) == 124u && !r[CLEAN_TX_CLEANUP_REQUEST] &&
           !r[CLEAN_RESPONSE_PENDING] && !r[CLEAN_TX_CLEANUP_COUNT] && !phy_writes);
    puts("PSM TX cleanup: RX priority, active RX/TX/drain deferral, owned completion and PHY register preservation; NOT RF");
}

static void aggregate_handoff_trace(void) {
#if CLEAN_RX_AMPDU_HANDOFF == 1
    uint16_t r[0x1800]={0};
    r[0x1782u]=80u;
    assert(psm_run(r,0,AMPDU_HDR_ENTER)==154u && r[0x7a1u]==80u);
    assert(!r[CLEAN_AMPDU_IF_SHM+CLEAN_AMPDU_HANDOFF_COMMIT]);
    r[CLEAN_AMPDU_IF_SHM]=1u;
    for (uint32_t slot=0u;slot<3u;++slot) {
        r[CLEAN_AMPDU_PROBE_SHM]=(uint16_t)slot;
        r[0x1119u]=(uint16_t)(100u+slot);
        r[0x100cu]=(uint16_t)(6u+4u*slot);
        r[0x1001u]=CLEAN_SHM_FRAME;
        r[0x102au]=6u;
        r[0x1004u]=0x800u;
        r[0x1024u]=0xc02u;
        r[0x1782u]=(uint16_t)(80u-4u*slot);
        r[0x100eu]=0xa00u;
        for (uint32_t i=0u;i<12u;++i)
            r[CLEAN_SHM_FRAME+i]=(uint16_t)(slot*100u+i);
        uint16_t before[0x1800];
        memcpy(before,r,sizeof(r));
        assert(psm_run(r,0,AMPDU_HDR_ENTER)==154u);
        const uint16_t *p=r+CLEAN_AMPDU_IF_SHM+CLEAN_AMPDU_HANDOFF_WORD+
            CLEAN_AMPDU_HANDOFF_WORDS*slot;
        assert(p[0]==100u+slot && p[1]==6u+4u*slot &&
               p[2]==CLEAN_SHM_FRAME && p[3]==6u && p[4]==0x800u &&
               p[5]==0xc02u && p[6]==80u-4u*slot && p[7]==0xa00u);
        for (uint32_t i=0u;i<12u;++i) assert(p[8u+i]==slot*100u+i);
        assert(r[0x7a1u]==r[0x1782u] &&
               r[CLEAN_AMPDU_IF_SHM+CLEAN_AMPDU_HANDOFF_COMMIT+slot]==1u);
        assert(!memcmp(r+0x1000u,before+0x1000u,0x800u*sizeof(uint16_t)));
        r[0x1119u]=1000u;
        assert(psm_run(r,0,AMPDU_HDR_ENTER)==154u && p[0]==100u+slot);
    }
    r[CLEAN_AMPDU_PROBE_SHM]=3u;
    assert(psm_run(r,0,AMPDU_HDR_ENTER)==154u);
    puts("PSM handoff: prefix SHM/RDF snapshots, original store, one-shot commits, no hardware writes; NOT RF");
#endif
}

static void aggregate_probe_trace(void) {
#if CLEAN_RX_AMPDU_PROBE
    uint16_t base[0x1800]={0}, r[0x1800];
    base[0x1009u]=2u;
    base[0x100cu]=86u;
    base[0x100du]=0x1922u;
    base[0x100eu]=0x1234u;
    for (uint32_t i=0u;i<136u;++i) base[CLEAN_SHM_FRAME+i]=(uint16_t)(0x7010u+i);
    base[CLEAN_SHM_FRAME+1u]=0xfc00u;
    base[CLEAN_SHM_FRAME+2u]=0x0f00u;
    for (uint32_t fault=0u;fault<5u;++fault) {
        memcpy(r,base,sizeof(r));
        if (fault==1u) r[0x1009u]=1u;
        if (fault==2u) r[CLEAN_SHM_FRAME+2u]^=0x0800u;
        if (fault==3u) r[CLEAN_SHM_FRAME+1u]^=0x0100u;
        if (fault==4u) r[CLEAN_SHM_FRAME+2u]|=1u;
        assert(psm_run(r,0,AMPDU_TRACE_BAD)==
               (!fault && CLEAN_RX_AMPDU_HANDOFF==2 ?
                    (CLEAN_RX_AMPDU_DMA ? 198u : 163u) : 263u));
        assert(r[CLEAN_AMPDU_PROBE_SHM]==(fault==0u));
        if (!fault) {
            assert(r[CLEAN_AMPDU_PROBE_SHM+1u]==2u &&
                   r[CLEAN_AMPDU_PROBE_SHM+2u]==86u);
            for (uint32_t i=0u;i<44u;++i)
                assert(r[CLEAN_AMPDU_PROBE_SHM+8u+i]==base[CLEAN_SHM_FRAME+i]);
        }
        assert(!r[CLEAN_ACK_SENT] && !r[CLEAN_CTS_SENT]);
    }
    memcpy(r,base,sizeof(r));
    assert(psm_run(r,0,AMPDU_TRACE_GOOD)==
           (CLEAN_RX_AMPDU_INTERFACE==3 && !CLEAN_RX_AMPDU_DMA ?
                (CLEAN_RX_AMPDU_HANDOFF==2 ? 163u : 130u) : 198u));
    assert(r[CLEAN_AMPDU_PROBE_SHM]==1u && r[CLEAN_AMPDU_PROBE_SHM+1u]==1u);
    assert(psm_run(r,0,AMPDU_TRACE_GOOD)==
           (CLEAN_RX_AMPDU_INTERFACE==3 && !CLEAN_RX_AMPDU_DMA ?
                (CLEAN_RX_AMPDU_HANDOFF==2 ? 163u : 130u) : 198u) &&
           r[CLEAN_AMPDU_PROBE_SHM]==2u);
    assert(psm_run(r,0,AMPDU_TRACE_GOOD)==
#if CLEAN_RX_AMPDU_SHM
           AMPDU_SHM_WAIT &&
#else
           198u &&
#endif
           r[CLEAN_AMPDU_PROBE_SHM]==3u);
    assert(psm_run(r,0,AMPDU_TRACE_GOOD)==198u && r[CLEAN_AMPDU_PROBE_SHM]==3u);
    for (uint32_t i=0u;i<132u;++i)
        assert(r[CLEAN_AMPDU_PROBE_SHM+8u+i]==base[CLEAN_SHM_FRAME+i%44u]);
    puts("PSM A-MPDU probe: three-slot retention, bounded commits, FCS paths and no response; NOT RF");
#endif
}

static void aggregate_interface_trace(void) {
#if CLEAN_RX_AMPDU_INTERFACE
    uint16_t r[0x1800]={0};
    r[CLEAN_SHM_FRAME+1u]=0xfc00u;
    r[CLEAN_SHM_FRAME+2u]=0x0f00u;
    r[0x100cu]=6u;
    r[0x1119u]=65530u;
    assert(psm_run(r,0,AMPDU_IF_PREFIX)==148u);
    assert(r[CLEAN_AMPDU_IF_SHM]==1u && r[CLEAN_AMPDU_IF_SHM+2u]==65530u &&
           r[CLEAN_AMPDU_IF_SHM+3u]==6u);
    r[CLEAN_AMPDU_PROBE_SHM]=1u;
    r[0x100cu]=86u;
    r[0x100du]=0x4122u;
    r[0x100eu]=0x0a42u;
    r[0x1024u]=0x102u;
    r[0x102au]=86u;
    r[0x1004u]=0x20c0u;
    r[0x1782u]=1u;
    psm_clock_step=1u;
    uint32_t events=(1u<<3u)|(1u<<6u)|(1u<<14u);
    assert(psm_run(r,(int)events,AMPDU_IF_ENTER)==198u);
    psm_clock_step=0u;
    assert(r[CLEAN_AMPDU_IF_SHM+1u]==CLEAN_AMPDU_IF_RECORDS);
    for (uint32_t i=0u;i<CLEAN_AMPDU_IF_RECORDS;++i) {
        const uint16_t *row=r+CLEAN_AMPDU_IF_SHM+4u+8u*i;
        assert(row[1]==86u && row[2]==0x4122u && row[3]==0x0a42u &&
               row[4]==0x102u && row[5]==86u && row[6]==0x20c0u && row[7]==events);
        if (i) assert((uint16_t)(row[0]-row[-8])>=32u);
    }
    uint16_t stamp=r[CLEAN_AMPDU_IF_SHM+4u];
    assert(psm_run(r,0,AMPDU_IF_ENTER)==198u &&
           r[CLEAN_AMPDU_IF_SHM+4u]==stamp);
    r[CLEAN_AMPDU_IF_SHM+1u]=0u;
    assert(psm_run(r,0,AMPDU_IF_ENTER)==198u);
    assert(r[CLEAN_AMPDU_IF_SHM]==0x8001u && r[CLEAN_AMPDU_IF_SHM+1u]==1u);
    r[CLEAN_AMPDU_IF_SHM]=1u;
    r[CLEAN_AMPDU_IF_SHM+1u]=0u;
    psm_clock_step=1u;
    assert(psm_run(r,(int)events,AMPDU_IF_BODY)==163u);
    psm_clock_step=0u;
    assert(r[CLEAN_AMPDU_IF_SHM]==2u &&
           r[CLEAN_AMPDU_IF_SHM+1u]==CLEAN_AMPDU_IF_RECORDS && r[0x1780u]==65535u);
    uint32_t end=ACK_PROGRAM_END;
#if CLEAN_RX_AMPDU_INTERFACE >= 3
    end=AMPDU_IF_EARLY;
    uint32_t steps;
    r[CLEAN_SHM_FRAME+2u]=0x0f00u;
    r[CLEAN_AMPDU_IF_SHM]=1u;
    r[0x100du]=0x1922u;
    uint32_t pending=(1u<<6u)|(1u<<3u);
    assert(receive_run(r,&pending,AMPDU_IF_EARLY,&steps)==197u);
    assert(!(pending&(1u<<6u)) && (pending&(1u<<3u)));
    r[0x100du]=0x1822u;
    r[0x1780u]=1u;
    pending=(1u<<6u)|(1u<<3u);
    assert(receive_run(r,&pending,RX_COMPLETE,&steps)==
           (CLEAN_RX_AMPDU_HANDOFF==2 ? 197u : 262u));
    assert(!!(pending&(1u<<6u))==(CLEAN_RX_AMPDU_HANDOFF!=2));
#if CLEAN_RX_AMPDU_HANDOFF == 2
    pending=1u<<6u;
    assert(receive_run(r,&pending,RX_COMPLETE,&steps)==262u);
    assert(!(pending&(1u<<6u)));
#endif
    uint16_t early[0x1800]={0};
    early[CLEAN_SHM_FRAME+1u]=0xfc00u;
    early[CLEAN_SHM_FRAME+2u]=0x0f00u;
    early[CLEAN_AMPDU_IF_SHM]=1u;
    early[0x100cu]=86u;
    early[0x100du]=0x1922u;
    early[0x100eu]=0x0a41u;
    assert(psm_run(early,0,AMPDU_IF_GOOD)==
           (CLEAN_RX_AMPDU_DMA ? 198u : CLEAN_RX_AMPDU_HANDOFF==2 ? 163u : 130u));
#if CLEAN_RX_AMPDU_DMA
    assert(early[CLEAN_AMPDU_DMA_PENDING]==1u);
    assert((early[0x1004u]&3u)==2u);
    early[0x1004u]&=(uint16_t)~3u;
    assert(psm_run(early,0,AMPDU_DMA_DONE)==163u);
    assert(!early[CLEAN_AMPDU_DMA_PENDING] && early[CLEAN_AMPDU_DMA_SUBMITTED]==1u);
#endif
#if CLEAN_RX_AMPDU_HANDOFF == 2
    assert(early[0x102au]==80u && early[0x1004u]==0x20c0u &&
           early[0x1780u]==65535u && early[0x1001u]==CLEAN_SHM_FRAME);
    early[0x100du]=0x1822u;
    assert(psm_run(early,0,AMPDU_IF_BAD)==(CLEAN_RX_AMPDU_DMA ? 198u : 163u));
#if CLEAN_RX_AMPDU_DMA
    assert(early[CLEAN_AMPDU_DMA_PENDING]==2u);
    assert((early[0x1004u]&3u)==2u);
    early[0x1004u]&=(uint16_t)~3u;
    assert(psm_run(early,0,AMPDU_DMA_DONE)==163u);
    assert(!early[CLEAN_AMPDU_DMA_PENDING] && early[CLEAN_AMPDU_DMA_SUBMITTED]==2u);
#endif
    assert(early[CLEAN_AMPDU_PROBE_SHM]==2u &&
           early[CLEAN_AMPDU_PROBE_SHM+1u]==2u &&
           early[CLEAN_AMPDU_IF_SHM+19u]==2u);
    early[0x100cu]=82u;
    assert(psm_run(early,0,AMPDU_IF_BAD)==262u &&
           early[CLEAN_AMPDU_PROBE_SHM]==2u);
    early[CLEAN_AMPDU_PROBE_SHM]=1u;
    early[0x100cu]=86u;
#endif
    assert(early[CLEAN_AMPDU_PROBE_SHM]==1u &&
           early[CLEAN_AMPDU_PROBE_SHM+5u]==0u &&
           early[CLEAN_AMPDU_IF_SHM]==CLEAN_RX_AMPDU_INTERFACE);
    early[CLEAN_AMPDU_PROBE_SHM]=2u;
    early[0x1009u]=2u;
    early[0x100cu]=82u;
    early[0x100du]=0x4000u;
    assert(psm_run(early,0,AMPDU_TRACE_BAD)==
#if CLEAN_RX_AMPDU_SHM
           AMPDU_SHM_WAIT);
#else
           263u);
#endif
    assert(early[CLEAN_AMPDU_PROBE_SHM]==2u &&
           early[CLEAN_AMPDU_IF_SHM+132u]==82u &&
           early[CLEAN_AMPDU_IF_SHM+137u]==2u);
    for (uint32_t i=0u;i<44u;++i)
        assert(early[CLEAN_AMPDU_IF_SHM+138u+i]==early[CLEAN_SHM_FRAME+i]);
#endif
    for (uint32_t pc=AMPDU_IF_PREFIX;pc<end;++pc) {
        uint64_t w=instruction(pc);
        uint32_t op=(uint32_t)(w>>39u), d=(uint32_t)w&0x1fffu;
        assert(op>=0x400u || d<0x1000u || d>=0x1780u);
        assert((op&0x7e0u)!=0x780u);
    }
    puts("PSM aggregate interface: passive records, nonclearing events, clock wrap, bounded timeout and one-shot commits pass; NOT RF");
#endif
}

static void aggregate_ba_transmit(void) {
#if CLEAN_RX_AMPDU_BA
    for (uint32_t bad=0u;bad<8u;++bad) {
        uint16_t r[0x1800]={0};
        r[CLEAN_AMPDU_BA_WORD]=1u;
        r[CLEAN_AMPDU_PROBE_SHM]=3u;
        r[0x1782u]=(uint16_t)(bad & 4u ? 2u : 1u);
        r[CLEAN_AMPDU_BA_WORD+14u]=0x040bu;
        r[CLEAN_AMPDU_BA_WORD+17u]=0x000bu;
        for (uint32_t i=0u;i<3u;++i) {
            r[0x612u+i]=(uint16_t)(0x1202u+i);
            r[CLEAN_ACK_PEER+i]=(uint16_t)(0x3402u+i);
        }
        for (uint32_t n=0u;n<3u;++n) {
            uint32_t slot=CLEAN_AMPDU_PROBE_SHM+8u+44u*n;
            r[CLEAN_AMPDU_IF_SHM+11u+8u*n]=(uint16_t)(bad & (1u<<n) ? 2u : 1u);
            r[slot+4u]=0x0288u; r[slot+15u]=(uint16_t)(n<<4u); r[slot+16u]=0x60u;
            for (uint32_t i=0u;i<3u;++i) {
                r[slot+6u+i]=r[0x612u+i];
                r[slot+9u+i]=r[CLEAN_ACK_PEER+i];
            }
        }
        if (!bad) {
            uint16_t invalid[0x1800];
            const uint32_t fields[]={4u,6u,7u,8u,9u,10u,11u,15u,16u};
            for (uint32_t n=0u;n<3u;++n)
                for (uint32_t i=0u;i<sizeof(fields)/sizeof(fields[0]);++i) {
                    memcpy(invalid,r,sizeof(r));
                    invalid[CLEAN_AMPDU_PROBE_SHM+8u+44u*n+fields[i]]^=1u;
                    assert(psm_run(invalid,0,AMPDU_BA_ENTER)==AMPDU_SHM_WAIT);
                    assert(!invalid[CLEAN_AMPDU_BA_WORD+2u] &&
                           invalid[CLEAN_AMPDU_BA_WORD+4u]==1u);
                }
            for (uint32_t count=0u;count<3u;++count) {
                memcpy(invalid,r,sizeof(r)); invalid[CLEAN_AMPDU_PROBE_SHM]=(uint16_t)count;
                assert(psm_run(invalid,0,AMPDU_BA_ENTER)==AMPDU_SHM_WAIT);
                assert(!invalid[CLEAN_AMPDU_BA_WORD+2u]);
            }
            memcpy(invalid,r,sizeof(r)); invalid[CLEAN_AMPDU_BA_WORD]=0u;
            assert(psm_run(invalid,0,AMPDU_BA_ENTER)==AMPDU_SHM_WAIT);
            assert(!invalid[CLEAN_AMPDU_BA_WORD+2u]);
        }
        assert(psm_run(r,0,AMPDU_BA_ENTER)==
               (bad==7u ? AMPDU_SHM_WAIT : AMPDU_BA_EOF_WAIT));
        assert(r[CLEAN_AMPDU_BA_WORD+1u]==(7u^bad));
        if (bad==7u) { assert(!r[CLEAN_AMPDU_BA_WORD+2u]); continue; }
        assert(!r[0x1081u]);
        r[0x100du]=0x4000u; r[0x1119u]=100u;
        assert(psm_run(r,0,AMPDU_BA_EOF_WAIT)==AMPDU_BA_START_WAIT);
        assert(r[0x10e3u]==0x94u && r[0x10ebu]==4u && !r[0x10ecu]);
        assert(r[0x10edu]==(7u^bad) && !r[0x10eeu] && !r[0x10efu] && !r[0x10f0u]);
        assert(r[0x1084u]==65535u && r[0x1085u]==1u && r[0x10b5u]==34u);
        assert(r[0x1080u]==0x4021u && r[0x1081u]==0xd0u);
        assert(r[CLEAN_AMPDU_BA_WORD+2u]==1u && !r[CLEAN_AMPDU_BA_WORD+3u]);
        r[0x1148u]=0x100u; r[0x1119u]=110u;
        assert(psm_run(r,0,AMPDU_BA_START_WAIT)==AMPDU_BA_END_WAIT);
        assert(r[CLEAN_AMPDU_BA_WORD+3u]==1u && r[CLEAN_AMPDU_BA_WORD+7u]==110u);
        r[0x1148u]=0u; r[0x1119u]=160u;
        assert(psm_run(r,0,AMPDU_BA_END_WAIT)==AMPDU_SHM_WAIT);
        assert(r[CLEAN_AMPDU_BA_WORD+9u]==160u && !r[CLEAN_AMPDU_BA_WORD+4u]);
        assert(r[0x1782u]==(bad & 4u ? 2u : 1u));
        assert(!r[CLEAN_ACK_SENT] && !r[CLEAN_CTS_SENT]);
        const uint32_t timeout[]={AMPDU_BA_EOF_DEC,AMPDU_BA_START_DEC,AMPDU_BA_END_DEC};
        for (uint32_t i=0u;i<3u;++i) {
            r[0x601u]=0u; r[0x1780u]=1u;
            r[0x1080u]=i ? 0x4021u : 0u; r[0x1081u]=i ? 0xd0u : 0u;
            uint16_t errors=r[CLEAN_AMPDU_BA_WORD+4u];
            assert(psm_run(r,0,timeout[i])==AMPDU_SHM_WAIT);
            assert(r[CLEAN_AMPDU_BA_WORD+4u]==errors+1u);
            assert(!r[0x1080u] && !r[0x1081u]);
        }
    }
    puts("PSM diagnostic BA: retained-only bitmap, pinned header, EOF gate, 32-byte template, PHY start/end, bounded cancellation and outcome preservation; NOT RF/SIFS qualification");
#endif
}

static void aggregate_terminal_handoff(void) {
#if CLEAN_RX_AMPDU_SHM
    const uint32_t status=CLEAN_AMPDU_IF_SHM+CLEAN_AMPDU_SHM_STATUS_WORD;
    assert(instruction(AMPDU_SHM_WAIT)==
           WORD(0xd1u,0x601u,0x1800u,AMPDU_SHM_WAIT));
    for (uint32_t count=1u;count<=3u;++count)
        for (uint32_t outcome=1u;outcome<=2u;++outcome)
            for (uint32_t eof=0u;eof<2u;++eof) {
                uint16_t r[0x1800]={0}, frozen[0x1800];
                r[CLEAN_AMPDU_PROBE_SHM]=(uint16_t)count;
                r[0x100du]=(uint16_t)(eof<<14u);
                r[0x1782u]=(uint16_t)outcome;
                r[0x1119u]=65530u; r[0x111au]=7u;
                int commit=count==3u || eof;
                assert(psm_run(r,0,AMPDU_IF_ADVANCE)==
                       (commit ? AMPDU_SHM_WAIT : 163u));
                assert(r[CLEAN_AMPDU_IF_SHM+1u]==count);
                if (!commit) {
                    assert(!r[status+2u] && !r[0x601u]);
                    continue;
                }
                assert(r[0x601u]==CLEAN_AMPDU_SHM_HELD_STATE);
                assert(r[status]==65530u && r[status+1u]==7u &&
                       r[status+2u]==1u && !r[status+3u]);
                memcpy(frozen,r,sizeof(r));
                assert(psm_run(r,0,AMPDU_SHM_WAIT)==AMPDU_SHM_WAIT);
                assert(!memcmp(r,frozen,sizeof(r)));
                r[0x601u]=0u; r[0x1119u]=10u; r[0x111au]=8u;
                assert(psm_run(r,0,AMPDU_SHM_WAIT)==(outcome==2u ? 263u : 266u));
                assert(r[status+2u]==1u && r[status+3u]==1u &&
                       r[status+8u]==10u && r[status+9u]==8u);
                assert(((uint32_t)r[status+8u] |
                        ((uint32_t)r[status+9u]<<16u)) -
                       ((uint32_t)r[status] | ((uint32_t)r[status+1u]<<16u))==16u);
                assert(r[CLEAN_AMPDU_PROBE_SHM]==count);
                assert(!r[CLEAN_ACK_SENT] && !r[CLEAN_CTS_SENT]);
            }
    uint16_t r[0x1800]={0};
    r[status+2u]=65535u; r[status+3u]=65535u;
    r[0x1782u]=1u;
    assert(psm_run(r,0,AMPDU_SHM_COMMIT)==AMPDU_SHM_WAIT);
    assert(!r[status+2u] && r[status+3u]==65535u);
    r[0x601u]=0u;
    assert(psm_run(r,0,AMPDU_SHM_WAIT)==266u && !r[status+3u]);
    puts("PSM SHM terminal: explicit immutable lease, good/bad final, partial EOF, held wait, timestamp wrap and release without ordinary RX; NOT BA/SIFS qualification");
#endif
}

static void passive_ht_observer(void) {
#if CLEAN_RX_HT_OBSERVER
    uint16_t r[0x1800] = {0};
    r[0x1782u] = 24u;
    r[0x1009u] = 2u;
    r[CLEAN_SHM_FRAME+1u] = 0xae00u;
    r[CLEAN_SHM_FRAME+2u] = 0x0f00u;
    r[CLEAN_SHM_FRAME+3u] = 0x0228u;
    for (uint32_t i = 0u; i < 3u; ++i)
        r[CLEAN_OBSERVER_SHM+3u+i] = r[CLEAN_SHM_FRAME+9u+i] = (uint16_t)(0x1234u+i);
    for (uint32_t gate = 0u; gate < 2u; ++gate) {
        r[CLEAN_OBSERVER_SHM] = (uint16_t)gate;
        for (uint32_t n = 0u; n < CLEAN_OBSERVER_RECORDS; ++n) {
            uint16_t before = r[CLEAN_OBSERVER_SHM+1u];
            assert(psm_run(r,0,OBSERVER_ENTER) == 148u);
            assert(!r[0x7a1u] && r[0x1782u] == 24u);
            assert(r[CLEAN_OBSERVER_SHM+1u] == before + gate);
            if (gate) {
                uint32_t record = CLEAN_OBSERVER_SHM+CLEAN_OBSERVER_HEADER_WORDS+
                    n*CLEAN_OBSERVER_RECORD_WORDS;
                assert(r[record+6u] == 0xae00u && r[record+7u] == 0x0f00u &&
                       r[record+8u] == 0x0228u);
            }
            assert(!r[0x1080u] && !r[0x1081u] && !r[0x10b4u]);
        }
    }
    uint16_t saved[CLEAN_OBSERVER_WORDS];
    memcpy(saved,r+CLEAN_OBSERVER_SHM,sizeof(saved));
    assert(psm_run(r,0,OBSERVER_ENTER) == 148u);
    assert(r[CLEAN_OBSERVER_SHM+2u] == 1u);
    assert(!memcmp(saved+CLEAN_OBSERVER_HEADER_WORDS,
                   r+CLEAN_OBSERVER_SHM+CLEAN_OBSERVER_HEADER_WORDS,
                   sizeof(saved)-2u*CLEAN_OBSERVER_HEADER_WORDS));
    r[CLEAN_OBSERVER_SHM+2u] = 65535u;
    assert(psm_run(r,0,OBSERVER_ENTER) == 148u);
    assert(r[CLEAN_OBSERVER_SHM+2u] == 65535u);
    memset(r+CLEAN_OBSERVER_SHM+1u,0,4u);
    r[CLEAN_SHM_FRAME+1u] = 11u;
    assert(psm_run(r,0,OBSERVER_ENTER) == 148u);
    assert(!r[CLEAN_OBSERVER_SHM+1u]);
    const uint32_t record = CLEAN_OBSERVER_SHM+CLEAN_OBSERVER_HEADER_WORDS;
    for (uint32_t fault = 0u; fault < 10u; ++fault) {
        memset(r,0,sizeof(r));
        r[CLEAN_OBSERVER_SHM] = 1u;
        r[0x1009u] = 2u; r[0x100cu] = 176u; r[0x1782u] = 24u;
        r[CLEAN_SHM_FRAME+1u] = 0xae00u;
        r[CLEAN_SHM_FRAME+2u] = 0x0f00u;
        r[CLEAN_SHM_FRAME+3u] = 0x0228u;
        for (uint32_t i = 0u; i < 20u; ++i)
            r[CLEAN_SHM_FRAME+4u+i] = (uint16_t)(0x1120u+i);
        assert(psm_run(r,0,OBSERVER_START) == 130u);
        assert(psm_run(r,0,HT_TRACE_ENTER) == 148u);
        assert(r[CLEAN_OBSERVER_PENDING] == 1u && r[record+10u] == 1u);
        if (fault < 3u) r[CLEAN_SHM_FRAME+1u+fault] ^= 1u;
        if (fault == 3u) ++r[CLEAN_OBSERVER_EPOCH];
        if (fault == 4u) r[0x100cu] = 45u;
        if (fault == 5u) assert(psm_run(r,0,OBSERVER_START) == 130u);
        if (fault == 6u) {
            r[CLEAN_SHM_FRAME+1u] = 11u;
            assert(psm_run(r,0,HT_TRACE_ENTER) == 148u);
            r[CLEAN_SHM_FRAME+1u] = 0xae00u;
        }
        int bad = fault == 7u;
        assert(psm_run(r,0,bad ? HT_TRACE_BAD : HT_TRACE_GOOD) == (bad ? 263u : 198u));
        assert(!r[CLEAN_OBSERVER_PENDING]);
        assert(r[CLEAN_OBSERVER_HELD] == (fault >= 8u ? 1u : 0u));
        if (fault >= 7u) {
            assert(r[record+11u] == (bad ? 2u : 1u));
            for (uint32_t i = 0u; i < 20u; ++i)
                assert(r[record+18u+i] == (uint16_t)(0x1120u+i));
            assert(r[record+38u] == 1u && r[record+39u] == 1u);
        } else {
            assert(r[record+11u] == (fault < 5u ? 3u : fault == 5u ? 7u : 8u));
            assert(!r[record+18u]);
        }
        uint16_t committed[CLEAN_OBSERVER_RECORD_WORDS];
        memcpy(committed,r+record,sizeof(committed));
        assert(psm_run(r,0,HT_TRACE_GOOD) == 198u);
        assert(!memcmp(committed,r+record,sizeof(committed)));
        assert(!r[0x1080u] && !r[0x1081u] && !r[0x10b4u]);
    }
    puts("PSM observer: fresh epoch/prefix/terminal ownership, bad/short/mismatched/stale rejection and immutable one-shot MAC snapshots; NOT RF");
    for (uint32_t path = 0u; path < 6u; ++path) {
        memset(r,0,sizeof(r));
        r[CLEAN_OBSERVER_SHM] = r[CLEAN_OBSERVER_PENDING] = 1u;
        r[CLEAN_OBSERVER_EPOCH] = r[record+10u] = 9u;
        r[0x100cu] = 862u; r[0x100du] = 0x1000u;
        r[0x1004u] = 0x8002u; r[0x1002u] = 0x3ffu;
        r[0x102au] = 0x2345u; r[0x102bu] = 0x3456u; r[0x10afu] = 0x4567u;
        uint32_t expected = path == 0u ? 4u : path == 1u ? 5u :
                            path < 5u ? 6u : 7u;
        if (path == 0u) assert(psm_run(r,0,DCF_BAD_LENGTH) == 266u && r[0x7a9u] == 1u);
        else if (path == 1u) assert(psm_run(r,0,OBSERVER_EXIT_ABORT) == 266u);
        else if (path == 5u) assert(psm_run(r,0,OBSERVER_START) == 130u);
        else {
            uint32_t events = path == 2u ? 0u : 1u<<6u, steps;
            if (path == 4u) r[0x100du] |= 0x100u;
            r[0x1780u] = 1u;
            assert(receive_run(r,&events,RX_COMPLETE,&steps) == 262u);
            assert(steps < 80u && !r[0x1780u]);
            assert(r[record+40u] == path-1u);
            assert(r[record+46u] == (path == 4u ? 862u : 0u));
        }
        assert(r[record+11u] == expected && r[record+14u] == 862u);
        assert(r[record+41u] == 0x8002u && r[record+42u] == 0x3ffu &&
               r[record+43u] == 0x2345u && r[record+44u] == 0x3456u &&
               r[record+45u] == 0x4567u);
        assert(!r[CLEAN_OBSERVER_PENDING] && !r[CLEAN_OBSERVER_HELD]);
        assert(!r[0x1080u] && !r[0x1081u] && !r[0x10b4u]);
    }
    memset(r,0,sizeof(r));
    r[CLEAN_OBSERVER_PENDING] = 1u;
    r[0x100cu] = 13u; r[0x100du] = 0x4000u;
    uint32_t pending = 1u<<6u, steps;
    assert(receive_run(r,&pending,RX_COMPLETE,&steps) == 263u);
    assert(r[record+11u] == 3u && r[record+14u] == 13u &&
           !r[CLEAN_OBSERVER_PENDING] && !r[CLEAN_OBSERVER_HELD]);
    r[record+11u] = 1u;
    uint16_t committed[CLEAN_OBSERVER_RECORD_WORDS];
    memcpy(committed,r+record,sizeof(committed));
    r[CLEAN_OBSERVER_PENDING] = 1u;
    assert(psm_run(r,0,OBSERVER_EXIT_ABORT) == 266u);
    assert(!memcmp(committed,r+record,sizeof(committed)));
    memset(r,0,sizeof(r));
    r[CLEAN_OBSERVER_PENDING] = 1u;
    r[CLEAN_OBSERVER_PROGRESS] = 3u;
    r[CLEAN_OBSERVER_HINT_LENGTH] = 304u;
    r[0x100cu] = 862u; r[0x100du] = 0x1100u; r[0x1780u] = 1u;
    pending = 1u<<6u;
    assert(receive_run(r,&pending,RX_COMPLETE,&steps) == 262u);
    assert(r[record+11u] == 6u && r[record+46u] == 304u);
    puts("PSM observer exits: length, abort, watchdog, event6 and good-only hint remain distinct; no early delivery or TX");
#endif
}

#if CLEAN_RX_HT_EARLY_SAVE
static void early_snapshot_seed(uint16_t r[0x1800], uint32_t length) {
    memset(r,0,0x1800u*sizeof(*r));
    r[CLEAN_OBSERVER_SHM] = r[CLEAN_OBSERVER_PENDING] = 1u;
    r[CLEAN_OBSERVER_EPOCH] = 9u;
    r[0x100cu] = (uint16_t)(length+6u); r[0x100du] = 0x1100u;
    r[0x1001u] = CLEAN_SHM_FRAME; r[0x1004u] = 0x8802u;
    r[0x102au] = 0x222u; r[0x10afu] = 0x1000u;
    r[0x1024u] = 0xa3ffu;
    r[0x1780u] = 1u; r[0x1782u] = 0x1234u; r[0x1783u] = 0x5678u;
    r[CLEAN_SHM_FRAME+1u] = (uint16_t)(((length+4u)&255u)<<8u);
    r[CLEAN_SHM_FRAME+2u] = (uint16_t)(0x0f00u|((length+4u)>>8u));
    for (uint32_t i = 0u; i < CLEAN_OBSERVER_EARLY_MAX/2u; ++i) {
        r[CLEAN_SHM_FRAME+4u+i] = (uint16_t)(0x3000u+i);
        r[CLEAN_OBSERVER_EARLY_SHM+CLEAN_OBSERVER_EARLY_HEADER+i] = 0xbeefu;
    }
    for (uint32_t i = 0x600u; i < CLEAN_SHM_FRAME; ++i) r[i] = 0xa55au;
}

static void passive_early_snapshot(void) {
    uint16_t r[0x1800];
    for (uint32_t length = 44u; length <= CLEAN_OBSERVER_EARLY_MAX; ++length) {
        early_snapshot_seed(r,length);
        uint32_t events = 1u<<6u, steps;
        assert(receive_run(r,&events,RX_COMPLETE,&steps) == 262u && steps < 1300u);
        assert(r[CLEAN_OBSERVER_EARLY_SHM] == 1u &&
               r[CLEAN_OBSERVER_EARLY_SHM+1u] == 9u &&
               r[CLEAN_OBSERVER_EARLY_SHM+2u] == length+6u &&
               r[CLEAN_OBSERVER_EARLY_SHM+10u] == 1u);
        for (uint32_t i = 0u; i < CLEAN_OBSERVER_EARLY_MAX/2u; ++i)
            assert(r[CLEAN_OBSERVER_EARLY_SHM+CLEAN_OBSERVER_EARLY_HEADER+i] ==
                   (i < (length+1u)/2u ? (uint16_t)(0x3000u+i) : 0xbeefu));
        for (uint32_t i = 0x600u; i < CLEAN_SHM_FRAME; ++i) assert(r[i] == 0xa55au);
        assert(r[0x1782u] == 0x1234u && r[0x1783u] == 0x5678u &&
               events == (1u<<6u) && !r[CLEAN_OBSERVER_HELD] &&
               !r[0x1080u] && !r[0x1081u] && !r[0x10b4u]);
        assert(r[0x1001u] == CLEAN_SHM_FRAME && r[0x1004u] == 0x8802u &&
               r[0x102au] == 0x222u && r[0x10afu] == 0x1000u);
#if CLEAN_RX_HT_ADVANCE
        assert(!r[CLEAN_OBSERVER_HANDOFF_SHM]);
        if (length == 170u || length == 298u) {
            printf("PSM fast snapshot %u bytes: %u modeled instructions including timeout; NOT RF timing\n",
                   length,steps);
            assert(steps <= (length == 170u ? 200u : 300u));
        }
#endif
    }
    for (uint32_t fault = 1u; fault <= 16u; ++fault) {
        early_snapshot_seed(r,298u);
        uint32_t events = 1u<<6u, steps;
        if (fault <= 5u) early_copy_fault = fault;
        if (fault == 6u) r[0x100cu] = CLEAN_OBSERVER_EARLY_MAX+7u;
        if (fault == 7u) r[0x100cu] = 49u;
        if (fault == 8u) r[CLEAN_SHM_FRAME+1u] |= 1u;
        if (fault == 9u) r[CLEAN_SHM_FRAME+2u] &= ~0x800u;
        if (fault == 10u) r[CLEAN_OBSERVER_SHM] = 0u;
        if (fault == 11u) r[CLEAN_OBSERVER_PENDING] = 17u;
        if (fault == 12u) r[0x100du] &= ~0x100u;
        if (fault == 13u) events = 0u;
        if (fault == 14u) r[0x100du] |= 0x4000u;
        if (fault == 15u) r[CLEAN_OBSERVER_PENDING] = 0u;
        if (fault == 16u) r[0x100du] |= 0x8000u;
        assert(receive_run(r,&events,RX_COMPLETE,&steps) == 262u);
        assert(!r[CLEAN_OBSERVER_EARLY_SHM]);
        assert(r[CLEAN_OBSERVER_EARLY_SHM+11u] == (fault == 6u ? 1u : 0u));
        assert(r[0x1782u] == 0x1234u && r[0x1783u] == 0x5678u);
    }
    early_snapshot_seed(r,170u);
    uint32_t events = 1u<<6u, steps;
    early_status_after_hint = 1u;
    assert(receive_run(r,&events,RX_COMPLETE,&steps) == 262u);
    assert(r[CLEAN_OBSERVER_EARLY_SHM] == 1u &&
           r[CLEAN_OBSERVER_EARLY_SHM+3u] == 0x1100u && r[0x100du] == 0x5100u);
    early_snapshot_seed(r,170u);
    events = 1u<<6u;
    assert(receive_run(r,&events,RX_COMPLETE,&steps) == 262u);
    assert(r[CLEAN_OBSERVER_EARLY_SHM+10u] == 1u);
    for (uint32_t attempt = 0u; attempt < 3u; ++attempt) {
        uint16_t before[CLEAN_OBSERVER_EARLY_WORDS];
        memcpy(before,r+CLEAN_OBSERVER_EARLY_SHM,sizeof(before));
        r[CLEAN_OBSERVER_PENDING] = (uint16_t)(attempt+2u);
        r[CLEAN_OBSERVER_PROGRESS] = 0u; r[0x1780u] = 1u;
        if (attempt) {
            r[CLEAN_SHM_FRAME+1u] = 0x5e00u;
            r[CLEAN_SHM_FRAME+2u] = 0x0f03u;
        }
        r[CLEAN_SHM_FRAME+4u] = (uint16_t)(0x7000u+attempt);
        assert(receive_run(r,&events,RX_COMPLETE,&steps) == 262u);
        if (attempt == 1u)
            assert(r[CLEAN_OBSERVER_EARLY_SHM] == 3u &&
                   r[CLEAN_OBSERVER_EARLY_SHM+10u] == 2u &&
                   r[CLEAN_OBSERVER_EARLY_SHM+CLEAN_OBSERVER_EARLY_HEADER] == 0x7001u);
        else assert(!memcmp(before,r+CLEAN_OBSERVER_EARLY_SHM,sizeof(before)));
    }
    puts("PSM early snapshot: every supported length, odd tails, control SHM/register preservation, stale-copy rejection and single-to-multi priority pass");
#if CLEAN_RX_HT_ADVANCE
    for (uint32_t order = 0u; order < 2u; ++order)
    for (uint32_t fault = 0u; fault < 8u; ++fault) {
        uint32_t first = order ? 298u : 170u, second = order ? 170u : 298u;
        early_snapshot_seed(r,first);
        r[CLEAN_SHM_FRAME+1u] = 0xde00u;
        r[CLEAN_SHM_FRAME+2u] = 0x0f01u;
        r[CLEAN_SHM_FRAME+3u] = 0x03b0u;
        r[0x102bu] = (uint16_t)second; r[0x1078u] = 0xffffu;
        events = (1u<<6u)|(1u<<3u);
        if (fault == 1u) r[0x102bu] = 554u;
        if (fault == 2u) events &= ~(1u<<3u);
        if (fault == 3u) r[CLEAN_SHM_FRAME+1u] = 0x5e00u;
        if (fault == 4u) r[0x100du] |= 0x4000u;
        if (fault == 5u) r[0x100du] |= 0x8000u;
        if (fault == 6u) r[CLEAN_OBSERVER_ADVANCE] = 1u;
        if (fault == 7u) r[0x102bu] = (uint16_t)first;
        uint32_t next = receive_run(r,&events,RX_COMPLETE,&steps);
        if (!fault) {
            assert(next == 163u && r[CLEAN_OBSERVER_ADVANCE] == 1u);
            assert(r[CLEAN_OBSERVER_ADVANCE+1u] == second && r[0x102au] == second);
            assert(r[0x1001u] == CLEAN_SHM_FRAME && r[0x1078u] == 0xff7fu);
            assert(r[0x1780u] == 65535u && !(events&(1u<<6u)));
            assert(r[CLEAN_OBSERVER_PENDING] == 1u && r[CLEAN_OBSERVER_EPOCH] == 9u);
            assert(r[CLEAN_SHM_FRAME+1u] == 0xde00u &&
                   r[CLEAN_SHM_FRAME+2u] == 0x0f01u &&
                   r[CLEAN_SHM_FRAME+3u] == 0x03b0u);
            assert(steps <= 400u);
            assert(r[CLEAN_OBSERVER_HANDOFF_SHM] == 1u &&
                   r[CLEAN_OBSERVER_HANDOFF_SHM+1u] == 9u &&
                   r[CLEAN_OBSERVER_HANDOFF_SHM+2u] == 7u &&
                   !r[CLEAN_OBSERVER_HANDOFF_SHM+3u]);
            for (uint32_t phase = 0u; phase < CLEAN_OBSERVER_HANDOFF_RECORDS; ++phase) {
                const uint16_t *h = r+CLEAN_OBSERVER_HANDOFF_SHM+
                    CLEAN_OBSERVER_HANDOFF_HEADER+phase*CLEAN_OBSERVER_HANDOFF_RECORD_WORDS;
                assert(h[2] == first+6u && h[3] == 0x1100u &&
                       h[6] == CLEAN_SHM_FRAME && h[8] == second);
                assert(h[5] == (phase == 2u ? ((0x8802u&~0x3fc0u)|0x20c0u) : 0x8802u));
                assert(h[7] == (phase == 2u ? second : 0x222u));
                assert(h[11] == (phase == 2u ? 0xff7fu : 0xffffu));
                assert(h[10] == 0xa3ffu);
            }
        } else {
            assert(next == (fault == 4u ? 197u : 262u) &&
                   r[CLEAN_OBSERVER_ADVANCE] == (fault == 6u));
            assert(r[0x1001u] == CLEAN_SHM_FRAME && r[0x102au] == 0x222u);
        }
        assert(r[0x1782u] == 0x1234u && r[0x1783u] == 0x5678u);
        assert(!r[0x1080u] && !r[0x1081u] && !r[0x10b4u]);
    }
    puts("PSM bounded advance: exact 478-byte 170/298 pair in both orders, GoodFCS, EOF/abort exclusion, one-shot arm, prefix/epoch preservation and no TX; NOT RF");
    early_snapshot_seed(r,170u);
    r[CLEAN_SHM_FRAME+1u] = 0xde00u;
    r[CLEAN_SHM_FRAME+2u] = 0x0f01u;
    r[0x102bu] = 298u;
    events = (1u<<6u)|(1u<<3u);
    handoff_state_after_arm = 1u;
    assert(receive_run(r,&events,RX_COMPLETE,&steps) == 163u);
    for (uint32_t phase = 0u; phase < CLEAN_OBSERVER_HANDOFF_RECORDS; ++phase) {
        const uint16_t *h = r+CLEAN_OBSERVER_HANDOFF_SHM+
            CLEAN_OBSERVER_HANDOFF_HEADER+phase*CLEAN_OBSERVER_HANDOFF_RECORD_WORDS;
        assert(h[2] == (phase == 2u ? 0u : 176u));
        assert(h[3] == (phase == 2u ? 0x1000u : 0x1100u));
    }
    assert(r[CLEAN_OBSERVER_EARLY_SHM+2u] == 176u &&
           r[CLEAN_OBSERVER_EARLY_SHM+3u] == 0x1100u &&
           r[CLEAN_OBSERVER_HANDOFF_SHM+2u] == 7u);
    puts("PSM handoff trace: three phases, modeled post-arm state change and immutable terminal/new-RX retention; NOT RF timing");
    for (uint32_t outcome = 0u; outcome < 4u; ++outcome) {
        early_snapshot_seed(r,170u);
        const uint32_t record = CLEAN_OBSERVER_SHM+CLEAN_OBSERVER_HEADER_WORDS;
        r[CLEAN_OBSERVER_SHM+1u] = 1u; r[record+10u] = 9u;
        r[record+6u] = r[CLEAN_SHM_FRAME+1u] = 0xde00u;
        r[record+7u] = r[CLEAN_SHM_FRAME+2u] = 0x0f01u;
        r[record+8u] = r[CLEAN_SHM_FRAME+3u] = 0x03b0u;
        r[0x102bu] = 298u; events = (1u<<6u)|(1u<<3u);
        assert(receive_run(r,&events,RX_COMPLETE,&steps) == 163u);
        uint16_t retained[CLEAN_OBSERVER_EARLY_RETAIN_WORDS];
        memcpy(retained,r+CLEAN_OBSERVER_EARLY_SHM,sizeof(retained));
        uint16_t handoff[CLEAN_OBSERVER_HANDOFF_HEADER+
            CLEAN_OBSERVER_HANDOFF_RECORDS*CLEAN_OBSERVER_HANDOFF_RECORD_WORDS];
        memcpy(handoff,r+CLEAN_OBSERVER_HANDOFF_SHM,sizeof(handoff));
        /* Inject a modeled hardware body; this does not validate the RF handoff. */
        r[0x100cu] = 304u; r[0x100du] = 0x5100u;
        for (uint32_t i = 0u; i < 20u; ++i) r[CLEAN_SHM_FRAME+4u+i] = (uint16_t)(0x4000u+i);
        uint32_t target = outcome == 0u ? HT_TRACE_GOOD : outcome == 1u ? HT_TRACE_BAD :
            outcome == 2u ? OBSERVER_EXIT_ABORT : OBSERVER_EXIT_TIMEOUT;
        uint32_t end = psm_run(r,0,target);
        assert(end == (outcome == 0u ? 198u : outcome == 1u ? 263u : outcome == 2u ? 266u : 262u));
        assert(r[record+11u] == (outcome == 0u ? 1u : outcome == 1u ? 2u : outcome == 2u ? 5u : 6u));
        assert(!r[CLEAN_OBSERVER_PENDING] && r[CLEAN_OBSERVER_HELD] == (outcome == 0u));
        assert(r[record+14u] == 304u && r[record+38u] == 9u && r[record+39u] == 1u);
        if (outcome < 2u) assert(r[record+18u] == 0x4000u);
        if (outcome < 2u) {
            assert(r[CLEAN_OBSERVER_SECOND_SHM] == 1u &&
                   r[CLEAN_OBSERVER_SECOND_SHM+1u] == 9u &&
                   r[CLEAN_OBSERVER_SECOND_SHM+2u] == 304u &&
                   r[CLEAN_OBSERVER_SECOND_SHM+10u] == outcome+1u);
            assert(r[CLEAN_OBSERVER_SECOND_SHM+CLEAN_OBSERVER_SECOND_HEADER] == 0x4000u);
            for (uint32_t i = 0u; i < CLEAN_OBSERVER_SECOND_MAX/2u; ++i)
                assert(r[CLEAN_OBSERVER_SECOND_SHM+CLEAN_OBSERVER_SECOND_HEADER+i] ==
                       r[CLEAN_SHM_FRAME+4u+i]);
            assert(r[0x600u] == 0xa55au);
        } else assert(!r[CLEAN_OBSERVER_SECOND_SHM]);
        assert(psm_run(r,0,OBSERVER_START) == 130u);
        assert(!r[CLEAN_OBSERVER_PENDING] && !r[CLEAN_OBSERVER_HELD] && r[CLEAN_OBSERVER_EPOCH] == 10u);
        assert(!memcmp(retained,r+CLEAN_OBSERVER_EARLY_SHM,sizeof(retained)));
        assert(!memcmp(handoff,r+CLEAN_OBSERVER_HANDOFF_SHM,sizeof(handoff)));
        assert(!r[0x1080u] && !r[0x1081u] && !r[0x10b4u]);
    }
    puts("PSM bounded advance: modeled second-body terminal/bad/abort/watchdog ownership and new-RX retention pass; NOT RF/DMA recovery qualification");
    for (uint32_t fault = 0u; fault < 6u; ++fault) {
        early_snapshot_seed(r,170u);
        r[CLEAN_OBSERVER_EARLY_SHM] = 1u;
        r[CLEAN_OBSERVER_EARLY_SHM+1u] = 9u;
        r[CLEAN_OBSERVER_ADVANCE] = 1u;
        r[CLEAN_OBSERVER_OUTCOME] = 1u;
        if (fault == 0u) r[CLEAN_OBSERVER_ADVANCE] = 0u;
        if (fault == 1u) r[CLEAN_OBSERVER_PENDING] = 2u;
        if (fault == 2u) r[CLEAN_OBSERVER_EPOCH] = 10u;
        if (fault == 3u) r[0x100cu] = 49u;
        if (fault == 4u) r[0x100cu] = 307u;
        if (fault == 5u) r[CLEAN_OBSERVER_SECOND_SHM] = 7u;
        uint16_t retained[CLEAN_OBSERVER_EARLY_RETAIN_WORDS];
        memcpy(retained,r+CLEAN_OBSERVER_EARLY_SHM,sizeof(retained));
        assert(psm_run(r,0,OBSERVER_SECOND) == 198u);
        assert(r[CLEAN_OBSERVER_SECOND_SHM] == (fault == 5u ? 7u : 0u));
        assert(!memcmp(retained,r+CLEAN_OBSERVER_EARLY_SHM,sizeof(retained)));
        assert(r[0x600u] == 0xa55au);
        assert(!r[0x1080u] && !r[0x1081u] && !r[0x10b4u]);
    }
    puts("PSM continuation snapshot: exact copy bounds, missing/wrong owner, epoch, short/oversize and immutable commit guards pass");
#endif
}
#endif

int main(void) {
#if CLEAN_RX_HT_EARLY_SAVE
    passive_early_snapshot();
#endif
    passive_ht_observer();
#if CLEAN_RX_AMPDU_DMA
    for (uint32_t outcome=0u;outcome<=2u;++outcome)
        for (uint32_t count=1u;count<=3u;++count)
            for (uint32_t eof=0u;eof<2u;++eof) {
                uint16_t r[0x1800]={0};
                r[CLEAN_AMPDU_DMA_PENDING]=(uint16_t)outcome;
                r[CLEAN_AMPDU_PROBE_SHM]=(uint16_t)count;
                r[CLEAN_AMPDU_PROBE_SHM+3u]=(uint16_t)(eof<<14u);
                r[CLEAN_AMPDU_DMA_SUBMITTED]=65535u;
                r[0x1781u]=86u;
                assert(psm_run(r,0,AMPDU_DMA_LENGTH)==220u);
                assert(r[0x7f4u]==(!outcome || count==1u ? 86u : 80u));
                assert(psm_run(r,0,AMPDU_DMA_DONE)==
                       (!outcome ? 253u : count==3u || eof ? 266u : 163u));
                assert(!r[CLEAN_AMPDU_DMA_PENDING]);
                assert(r[CLEAN_AMPDU_DMA_SUBMITTED]==(outcome ? 0u : 65535u));
                assert(r[0x601u]==(outcome ? 0u : 14u));
                assert(!r[CLEAN_ACK_SENT] && !r[CLEAN_CTS_SENT]);
            }
#endif
    aggregate_terminal_handoff();
    aggregate_ba_transmit();
    aggregate_handoff_trace();
    aggregate_interface_trace();
    aggregate_probe_trace();
    transmit_cleanup();
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
    receive_completion();
    receive_trace();
    ht_receive_trace();
    ht_failure_capture();
    authentication_failure_capture();
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
#if CLEAN_RX_HT_REARM_WAIT
            for (uint32_t event = 0u; event < 4u; ++event)
                for (uint32_t clear = 0u; clear < 2u; ++clear)
                    assert(rearm_flush(delays[i],bits,
                        ((event&1u) ? 8u : 0u) | ((event&2u) ? 64u : 0u),clear,0) ==
                           ((bits & 0x14u) == 0u || delays[i] < 65535u));
#endif
        }
    }
#if CLEAN_RX_HT_REARM_RECOVER
    for (uint32_t events = 0u; events < 64u; ++events) {
        const uint32_t bits[] = {3u,5u,6u,11u,13u,14u};
        uint32_t pending = 1u << 9u;
        for (uint32_t i = 0u; i < 6u; ++i)
            if (events&(1u<<i)) pending |= 1u<<bits[i];
        assert(rearm_flush(65536u,0x14u,pending,0,1));
        assert(!rearm_flush(65536u,0x14u,pending,0,0));
    }
#endif
    puts("PSM: image identity, capture bounds, RX streaming and startup flush OK");
    puts("PSM: single-SHM inputs, pinned ACK/CTS addresses, bounded ACK suppression at all 12 rates and 100000 native ACK rearms OK; NOT RF timing");
    return 0;
}
