        uint16_t st = shm_read16(0x601u);
        if (st == PSM_STATE_FAULT) {
            if (STATION(station_enabled) || CLEAN_RX_HT_REARM_WAIT || CLEAN_RX_HT_COPY_WAIT) {
#if CLEAN_RX_HT_REARM_WAIT || CLEAN_RX_HT_COPY_WAIT
                snapshot_psm(LOOP(rx_start),LOOP(polls));
                fail(0x88u,0x602u,shm_read16(0x602u));
#else
                fail(0x88u,0x601u,st);
#endif
                break;
            }
            if (!d11_restart_rx_psm(LOOP(channel))) break;
            TRANSPORT(capture_prepared) = 0u;
            mmio_write32(D11_MACCONTROL, LOOP(mac_en));
            continue;
        }
        if (st == PSM_STATE_CAPTURED) {
            if (LOOP(capture_target_reached)) continue;
            if (STATION(station_mac)) {
                station_consume_ack();
                if (shm_read16(0x601u) != PSM_STATE_CAPTURED) continue;
                station_observe_capture();
                if (STATION(station_hw)) {
                    uint32_t captured = (uint32_t)shm_read16(CLEAN_DCF_RX_LO) |
                        ((uint32_t)shm_read16(CLEAN_DCF_RX_HI) << 16u);
                    uint32_t held = mac_now() - captured;
                    if (held > MAC_TRACE->rx_capture_max_us)
                        MAC_TRACE->rx_capture_max_us = held;
                }
                uint32_t length = shm_read16(0x606u);
                if (length < CLEAN_PLCP_BYTES ||
                    !clean_mac_rx_relevant(STATION(nav_frame), length - CLEAN_PLCP_BYTES,
                                           STATION(station_address), STATION(station_peer))) {
                    if (!TRANSPORT(capture_prepared)) (void)rx_dma_poll_and_reclaim(0);
                    TRANSPORT(capture_prepared) = 0u;
                    ++MAC_TRACE->rx_filtered;
                    shm_write16(0x601u, 0u);
                    STATION(nav_sampled) = 0u;
                    continue;
                }
            }
            if (STATION(station_enabled) || CLEAN_RX_HT_DATA || CLEAN_RX_HT_OBSERVER_FIFO) {
                int credit = (STATION(station_mac) || CLEAN_RX_AMPDU_SHM || CLEAN_RX_HT_OBSERVER_FIFO) ?
                    clean_sdio_credit_available(MULTI->captured_frames, TRANSPORT(rx_published_frames),
                                                FW_MAX_FRAMES) :
                    clean_sdio_credit_available(TRANSPORT(sdio_tx_posted), MBOX->f2_consumed,
                                                SDIO_TX_WINDOW);
                if (credit < 0) {
                    fail(0x8cu, (STATION(station_mac) || CLEAN_RX_AMPDU_SHM || CLEAN_RX_HT_OBSERVER_FIFO) ? FW_MULTI_ADDR :
                        FW_MBOX_ADDR + CLEAN_SDIO_ACK_OFFSET,
                        (STATION(station_mac) || CLEAN_RX_AMPDU_SHM || CLEAN_RX_HT_OBSERVER_FIFO) ?
                            MULTI->captured_frames - TRANSPORT(rx_published_frames) :
                        MBOX->f2_consumed);
                    break;
                }
                /* An ACKed frame is retained in SHM when all SRAM slots are
                 * occupied, or in its SRAM slot until F2 accepts it. */
                if (!credit) {
                    if (STATION(station_enabled)) ++MAC_RADIO->rx_waits;
                    continue;
                }
            }
            uint32_t total = MULTI->captured_frames;
            uint32_t idx = total & (FW_MAX_FRAMES - 1u);
            volatile fw_frame_slot_t *slot = &MULTI->frames[idx];
            if (!TRANSPORT(capture_prepared)) {
                uint16_t rxe_len = shm_read16(0x606u);
                uint16_t fc_word = shm_read16(CLEAN_SHM_FRAME + 4u);
                uint16_t ra_word0 = shm_read16(CLEAN_SHM_FRAME + 6u);
                uint16_t ra_word1 = shm_read16(CLEAN_SHM_FRAME + 7u);
                uint16_t ra_word2 = shm_read16(CLEAN_SHM_FRAME + 8u);
                uint32_t frame_type = (fc_word >> 2u) & 3u;
                if ((LOOP(flags) & CFG_TX_ENABLE) && !STATION(station_enabled) && rxe_len >= 30u &&
                    (frame_type == 0u || frame_type == 2u) &&
                    ra_word0 == shm_read16(0x612u) &&
                    ra_word1 == shm_read16(0x613u) &&
                    ra_word2 == shm_read16(0x614u)) {
                    uint16_t ta_w0 = shm_read16(CLEAN_SHM_FRAME + 9u);
                    uint16_t ta_w1 = shm_read16(CLEAN_SHM_FRAME + 10u);
                    uint16_t ta_w2 = shm_read16(CLEAN_SHM_FRAME + 11u);
                    uint8_t ta[6] = {
                        (uint8_t)ta_w0, (uint8_t)(ta_w0 >> 8u),
                        (uint8_t)ta_w1, (uint8_t)(ta_w1 >> 8u),
                        (uint8_t)ta_w2, (uint8_t)(ta_w2 >> 8u)};
                    d11_transmit_ack_frame(ta, LOOP(channel), LOOP(mac_en) & ~2u);
                    mmio_write32(D11_MACCONTROL, LOOP(mac_en));
                }
                slot->frame_index = (uint16_t)(total + 1u);
#if CLEAN_RX_HT_OBSERVER_FIFO
                uint16_t correlation = shm_read16(CLEAN_OBSERVER_HELD);
#if CLEAN_RX_HT_ADVANCE
                if (correlation && correlation == shm_read16(CLEAN_OBSERVER_EARLY_SHM) &&
                    shm_read16(CLEAN_OBSERVER_ADVANCE) == 1u)
                    correlation |= CLEAN_OBSERVER_ADVANCE_TAG;
#endif
                TRANSPORT(observer_capture_ids)[idx] = correlation;
#endif
                slot->rxe_len = rxe_len;
                slot->rxe_status1 = shm_read16(0x7a7u);
                slot->rxe_status2 = shm_read16(0x603u);
                /* The held PSM header belongs to this capture; DMA can lag it. */
                slot->phy_status0 = shm_read16(CLEAN_SHM_PHY_STATUS0);
                slot->rx_tick_hi = (uint16_t)((ARM_CYCLE_COUNTER - LOOP(rx_start)) >> 16u);
                uint32_t copy_words = ((uint32_t)rxe_len + 3u) >> 1u;
                if (copy_words > CLEAN_RAW_BYTES / 2u) copy_words = CLEAN_RAW_BYTES / 2u;
                for (uint32_t w_idx = 0u; w_idx < copy_words; ++w_idx) {
                    uint16_t w = shm_read16(CLEAN_SHM_FRAME + w_idx);
                    slot->raw[2u * w_idx] = (uint8_t)w;
                    slot->raw[2u * w_idx + 1u] = (uint8_t)(w >> 8u);
                }
                for (uint32_t b_idx = 2u * copy_words; b_idx < CLEAN_RAW_BYTES; ++b_idx)
                    slot->raw[b_idx] = 0u;
                if ((slot->phy_status0 & 3u) >= 2u && rxe_len >= CLEAN_PLCP_BYTES + 14u) {
                    HT_RX->rxe_length = rxe_len;
                    for (uint32_t i = 0u; i < 4u; ++i)
                        HT_RX->phy[i] = shm_read16(CLEAN_SHM_PHY_STATUS0 + i);
                    for (uint32_t i = 0u; i < 6u; ++i) HT_RX->sig[i] = slot->raw[2u+i];
                    uint32_t length = rxe_len - CLEAN_PLCP_BYTES;
                    for (uint32_t i = 0u; i < sizeof(HT_RX->mpdu); ++i)
                        HT_RX->mpdu[i] = i < length ? slot->raw[8u+i] : 0u;
                    ++HT_RX->captures;
                }
                slot->dma_rx_len = 0u;
                slot->dma_completed = 0u;
                slot->f2_received = 0u;
                slot->f2_sdpcm_len = 0u;
                (void)rx_dma_poll_and_reclaim(slot);
                TRANSPORT(capture_prepared) = 1u;
            }
            if (STATION(station_mac) || CLEAN_RX_AMPDU_SHM || CLEAN_RX_HT_OBSERVER_FIFO) {
                TRANSPORT(capture_prepared) = 0u;
                MULTI->captured_frames = total + 1u;
                if (!LOOP(scanner_mode) && total + 1u >= LOOP(target_frames))
                    LOOP(capture_target_reached) = 1u;
                shm_write16(0x601u, 0u);
                STATION(nav_sampled) = 0u;
                continue;
            }
            int posted = sdio_f2_send_frame(slot, LOOP(channel));
            if (MBOX->fw_state == FW_STATE_ERROR) break;
            if ((STATION(station_enabled) || CLEAN_RX_HT_DATA) && !posted) {
                if (STATION(station_enabled)) ++MAC_RADIO->rx_waits;
                continue;
            }
            TRANSPORT(capture_prepared) = 0u;
            MULTI->captured_frames = total + 1u;
#if CLEAN_RX_AMPDU_DMA
            TRANSPORT(rx_published_frames) = total + 1u;
#endif
            if (!LOOP(scanner_mode) && total + 1u >= LOOP(target_frames))
                break;
            shm_write16(0x601u, 0u);
            STATION(nav_sampled) = 0u;
        }
