static void snapshot_dcf_trace(void) {
    DCF_TRACE->draw = shm_read16(CLEAN_DCF_TRACE_SHM);
    DCF_TRACE->before = shm_read16(CLEAN_DCF_TRACE_SHM + 1u);
    DCF_TRACE->after = shm_read16(CLEAN_DCF_TRACE_SHM + 2u);
    DCF_TRACE->clock = shm_read16(CLEAN_DCF_TRACE_SHM + 3u);
    DCF_TRACE->fault = shm_read16(CLEAN_DCF_TRACE_SHM + 4u);
    DCF_TRACE->slots = shm_read16(CLEAN_DCF_SLOTS);
    DCF_TRACE->magic = CLEAN_DCF_TRACE_MAGIC;
}

static void station_publish_mac(void) {
    if (!STATION(station_mac)) return;
    snapshot_ack_test();
    volatile clean_edca_status_t *edca = (volatile clean_edca_status_t *)CLEAN_EDCA_STATUS_ADDR;
    edca->magic = CLEAN_EDCA_STATUS_MAGIC;
    edca->cw_min = station_dcf.cw_min;
    edca->cw_max = station_dcf.cw_max;
    edca->aifs_us = STATION(station_hw) ? shm_read16(CLEAN_DCF_DIFS_US) :
        (uint16_t)(10u + station_dcf.aifsn * station_dcf.slot_us);
    edca->txop_32us = STATION(station_edca).enabled ?
        STATION(station_edca).parameters[STATION(station_edca).selected].txop_32us : 0u;
    edca->ac = STATION(station_edca).selected;
    edca->tid = STATION(station_edca).tid;
    edca->enabled = STATION(station_edca).enabled;
    edca->reserved = 0u;
    MAC_RADIO->cw = station_dcf.cw;
    MAC_RADIO->slots = STATION(station_hw) ? shm_read16(CLEAN_DCF_SLOTS) : station_dcf.slots;
    MAC_RADIO->nav_until = station_dcf.nav_valid ? station_dcf.nav_until : 0u;
    MAC_RADIO->pending_rx = shm_read16(0x601u) == PSM_STATE_CAPTURED ||
        MULTI->captured_frames != TRANSPORT(rx_published_frames);
    uint16_t cts = shm_read16(CLEAN_CTS_SENT), short_count = shm_read16(CLEAN_SHORT_SENT);
    uint16_t ack = shm_read16(CLEAN_ACK_CONSUMED);
    MAC_RADIO->control_acks += (uint16_t)(ack - PUBLICATION(ack_sampled));
    MAC_RADIO->cts_responses += (uint16_t)(cts - PUBLICATION(cts_sampled));
    MAC_RADIO->short_responses += (uint16_t)(short_count - PUBLICATION(short_sampled));
    PUBLICATION(cts_sampled) = cts;
    PUBLICATION(short_sampled) = short_count;
    PUBLICATION(ack_sampled) = ack;
    for (uint32_t i = 0u; i < 4u; ++i) {
        uint16_t count = shm_read16(CLEAN_ACK_KINDS + i);
        MAC_TRACE->responses[i] += (uint16_t)(count - PUBLICATION(kinds_sampled)[i]);
        PUBLICATION(kinds_sampled)[i] = count;
    }
    MAC_RADIO->policy_pending = STATION(policy_pending);
    MAC_TRACE->ack_stage = shm_read16(CLEAN_ACK_STAGE);
    MAC_TRACE->ack_frame_control = shm_read16(CLEAN_ACK_FRAME_CONTROL);
    MAC_TRACE->ack_rx_status = shm_read16(CLEAN_ACK_RX_STATUS);
    MAC_TRACE->bad_fcs = shm_read16(0x7a8u);
    MAC_TRACE->bad_frame_control = shm_read16(CLEAN_BAD_FRAME_CONTROL);
    MAC_TRACE->bad_frame_length = shm_read16(CLEAN_BAD_FRAME_LENGTH);
    MAC_TRACE->ack_elapsed = shm_read16(CLEAN_ACK_ELAPSED);
    MAC_TRACE->ack_window = shm_read16(CLEAN_ACK_WINDOW);
    MAC_TRACE->response_delay = shm_read16(CLEAN_RESPONSE_DELAY);
    MAC_TRACE->ack_dropped = shm_read16(CLEAN_TEST_ACK_DROPPED);
    if (STATION(station_hw)) {
        MAC_TRACE->ifs_status = mmio_read16(D11_BASE + 0x690u);
        MAC_TRACE->ifs_backoff = mmio_read16(D11_BASE + 0x68au);
        MAC_TRACE->ifs_idle = mmio_read16(D11_BASE + 0x68eu);
        MAC_TRACE->psm_ifs = shm_read16(CLEAN_DCF_IFS_SAMPLE);
        MAC_TRACE->psm_tsf = shm_read16(CLEAN_DCF_CLOCK_SAMPLE);
        MAC_TRACE->psm_polls = shm_read16(CLEAN_DCF_POLLS);
        snapshot_dcf_trace();
    }
    MAC_RADIO->address_lo = (uint32_t)STATION(station_address)[0] |
        ((uint32_t)STATION(station_address)[1] << 8u) |
        ((uint32_t)STATION(station_address)[2] << 16u) |
        ((uint32_t)STATION(station_address)[3] << 24u);
    MAC_RADIO->address_hi = (uint32_t)STATION(station_address)[4] |
        ((uint32_t)STATION(station_address)[5] << 8u);
}

