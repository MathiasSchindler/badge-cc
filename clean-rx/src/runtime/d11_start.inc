static int load_and_start_psm(uint32_t generation, uint32_t channel) {
    if (CLEAN_RX_HT_RX) {
        mmio_write32(D11_MACCONTROL, 0x84020400u);
        uint32_t maccontrol = mmio_read32(D11_MACCONTROL);
        if (maccontrol & 3u) return fail(0x8eu, D11_MACCONTROL, maccontrol);
        const uint16_t locations[] = {0u,0x400u,0x800u,0xc00u,0xfffu};
        for (uint32_t i = 0u; i < 5u; ++i)
            shm_write16(locations[i], (uint16_t)(0xa10fu + 0x111u*i));
        for (uint32_t i = 0u; i < 5u; ++i) {
            uint16_t value = shm_read16(locations[i]);
            if (value != (uint16_t)(0xa10fu + 0x111u*i))
                return fail(0x8eu, locations[i], value);
        }
        for (uint32_t i = 0u; i < 5u; ++i) shm_write16(locations[i], 0u);
    }
#if CLEAN_RX_HT_REARM_WAIT
    for (uint32_t i = 0u; i < CLEAN_OBSERVER_REARM_WORDS; ++i) {
        shm_write16(CLEAN_OBSERVER_REARM_SHM+i,0u);
        ((volatile uint16_t *)CLEAN_OBSERVER_REARM_ADDR)[i] = 0u;
    }
#endif
    uint32_t words = psm_ucode_byte_count / 4u;
    for (uint32_t i = 0u; i < words; ++i) {
        const uint8_t *b = psm_ucode_bytes + 4u * i;
        uint32_t w = (uint32_t)b[0] | ((uint32_t)b[1] << 8u) |
                     ((uint32_t)b[2] << 16u) | ((uint32_t)b[3] << 24u);
        mmio_write32(D11_OBJ_ADDR, i);
        (void)mmio_read32(D11_OBJ_ADDR);
        mmio_write32(D11_OBJ_DATA, w);
    }
    for (uint32_t i = 0u; i < words; ++i) {
        const uint8_t *b = psm_ucode_bytes + 4u * i;
        uint32_t expected = (uint32_t)b[0] | ((uint32_t)b[1] << 8u) |
                            ((uint32_t)b[2] << 16u) | ((uint32_t)b[3] << 24u);
        mmio_write32(D11_OBJ_ADDR, i);
        (void)mmio_read32(D11_OBJ_ADDR);
        uint32_t actual = mmio_read32(D11_OBJ_DATA);
        if (actual != expected)
            return fail(0x70u, i, actual);
    }

    for (uint32_t i = 0u; i < 32u; ++i)  shm_write16(0x600u + i, 0u);
    for (uint32_t i = 0u; i < 480u; ++i) shm_write16(0x620u + i, 0u);
    STATION(ack_test_epoch) = 0u;
    if (!station_response_evidence_reset()) return 0;
#if CLEAN_RX_AMPDU_PROBE
    for (uint32_t i = 0u; i < CLEAN_AMPDU_PROBE_WORDS; ++i)
        shm_write16(CLEAN_AMPDU_PROBE_SHM + i, 0u);
    ((volatile clean_ampdu_probe_trace_t *)CLEAN_AMPDU_PROBE_ADDR)->magic = 0u;
#endif
#if CLEAN_RX_AMPDU_INTERFACE
    for (uint32_t i = 0u; i < CLEAN_AMPDU_IF_WORDS; ++i)
        shm_write16(CLEAN_AMPDU_IF_SHM + i, 0u);
    ((volatile clean_ampdu_interface_t *)CLEAN_AMPDU_IF_ADDR)->magic = 0u;
#endif
#if CLEAN_RX_AMPDU_DMA
    shm_write16(CLEAN_AMPDU_DMA_PENDING, 0u);
    shm_write16(CLEAN_AMPDU_DMA_SUBMITTED, 0u);
    ((volatile clean_ampdu_dma_state_t *)CLEAN_AMPDU_DMA_STATE_ADDR)->magic = 0u;
#endif
    volatile uint32_t *tail = (volatile uint32_t *)(uintptr_t)CLEAN_TX_TAIL_ADDR;
    for (uint32_t i = 0u; i < 4u; ++i) tail[i] = 0u;
    volatile clean_tx_abort_status_t *abort_status =
        (volatile clean_tx_abort_status_t *)CLEAN_TX_ABORT_ADDR;
    volatile uint8_t *abort_bytes = (volatile uint8_t *)abort_status;
    for (uint32_t i = 0u; i < sizeof(*abort_status); ++i) abort_bytes[i] = 0u;
    abort_status->magic = CLEAN_TX_ABORT_MAGIC;
    volatile uint8_t *edge_bytes = (volatile uint8_t *)CLEAN_PHY_EDGE_ADDR;
    for (uint32_t i = 0u; i < sizeof(clean_phy_edge_t); ++i) edge_bytes[i] = 0u;
    STATION(phy_error_previous) = 0u;
    volatile uint8_t *ht_bytes = (volatile uint8_t *)HT_RX;
    for (uint32_t i = 0u; i < sizeof(*HT_RX); ++i) ht_bytes[i] = 0u;
    HT_RX->magic = CLEAN_HT_RX_MAGIC;
    volatile uint8_t *ht_diag_bytes = (volatile uint8_t *)HT_DIAG;
    for (uint32_t i = 0u; i < sizeof(*HT_DIAG); ++i) ht_diag_bytes[i] = 0u;
    ((volatile clean_ht_failure_t *)CLEAN_HT_FAILURE_ADDR)->magic = 0u;
    ((volatile clean_auth_failure_t *)CLEAN_AUTH_FAILURE_ADDR)->magic = 0u;
    HT_DIAG->magic = CLEAN_HT_DIAG_MAGIC;
    shm_write16(CLEAN_SHM_CAPTURE_LIMIT, CLEAN_RAW_BYTES);
    shm_write16(CLEAN_SHM_RXE_LIMIT, CLEAN_RXE_BYTES);

    shm_write16(0x60cu, (uint16_t)generation);
    shm_write16(0x60du, (uint16_t)(generation >> 16u));
    shm_write16(0x610u, (uint16_t)generation);
    shm_write16(0x611u, (uint16_t)(generation >> 16u));

    static const uint8_t start_bits[4] = {0u, 4u, 6u, 2u};
    uint32_t started = ARM_CYCLE_COUNTER;
    for (uint32_t i = 0u; i < 4u; ++i) {
        mmio_write32(D11_MACCONTROL, 0x84020400u | start_bits[i]);
        (void)mmio_read32(D11_MACCONTROL);
    }
    uint32_t ready = 0u;
    for (uint32_t poll = 0u; poll < 2000u; ++poll) {
        uint16_t st = shm_read16(0x601u);
        if (st == PSM_STATE_READY) {
            snapshot_psm(started, poll);
            ready = 1u;
            break;
        }
        if (st == PSM_STATE_FAULT) {
            snapshot_psm(started, poll);
            /* Observe late completion without restarting or accepting the failed PSM. */
            (void)delay_ticks(2000000u);
            sample_rxe8(MBOX->rxe_post);
            snapshot_rx_trace();
            return fail(0x71u, shm_read16(0x602u), st);
        }
        (void)delay_ticks(500u);
    }
    if (!ready) {
        snapshot_psm(started, 2000u);
        return fail(0x72u, 0x601u, shm_read16(0x601u));
    }

    mmio_write32(D11_MACCONTROL, 0x84020400u);
    (void)mmio_read32(D11_MACCONTROL);
    shm_write16(0x609u, (uint16_t)channel);
    return 1;
}

