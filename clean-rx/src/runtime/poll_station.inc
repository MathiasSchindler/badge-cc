#if CLEAN_RX_HT_OBSERVER_FIFO
        if (!station_publish_capture(LOOP(channel))) break;
#endif
#if CLEAN_RX_HT_EARLY_FIFO
        if (!observer_receive_early(LOOP(rx_start))) break;
#endif
#if CLEAN_RX_AMPDU_DMA
        if (!STATION(station_mac) && !ampdu_dma_receive(LOOP(rx_start))) break;
#endif
#if CLEAN_RX_AMPDU_SHM
        if (!ampdu_shm_receive(LOOP(mac_en), LOOP(rx_start))) break;
#endif
#if CLEAN_RX_AMPDU_DELIVERY
        if (!STATION(station_mac) && !station_publish_capture(LOOP(channel))) break;
#endif
#if CLEAN_RX_AMPDU_SHM
        if (!STATION(station_mac) && LOOP(capture_target_reached) &&
            MULTI->captured_frames == TRANSPORT(rx_published_frames)) break;
#endif
        if (STATION(station_mac)) {
            if (STATION(policy_pending) && cmd != STATION(policy_command)) {
                fail(0x85u, FW_MBOX_ADDR + 24u, cmd);
                break;
            }
            if (!station_publish_capture(LOOP(channel))) break;
            if (LOOP(capture_target_reached) &&
                MULTI->captured_frames == TRANSPORT(rx_published_frames)) break;
            if (!station_counter_ok()) break;
            station_consume_ack();
            station_observe_capture();
            uint32_t now = mac_now();
            if (!STATION(station_hw)) (void)clean_dcf_poll(&station_dcf, now,
                station_phy_busy() ||
                !station_rx_idle(shm_read16(0x601u)));
            if (STATION(station_hw) && station_dcf.nav_valid &&
                now - station_dcf.nav_until < 0x80000000u) station_dcf.nav_valid = 0u;
            if (!station_dcf.nav_valid && !shm_read16(CLEAN_CTS_ALLOWED))
                shm_write16(CLEAN_CTS_ALLOWED, 1u);
            station_apply_policy(LOOP(mac_en));
            if (MBOX->fw_state == FW_STATE_ERROR) break;
            cmd = STATION(policy_pending) ? 2u : MBOX->host_cmd;
        }
        if (STATION(station_enabled) && shm_read16(CLEAN_ACK_ERROR)) {
            fail(0x87u, CLEAN_ACK_ERROR, shm_read16(CLEAN_ACK_ERROR));
            break;
        }
#if CLEAN_RX_HT_OBSERVER_FIFO
        if (cmd == 3u) {
            if (!LOOP(observer_stop_polls)) {
                LOOP(observer_stop_started) = mac_now();
                mmio_write32(D11_MACCONTROL,0x04020400u);
                (void)mmio_read32(D11_MACCONTROL);
            }
            int drain = clean_sdio_drain_state(MULTI->captured_frames,TRANSPORT(rx_published_frames),
                FW_MAX_FRAMES,LOOP(observer_stop_started),mac_now(),LOOP(observer_stop_polls)++);
            if (drain < 0) {
                fail(drain == -1 ? 0x8cu : 0x89u,FW_MULTI_ADDR,
                     MULTI->captured_frames-TRANSPORT(rx_published_frames));
                break;
            }
            uint32_t pending = MULTI->captured_frames-TRANSPORT(rx_published_frames);
            if (shm_read16(0x601u) == PSM_STATE_CAPTURED && pending < FW_MAX_FRAMES)
                cmd = 2u;
            else if (drain > 0) continue;
        }
#endif
