#ifndef CLEAN_RX_TX_DIAG_H
#define CLEAN_RX_TX_DIAG_H

#include "types.h"
#include "frame_limits.h"

/* Keep legacy length bits 8..17; use unused bits 29..30 for the extension. */
static inline uint32_t clean_tx_length_bits(uint32_t length) {
    return ((length & 0x3ffu) << 8u) | ((length & 0xc00u) << 19u);
}

static inline uint32_t clean_tx_length(uint32_t command) {
    return ((command >> 8u) & 0x3ffu) | ((command >> 19u) & 0xc00u);
}

#define CLEAN_TX_DIAG_MAGIC 0x33445854u
#define CLEAN_TX_DIAG_OFFSET 296u
#define CLEAN_TX_REARM_RX (1u << 27)
#define CLEAN_TX_MATCH_SOURCE (1u << 28)
#define CLEAN_TX_CCK_SHORT 10u
#define CLEAN_RX_PHASE_MAGIC 0x5258u
#define CLEAN_TX_PHY_COUNT 26u
#define CLEAN_TX_PHY_REGS {0x631u,0x62au,0x439u,0x4b0u,0x43au,0x4b5u,0x4fbu,0x46eu, \
                          0x44cu,0x44du,0x938u,0x939u,0x440u,0x980u,0x981u,0x986u, \
                          0x448u,0x4a4u,0x4d0u,0x4a5u,0x9acu,0x9f4u,0x4d7u,0x6d3u,0x6dau,0x987u}
#define CLEAN_TX_RADIO_REGS {0x79u,0xa4u,0x03u,0x34u,0x72u,0x73u,0x76u,0x77u}
#define CLEAN_TX_IHR_REGS {0x50cu,0x514u,0x568u,0x56au,0x570u,0x572u,0x7c0u,0x7d0u}

typedef struct {
    uint32_t magic;
    uint32_t start_ticks;
    uint32_t trigger_ticks;
    uint32_t complete_ticks;
    uint32_t resume_ticks;
    uint32_t status_before;
    uint32_t status_first;
    uint32_t status_after;
    uint32_t maccontrol;
    uint32_t gci_output;
    uint32_t gci_control;
    uint32_t psm_before;
    uint16_t phy[CLEAN_TX_PHY_COUNT];
    uint16_t radio[8];
    uint16_t iq[8];
    uint16_t ihr[8];
    uint32_t psm_after;
    uint32_t length;
    uint16_t phy_error;
    uint16_t channel;
    uint32_t psm_debug;
    uint32_t mac_trigger_us;
    uint32_t mac_complete_us;
    uint32_t mac_resume_us;
    uint32_t phy_debug_first;
} clean_tx_diag_t;

_Static_assert(sizeof(clean_tx_diag_t) == 180u, "TX diagnostic size");

#define CLEAN_TX_ERROR_MAGIC 0x31505854u
#define CLEAN_TX_ERROR_ADDR 0x20560u
#define CLEAN_TX_ERROR_DETAIL 876u
enum { CLEAN_TX_PREPARE, CLEAN_TX_ARMED, CLEAN_TX_FIRED, CLEAN_TX_FIRST_ERROR };
typedef struct {
    uint32_t at;
    uint16_t error, txe, ifs, phyctl, phyctl1, bytes, select, owners;
} clean_tx_phase_t;
typedef struct {
    uint32_t magic, valid;
    uint16_t select_operand, select_readback;
    uint16_t plcp[3], reserved;
    clean_tx_phase_t phase[4];
} clean_tx_error_status_t;
_Static_assert(sizeof(clean_tx_error_status_t) == 100u &&
               CLEAN_TX_ERROR_DETAIL + sizeof(clean_tx_error_status_t) <= 976u &&
               CLEAN_TX_ERROR_ADDR + sizeof(clean_tx_error_status_t) <= 0x20d00u,
               "TX phase evidence must fit before DCF evidence");

