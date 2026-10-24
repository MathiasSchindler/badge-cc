        if (cmd == 3u) {
            station_publish_mac();
            snapshot_ack_rx();
            if (STATION(station_enabled)) {
                UNICAST->ack_sent = shm_read16(CLEAN_ACK_SENT);
                UNICAST->ack_error = shm_read16(CLEAN_ACK_ERROR);
                UNICAST->ack_txe_status = shm_read16(0x635u);
                UNICAST->ack_ifs_status = shm_read16(0x636u);
            }
            break;
        }
        if ((cmd & 0xffu) == 4u) {
            if (STATION(station_enabled)) {
                fail(0x85u, FW_MBOX_ADDR + 24u, cmd);
                break;
            }
            uint32_t new_ch = (cmd >> 8u) & 0xffu;
            if (new_ch >= 1u && new_ch <= 14u) {
                mmio_write32(D11_MACCONTROL, 0x84020400u);
                (void)mmio_read32(D11_MACCONTROL);
                (void)delay_ticks(2000u);
                if (phy_fast_switch_channel_2g(new_ch, &LOOP(vco_st))) {
                    MBOX->vco_status = LOOP(vco_st);
                    LOOP(channel) = new_ch;
                }
                shm_write16(0x609u, (uint16_t)LOOP(channel));
                if (shm_read16(0x601u) == PSM_STATE_CAPTURED || shm_read16(0x601u) == PSM_STATE_FAULT) {
                    shm_write16(0x601u, 0u);
                    TRANSPORT(capture_prepared) = 0u;
                }
                mmio_write32(D11_MACCONTROL, LOOP(mac_en));
                MBOX->maccontrol_active = mmio_read32(D11_MACCONTROL);
            }
            MBOX->host_cmd = 2u;
            __asm__ volatile("dsb" ::: "memory");
            continue;
        }
        if ((cmd & 0xffu) == 5u || (cmd & 0xffu) == CLEAN_TX_CCK_SHORT ||
            (cmd & 0xffu) == CLEAN_HT_PROBE_CMD || (cmd & 0xffu) == CLEAN_HT_DATA_CMD) {
            if ((LOOP(flags) & CFG_TX_ENABLE) == 0u) {
                fail(0x81u, FW_MBOX_ADDR + 24u, cmd);
                break;
            }
            uint32_t tx_len  = clean_tx_length(cmd);
            uint32_t is_ofdm = (cmd >> 18u) & 1u;
            uint32_t tx_cnt  = (cmd >> 19u) & 0xffu;
            if (tx_cnt == 0u) tx_cnt = 1u;
            uint32_t reliable = (cmd & CLEAN_TX_ACK_REQUIRED) != 0u;
            uint32_t short_preamble = (cmd & 255u) == CLEAN_TX_CCK_SHORT;
            const volatile uint8_t *packet = (const volatile uint8_t *)FW_TX_BUF_ADDR;
            uint32_t ht_probe = (cmd & 255u) == CLEAN_HT_DATA_CMD ? 2u :
                (cmd & 255u) == CLEAN_HT_PROBE_CMD ? 1u : 0u;
            uint32_t ht_clocked = CLEAN_RX_DIAGNOSTIC &&
                ht_probe == 2u && packet[0] == CLEAN_HT_CLOCKED_VERSION;
            const volatile uint8_t *frame = packet + (ht_probe == 2u ? CLEAN_HT_DATA_HEADER : 0u);
#if CLEAN_RX_DIAGNOSTIC
            if ((ht_probe == 1u && !clean_ht_probe_command_valid(cmd, CLEAN_RX_HT_PROBE,
                                                               STATION(station_enabled), packet)) ||
                (ht_probe == 2u && !(ht_clocked ?
                    clean_ht_clocked_command_valid(cmd, CLEAN_RX_HT_DATA,
                        STATION(station_hw) ? 3u : 0u, packet) :
                    clean_ht_data_command_valid(cmd, CLEAN_RX_HT_DATA, STATION(station_enabled), packet)))) {
#else
            if (ht_probe && (ht_probe != 2u || STATION(station_enabled) ||
                !clean_ht_payload_command_valid(cmd, CLEAN_RX_HT_DATA, 0u, packet))) {
#endif
                fail(0x85u, FW_MBOX_ADDR + 24u, cmd);
                break;
            }
            if (ht_clocked) reliable = 1u;
            if (ht_clocked && !clean_ht_clocked_policy_valid(MAC_RADIO->profile,
                    STATION(policy_basic), STATION(policy_short_slot), STATION(policy_response))) {
                fail(0x85u, FW_MBOX_ADDR + 24u, cmd);
                break;
            }
            int tx_tid = ht_clocked ? (int)(frame[30] & 15u) :
                clean_station_tx_tid(frame + 6u, tx_len >= 6u ? tx_len - 6u : 0u,
                                     STATION(station_edca).enabled);
            if (STATION(station_edca).enabled && (tx_tid < 0 || tx_tid >= 8 ||
                                         (ht_probe && !ht_clocked))) {
                fail(0x85u, FW_TX_BUF_ADDR, cmd);
                break;
            }
            if (STATION(station_enabled) && (cmd & (CLEAN_TX_REARM_RX | CLEAN_TX_MATCH_SOURCE))) {
                fail(0x85u, FW_MBOX_ADDR + 24u, cmd);
                break;
            }
            uint8_t addresses_match = 1u;
            if (reliable && tx_len >= 22u)
                for (uint32_t i = 0u; i < 6u; ++i)
                    if (frame[10u + i] != STATION(station_peer)[i] ||
                        frame[16u + i] != STATION(station_address)[i]) addresses_match = 0u;
            if (reliable && (!STATION(station_enabled) || tx_cnt > 8u ||
                tx_len < 34u || tx_len > CLEAN_TX_BYTES ||
                (!ht_clocked && !is_ofdm &&
                    (!STATION(station_mac) || !clean_cck_plcp_valid(frame, tx_len - 6u))) ||
                !clean_mac_valid(frame + 6u, tx_len - 6u) ||
                (frame[10] & 1u) ||
                (!ht_clocked && tx_tid < 0) ||
                !addresses_match)) {
                fail(0x85u, FW_TX_BUF_ADDR, tx_len);
                break;
            }
            if (STATION(station_mac) && !reliable) {
                fail(0x85u, FW_MBOX_ADDR + 24u, cmd);
                break;
            }
            if ((cmd & CLEAN_TX_MATCH_SOURCE) &&
                (tx_len < 34u || tx_len > CLEAN_TX_BYTES || (frame[6] & 0x0fu) != 0u ||
                 (frame[7] & 3u) != 0u || (frame[16] & 1u) != 0u)) {
                fail(0x85u, FW_TX_BUF_ADDR, tx_len);
                break;
            }
            if (reliable) {
                UNICAST->magic = CLEAN_UNICAST_MAGIC;
                UNICAST->result = CLEAN_UNICAST_IDLE;
                UNICAST->attempts = 0u;
                UNICAST->ack_received = 0u;
                UNICAST->started_us = mac_now();
                STATION(access_started) = UNICAST->started_us;
            }
            if (STATION(station_mac)) {
                int selected = 1;
                if (STATION(station_edca).enabled && !STATION(policy_pending)) {
                    uint32_t exchange;
                    if (ht_clocked) {
                        int rate = clean_ht_ack_rate(STATION(policy_basic), frame[0]);
                        uint32_t ack = rate < 0 ? 0u : rate < 4 ?
                            clean_cck_airtime_us(14u, (uint32_t)rate, 0u) :
                            clean_ofdm_airtime_us(14u, (uint32_t)rate - 4u);
                        exchange = ack ? clean_ht20_airtime_us(CLEAN_HT_ACK_MPDU,
                                                               frame[0]) + 10u + ack : 0u;
                    } else exchange = clean_legacy_exchange_us(frame, tx_len, is_ofdm,
                                                               short_preamble, STATION(policy_basic));
                    selected = station_edca_select(LOOP(mac_en), (uint32_t)tx_tid, exchange);
                    if (!selected) break;
                }
                uint32_t identity = (uint32_t)frame[28] | ((uint32_t)frame[29] << 8u) |
                    ((uint32_t)frame[6] << 16u) |
                    (frame[6] == 0x88u || frame[6] == 0xc8u ?
                        (uint32_t)(frame[30] & 15u) << 24u : 0u);
                if (STATION(last_tx_valid) && identity != STATION(last_tx_identity)) {
                    uint8_t redraw = station_dcf.cw != station_dcf.cw_min;
                    clean_dcf_new_frame(&station_dcf, mac_now());
                    if (redraw) station_hw_backoff();
                }
                int credit = clean_sdio_credit_available(TRANSPORT(sdio_tx_posted), MBOX->f2_consumed,
                                                          SDIO_TX_WINDOW);
                if (credit < 0) {
                    fail(0x8cu, FW_MBOX_ADDR + CLEAN_SDIO_ACK_OFFSET, MBOX->f2_consumed);
                    break;
                }
                if (selected == 2 || STATION(policy_pending) || !credit || !station_rx_idle(shm_read16(0x601u)) ||
                    station_phy_busy()) {
                    UNICAST->result = CLEAN_UNICAST_BUSY;
                    ++MAC_RADIO->deferrals;
                    UNICAST->finished_us = mac_now();
                    (void)clean_dcf_poll(&station_dcf, UNICAST->finished_us, 1);
                    station_publish_mac();
                    MBOX->host_cmd = 2u;
                    __asm__ volatile("dsb" ::: "memory");
                    continue;
                }
            }
            if (!STATION(station_mac)) {
                mmio_write32(D11_MACCONTROL, 0x84020400u);
                (void)mmio_read32(D11_MACCONTROL);
                (void)delay_ticks(2000u);
            }
            if (cmd & CLEAN_TX_MATCH_SOURCE) {
                d11_set_addrmatch(0u, frame + 16u);
                d11_set_addrmatch(3u, frame + 22u);
            }
            for (uint32_t t = 0u; t < tx_cnt; ++t) {
                if (reliable) {
                    /* Freeze the sequence number, change only Retry and its FCS. */
                    if (t) {
                        volatile uint8_t *retry = (volatile uint8_t *)FW_TX_BUF_ADDR;
                        if (!clean_mac_retry(retry + 6u, tx_len - 6u)) {
                            fail(0x85u, FW_TX_BUF_ADDR, tx_len);
                            break;
                        }
                    }
                    shm_write16(CLEAN_ACK_SEEN, 0u);
                    shm_write16(CLEAN_ACK_WAITING, STATION(station_mac) ? 0u : 1u);
                }
                int submitted = d11_transmit_template_frame(tx_len, is_ofdm, LOOP(channel),
                    LOOP(mac_en) & ~2u, reliable, short_preamble, ht_probe);
                if (!submitted) break;
                if (submitted == 2) {
                    shm_write16(CLEAN_ACK_WAITING, 0u);
                    UNICAST->result = CLEAN_UNICAST_BUSY;
                    ++MAC_RADIO->deferrals;
                    break;
                }
                if (submitted == 3) {
                    shm_write16(CLEAN_ACK_WAITING, 0u);
                    UNICAST->result = CLEAN_UNICAST_TIMEOUT;
                    ++MAC_RADIO->timeouts;
                    clean_dcf_complete(&station_dcf, mac_now(), 0);
                    station_hw_backoff();
                    break;
                }
                if (reliable) {
                    mmio_write32(D11_MACCONTROL, LOOP(mac_en));
                    uint32_t wait = mac_now();
                    uint32_t ack = 0u;
                    uint32_t timeout = 1500u;
                    if (STATION(station_mac)) {
                        uint32_t rate = is_ofdm ?
                            (uint32_t)clean_ofdm_rate_index(frame[0]) + 4u :
                            (uint32_t)clean_cck_rate_index(frame[0]);
                        timeout = ht_clocked ?
                            clean_ht_ack_timeout_us(frame[0], station_dcf.slot_us) :
                            clean_ack_timeout_us(rate, station_dcf.slot_us);
                    }
                    for (uint32_t poll = 0u; poll < 200000u; ++poll) {
                        if (STATION(station_mac)) station_service_tx();
                        ack = shm_read16(CLEAN_ACK_SEEN);
                        if (ack || (STATION(station_mac) && !shm_read16(CLEAN_ACK_WAITING)) ||
                            mac_now() - wait >= timeout) break;
                    }
                    shm_write16(CLEAN_ACK_WAITING, 0u);
                    if (ack) {
                        UNICAST->ack_received = 1u;
                        UNICAST->result = CLEAN_UNICAST_ACKED;
                        if (STATION(station_mac)) {
                            clean_dcf_complete(&station_dcf, mac_now(), 1);
                            station_hw_backoff();
                            /* ACK control frames are consumed by the MAC, not
                             * application traffic occupying the only F2 credit. */
                            station_consume_ack();
                        }
                        break;
                    }
                    UNICAST->result = CLEAN_UNICAST_TIMEOUT;
                    if (STATION(station_mac)) {
                        ++MAC_RADIO->timeouts;
                        clean_dcf_complete(&station_dcf, mac_now(), 0);
                        station_hw_backoff();
                    }
                    /* A reply can still be arriving after an ACK timeout.
                     * Keep station RX live, not just completed captures. */
                    if (shm_read16(0x601u) == PSM_STATE_CAPTURED) break;
                    if (!STATION(station_mac))
                        mmio_write32(D11_MACCONTROL, 0x84020400u);
                }
                if (!STATION(station_mac) && t + 1u < tx_cnt)
                    (void)delay_ticks(180u * 1500u);
            }
            if (reliable) {
                UNICAST->finished_us = mac_now();
                UNICAST->ack_sent = shm_read16(CLEAN_ACK_SENT);
                UNICAST->ack_error = shm_read16(CLEAN_ACK_ERROR);
                station_publish_mac();
            }
            if (MBOX->fw_state == FW_STATE_ERROR) break;
            if (cmd & CLEAN_TX_REARM_RX) {
                if (!d11_restart_rx_psm(LOOP(channel))) break;
            } else if (!reliable &&
                       !(CLEAN_RX_AMPDU_BA && ht_probe == 2u &&
                         packet[0] == CLEAN_HT_AMPDU_PROBE_VERSION) &&
                       (shm_read16(0x601u) == PSM_STATE_CAPTURED ||
                       shm_read16(0x601u) == PSM_STATE_FAULT))
                shm_write16(0x601u, 0u);
            if (!reliable) TRANSPORT(capture_prepared) = 0u;
            TRANSPORT(rx_after_explicit_tx) = 1u;
            mmio_write32(D11_MACCONTROL, LOOP(mac_en));
            MBOX->maccontrol_active = mmio_read32(D11_MACCONTROL);
            PUBLICATION(tx_diag).resume_ticks = ARM_CYCLE_COUNTER;
            PUBLICATION(tx_diag).mac_resume_us = mac_now();
            PUBLICATION(tx_diag).psm_after = shm_read16(0x601u);
            publish_tx_diag();
            MBOX->host_cmd = 2u;
            __asm__ volatile("dsb" ::: "memory");
            continue;
        }
        if ((cmd & 0xffu) == 7u) {
            const volatile uint8_t *p = (const volatile uint8_t *)FW_TX_BUF_ADDR;
            uint16_t basic = (uint16_t)((uint16_t)p[12] | ((uint16_t)p[13] << 8u));
            uint8_t nonzero = 0u, peer = 0u;
            for (uint32_t i = 0u; i < 6u; ++i) { nonzero |= p[i]; peer |= p[6u + i]; }
            if (!(LOOP(flags) & CFG_TX_ENABLE) || !nonzero || !peer || (p[0] & 1u) ||
                (p[6] & 1u) || clean_ack_rate(basic, 0u) < 0 || p[14] > 1u ||
                (p[15] < 1u || p[15] > 3u) || STATION(station_enabled)) {
                fail(0x85u, FW_TX_BUF_ADDR, basic);
                break;
            }
            mmio_write32(D11_MACCONTROL, clean_mac_pause_psm(LOOP(mac_en)));
            (void)mmio_read32(D11_MACCONTROL);
            for (uint32_t i = 0u; i < 3u; ++i)
                shm_write16(0x612u + i, (uint16_t)((uint16_t)p[2u*i] |
                            ((uint16_t)p[2u*i + 1u] << 8u)));
            d11_set_addrmatch(0u, p);
            d11_set_addrmatch(3u, p + 6u);
            for (uint32_t i = 0u; i < 3u; ++i)
                shm_write16(CLEAN_ACK_PEER + i, (uint16_t)((uint16_t)p[6u + 2u*i] |
                            ((uint16_t)p[7u + 2u*i] << 8u)));
            for (uint32_t i = 0u; i < 3u; ++i) {
                if (shm_read16(0x612u + i) !=
                    ((uint16_t)p[2u*i] | ((uint16_t)p[2u*i+1u] << 8u)) ||
                    shm_read16(CLEAN_ACK_PEER + i) !=
                    ((uint16_t)p[6u+2u*i] | ((uint16_t)p[7u+2u*i] << 8u))) {
                    fail(0x85u, 0x612u + i, shm_read16(0x612u + i));
                    break;
                }
            }
            if (MBOX->fw_state == FW_STATE_ERROR) break;
            if (!station_response_rates(basic)) break;
            STATION(policy_basic) = basic;
            STATION(policy_short_slot) = p[14];
            STATION(policy_response) = STATION(policy_pending) = 0u;
            STATION(policy_command) = 0u;
            STATION(station_edca).enabled = STATION(policy_edca_pending) = 0u;
            shm_write16(CLEAN_ACK_WAITING, 0u);
            shm_write16(CLEAN_ACK_SEEN, 0u);
            shm_write16(CLEAN_ACK_SENT, 0u);
            shm_write16(CLEAN_ACK_ERROR, 0u);
            shm_write16(CLEAN_QOS_RESPONSE_POLICY, 0u);
            shm_write16(CLEAN_RESPONSE_POLICY, p[15] >= 2u ?
                        3u | CLEAN_RESPONSE_INTERNAL_ACK : 0u);
            shm_write16(CLEAN_ACK_CONSUMED, 0u);
            MAC_TRACE->magic = CLEAN_MAC_TRACE_MAGIC;
            for (uint32_t i = 0u; i < 4u; ++i) shm_write16(CLEAN_ACK_KINDS + i, 0u);
            shm_write16(CLEAN_CTS_ALLOWED, 1u);
            shm_write16(CLEAN_CTS_SENT, 0u);
            shm_write16(CLEAN_SHORT_SENT, 0u);
            shm_write16(CLEAN_TX_CLEANUP_REQUEST, 0u);
            shm_write16(CLEAN_TX_CLEANUP_COUNT, 0u);
            shm_write16(CLEAN_TX_COMPLETION, 0u);
            shm_write16(CLEAN_TX_PRE_RESPONSE, 0u);
            TX_COMPLETION->magic = CLEAN_TX_COMPLETION_MAGIC;
            TX_COMPLETION->latched = TX_COMPLETION->preserved = TX_COMPLETION->raw_conditions = 0u;
            TX_COMPLETION->request = TX_COMPLETION->pipeline = 0u;
            TX_COMPLETION->timeouts = 0u;
            *(volatile uint32_t *)CLEAN_TX_START_HANDOFFS_ADDR = 0u;
            *(volatile uint32_t *)CLEAN_TX_COMPLETION_HANDOFFS_ADDR = 0u;
            if (!station_response_evidence_reset()) break;
            shm_write16(CLEAN_ACK_ENABLE, 1u);
            STATION(station_enabled) = 1u;
            STATION(station_mac) = (uint8_t)(p[15] >= 2u);
            STATION(station_hw) = (uint8_t)(p[15] == 3u);
            TRANSPORT(rx_published_frames) = MULTI->captured_frames;
            STATION(rx_discard_fcs) = shm_read16(0x7a8u);
            STATION(rx_discard_length) = shm_read16(0x7a9u);
            for (uint32_t i = 0u; i < 6u; ++i) {
                STATION(station_address)[i] = p[i];
                STATION(station_peer)[i] = p[6u + i];
            }
            clean_dcf_init(&station_dcf, mac_now(),
                ARM_CYCLE_COUNTER ^ ((uint32_t)p[2] << 24u) ^ ((uint32_t)p[3] << 16u) ^
                ((uint32_t)p[4] << 8u) ^ p[5], p[14]);
            station_hw_slot();
            station_hw_backoff();
            shm_write16(CLEAN_DCF_ENABLE, STATION(station_hw));
            MAC_RADIO->magic = CLEAN_MAC_RADIO_MAGIC;
            MAC_RADIO->profile = p[15];
            station_publish_mac();
            STATION(nav_sampled) = 0u;
            UNICAST->magic = CLEAN_UNICAST_MAGIC;
            UNICAST->result = CLEAN_UNICAST_IDLE;
            phy_tx_hw_configure(LOOP(channel));
            phy_tx_set_inhibit(0);
            phy_tx_set_automatic();
            mmio_write32(D11_MACCONTROL, LOOP(mac_en));
            MBOX->host_cmd = 2u;
            __asm__ volatile("dsb" ::: "memory");
            continue;
        }
        if ((cmd & 0xffu) == 8u || (cmd & 0xffu) == CLEAN_WMM_POLICY_CMD) {
            const volatile uint8_t *p = (const volatile uint8_t *)FW_TX_BUF_ADDR;
            uint16_t basic = (uint16_t)((uint16_t)p[12] | ((uint16_t)p[13] << 8u));
            clean_edca_ac_t next_edca[4];
            uint32_t wmm = (cmd & 0xffu) == CLEAN_WMM_POLICY_CMD;
            uint8_t matches = 1u;
            for (uint32_t i = 0u; i < 6u; ++i)
                if (p[i] != STATION(station_address)[i] || p[6u + i] != STATION(station_peer)[i])
                    matches = 0u;
            if (!STATION(station_mac) || !matches || clean_ack_rate(basic, 0u) < 0 ||
                p[14] > 1u || p[15] != MAC_RADIO->profile ||
                (wmm && (!STATION(station_hw) || !clean_wmm_policy_decode(next_edca, cmd,
                    CLEAN_RX_WMM, MAC_RADIO->profile, p)))) {
                fail(0x85u, FW_TX_BUF_ADDR, basic);
                break;
            }
            STATION(policy_basic) = basic;
            STATION(policy_short_slot) = p[14];
            if (wmm) {
                for (uint32_t i = 0u; i < 4u; ++i) STATION(policy_edca)[i] = next_edca[i];
                STATION(policy_response) = p[16];
                STATION(policy_edca_pending) = 1u;
            }
            STATION(policy_pending) = 1u;
            STATION(policy_command) = cmd;
            continue;
        }
        if ((cmd & 0xffu) == CLEAN_AMPDU_BA_CMD) {
#if CLEAN_RX_AMPDU_BA
            uint8_t plcp[6];
            uint32_t mcs = cmd >> 8u;
            int rate = clean_ht_ack_rate(STATION(policy_basic), mcs);
            if (!STATION(station_hw) || mcs >= 8u || STATION(policy_basic) != 0x150u ||
                rate < 4 || shm_read16(CLEAN_AMPDU_BA_WORD) ||
                AGGREGATE(ampdu_shm_copied) || shm_read16(CLEAN_AMPDU_PROBE_SHM) ||
                !clean_ofdm_plcp_rate(plcp, 32u, (uint32_t)rate - 4u)) {
                fail(0x85u, FW_MBOX_ADDR + 24u, cmd);
                break;
            }
            mmio_write32(D11_MACCONTROL, clean_mac_pause_psm(LOOP(mac_en)));
            uint32_t control = mmio_read32(D11_MACCONTROL);
            if ((control & 3u) != (clean_mac_pause_psm(LOOP(mac_en)) & 3u)) {
                fail(0x8du, D11_MACCONTROL, control);
                break;
            }
            for (uint32_t i = 1u; i < CLEAN_AMPDU_BA_WORDS; ++i) {
                uint16_t value = i >= 14u && i <= 16u ?
                    (uint16_t)(plcp[2u*(i-14u)] |
                        ((uint16_t)plcp[2u*(i-14u)+1u] << 8u)) :
                    i == 17u ? clean_ofdm_rates[rate-4].phyctl1 : 0u;
                shm_write16(CLEAN_AMPDU_BA_WORD + i, value);
                if (shm_read16(CLEAN_AMPDU_BA_WORD + i) != value) {
                    fail(0x8du, CLEAN_AMPDU_BA_WORD + i, value);
                    break;
                }
            }
            if (MBOX->fw_state == FW_STATE_ERROR) break;
            shm_write16(CLEAN_AMPDU_BA_WORD, 1u);
            if (shm_read16(CLEAN_AMPDU_BA_WORD) != 1u) {
                fail(0x8du, CLEAN_AMPDU_BA_WORD, shm_read16(CLEAN_AMPDU_BA_WORD));
                break;
            }
            mmio_write32(D11_MACCONTROL, LOOP(mac_en));
            MBOX->host_cmd = 2u;
            __asm__ volatile("dsb" ::: "memory");
            continue;
#else
            fail(0x85u, FW_MBOX_ADDR + 24u, cmd);
            break;
#endif
        }
        if ((cmd & 0xffu) == CLEAN_HT_RESPONSE_CMD) {
            int policy = clean_ht_response_policy(cmd, CLEAN_RX_HT_RESPONSE,
                                                   MAC_RADIO->profile);
            if (!STATION(station_hw) || policy < 0) {
                fail(0x85u, FW_MBOX_ADDR + 24u, cmd);
                break;
            }
            STATION(policy_response) = (uint8_t)policy;
            STATION(policy_pending) = 1u;
            STATION(policy_command) = cmd;
            continue;
        }
        if ((cmd & 0xffu) == CLEAN_EDCA_CMD) {
            const volatile uint8_t *p = (const volatile uint8_t *)FW_TX_BUF_ADDR;
            if (!clean_edca_command_valid(cmd, CLEAN_RX_EDCA, MAC_RADIO->profile) ||
                !STATION(station_hw) || !clean_edca_decode(STATION(policy_edca), p) ||
                !clean_edca_valid(STATION(policy_edca))) {
                fail(0x85u, FW_TX_BUF_ADDR, cmd);
                break;
            }
            STATION(policy_edca_pending) = STATION(policy_pending) = 1u;
            STATION(policy_command) = cmd;
            continue;
        }
#if CLEAN_RX_DIAGNOSTIC
#include "diagnostic/ack_command.inc"
#endif
        if ((cmd & 0xffu) == 6u) {
            if (STATION(station_enabled)) {
                fail(0x85u, FW_MBOX_ADDR + 24u, cmd);
                break;
            }
            /* Install 128-bit AES-CCMP Temporal Key (TK) from FW_TX_BUF_ADDR into SHM 0x618..0x61f and enable HW CCMP */
            const volatile uint8_t *key_buf = (const volatile uint8_t *)FW_TX_BUF_ADDR;
            mmio_write32(D11_MACCONTROL, 0x84020400u);
            (void)mmio_read32(D11_MACCONTROL);
            for (uint32_t k = 0u; k < 8u; ++k) {
                uint16_t kw = (uint16_t)key_buf[2u * k] | ((uint16_t)key_buf[2u * k + 1u] << 8u);
                shm_write16(0x618u + k, kw);
            }
            shm_write16(0x615u, (uint16_t)((cmd >> 8u) & 1u));
            if (shm_read16(0x601u) == PSM_STATE_CAPTURED || shm_read16(0x601u) == PSM_STATE_FAULT) {
                shm_write16(0x601u, 0u);
                TRANSPORT(capture_prepared) = 0u;
            }
            mmio_write32(D11_MACCONTROL, LOOP(mac_en));
            MBOX->maccontrol_active = mmio_read32(D11_MACCONTROL);
            MBOX->host_cmd = 2u;
            __asm__ volatile("dsb" ::: "memory");
            continue;
        }
