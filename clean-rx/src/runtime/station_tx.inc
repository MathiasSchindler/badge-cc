static int station_edca_select(uint32_t mac_en, uint32_t tid, uint32_t exchange_us) {
    mmio_write32(D11_MACCONTROL, clean_mac_pause_psm(mac_en));
    (void)mmio_read32(D11_MACCONTROL);
    if (!station_rx_idle(shm_read16(0x601u)) || station_phy_busy()) {
        mmio_write32(D11_MACCONTROL, mac_en);
        return 2;
    }
    uint16_t slots = shm_read16(CLEAN_DCF_SLOTS);
    if (slots > CLEAN_DCF_MAX_SLOTS)
        return fail(0x8eu, CLEAN_DCF_SLOTS, slots);
    station_dcf.slots = slots;
    if (clean_edca_select(&STATION(station_edca), tid, mac_now()) < 0)
        return fail(0x85u, FW_TX_BUF_ADDR, tid);
    if (!exchange_us || !clean_edca_txop_valid(&STATION(station_edca), exchange_us))
        return fail(0x85u, FW_TX_BUF_ADDR, tid);
    station_hw_slot();
    station_hw_backoff();
    mmio_write32(D11_MACCONTROL, mac_en);
    return 1;
}

static int station_wait_access(uint32_t mac_en, uint16_t ack_sent, uint16_t cts_sent) {
    if (STATION(station_hw)) shm_write16(CLEAN_DCF_REQUEST, 1u);
    mmio_write32(D11_MACCONTROL, mac_en);
    (void)mmio_read32(D11_MACCONTROL);
    for (uint32_t poll = 0u; poll < 2000000u; ++poll) {
        uint32_t now = mac_now();
        uint16_t state = shm_read16(0x601u);
        if (STATION(station_hw) && shm_read16(CLEAN_DCF_REQUEST) == CLEAN_DCF_FIRED)
            return 1;
        int credit = clean_sdio_credit_available(TRANSPORT(sdio_tx_posted), MBOX->f2_consumed,
                                                  SDIO_TX_WINDOW);
        if (credit < 0) return fail(0x8cu, FW_MBOX_ADDR + CLEAN_SDIO_ACK_OFFSET, MBOX->f2_consumed);
        if (state == PSM_STATE_FAULT) return fail(0x88u, 0x601u, state);
        if (!station_counter_ok()) return 0;
        if (shm_read16(CLEAN_ACK_ERROR)) return fail(0x87u, CLEAN_ACK_ERROR, shm_read16(CLEAN_ACK_ERROR));
        if (state == PSM_STATE_CAPTURED || !credit ||
            (STATION(station_hw) && shm_read16(CLEAN_DCF_REQUEST) == CLEAN_DCF_DEFERRED) ||
            shm_read16(CLEAN_ACK_SENT) != ack_sent ||
            shm_read16(CLEAN_CTS_SENT) != cts_sent ||
            now - STATION(access_started) >= 50000u) {
            if (STATION(station_hw)) {
                mmio_write32(D11_MACCONTROL, mac_en & ~2u);
                (void)mmio_read32(D11_MACCONTROL);
                uint16_t request = shm_read16(CLEAN_DCF_REQUEST);
                shm_write16(CLEAN_DCF_REQUEST, clean_dcf_cancel_request(request));
                mmio_write32(D11_MACCONTROL, mac_en);
                if (request == CLEAN_DCF_FIRED) return 1;
            } else (void)clean_dcf_poll(&station_dcf, now, 1);
            return 2;
        }
        if (STATION(station_hw)) continue;
        /* PHYDEBUG: CRS, TX byte strobe and MAC TX-frame request. TXA alone
         * is not a whole-PPDU-active indication (brcmsmac d11.h). */
        uint32_t busy = station_phy_busy() || !station_rx_idle(state);
        if (!clean_dcf_poll(&station_dcf, now, busy != 0u)) continue;
        mmio_write32(D11_MACCONTROL, 0x84020400u);
        (void)mmio_read32(D11_MACCONTROL);
        if (station_phy_busy() ||
            !station_rx_idle(shm_read16(0x601u)) || shm_read16(CLEAN_ACK_SENT) != ack_sent ||
            shm_read16(CLEAN_CTS_SENT) != cts_sent) {
            (void)clean_dcf_poll(&station_dcf, mac_now(), 1);
            mmio_write32(D11_MACCONTROL, mac_en);
            return 2;
        }
        return 1;
    }
    return fail(0x82u, D11_BASE + (STATION(station_hw) ? 0x180u : 0x1cu), mac_now());
}

