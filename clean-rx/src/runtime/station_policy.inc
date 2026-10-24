static void station_apply_policy(uint32_t mac_en) {
    if (!STATION(policy_pending) || !station_policy_boundary(shm_read16(0x601u))) return;
    uint32_t now = mac_now();
    clean_edca_t next_edca;
    clean_bytes_copy((uint8_t *)&next_edca, (const uint8_t *)&STATION(station_edca), sizeof(next_edca));
    clean_dcf_t next_legacy, previous;
    clean_bytes_copy((uint8_t *)&next_legacy, (const uint8_t *)&STATION(station_legacy_dcf),
                      sizeof(next_legacy));
    clean_bytes_copy((uint8_t *)&previous, (const uint8_t *)&station_dcf, sizeof(previous));
    if (STATION(policy_edca_pending)) {
        if (!clean_edca_configure(&next_edca, STATION(policy_edca), now,
                                  previous.random, STATION(policy_short_slot))) {
            fail(0x85u, FW_TX_BUF_ADDR, STATION(policy_command));
            return;
        }
        for (uint32_t i = 0u; i < 4u; ++i) {
            clean_dcf_t *d = &next_edca.ac[i];
            uint32_t remaining = previous.nav_until - now;
            if (previous.nav_valid && remaining && remaining <= 32767u)
                clean_dcf_nav(d, now, (uint16_t)remaining);
            if (previous.ifs_us > 10u + previous.aifsn * previous.slot_us)
                clean_dcf_bad_rx(d, now);
        }
    } else if (next_edca.enabled) {
        for (uint32_t i = 0u; i < 4u; ++i)
            clean_dcf_slot(&next_edca.ac[i], now, STATION(policy_short_slot));
    } else clean_dcf_slot(&next_legacy, now, STATION(policy_short_slot));
    mmio_write32(D11_MACCONTROL, clean_mac_pause_psm(mac_en));
    uint32_t control = mmio_read32(D11_MACCONTROL);
    if ((control & 3u) != (clean_mac_pause_psm(mac_en) & 3u)) {
        fail(0x85u, D11_MACCONTROL, control);
        return;
    }
    if (!station_policy_boundary(shm_read16(0x601u)) || station_phy_busy()) {
        mmio_write32(D11_MACCONTROL, mac_en);
        return;
    }
    if (STATION(station_hw) && !STATION(policy_edca_pending)) {
        uint16_t slots = shm_read16(CLEAN_DCF_SLOTS);
        if (slots > CLEAN_DCF_MAX_SLOTS) {
            fail(0x85u, CLEAN_DCF_SLOTS, slots);
            return;
        }
        if (next_edca.enabled) next_edca.ac[next_edca.selected].slots = slots;
        else next_legacy.slots = slots;
    }
    if (!station_response_rates(STATION(policy_basic))) return;
    for (uint32_t i = 0u; i < 3u; ++i)
        if (!station_policy_word(0x612u + i, (uint16_t)(STATION(station_address)[2u*i] |
                ((uint16_t)STATION(station_address)[2u*i + 1u] << 8u))) ||
            !station_policy_word(CLEAN_ACK_PEER + i, (uint16_t)(STATION(station_peer)[2u*i] |
                ((uint16_t)STATION(station_peer)[2u*i + 1u] << 8u)))) return;
    clean_bytes_copy((uint8_t *)&STATION(station_edca), (const uint8_t *)&next_edca, sizeof(STATION(station_edca)));
    clean_bytes_copy((uint8_t *)&STATION(station_legacy_dcf), (const uint8_t *)&next_legacy,
                      sizeof(STATION(station_legacy_dcf)));
    station_hw_slot();
    station_hw_backoff();
    shm_write16(CLEAN_QOS_RESPONSE_POLICY, STATION(policy_response));
    if (!station_policy_word(CLEAN_QOS_RESPONSE_POLICY, STATION(policy_response))) return;
    if (STATION(station_hw) &&
        (!station_policy_word(CLEAN_DCF_SLOT_US, station_dcf.slot_us) ||
         !station_policy_word(CLEAN_DCF_DIFS_US,
             (uint16_t)(10u + station_dcf.aifsn * station_dcf.slot_us)) ||
         !station_policy_word(CLEAN_DCF_EIFS_US, 314u) ||
         !station_policy_word(CLEAN_DCF_SLOTS, station_dcf.slots) ||
         !station_policy_word(CLEAN_DCF_IDLE_VALID, 0u))) return;
    if (STATION(station_hw)) {
        uint16_t slot = mmio_read16(D11_BASE + 0x684u);
        if (slot != 0x1feu + station_dcf.slot_us) {
            fail(0x85u, D11_BASE + 0x684u, slot);
            return;
        }
    }
    STATION(policy_edca_pending) = STATION(policy_pending) = 0u;
    station_publish_mac();
    __asm__ volatile("dsb" ::: "memory");
    mmio_write32(D11_MACCONTROL, mac_en);
    control = mmio_read32(D11_MACCONTROL);
    if ((control & 3u) != (mac_en & 3u)) {
        fail(0x85u, D11_MACCONTROL, control);
        return;
    }
    MBOX->host_cmd = 2u;
    __asm__ volatile("dsb" ::: "memory");
}

