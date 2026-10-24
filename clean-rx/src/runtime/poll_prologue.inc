#if CLEAN_RX_HT_REARM_RESET
        observer_reset_service();
#endif
        if ((LOOP(polls) & 4095u) == 0u) {
            snapshot_psm(LOOP(rx_start), LOOP(polls));
            station_publish_mac();
        }