/* RX can hold the PSM across the PHY-start pulse. Hand over ARM's owned
 * observation while the PSM is paused, without manufacturing completion. */
static uint32_t station_observe_tx_start(uint16_t ifs, uint32_t observed_at) {
    if (!(ifs & 0x100u) || shm_read16(CLEAN_DCF_TX_PENDING) != 1u) return 0u;
    uint32_t control = mmio_read32(D11_MACCONTROL), handed = 0u;
    mmio_write32(D11_MACCONTROL, clean_mac_pause_psm(control));
    (void)mmio_read32(D11_MACCONTROL);
    if (clean_tx_phy_start_owned(shm_read16(CLEAN_DCF_REQUEST),
            shm_read16(CLEAN_DCF_TX_PENDING), shm_read16(CLEAN_RESPONSE_PENDING),
            shm_read16(CLEAN_TX_COMPLETION), ifs)) {
        shm_write16(CLEAN_DCF_TX_LO, (uint16_t)observed_at);
        shm_write16(CLEAN_DCF_TX_HI, (uint16_t)(observed_at >> 16u));
        shm_write16(CLEAN_DCF_ACK_UNTIL,
            (uint16_t)(observed_at + shm_read16(CLEAN_DCF_ACK_US)));
        shm_write16(CLEAN_DCF_TX_PENDING, 0u);
        ++*(volatile uint32_t *)CLEAN_TX_START_HANDOFFS_ADDR;
        handed = 1u;
    }
    mmio_write32(D11_MACCONTROL, control);
    (void)mmio_read32(D11_MACCONTROL);
    return handed;
}

/* A held RX capture prevents PSM's idle sampler from seeing late Data done.
 * Pause it to prove TXE has not been reused before handing over real status. */
static uint32_t station_observe_tx_completion(void) {
    if (shm_read16(0x601u) != PSM_STATE_CAPTURED ||
        (shm_read16(CLEAN_TX_COMPLETION) & 7u) == 7u) return 0u;
    uint32_t control = mmio_read32(D11_MACCONTROL), handed = 0u;
    mmio_write32(D11_MACCONTROL, clean_mac_pause_psm(control));
    (void)mmio_read32(D11_MACCONTROL);
    uint16_t completion = shm_read16(CLEAN_TX_COMPLETION);
    if (shm_read16(0x601u) == PSM_STATE_CAPTURED &&
        (completion & 7u) != 7u &&
        clean_tx_completion_owned(shm_read16(CLEAN_DCF_REQUEST),
            shm_read16(CLEAN_DCF_TX_PENDING), shm_read16(CLEAN_RESPONSE_PENDING),
            completion, mmio_read16(D11_BASE + 0x50eu),
            mmio_read16(D11_BASE + 0x690u))) {
        shm_write16(CLEAN_TX_COMPLETION, completion | 7u);
        ++*(volatile uint32_t *)CLEAN_TX_COMPLETION_HANDOFFS_ADDR;
        handed = 1u;
    }
    mmio_write32(D11_MACCONTROL, control);
    (void)mmio_read32(D11_MACCONTROL);
    return handed;
}

