#ifndef CLEAN_RUNTIME_CONTRACTS_H
#define CLEAN_RUNTIME_CONTRACTS_H

#include "state.h"

#if CLEAN_RX_HT_REARM_WAIT
_Static_assert(CLEAN_AUTH_FAILURE_ADDR+sizeof(clean_auth_failure_t) <= CLEAN_OBSERVER_REARM_ADDR &&
               CLEAN_OBSERVER_REARM_ADDR+2u*CLEAN_OBSERVER_REARM_WORDS <= 0x20d80u &&
               CLEAN_OBSERVER_REARM_SHM >= 0x7b0u &&
               CLEAN_OBSERVER_REARM_SHM+CLEAN_OBSERVER_REARM_WORDS <= 0x7f4u,
               "Rearm decision trace must not overlap diagnostics or capture storage");
#endif

_Static_assert(CLEAN_RX_LAYOUT_ADDR+4u*CLEAN_RX_LAYOUT_WORDS <= CLEAN_OBSERVER_PHY_ADDR &&
               CLEAN_OBSERVER_PHY_ADDR+CLEAN_OBSERVER_PHY_BYTES <= CLEAN_HT_FAILURE_ADDR,
               "Stopped PHY snapshot must fit the unused layout/failure gap");

_Static_assert(CLEAN_HT_EARLY_WORDS == 10u &&
               4u+2u*(CLEAN_HT_EARLY_WORDS+4u) == CLEAN_OBSERVER_PHY_BYTES,
               "Receive-window publication must fit the shared diagnostic gap");

_Static_assert(CLEAN_MAC_RADIO_ADDR >= CLEAN_UNICAST_ADDR + sizeof(clean_unicast_status_t) &&
               CLEAN_MAC_RADIO_ADDR + sizeof(clean_mac_radio_status_t) <= FW_MULTI_ADDR,
               "MAC radio status does not overlap transport or captures");

_Static_assert(CLEAN_DCF_MAX_SLOTS == CLEAN_MAC_CW_MAX, "PSM/software contention bound");

_Static_assert(FW_MAX_FRAMES > 0u && FW_MAX_FRAMES <= 16u &&
               (FW_MAX_FRAMES & (FW_MAX_FRAMES - 1u)) == 0u,
               "Capture FIFO must fit credit validation and power-of-two indexing");

_Static_assert(CLEAN_MAC_TRACE_ADDR >= CLEAN_MAC_RADIO_ADDR + sizeof(clean_mac_radio_status_t) &&
               CLEAN_MAC_TRACE_ADDR + sizeof(clean_mac_trace_status_t) <= FW_MULTI_ADDR,
               "MAC trace does not overlap radio status or captures");

_Static_assert(__builtin_offsetof(fw_mbox_t, rx_input) + 32u == CLEAN_TX_DIAG_OFFSET,
               "TX diagnostic trace offset");

_Static_assert(sizeof(clean_tx_diag_t) <= sizeof(((fw_mbox_t *)0)->rx_input),
               "TX diagnostic fits mailbox");

_Static_assert(__builtin_offsetof(fw_mbox_t, f2_counts) == 444u, "F2 counter offset");

_Static_assert(__builtin_offsetof(fw_mbox_t, f2_consumed) == CLEAN_SDIO_ACK_OFFSET,
               "F2 host-consumption offset");

_Static_assert(__builtin_offsetof(fw_mbox_t, f2_posted) == CLEAN_SDIO_POSTED_OFFSET,
               "F2 publication offset");

#endif
