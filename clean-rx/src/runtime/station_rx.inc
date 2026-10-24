static int station_counter_ok(void) {
    if (!STATION(station_hw) || !shm_read16(CLEAN_DCF_TRACE_SHM + 4u)) return 1;
    station_publish_mac();
    return fail(0x8eu, CLEAN_DCF_SLOTS, shm_read16(CLEAN_DCF_TRACE_SHM + 1u));
}

static void station_consume_ack(void) {
    if (shm_read16(0x601u) != PSM_STATE_CAPTURED || shm_read16(0x606u) != 20u ||
        shm_read16(CLEAN_SHM_FRAME + 4u) != 0xd4u) return;
    for (uint32_t i = 0u; i < 3u; ++i)
        if (shm_read16(CLEAN_SHM_FRAME + 6u + i) !=
            ((uint16_t)STATION(station_address)[2u*i] |
             ((uint16_t)STATION(station_address)[2u*i + 1u] << 8u))) return;
    if (!TRANSPORT(capture_prepared)) (void)rx_dma_poll_and_reclaim(0);
    else ++MAC_RADIO->prepared_acks;
    /* An ACK can complete between the first state sample and transport
     * preparation. Its staged bytes must not survive into the next capture. */
    TRANSPORT(capture_prepared) = 0u;
    station_good_rx();
    ++MAC_RADIO->control_acks;
    shm_write16(0x601u, 0u);
    STATION(nav_sampled) = 0u;
}

static void station_service_tx(void) {
    station_sample_phy_error();
    if (STATION(station_hw) && shm_read16(CLEAN_DCF_REQUEST) == CLEAN_DCF_FIRED &&
        !STATION(cck_tx_tail).ended && !shm_read16(CLEAN_DCF_TX_PENDING))
        STATION(ack_started) = (uint32_t)shm_read16(CLEAN_DCF_TX_LO) |
            ((uint32_t)shm_read16(CLEAN_DCF_TX_HI) << 16u);
    uint32_t now = mac_now();
    if (STATION(cck_ack_window) &&
        clean_tx_tail_poll(&STATION(cck_tx_tail), (mmio_read16(D11_BASE + 0x690u) & 0x100u) != 0u)) {
        volatile uint32_t *tail = (volatile uint32_t *)(uintptr_t)CLEAN_TX_TAIL_ADDR;
        tail[0] = now - STATION(ack_started);
        tail[1] = mmio_read16(D11_BASE + 0x506u);
        tail[2] = mmio_read16(D11_BASE + 0x54cu);
        tail[3] = mmio_read16(D11_BASE + 0x56au);
        /* The CCK TX-request edge precedes the physical tail by more than
         * PLCP airtime alone. Start its bounded ACK wait at observed TX end. */
        shm_write16(CLEAN_DCF_ACK_UNTIL, (uint16_t)(now + STATION(cck_ack_window)));
        shm_write16(CLEAN_DCF_ACK_US, (uint16_t)(now - STATION(ack_started) + STATION(cck_ack_window)));
        STATION(ack_started) = now;
        STATION(ack_window) = STATION(cck_ack_window);
    }
    if (now - STATION(ack_started) >= STATION(ack_window) && shm_read16(CLEAN_ACK_WAITING)) {
        STATION(ack_expiry_at) = now;
        STATION(ack_expiry_started) = STATION(ack_started);
        STATION(ack_expiry_window) = STATION(ack_window);
        shm_write16(CLEAN_ACK_CLEAR_NOW, (uint16_t)now);
        shm_write16(CLEAN_ACK_CLEAR_DUE, shm_read16(CLEAN_DCF_ACK_UNTIL));
        shm_write16(CLEAN_ACK_CLEAR_REASON, 1u);
        shm_write16(CLEAN_ACK_WAITING, 0u);
    }
    station_consume_ack();
}

static void station_observe_capture(void) {
    station_observe_errors(mac_now());
    uint16_t state = shm_read16(0x601u);
    if (state != PSM_STATE_CAPTURED) { STATION(nav_sampled) = 0u; return; }
    if (STATION(nav_sampled)) return;
    STATION(nav_sampled) = 1u;
    uint32_t n = shm_read16(0x606u);
    if (n < 20u || n > CLEAN_RXE_BYTES) {
        station_bad_rx(mac_now());
        return;
    }
    n -= CLEAN_PLCP_BYTES;
    for (uint32_t i = 0u; i < n; i += 2u) {
        uint16_t w = shm_read16(CLEAN_SHM_FRAME + 4u + i / 2u);
        STATION(nav_frame)[i] = (uint8_t)w;
        if (i + 1u < n) STATION(nav_frame)[i + 1u] = (uint8_t)(w >> 8u);
    }
    int duration = clean_mac_nav_duration(STATION(nav_frame), n, STATION(station_address));
    uint32_t now = STATION(station_hw) ?
        (uint32_t)shm_read16(CLEAN_DCF_RX_LO) |
        ((uint32_t)shm_read16(CLEAN_DCF_RX_HI) << 16u) : mac_now();
    station_observe_errors(now);
    if (duration < 0) station_bad_rx(now);
    else {
        station_good_rx();
        if (STATION(station_edca).enabled) {
            for (uint32_t i = 0u; i < 4u; ++i)
                clean_dcf_nav(&STATION(station_edca).ac[i], now, (uint16_t)duration);
        } else clean_dcf_nav(&station_dcf, now, (uint16_t)duration);
        if (station_dcf.nav_valid) shm_write16(CLEAN_CTS_ALLOWED, 0u);
        if (STATION(station_hw) && station_dcf.nav_valid) {
            uint32_t remaining = station_dcf.nav_until - mac_now();
            if (remaining && remaining <= 32767u) {
                mmio_write16(D11_BASE + 0x70cu, (uint16_t)remaining);
                mmio_write16(D11_BASE + 0x700u, 0x103cu);
            }
        }
    }
}