/* 0: fatal, 1: complete, 2: deferred, 3: attempted but completion timed out. */
static int d11_transmit_template_frame(uint32_t total_len, uint32_t is_ofdm, uint32_t channel,
                                       uint32_t mac_tx_ctl, uint32_t reliable,
                                       uint32_t short_preamble, uint32_t ht_probe) {
    if (total_len < 16u || total_len > CLEAN_TX_BYTES)
        return fail(0x83u, FW_TX_BUF_ADDR, total_len);
    const volatile uint8_t *tx_buf = (const volatile uint8_t *)FW_TX_BUF_ADDR +
        (ht_probe == 2u ? CLEAN_HT_DATA_HEADER : 0u);
    uint16_t phyctl1 = 0x1a02u;
    uint32_t ofdm_airtime = 0u;
    clean_ht_tx_t ht_tx;
    uint32_t station = shm_read16(CLEAN_ACK_ENABLE);
    uint32_t ba_listen = CLEAN_RX_AMPDU_BA && ht_probe == 2u &&
        ((const volatile uint8_t *)FW_TX_BUF_ADDR)[0] == CLEAN_HT_AMPDU_PROBE_VERSION;
    if (ba_listen) station = 1u;
    uint16_t ack_sent = shm_read16(CLEAN_ACK_SENT);
    uint16_t cts_sent = shm_read16(CLEAN_CTS_SENT);
    if (ht_probe) {
#if CLEAN_RX_DIAGNOSTIC
        uint32_t aggregate_probe = ht_probe == 2u && CLEAN_RX_AMPDU_PROBE &&
            ((const volatile uint8_t *)FW_TX_BUF_ADDR)[0] == CLEAN_HT_AMPDU_PROBE_VERSION;
#endif
        uint32_t ht_length = total_len - CLEAN_PLCP_BYTES;
        clean_ht20_config_t config;
        if (!(ht_probe == 2u ? CLEAN_RX_HT_DATA : CLEAN_RX_HT_PROBE) ||
#if CLEAN_RX_DIAGNOSTIC
            !(aggregate_probe ? clean_ht_ampdu_probe_sig_valid(tx_buf, ht_length) :
              CLEAN_RX_HT_STIMULUS &&
              ((const volatile uint8_t *)FW_TX_BUF_ADDR)[0] == CLEAN_HT_STIMULUS_VERSION ?
                  clean_ht_stimulus_sig_valid(tx_buf, ht_length) :
              CLEAN_RX_HT_HANDOFF_STIMULUS &&
              ((const volatile uint8_t *)FW_TX_BUF_ADDR)[0] == CLEAN_HT_HANDOFF_VERSION ?
                  clean_ht_handoff_sig_valid(tx_buf, ht_length) :
                                clean_ht20_sig_valid(tx_buf, ht_length)) ||
#else
            !clean_ht20_sig_valid(tx_buf, ht_length) ||
#endif
            !clean_ht20_configure(&config, ht_length, tx_buf[0], CLEAN_HT20_MCS_MASK))
            return fail(0x83u, FW_TX_BUF_ADDR, total_len);
        ht_tx = config.controls;
        phyctl1 = config.controls.phyctl1;
        ofdm_airtime = config.airtime_us;
    } else if (is_ofdm) {
        int rate = clean_ofdm_rate_index(tx_buf[0]);
        if (rate < 0 || !clean_ofdm_plcp_valid(tx_buf, total_len - CLEAN_PLCP_BYTES))
            return fail(0x83u, FW_TX_BUF_ADDR, total_len);
        phyctl1 = clean_ofdm_rates[(uint32_t)rate].phyctl1;
        ofdm_airtime = clean_ofdm_airtime_us(total_len - CLEAN_PLCP_BYTES, (uint32_t)rate);
    } else {
        int rate = clean_cck_rate_index(tx_buf[0]);
        if (rate < 0 || !clean_cck_plcp_valid(tx_buf, total_len - CLEAN_PLCP_BYTES) ||
            !clean_cck_preamble_valid((uint32_t)rate, short_preamble))
            return fail(0x83u, FW_TX_BUF_ADDR, total_len);
        phyctl1 = clean_cck_rates[rate].phyctl1;
    }
    if (is_ofdm && short_preamble) return fail(0x83u, FW_TX_BUF_ADDR, total_len);
    uint32_t airtime = is_ofdm || ht_probe ? ofdm_airtime :
        clean_cck_airtime_us(total_len - CLEAN_PLCP_BYTES,
                             (uint32_t)clean_cck_rate_index(tx_buf[0]), short_preamble);
    uint32_t ack_timeout = STATION(station_mac) ? (ht_probe ?
        clean_ht_ack_timeout_us(tx_buf[0], station_dcf.slot_us) :
        clean_ack_timeout_us(is_ofdm ? (uint32_t)clean_ofdm_rate_index(tx_buf[0]) + 4u :
            (uint32_t)clean_cck_rate_index(tx_buf[0]), station_dcf.slot_us)) : 0u;
    PUBLICATION(tx_diag).magic = CLEAN_TX_DIAG_MAGIC;
    PUBLICATION(tx_diag).resume_ticks = 0u;
    PUBLICATION(tx_diag).mac_resume_us = 0u;
    PUBLICATION(tx_diag).psm_after = 0u;
    TX_COMPLETION->latched = 0u;
    uint32_t phy_started_by_arm = 0u;
    uint32_t completion_by_arm = 0u;
    PUBLICATION(tx_diag).start_ticks = ARM_CYCLE_COUNTER;
    PUBLICATION(tx_diag).length = total_len;
    PUBLICATION(tx_diag).channel = (uint16_t)channel;
    PUBLICATION(tx_diag).psm_before = shm_read16(0x601u);
    PUBLICATION(tx_diag).status_before = tx_status();

    mmio_write32(D11_MACCONTROL, STATION(station_mac) ?
                 clean_mac_pause_psm(mac_tx_ctl) : 0x84020400u);
    (void)mmio_read32(D11_MACCONTROL);
    if (STATION(station_mac) && (station_phy_busy() || !station_rx_idle(shm_read16(0x601u)))) {
        mmio_write32(D11_MACCONTROL, mac_tx_ctl | 2u);
        (void)mmio_read32(D11_MACCONTROL);
        return 2;
    }
    volatile clean_tx_error_status_t *tx_error =
        (volatile clean_tx_error_status_t *)CLEAN_TX_ERROR_ADDR;
    tx_error->magic = CLEAN_TX_ERROR_MAGIC;
    tx_error->valid = 0u;
    tx_error->select_operand = tx_error->select_readback = 0u;
    station_tx_phase(CLEAN_TX_PREPARE);
    if (STATION(station_hw)) shm_write16(CLEAN_DCF_IDLE_VALID, 0u);
    MBOX->rc_dacbuf = 0x55550001u;
    phy_tx_hw_configure(channel);
    if (STATION(station_mac) || ba_listen) phy_tx_set_automatic();
    else phy_tx_set_inhibit(0);

    MBOX->rc_dacbuf = 0x55550002u;
    uint16_t fc_tx = (uint16_t)tx_buf[6] | ((uint16_t)tx_buf[7] << 8u);
    uint32_t use_hw_ccmp = (shm_read16(0x615u) != 0u) && ((fc_tx & 0x4000u) != 0u);
    uint32_t words = (total_len + 3u) >> 2u;
    if (ht_probe) HT_DIAG->template_crc_zeroed = ht_probe == 1u && CLEAN_RX_HT_CLEAR_SIG_CRC;
    mmio_write32(D11_TPL_WR_PTR, 0u);
    for (uint32_t i = 0u; i < words; ++i) {
        const volatile uint8_t *b = tx_buf + 4u * i;
        uint32_t w = (uint32_t)b[0] | ((uint32_t)b[1] << 8u) |
                     ((uint32_t)b[2] << 16u) | ((uint32_t)b[3] << 24u);
        w = clean_ht_probe_template_word(w, i, ht_probe == 1u, CLEAN_RX_HT_CLEAR_SIG_CRC);
#if CLEAN_RX_HT_REJECT_BAD_FCS
        w = clean_ht_reject_bad_fcs_word(w, i, total_len, ht_probe,
                                        (const volatile uint8_t *)FW_TX_BUF_ADDR);
#endif
        mmio_write32(D11_TPL_WR_DATA, w);
    }
    mmio_write32(D11_TPL_WR_PTR, 0u);
    for (uint32_t i = 0u; i < words; ++i) {
        const volatile uint8_t *b = tx_buf + 4u * i;
        uint32_t expected = (uint32_t)b[0] | ((uint32_t)b[1] << 8u) |
                            ((uint32_t)b[2] << 16u) | ((uint32_t)b[3] << 24u);
        expected = clean_ht_probe_template_word(expected, i, ht_probe == 1u, CLEAN_RX_HT_CLEAR_SIG_CRC);
#if CLEAN_RX_HT_REJECT_BAD_FCS
        expected = clean_ht_reject_bad_fcs_word(expected, i, total_len, ht_probe,
                                               (const volatile uint8_t *)FW_TX_BUF_ADDR);
#endif
        uint32_t actual = mmio_read32(D11_TPL_WR_DATA);
        if (actual != expected) return fail(0x8du, i * 4u, actual);
    }

    mmio_write32(D11_MACCONTROL, mac_tx_ctl);
    (void)mmio_read32(D11_MACCONTROL);
    MBOX->rc_dacbuf = 0x55550003u;
    if (STATION(station_mac)) mmio_write16(0x18001502u, 0u);
    mmio_write16(0x18001570u, 0x0000u);
    mmio_write16(0x18001572u, (uint16_t)(total_len << 4u));
    mmio_write16(0x18001568u, 0x0000u);
    if (use_hw_ccmp) {
        uint16_t iv_off = ((fc_tx & 0x0080u) != 0u) ? 32u : 30u;
        mmio_write16(0x180017c8u, 0x0000u);
        mmio_write16(0x180017c2u, iv_off);
        mmio_write16(0x180017c6u, 0x0110u);
        for (uint32_t k = 0u; k < 8u; ++k) {
            mmio_write16(0x180017c4u, shm_read16(0x618u + k));
        }
        mmio_write16(0x180017c0u, 0x1170u);
        mmio_write16(0x180017d4u, 0x0000u);
        mmio_write16(0x180017d6u, (uint16_t)total_len);
        mmio_write16(0x180017d0u, 0x0305u);
    } else {
        mmio_write16(0x180017c0u, 0x0100u);
        mmio_write16(0x180017d0u, 0x0300u);
    }
    if (ht_probe) {
        mmio_write16(0x18001510u, ht_tx.mmplcp[0]);
        mmio_write16(0x18001512u, ht_tx.mmplcp[1]);
    }
    mmio_write16(0x1800150cu, ht_probe ? ht_tx.phyctl :
                 is_ofdm ? 0x0041u : short_preamble ? 0x0050u : 0x0040u);
    mmio_write16(0x18001514u, phyctl1);
    if (ht_probe) {
        const uint16_t offsets[4] = {0x50cu,0x514u,0x510u,0x512u};
        const uint16_t expected[4] = {ht_tx.phyctl,ht_tx.phyctl1,ht_tx.mmplcp[0],ht_tx.mmplcp[1]};
        volatile uint16_t *actual = (volatile uint16_t *)&HT_DIAG->tx;
        for (uint32_t i = 0u; i < 4u; ++i) {
            actual[i] = mmio_read16(D11_BASE + offsets[i]);
            if (actual[i] != expected[i])
                return fail(0x8du, D11_BASE + offsets[i], actual[i]);
        }
    }
    mmio_write16(0x18001508u, 0x0000u);
    mmio_write16(0x1800150au, 0x0000u);
    mmio_write16(0x1800154cu, 0x0000u);
    mmio_write16(0x1800156au, (uint16_t)total_len);
    if (!STATION(station_mac)) mmio_write16(0x18001568u, 0x0826u);
    mmio_write16(0x18001500u, 0x0000u);
    static const uint16_t phy_regs[CLEAN_TX_PHY_COUNT] = CLEAN_TX_PHY_REGS;
    static const uint16_t radio_regs[8] = CLEAN_TX_RADIO_REGS;
    static const uint16_t ihr_regs[8] = CLEAN_TX_IHR_REGS;
    for (uint32_t i = 0u; i < CLEAN_TX_PHY_COUNT; ++i) PUBLICATION(tx_diag).phy[i] = phy_reg_read(phy_regs[i]);
    for (uint32_t i = 0u; i < 8u; ++i) {
        PUBLICATION(tx_diag).radio[i] = radio_reg_read(radio_regs[i]);
        PUBLICATION(tx_diag).ihr[i] = mmio_read16(D11_BASE + ihr_regs[i]);
    }
    uint16_t saved_9a5 = phy_reg_read(0x9a5u);
    phy_reg_mod(0x9a5u, 2u, 2u);
    for (uint32_t group = 0u; group < 2u; ++group) {
        phy_reg_write(0x00du, 0u);
        phy_reg_write(0x00eu, group ? 112u : 96u);
        for (uint32_t i = 0u; i < 4u; ++i)
            PUBLICATION(tx_diag).iq[group * 4u + i] = phy_reg_read(0x456u);
    }
    phy_reg_write(0x9a5u, saved_9a5);
    PUBLICATION(tx_diag).maccontrol = mmio_read32(D11_MACCONTROL);
    PUBLICATION(tx_diag).gci_output = mmio_read32(GCI_OUTPUT_REG1);
    PUBLICATION(tx_diag).gci_control = mmio_read32(GCI_CONTROL_REG1);
    MBOX->rc_dacbuf = 0x55550004u;
    if (STATION(station_mac)) {
        STATION(cck_tx_tail).active = STATION(cck_tx_tail).ended = 0u;
        volatile uint32_t *tail = (volatile uint32_t *)(uintptr_t)CLEAN_TX_TAIL_ADDR;
        tail[0] = tail[1] = tail[2] = tail[3] = 0u;
        STATION(cck_ack_window) = STATION(station_hw) && !is_ofdm && !ht_probe ?
            clean_ack_timeout_us((uint32_t)clean_cck_rate_index(tx_buf[0]), station_dcf.slot_us) : 0u;
        if (STATION(station_hw)) {
            shm_write16(CLEAN_DCF_ACK_US,
                (uint16_t)(airtime + ack_timeout));
        }
        station_tx_phase(CLEAN_TX_ARMED);
        int access = station_wait_access(mac_tx_ctl | 2u, ack_sent, cts_sent);
        if (access != 1) return access;
        station_tx_phase(CLEAN_TX_FIRED);
        if (!STATION(station_hw)) {
            mmio_write32(D11_MACCONTROL, mac_tx_ctl);
            (void)mmio_read32(D11_MACCONTROL);
            shm_write16(CLEAN_ACK_SEEN, 0u);
            shm_write16(CLEAN_ACK_WAITING, 1u);
        }
    }
    PUBLICATION(tx_diag).trigger_ticks = ARM_CYCLE_COUNTER;
    PUBLICATION(tx_diag).mac_trigger_us = STATION(station_hw) ?
        (uint32_t)shm_read16(CLEAN_DCF_TX_LO) |
        ((uint32_t)shm_read16(CLEAN_DCF_TX_HI) << 16u) : mac_now();
    if (STATION(station_mac)) {
        STATION(ack_started) = PUBLICATION(tx_diag).mac_trigger_us;
        STATION(ack_window) = airtime + ack_timeout;
    }
    if (!STATION(station_hw)) {
        if (STATION(station_mac)) mmio_write16(0x18001568u, 0x0826u);
        mmio_write16(0x18001502u, 0x00d0u);
    }
    if (reliable) ++UNICAST->attempts;
    if (STATION(station_mac)) {
        STATION(last_tx_identity) = (uint32_t)tx_buf[28] | ((uint32_t)tx_buf[29] << 8u) |
            ((uint32_t)tx_buf[6] << 16u) |
            (ht_probe ? (uint32_t)(tx_buf[30] & 15u) << 24u : 0u);
        STATION(last_tx_valid) = 1u;
    }
    MBOX->rc_dacbuf = 0x55550005u;
    (void)mmio_read16(0x18001502u);
    PUBLICATION(tx_diag).phy_debug_first = mmio_read32(D11_BASE + 0x158u);
    uint32_t start_observed_at = mac_now();
    PUBLICATION(tx_diag).status_first = tx_status();
    if (STATION(station_hw))
        phy_started_by_arm = station_observe_tx_start((uint16_t)PUBLICATION(tx_diag).status_first,
                                                      start_observed_at);
    if (STATION(station_mac) || ba_listen) {
        mmio_write32(D11_MACCONTROL, mac_tx_ctl | 2u);
        (void)mmio_read32(D11_MACCONTROL);
    }

    uint16_t st_50e_seen = 0u, st_690_seen = 0u;
    uint32_t tx_conditions = 0u;
    uint32_t raw_conditions = 0u;
    uint32_t completion_polls = 2000u + airtime;
    if (STATION(station_hw)) {
        uint32_t completion_started = mac_now();
        for (uint32_t i = 0u; i < completion_polls; ++i) {
            station_service_tx();
            uint16_t txe = mmio_read16(D11_BASE + 0x50eu);
            uint16_t ifs = mmio_read16(D11_BASE + 0x690u);
            if (!(txe & 0x480u) && !(ifs & 0xf00u) &&
                clean_tx_error_sample_needed(tx_error, mmio_read16(D11_BASE + 0x3eeu)))
                station_tx_phase(CLEAN_TX_FIRST_ERROR);
            st_50e_seen |= txe;
            st_690_seen |= ifs;
            if (!(txe & 0x80u)) raw_conditions |= 1u;
            if (txe & 0x400u) raw_conditions |= 2u;
            if (!(ifs & 0x100u)) raw_conditions |= 4u;
            completion_by_arm |= station_observe_tx_completion();
            if ((shm_read16(CLEAN_TX_COMPLETION) & 7u) == 7u) {
                tx_conditions = 7u;
                break;
            }
            if (!clean_tx_completion_waiting(completion_started, mac_now(), airtime)) break;
            (void)delay_ticks(180u);
        }
    } else {
        for (uint32_t i = 0u; i < completion_polls; ++i) {
            if (STATION(station_mac)) station_service_tx();
            uint16_t v = mmio_read16(0x1800150eu);
            st_50e_seen |= v;
            if (!(v & 0x80u)) raw_conditions |= 1u;
            if (!(v & 0x80u)) {
                tx_conditions |= 1u;
                break;
            }
            (void)delay_ticks(180u);
        }
        for (uint32_t i = 0u; i < completion_polls; ++i) {
            if (STATION(station_mac)) station_service_tx();
            uint16_t v = mmio_read16(0x1800150eu);
            st_50e_seen |= v;
            if (v & 0x400u) raw_conditions |= 2u;
            if (v & 0x400u) {
                tx_conditions |= 2u;
                break;
            }
            (void)delay_ticks(180u);
        }
        for (uint32_t i = 0u; i < completion_polls; ++i) {
            if (STATION(station_mac)) station_service_tx();
            uint16_t v = mmio_read16(0x18001690u);
            st_690_seen |= v;
            if (!(v & 0x100u)) raw_conditions |= 4u;
            if (!(v & 0x100u)) {
                tx_conditions |= 4u;
                break;
            }
            (void)delay_ticks(180u);
        }
    }
    MBOX->vco_status = ((uint32_t)st_50e_seen << 16u) | st_690_seen;
    uint32_t phy_drained = 1u;
    if (STATION(station_mac)) {
        /* CCK drain overlaps the PPDU; do not hold its reply for a second
         * airtime. Other PHYs retain their conservative post-drain guard. */
        uint32_t wait = mac_now();
        if (STATION(cck_ack_window) && !shm_read16(CLEAN_DCF_TX_PENDING))
            wait = (uint32_t)shm_read16(CLEAN_DCF_TX_LO) |
                ((uint32_t)shm_read16(CLEAN_DCF_TX_HI) << 16u);
        phy_drained = 0u;
        for (uint32_t poll = 0u; poll < 2000000u; ++poll) {
            station_service_tx();
            if (mac_now() - wait >= airtime) {
                phy_drained = 1u;
                break;
            }
        }
    } else if (station) {
        uint32_t active_seen = PUBLICATION(tx_diag).phy_debug_first & 2u;
        uint32_t wait = mac_now();
        phy_drained = 0u;
        for (uint32_t poll = 0u; poll < 200000u; ++poll) {
            uint32_t active = mmio_read32(D11_BASE + 0x158u) & 2u;
            active_seen |= active;
            if (active_seen && !active) { phy_drained = 1u; break; }
            if (mac_now() - wait >= airtime + 2000u) break;
        }
        mmio_write16(0x18001502u, 0u);
        mmio_write16(0x18001568u, 0u);
        phy_tx_set_automatic();
    }
    if ((is_ofdm || ht_probe) && !station) {
        /* MAC completion precedes the OFDM PHY tail. Retain TX enable for a
         * complete PPDU after MAC drain, including the ERP signal extension. */
        uint32_t drain_start = mac_now();
        phy_drained = 0u;
        for (uint32_t poll = 0u; poll < 2000000u; ++poll) {
            if (mac_now() - drain_start >= ofdm_airtime) {
                phy_drained = 1u;
                break;
            }
        }
    }
    PUBLICATION(tx_diag).complete_ticks = ARM_CYCLE_COUNTER;
    PUBLICATION(tx_diag).mac_complete_us = mac_now();
    PUBLICATION(tx_diag).status_after = tx_status();
    PUBLICATION(tx_diag).phy_error = mmio_read16(D11_BASE + 0x3eeu);
    tx_error->select_operand = shm_read16(CLEAN_TX_SELECT_OPERAND);
    tx_error->select_readback = shm_read16(CLEAN_TX_SELECT_READBACK);
    for (uint32_t i = 0u; i < 3u; ++i)
        tx_error->plcp[i] = mmio_read16(D11_BASE + 0x5c0u + 2u * i);
    tx_error->reserved = 0u;
    PUBLICATION(tx_diag).psm_debug = mmio_read32(D11_BASE + 0x154u);
    if (STATION(station_hw)) {
        completion_by_arm |= station_observe_tx_completion();
        TX_COMPLETION->latched = shm_read16(CLEAN_TX_COMPLETION);
        TX_COMPLETION->raw_conditions = raw_conditions;
        TX_COMPLETION->request = shm_read16(CLEAN_DCF_REQUEST);
        TX_COMPLETION->pipeline = shm_read16(CLEAN_TX_PRE_RESPONSE) |
            ((uint32_t)shm_read16(CLEAN_DCF_TX_PENDING) << 16u) |
            (phy_started_by_arm << 17u) |
            ((uint32_t)shm_read16(CLEAN_RESPONSE_PENDING) << 18u) |
            ((uint32_t)shm_read16(CLEAN_TX_CLEANUP_REQUEST) << 20u) |
            (completion_by_arm << 22u) |
            ((uint32_t)shm_read16(0x601u) << 24u);
        if ((TX_COMPLETION->latched & 7u) == 7u) tx_conditions = 7u;
        if (!shm_read16(CLEAN_DCF_TX_PENDING)) {
            PUBLICATION(tx_diag).mac_trigger_us = (uint32_t)shm_read16(CLEAN_DCF_TX_LO) |
                ((uint32_t)shm_read16(CLEAN_DCF_TX_HI) << 16u);
            STATION(ack_started) = clean_tx_tail_ack_start(&STATION(cck_tx_tail), STATION(ack_started),
                                                  PUBLICATION(tx_diag).mac_trigger_us);
        }
        if (tx_conditions == 7u && !(st_50e_seen & 0x400u))
            ++TX_COMPLETION->preserved;
    }
    MBOX->fw_substage = tx_conditions;

    if (!station) {
        mmio_write16(0x18001502u, 0x0000u);
        mmio_write16(0x18001568u, 0x0000u);
        mmio_write16(0x180017c0u, 0x8300u);
        mmio_write16(0x180017d0u, 0x0300u);
        mmio_write32(D11_MACCONTROL, 0x84020400u);
        (void)mmio_read32(D11_MACCONTROL);
        phy_tx_set_inhibit(1);
    }
    uint32_t quiescent = STATION(station_hw) && phy_drained &&
        clean_tx_incomplete_retryable(TX_COMPLETION->latched, TX_COMPLETION->request,
            shm_read16(CLEAN_DCF_TX_PENDING), shm_read16(CLEAN_RESPONSE_PENDING),
            mmio_read16(D11_BASE + 0x50eu), mmio_read16(D11_BASE + 0x690u));
    uint32_t phy_aborted = quiescent && TX_COMPLETION->latched == 5u &&
        !shm_read16(CLEAN_RESPONSE_PENDING) && STATION(cck_ack_window) && STATION(cck_tx_tail).ended &&
        clean_tx_cck_abort_retryable(tx_error, PUBLICATION(tx_diag).phy_error, airtime);
    uint32_t incomplete = quiescent && (!PUBLICATION(tx_diag).phy_error || phy_aborted);
    if (phy_aborted) {
        volatile clean_tx_abort_status_t *s =
            (volatile clean_tx_abort_status_t *)CLEAN_TX_ABORT_ADDR;
        if (!s->count) {
            s->fired_at = tx_error->phase[CLEAN_TX_FIRED].at;
            s->before_error = tx_error->phase[CLEAN_TX_PREPARE].error;
            s->armed_error = tx_error->phase[CLEAN_TX_ARMED].error;
            s->fired_error = tx_error->phase[CLEAN_TX_FIRED].error;
            s->completion = (uint16_t)TX_COMPLETION->latched;
            s->first = tx_error->phase[CLEAN_TX_FIRST_ERROR];
        }
        ++s->count;
    }
    if (STATION(station_mac) && phy_drained && (tx_conditions == 7u || incomplete)) {
        uint32_t control = mmio_read32(D11_MACCONTROL);
        mmio_write32(D11_MACCONTROL, clean_mac_pause_psm(control));
        (void)mmio_read32(D11_MACCONTROL);
        shm_write16(CLEAN_TX_CLEANUP_REQUEST, incomplete ? CLEAN_TX_CLEANUP_ABORT : 1u);
        mmio_write32(D11_MACCONTROL, control);
        (void)mmio_read32(D11_MACCONTROL);
    }
    MBOX->rc_dacbuf = 0x55550000u | total_len;
    if (!phy_drained) return fail(0x82u, D11_BASE + 0x1cu, ofdm_airtime);
    if ((ht_probe || (is_ofdm && !station)) && PUBLICATION(tx_diag).phy_error != 0u)
        return fail(0x82u, D11_BASE + 0x3eeu, PUBLICATION(tx_diag).phy_error);
    if (incomplete) {
        ++TX_COMPLETION->timeouts;
        return 3;
    }
    return tx_conditions == 7u || fail(0x82u, 0x1800150eu, MBOX->vco_status);
}
