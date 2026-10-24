#include "cyw43_hw.h"
#include "sdio_ring.h"
#include "tx_diag.h"
#include "phy_rates.h"
#include "unicast.h"
#include "mac.h"
#include "firmware_command.h"
#include "profile_checks.h"
#if CLEAN_RX_DIAGNOSTIC
#include "diagnostic/ht_command.h"
#else
#include "ht_payload.h"
#endif
#include "ht_observer.h"
#include "observer_dma.h"
#include "early_rx.h"
#include "bytes.h"
#ifdef CLEAN_SDIO_SELF_TEST
#include "diagnostic/sdio_test.h"
#endif

#define MBOX  ((volatile fw_mbox_t *)FW_MBOX_ADDR)
#define MULTI ((volatile fw_multi_t *)FW_MULTI_ADDR)
#define UNICAST ((volatile clean_unicast_status_t *)CLEAN_UNICAST_ADDR)
#define MAC_RADIO ((volatile clean_mac_radio_status_t *)CLEAN_MAC_RADIO_ADDR)
#define MAC_TRACE ((volatile clean_mac_trace_status_t *)CLEAN_MAC_TRACE_ADDR)
#define DCF_TRACE ((volatile clean_dcf_trace_t *)CLEAN_DCF_TRACE_ADDR)
#define TX_COMPLETION ((volatile clean_tx_completion_status_t *)CLEAN_TX_COMPLETION_ADDR)
#define HT_RX ((volatile clean_ht_rx_t *)CLEAN_HT_RX_ADDR)
#define HT_DIAG ((volatile clean_ht_diag_t *)CLEAN_HT_DIAG_ADDR)
#include "runtime/contracts.h"

#include "runtime/publication_phy.inc"
#include "runtime/storage.inc"

#include "runtime/publication_tx.inc"
#include "runtime/d11_io.inc"
#include "runtime/transport.inc"
#include "runtime/station_ack.inc"
#include "runtime/station_response.inc"
#include "runtime/publication_ack.inc"
#include "runtime/station_policy.inc"
#include "runtime/publication_mac.inc"
#include "runtime/station_rx.inc"
#include "runtime/receive.inc"
#include "runtime/station_tx.inc"
#include "runtime/publication_rx.inc"
#include "runtime/recovery.inc"
#include "runtime/publication_psm.inc"
#include "runtime/d11_start.inc"

void cyw43_main(void) {
#include "runtime/startup.inc"
    for (; LOOP(scanner_mode) || LOOP(polls) < 1800000u; ++LOOP(polls)) {
#include "runtime/poll_prologue.inc"
        uint32_t cmd = MBOX->host_cmd;
#include "runtime/command_guard.inc"
#include "runtime/poll_station.inc"
#include "runtime/commands.inc"
#include "runtime/capture.inc"
    }
#include "runtime/shutdown.inc"
}
