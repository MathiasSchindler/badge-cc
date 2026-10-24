static uint32_t mac_now(void) {
    return mmio_read32(D11_BASE + (STATION(station_hw) ? 0x180u : 0x1cu));
}

static void station_sample_phy_error(void) {
    if (!STATION(station_hw)) return;
    volatile clean_phy_edge_t *p = (volatile clean_phy_edge_t *)CLEAN_PHY_EDGE_ADDR;
    if (p->error) return;
    uint16_t current = mmio_read16(D11_BASE + 0x3eeu);
    if (clean_phy_bit8_added(STATION(phy_error_previous), current)) {
        p->at = mac_now();
        p->before = STATION(phy_error_previous);
        p->txe = mmio_read16(D11_BASE + 0x50eu);
        p->ifs = mmio_read16(D11_BASE + 0x690u);
        p->phyctl = mmio_read16(D11_BASE + 0x50cu);
        p->phyctl1 = mmio_read16(D11_BASE + 0x514u);
        p->bytes = mmio_read16(D11_BASE + 0x56au);
        p->owners = shm_read16(CLEAN_DCF_REQUEST) |
            (shm_read16(CLEAN_DCF_TX_PENDING) << 4u) |
            (shm_read16(CLEAN_RESPONSE_PENDING) << 8u) |
            (shm_read16(CLEAN_TX_CLEANUP_REQUEST) << 12u);
        __asm__ volatile("dsb" ::: "memory");
        p->error = current;
    }
    STATION(phy_error_previous) = current;
}

static void station_tx_phase(uint32_t index) {
    station_sample_phy_error();
    volatile clean_tx_error_status_t *s =
        (volatile clean_tx_error_status_t *)CLEAN_TX_ERROR_ADDR;
    volatile clean_tx_phase_t *p = &s->phase[index];
    p->at = mac_now();
    p->error = mmio_read16(D11_BASE + 0x3eeu);
    p->txe = mmio_read16(D11_BASE + 0x50eu);
    p->ifs = mmio_read16(D11_BASE + 0x690u);
    p->phyctl = mmio_read16(D11_BASE + 0x50cu);
    p->phyctl1 = mmio_read16(D11_BASE + 0x514u);
    p->bytes = mmio_read16(D11_BASE + 0x56au);
    p->select = mmio_read16(D11_BASE + 0x568u);
    p->owners = shm_read16(CLEAN_DCF_REQUEST) |
        (shm_read16(CLEAN_DCF_TX_PENDING) << 4u) |
        (shm_read16(CLEAN_RESPONSE_PENDING) << 8u) |
        (shm_read16(CLEAN_TX_CLEANUP_REQUEST) << 12u);
    s->valid |= 1u << index;
}

static void station_bad_rx(uint32_t now) {
    if (!STATION(station_edca).enabled) clean_dcf_bad_rx(&STATION(station_legacy_dcf), now);
    else for (uint32_t i = 0u; i < 4u; ++i) clean_dcf_bad_rx(&STATION(station_edca).ac[i], now);
}

static void station_good_rx(void) {
    if (!STATION(station_edca).enabled) clean_dcf_good_rx(&STATION(station_legacy_dcf));
    else for (uint32_t i = 0u; i < 4u; ++i) clean_dcf_good_rx(&STATION(station_edca).ac[i]);
}

static void station_observe_errors(uint32_t now) {
    station_sample_phy_error();
    uint16_t fcs = shm_read16(0x7a8u), length = shm_read16(0x7a9u);
    if (fcs != STATION(rx_discard_fcs) || length != STATION(rx_discard_length))
        station_bad_rx(now);
    STATION(rx_discard_fcs) = fcs;
    STATION(rx_discard_length) = length;
}

static int station_rx_idle(uint16_t state) {
    return state == PSM_STATE_READY || state == PSM_STATE_WAIT_RX;
}

static int station_phy_busy(void) {
    return shm_read16(CLEAN_RESPONSE_PENDING) ||
           shm_read16(CLEAN_TX_CLEANUP_REQUEST) ||
           (mmio_read32(D11_BASE + 0x158u) & 7u) ||
           (mmio_read16(D11_BASE + 0x690u) & 0xf00u);
}

