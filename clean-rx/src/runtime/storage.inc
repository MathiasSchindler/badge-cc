#if CLEAN_RX_HT_WINDOW
static uint16_t STATION(observer_restart_attempts);
#endif

static uint32_t TRANSPORT(dma_consumer);
static uint32_t TRANSPORT(dma_producer);
static uint32_t TRANSPORT(sdio_tx_idx);
static uint32_t TRANSPORT(sdio_tx_posted);

#ifdef CLEAN_SDIO_SELF_TEST
#define SDIO_TX_WINDOW CLEAN_SDIO_SELF_TEST_WINDOW
#else
#define SDIO_TX_WINDOW CLEAN_SDIO_WINDOW
#endif
static uint8_t  TRANSPORT(sdpcm_seq);
static uint8_t TRANSPORT(rx_after_explicit_tx);
static uint8_t STATION(station_enabled);
static uint8_t STATION(station_mac), STATION(station_hw), STATION(nav_sampled);
static clean_dcf_t STATION(station_legacy_dcf);
static clean_edca_t STATION(station_edca);
static clean_edca_ac_t STATION(policy_edca)[4];
static uint8_t STATION(policy_edca_pending);
static uint16_t STATION(rx_discard_fcs), STATION(rx_discard_length);
static uint16_t STATION(phy_error_previous);
static uint8_t STATION(nav_frame)[CLEAN_MPDU_BYTES], STATION(station_address)[6], STATION(station_peer)[6];
static uint32_t STATION(access_started);
static uint32_t STATION(ack_started), STATION(ack_window);
static uint32_t STATION(cck_ack_window);
static clean_tx_tail_t STATION(cck_tx_tail);
static uint32_t STATION(last_tx_identity);
static uint8_t STATION(last_tx_valid);
static uint8_t TRANSPORT(capture_prepared);
static uint16_t STATION(ack_test_configured), STATION(ack_test_epoch);
static uint32_t STATION(ack_expiry_at), STATION(ack_expiry_started), STATION(ack_expiry_window);
static uint32_t TRANSPORT(rx_published_frames);
#if CLEAN_RX_HT_OBSERVER_FIFO
static uint16_t TRANSPORT(observer_capture_ids)[FW_MAX_FRAMES];
#endif
static uint8_t STATION(policy_pending), STATION(policy_short_slot), STATION(policy_response);
static uint16_t STATION(policy_basic);
static uint32_t STATION(policy_command);
static uint16_t PUBLICATION(cts_sampled), PUBLICATION(short_sampled), PUBLICATION(ack_sampled);
static uint16_t PUBLICATION(kinds_sampled)[4];
static clean_tx_diag_t PUBLICATION(tx_diag);




static clean_dcf_t *station_contention(void) {
    return STATION(station_edca).enabled ? &STATION(station_edca).ac[STATION(station_edca).selected] : &STATION(station_legacy_dcf);
}
#define station_dcf (*station_contention())