#define CLEAN_TX_ABORT_MAGIC 0x31415450u
#define CLEAN_TX_ABORT_ADDR (CLEAN_TX_ERROR_ADDR + sizeof(clean_tx_error_status_t))
#define CLEAN_TX_ABORT_DETAIL 1960u
typedef struct {
    uint32_t magic, count, fired_at;
    uint16_t before_error, armed_error, fired_error, completion;
    clean_tx_phase_t first;
} clean_tx_abort_status_t;
_Static_assert(sizeof(clean_tx_abort_status_t) == 40u &&
               CLEAN_TX_ABORT_DETAIL + sizeof(clean_tx_abort_status_t) <= 2000u &&
               CLEAN_TX_ABORT_ADDR + sizeof(clean_tx_abort_status_t) <= 0x20d00u,
               "First PHY abort must not overlap mode/challenge or frame slots");

#define CLEAN_PHY_EDGE_ADDR (CLEAN_TX_ABORT_ADDR + sizeof(clean_tx_abort_status_t))
#define CLEAN_PHY_EDGE_DETAIL 1040u
typedef struct {
    uint32_t at;
    uint16_t before, error, txe, ifs, phyctl, phyctl1, bytes, owners;
} clean_phy_edge_t;
_Static_assert(sizeof(clean_phy_edge_t) == 20u &&
               CLEAN_PHY_EDGE_ADDR + sizeof(clean_phy_edge_t) <= CLEAN_AMPDU_PROBE_ADDR &&
               CLEAN_PHY_EDGE_DETAIL + sizeof(clean_phy_edge_t) <= 1088u,
               "First PHY bit-0x08 observation must fit before probe/packet evidence");

static inline int clean_phy_bit8_added(uint16_t previous, uint16_t current) {
    return (current & ~previous & 0x08u) != 0u;
}

/* PHY error bits can survive successful packets. They are retained evidence,
 * never proof of a fresh fault or permission to credit a completion. */
static inline int clean_tx_error_sample_needed(const volatile clean_tx_error_status_t *s,
        uint16_t current) {
    uint16_t previous = s->phase[CLEAN_TX_FIRST_ERROR].error;
    return current && (!(s->valid & (1u << CLEAN_TX_FIRST_ERROR)) ||
        (!(previous & ~0x30u) && current != previous &&
         ((current & ~0x30u) || (current & previous) == previous)));
}

static inline int clean_tx_cck_abort_retryable(const volatile clean_tx_error_status_t *s,
        uint16_t final_error, uint32_t airtime) {
    const volatile clean_tx_phase_t *f = &s->phase[CLEAN_TX_FIRED];
    const volatile clean_tx_phase_t *e = &s->phase[CLEAN_TX_FIRST_ERROR];
    uint16_t before = s->phase[CLEAN_TX_PREPARE].error;
    return s->magic == CLEAN_TX_ERROR_MAGIC && s->valid == 15u &&
        !(before & ~0x30u) && before == s->phase[CLEAN_TX_ARMED].error &&
        before == f->error && final_error && !(final_error & ~0x30u) &&
        e->error == final_error &&
        f->owners == 2u && e->owners == 2u &&
        (f->phyctl & 0x0fu) == 0u && f->phyctl == e->phyctl &&
        f->phyctl1 == e->phyctl1 &&
        !(e->txe & 0x480u) && !(e->ifs & 0xf00u) &&
        e->at - f->at <= airtime + 2000u;
}

static inline int clean_tx_abort_status_valid(const clean_tx_abort_status_t *s,
        uint32_t timeouts) {
    if (s->magic != CLEAN_TX_ABORT_MAGIC || s->count > timeouts) return 0;
    return !s->count || (!(s->before_error & ~0x30u) &&
        s->before_error == s->armed_error && s->before_error == s->fired_error &&
        s->completion == 5u && s->first.error && !(s->first.error & ~0x30u) &&
        s->first.owners == 2u &&
        !(s->first.phyctl & 0x0fu) && !(s->first.txe & 0x480u) &&
        !(s->first.ifs & 0xf00u) && s->first.at - s->fired_at <= 32767u);
}

#endif