static void station_hw_backoff(void) {
    if (!STATION(station_hw)) return;
    uint32_t control = mmio_read32(D11_MACCONTROL);
    mmio_write32(D11_MACCONTROL, clean_mac_pause_psm(control));
    (void)mmio_read32(D11_MACCONTROL);
    shm_write16(CLEAN_DCF_TRACE_SHM, station_dcf.slots);
    shm_write16(CLEAN_DCF_SLOTS, station_dcf.slots);
    shm_write16(CLEAN_DCF_IDLE_VALID, 0u);
    mmio_write32(D11_MACCONTROL, control);
    (void)mmio_read32(D11_MACCONTROL);
}

static void station_hw_slot(void) {
    if (!STATION(station_hw)) return;
    mmio_write16(D11_BASE + 0x684u, (uint16_t)(0x1feu + station_dcf.slot_us));
    shm_write16(CLEAN_DCF_SLOT_US, station_dcf.slot_us);
    shm_write16(CLEAN_DCF_DIFS_US, (uint16_t)(10u + station_dcf.aifsn * station_dcf.slot_us));
    shm_write16(CLEAN_DCF_EIFS_US, 314u);
}

static uint16_t station_response_read(void *context, uint32_t word) {
    (void)context;
    return shm_read16(word);
}

static void station_response_write(void *context, uint32_t word, uint16_t value) {
    (void)context;
    shm_write16(word, value);
}

static int station_response_rates(uint16_t basic) {
    clean_response_io_t io = {.read = station_response_read, .write = station_response_write,
                              .context = 0};
    if (clean_response_rates_install(&io, basic, CLEAN_RX_HT_RESPONSE)) return 1;
    return fail(0x85u, io.failed_word ? io.failed_word : FW_TX_BUF_ADDR, io.actual);
}

static int station_policy_boundary(uint16_t state) {
    return station_rx_idle(state) || state == PSM_STATE_CAPTURED;
}

static int station_policy_word(uint32_t word, uint16_t expected) {
    uint16_t actual = shm_read16(word);
    return actual == expected ? 1 : fail(0x85u, word, actual);
}

#if CLEAN_RX_DIAGNOSTIC
#include "diagnostic/ack_configure.inc"
#define station_response_evidence_reset() station_ack_test_configure(0u)
#else
static int station_response_evidence_reset(void) {
    STATION(ack_test_configured) = 0u;
    ++STATION(ack_test_epoch);
    STATION(ack_expiry_at) = STATION(ack_expiry_started) = STATION(ack_expiry_window) = 0u;
    for (uint32_t i = 0u; i < CLEAN_ACK_OBSERVE_WORDS; ++i)
        shm_write16(CLEAN_ACK_OBSERVE_SHM + i, 0u);
    for (uint32_t i = 0u; i < 3u; ++i)
        shm_write16(CLEAN_ACK_ARITH_SHM + i, 0u);
    for (uint32_t i = 0u; i < CLEAN_ACK_TEST_WORDS; ++i)
        shm_write16(CLEAN_ACK_TEST_SHM + i, 0u);
    shm_write16(CLEAN_TEST_ACK_REMAINING, 0u);
    shm_write16(CLEAN_TEST_ACK_DROPPED, 0u);
    shm_write16(CLEAN_ACK_TEST_LIMIT, 0u);
    shm_write16(CLEAN_ACK_TEST_EPOCH, STATION(ack_test_epoch));
    for (uint32_t i = 0u; i < CLEAN_ACK_TEST_WORDS; ++i)
        if (!station_policy_word(CLEAN_ACK_TEST_SHM + i,
                i == 0u ? 0u : i == 9u ? STATION(ack_test_epoch) : 0u)) return 0;
    for (uint32_t i = 0u; i < CLEAN_ACK_OBSERVE_WORDS; ++i)
        if (!station_policy_word(CLEAN_ACK_OBSERVE_SHM + i, 0u)) return 0;
    for (uint32_t i = 0u; i < 3u; ++i)
        if (!station_policy_word(CLEAN_ACK_ARITH_SHM + i, 0u)) return 0;
    return station_policy_word(CLEAN_TEST_ACK_REMAINING, 0u) &&
        station_policy_word(CLEAN_TEST_ACK_DROPPED, 0u);
}
#endif