static void d11_set_addrmatch(uint16_t offset, const volatile uint8_t *address) {
    mmio_write16(D11_BASE + 0x420u, 0x0020u | offset);
    for (uint32_t i = 0u; i < 3u; ++i)
        mmio_write16(D11_BASE + 0x422u,
                     (uint16_t)address[2u * i] | ((uint16_t)address[2u * i + 1u] << 8u));
}

static int d11_restart_rx_psm(uint32_t channel) {
#if CLEAN_RX_HT_WINDOW
    ++STATION(observer_restart_attempts);
#endif
    mmio_write32(D11_MACCONTROL, 0x84020400u);
    (void)mmio_read32(D11_MACCONTROL);
    uint16_t saved_tk[8];
    uint16_t saved_ccmp_en = shm_read16(0x615u);
    for (uint32_t k = 0u; k < 8u; ++k) saved_tk[k] = shm_read16(0x618u + k);
    shm_write16(0x601u, 0u);
    shm_write16(0x602u, 0u);
    shm_write16(0x609u, (uint16_t)channel);
    static const uint8_t restart_bits[4] = {0u, 4u, 6u, 2u};
    for (uint32_t i = 0u; i < 4u; ++i) {
        mmio_write32(D11_MACCONTROL, 0x84020400u | restart_bits[i]);
        (void)mmio_read32(D11_MACCONTROL);
    }
    uint16_t state = 0u;
    for (uint32_t poll = 0u; poll < 2000u; ++poll) {
        state = shm_read16(0x601u);
        if (state == PSM_STATE_READY || state == PSM_STATE_FAULT) break;
        (void)delay_ticks(200u);
    }
    mmio_write32(D11_MACCONTROL, 0x84020400u);
    (void)mmio_read32(D11_MACCONTROL);
    if (state != PSM_STATE_READY) return fail(0x84u, 0x601u, state);
    shm_write16(0x609u, (uint16_t)channel);
    shm_write16(0x615u, saved_ccmp_en);
    for (uint32_t k = 0u; k < 8u; ++k) shm_write16(0x618u + k, saved_tk[k]);
    return 1;
}
