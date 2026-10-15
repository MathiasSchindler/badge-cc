#ifndef CLEAN_RX_TX_DIAG_H
#define CLEAN_RX_TX_DIAG_H

#define CLEAN_TX_DIAG_MAGIC 0x33445854u
#define CLEAN_TX_DIAG_OFFSET 296u
#define CLEAN_TX_REARM_RX (1u << 27)
#define CLEAN_TX_MATCH_SOURCE (1u << 28)
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

#endif
