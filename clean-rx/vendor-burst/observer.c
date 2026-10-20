#include "app.h"
#include "../badge/loader.h"

extern const uint8_t clean_image[], clean_image_end[];
const char ref_role[] = "CLEAN OBSERVER";
static clean_loader_t loader;
static uint8_t active, packet[CLEAN_RX_PACKET_BYTES], session[13];
static uint32_t idle_polls, rx_summaries;
#if REF_OBSERVER_PAUSE
static uint32_t pause_started;
#endif

#if CLEAN_RX_HT_OBSERVER_FIFO
static int stop_and_drain(void) {
#if REF_OBSERVER_PAUSE
    if (badger_bytes_load_u32(ref_detail+REF_FIFO_PAUSE_STATE) == 1u)
        badger_bytes_store_u32(ref_detail+REF_FIFO_PAUSE_STATE,3u);
#endif
    if (!clean_write(&loader,CLEAN_MBOX+24u,3u)) return 0;
    uint32_t started = ref_now();
    for (uint32_t polls = 0u; polls < 10000u && ref_now()-started < 1000000u; ++polls) {
        ref_idle();
        if (!active) return 0;
        uint32_t state;
        if (!clean_read(&loader,CLEAN_MBOX+4u,&state)) return 0;
        if (state == 255u) { loader.error = CLEAN_CONTROLLER; loader.actual = state; return 0; }
        if (state == 10u) {
            uint32_t posted;
            if (!clean_read(&loader,CLEAN_MBOX+CLEAN_SDIO_POSTED_OFFSET,&posted)) return 0;
            int credit = clean_sdio_credit_available(posted,loader.rx_consumed,CLEAN_SDIO_WINDOW);
            if (credit < 0) { loader.error = CLEAN_INPUT; loader.actual = posted; return 0; }
            if (posted == loader.rx_consumed) return 1;
        }
    }
    loader.error = CLEAN_TIMEOUT;
    return 0;
}
#endif
#if CLEAN_RX_HT_WINDOW
static uint8_t window[REF_WINDOW_BYTES], window_armed;
static uint32_t window_started, window_sampled, window_interval_ticks;

static int window_sample(void) {
    uint32_t count = badger_bytes_load_u32(window+4u);
    window_sampled = ref_now();
    if (count == REF_WINDOW_RECORDS) {
        badger_bytes_store_u32(window+8u,badger_bytes_load_u32(window+8u)+1u);
        return 1;
    }
    uint8_t *r = window+REF_WINDOW_HEADER_BYTES+count*REF_WINDOW_RECORD_BYTES;
    uint32_t before, after, captured, debug, state, error, error_address, error_value;
    if (!clean_read(&loader,CLEAN_OBSERVER_PHY_ADDR,&before) ||
        !clean_block(&loader,CLEAN_MBOX+60u,r+16u,84u,0u) ||
        !clean_read(&loader,CLEAN_MBOX+4u,&state) ||
        !clean_read(&loader,CLEAN_MBOX+12u,&error) ||
        !clean_read(&loader,CLEAN_MBOX+16u,&error_address) ||
        !clean_read(&loader,CLEAN_MBOX+20u,&error_value) ||
        !clean_read(&loader,CLEAN_MULTI+8u,&captured) ||
        !clean_read(&loader,0x18001154u,&debug) ||
        !clean_block(&loader,CLEAN_OBSERVER_PHY_ADDR,r+108u,32u,0u) ||
#if CLEAN_RX_HT_REARM_WAIT
        !clean_block(&loader,CLEAN_OBSERVER_REARM_ADDR,window+32u,
                     2u*CLEAN_OBSERVER_REARM_WORDS,0u) ||
#endif
        !clean_read(&loader,CLEAN_OBSERVER_PHY_ADDR,&after)) return 0;
    badger_bytes_store_u32(r,window_sampled-window_started);
    badger_bytes_store_u32(r+4u,ref_now()-window_sampled);
    badger_bytes_store_u32(r+8u,captured);
    badger_bytes_store_u32(r+12u,debug);
    badger_bytes_store_u32(r+100u,state);
    badger_bytes_store_u32(r+104u,error);
    badger_bytes_store_u32(r+140u,before);
    badger_bytes_store_u32(r+144u,after);
    badger_bytes_store_u32(r+148u,error_address);
    badger_bytes_store_u32(r+152u,error_value);
    badger_bytes_store_u32(r+156u,ref_u16(window+64u) |
        ((uint32_t)ref_u16(window+96u)<<16u));
    badger_bytes_store_u32(window+4u,count+1u);
    return 1;
}
#endif

static int shm_block_with(clean_loader_t *reader, uint32_t base, uint32_t words,
                          uint8_t *bytes, uint8_t write) {
    for (uint32_t i = 0u; i < words * 2u; i += 4u) {
        if (!clean_write(reader, 0x18001160u,
                         0x10000u | ((base + i / 2u) >> 1u))) return 0;
        uint32_t selected;
        if (!clean_read(reader, 0x18001160u, &selected)) return 0;
        if (selected != (0x10000u | ((base + i / 2u) >> 1u))) {
            reader->error = CLEAN_READBACK; return 0;
        }
        if (write) {
            uint32_t wanted = badger_bytes_load_u32(bytes + i), actual;
            if (!clean_write(reader, 0x18001164u, wanted) ||
                !clean_read(reader, 0x18001164u, &actual)) return 0;
            if (actual != wanted) { reader->error = CLEAN_READBACK; return 0; }
        } else {
            uint32_t value;
            if (!clean_read(reader, 0x18001164u, &value)) return 0;
            badger_bytes_store_u32(bytes + i, value);
        }
    }
    return 1;
}
static int shm_block(uint32_t base, uint32_t words, uint8_t *bytes, uint8_t write) {
    return shm_block_with(&loader,base,words,bytes,write);
}

static int hardware_snapshot(uint32_t phase) {
    for (uint32_t i = 0u; i < REF_HARDWARE_WORDS; ++i) {
        uint32_t value;
        if (!clean_read(&loader,ref_hardware_addresses[i],&value)) return 0;
        badger_bytes_store_u32(ref_detail+REF_HARDWARE_BASE+
            4u*(phase*REF_HARDWARE_WORDS+i),value);
    }
    return 1;
}

static int stopped_psm_snapshot(void) {
    uint32_t debug;
    if (!clean_read(&loader,0x18001154u,&debug)) return 0;
    badger_bytes_store_u32(ref_detail+REF_STOPPED_PSM,debug);
    return shm_block(0x600u,REF_STOPPED_PSM_WORDS,ref_detail+REF_STOPPED_PSM+4u,0u);
}

void ref_run(const uint8_t nonce[16]) {
    int ok = 0;
    if (nonce[14] == 1u && !active && nonce[13] && nonce[13] <= 14u) {
        badger_bytes_zero(ref_detail, REF_TRACE_BYTES);
        idle_polls = rx_summaries = 0u;
        badger_bytes_store_u32(ref_detail, REF_MAGIC);
        badger_bytes_copy(session, nonce, 13u);
        ref_mac(ref_detail + 32u, nonce);
        badger_bytes_store_u32(ref_detail + 24u, nonce[13]);
        badger_bytes_store_u32(ref_detail + 64u, REF_OBSERVER_ABI);
        clean_loader_init(&loader, wifi_board_wire(), ref_service, 0);
        loader.budget_us = 45000000u;
        ok = clean_boot(&loader, clean_image, (uint32_t)(clean_image_end-clean_image),
                         nonce[13], ref_generation, 0u);
        uint32_t mac = 0u, paused = 0u, flags = 0u;
        if (ok) ok = clean_read(&loader, CLEAN_MBOX + 28u, &flags) &&
            clean_read(&loader, 0x18001120u, &mac) &&
            clean_write(&loader, 0x18001120u, mac & ~2u) &&
            clean_read(&loader, 0x18001120u, &paused);
        if (ok && ((flags & 0x100u) || (paused & 3u) != ((mac & ~2u) & 3u))) {
            loader.error = CLEAN_READBACK; ok = 0;
        }
        uint8_t *record = ref_detail + REF_OBSERVER_DETAIL;
        if (ok) {
            record[0] = 1u;
            badger_bytes_copy(record + 6u, ref_detail + 32u, 6u);
            ok = hardware_snapshot(0u) &&
                shm_block(CLEAN_OBSERVER_SHM,CLEAN_OBSERVER_WORDS,record,1u) &&
                shm_block(CLEAN_OBSERVER_EARLY_SHM,CLEAN_OBSERVER_EARLY_WORDS,
                          ref_detail+REF_EARLY_BASE,1u) &&
                clean_write(&loader, 0x18001120u, mac) &&
                clean_write(&loader, CLEAN_MBOX + 24u, 2u) &&
                clean_wait_state(&loader, 9u, 100000u);
        }
        if (ok) {
            active = 1u;
            badger_bytes_store_u32(ref_detail + 4u, 1u);
            badger_bytes_store_u32(ref_detail + 56u, flags);
        }
    }
#if CLEAN_RX_HT_WINDOW
    else if (nonce[14] == 3u && active && !window_armed &&
             badger_bytes_equal(session,nonce,13u)) {
        badger_bytes_zero(window,sizeof(window));
        badger_bytes_store_u32(window,REF_WINDOW_MAGIC);
        badger_bytes_store_u32(window+12u,REF_WINDOW_INTERVAL);
        badger_bytes_store_u32(window+16u,ref_clock_ticks);
        badger_bytes_store_u32(window+20u,ref_clock_frames);
        badger_bytes_store_u32(window+24u,CLEAN_RX_HT_REARM_WAIT);
        badger_bytes_store_u32(window+28u,
            CLEAN_RX_HT_REARM_RECOVER | (CLEAN_RX_HT_REARM_INJECT<<1u) |
            (CLEAN_RX_HT_REARM_RESET<<2u) | (CLEAN_RX_HT_REARM_PARK<<3u));
        if (!ref_timer_calibration_valid(ref_clock_ticks,ref_clock_frames)) {
            loader.error = CLEAN_INPUT;
            badger_bytes_store_u32(ref_detail+8u,loader.error);
            ref_stop();
            return;
        }
        window_interval_ticks = ref_clock_ticks*(REF_WINDOW_INTERVAL/1000u)/ref_clock_frames;
        window_started = ref_now();
        window_armed = 1u;
        ok = window_sample();
        if (ok) badger_bytes_store_u32(ref_detail+4u,3u);
    } else if (nonce[14] == 4u && !active && window_armed &&
               badger_bytes_equal(session,nonce,13u)) {
        badger_bytes_zero(ref_detail+REF_WINDOW_BASE,REF_TRACE_BYTES-REF_WINDOW_BASE);
        badger_bytes_copy(ref_detail+REF_WINDOW_BASE,window,sizeof(window));
        badger_bytes_store_u32(ref_detail+4u,4u);
        ok = 1;
    }
#endif
    else if (nonce[14] == 2u && active && badger_bytes_equal(session, nonce, 13u)) {
        uint32_t mac, paused;
#if CLEAN_RX_HT_WINDOW
        ok = window_armed && window_sample();
        if (!ok && !loader.error) loader.error = CLEAN_INPUT;
#else
        ok = 1;
#endif
        ok = ok &&
#if CLEAN_RX_HT_OBSERVER_FIFO
            stop_and_drain() &&
#else
            clean_write(&loader, CLEAN_MBOX + 24u, 3u) &&
            clean_wait_state(&loader, 10u, 100000u) &&
#endif
            clean_read(&loader, 0x18001120u, &mac) &&
            clean_write(&loader, 0x18001120u, mac & ~2u) &&
            clean_read(&loader, 0x18001120u, &paused);
        if (ok && (paused & 3u) != ((mac & ~2u) & 3u)) {
            loader.error = CLEAN_READBACK; ok = 0;
        }
        if (ok) ok = hardware_snapshot(1u) && stopped_psm_snapshot() &&
#if CLEAN_RX_HT_COPY_DMA_PROBE
            clean_block(&loader,CLEAN_OBSERVER_DMA_ADDR,ref_detail+REF_SAMPLE_BASE,
                        CLEAN_OBSERVER_DMA_BYTES,0u) &&
#endif
#if CLEAN_RX_HT_PHY_SNAPSHOT
            clean_block(&loader,CLEAN_OBSERVER_PHY_ADDR,ref_detail+REF_PHY_SNAPSHOT,
                        CLEAN_OBSERVER_PHY_BYTES,0u) &&
#endif
            shm_block(CLEAN_OBSERVER_SHM,CLEAN_OBSERVER_WORDS,
                              ref_detail+REF_OBSERVER_DETAIL,0u) &&
            shm_block(CLEAN_OBSERVER_EARLY_SHM,CLEAN_OBSERVER_EARLY_WORDS,
                      ref_detail+REF_EARLY_BASE,0u) &&
            clean_block(&loader,CLEAN_MBOX,ref_detail+128u,128u,0u) &&
            clean_block(&loader,0x20380u,ref_detail+256u,36u,0u) &&
            clean_block(&loader,CLEAN_MBOX+REF_MBOX_HEAD_BYTES,ref_detail+REF_MBOX_TAIL,
                        REF_MBOX_BYTES-REF_MBOX_HEAD_BYTES,0u) &&
            clean_block(&loader,CLEAN_MULTI,ref_detail+REF_MULTI_DETAIL,40u,0u);
        if (ok) {
            const uint32_t state[8] = {loader.rx_consumed,loader.rx_status,loader.rx_resyncs,
                loader.rx_stalled,loader.rx_retries,loader.rx_credit,idle_polls,rx_summaries};
            for (uint32_t i = 0u; i < 8u; ++i)
                badger_bytes_store_u32(ref_detail+REF_LOADER_DETAIL+4u*i,state[i]);
            badger_bytes_store_u32(ref_detail + 4u, 2u);
        }
        ref_stop();
    } else loader.error = CLEAN_INPUT;
    if (!ok) {
        badger_bytes_store_u32(ref_detail + 8u, loader.error ? loader.error : CLEAN_INPUT);
        badger_bytes_store_u32(ref_detail + 16u, loader.stage);
        badger_bytes_store_u32(ref_detail + 20u, loader.address);
        ref_stop();
    }
}

void ref_idle(void) {
    if (!active) return;
#if CLEAN_RX_HT_WINDOW
    if (window_armed && ref_now()-window_sampled >= window_interval_ticks &&
        !window_sample()) {
        badger_bytes_store_u32(ref_detail+8u,loader.error);
        ref_stop();
        return;
    }
#endif
#if REF_OBSERVER_PAUSE
    if (badger_bytes_load_u32(ref_detail+REF_FIFO_PAUSE_STATE) == 1u) {
        uint32_t elapsed = ref_now()-pause_started;
        uint32_t duration = (ref_clock_ticks*600u+ref_clock_frames-1u)/ref_clock_frames;
        if (REF_OBSERVER_STOP_FULL || elapsed < duration) {
            uint32_t before, after, captured, states;
            if (!clean_read(&loader,CLEAN_MBOX+CLEAN_SDIO_POSTED_OFFSET,&before) ||
                !clean_read(&loader,CLEAN_MULTI+8u,&captured) ||
                !clean_read(&loader,CLEAN_MBOX+72u,&states) ||
                !clean_read(&loader,CLEAN_MBOX+CLEAN_SDIO_POSTED_OFFSET,&after)) {
                badger_bytes_store_u32(ref_detail+8u,loader.error);
                ref_stop();
                return;
            }
            if (before == after && before-loader.rx_consumed == 1u) {
                uint32_t queued = captured-before;
                if (queued > 8u) {
                    loader.error = CLEAN_INPUT;
                    badger_bytes_store_u32(ref_detail+8u,loader.error);
                    ref_stop();
                    return;
                }
                if (queued > badger_bytes_load_u32(ref_detail+REF_FIFO_PEAK))
                    badger_bytes_store_u32(ref_detail+REF_FIFO_PEAK,queued);
                if (queued == 8u && (states>>16u) == 14u)
                    badger_bytes_store_u32(ref_detail+REF_FIFO_HELD_SAMPLES,
                        badger_bytes_load_u32(ref_detail+REF_FIFO_HELD_SAMPLES)+1u);
            }
            return;
        }
        badger_bytes_store_u32(ref_detail+REF_FIFO_PAUSE_STATE,2u);
    }
#endif
    ++idle_polls;
    uint32_t size;
    if (!clean_receive(&loader, packet, &size)) {
        badger_bytes_store_u32(ref_detail + 8u, loader.error);
        ref_stop();
    } else if (size) {
        badger_bytes_store_u32(ref_detail + 60u,
            badger_bytes_load_u32(ref_detail + 60u) + 1u);
        if (rx_summaries < REF_RX_SUMMARY_COUNT) {
            if (!ref_rx_summary(ref_detail+REF_RX_SUMMARIES+rx_summaries*REF_RX_SUMMARY_BYTES,
                                packet,size,badger_bytes_load_u32(ref_detail+60u))) {
                loader.error = CLEAN_INPUT;
                badger_bytes_store_u32(ref_detail+8u,loader.error);
                ref_stop();
                return;
            }
            ++rx_summaries;
        }
        if (size >= 18u+40u && badger_bytes_equal(packet+18u+10u,ref_detail+32u,6u)) {
            badger_bytes_store_u32(ref_detail+76u,badger_bytes_load_u32(ref_detail+76u)+1u);
            if (REF_CLEAN_STIMULUS) {
                uint8_t nonce[14];
                badger_bytes_copy(nonce,session,13u);
                nonce[13] = (uint8_t)badger_bytes_load_u32(ref_detail+24u);
                uint32_t index = ref_stimulus_rx_index(packet,size,nonce);
                if (index < CLEAN_HT_STIMULUS_COUNT) {
#if REF_OBSERVER_PAUSE
                    if (index == 1u && !badger_bytes_load_u32(ref_detail+REF_FIFO_PAUSE_STATE)) {
                        pause_started = ref_now();
                        badger_bytes_store_u32(ref_detail+REF_FIFO_PAUSE_STATE,1u);
                    }
#endif
                    uint32_t mask = badger_bytes_load_u32(ref_detail+REF_STIMULUS_RX_MASK);
                    if (mask&(1u<<index))
                        badger_bytes_store_u32(ref_detail+REF_STIMULUS_RX_DUPLICATES,
                            badger_bytes_load_u32(ref_detail+REF_STIMULUS_RX_DUPLICATES)+1u);
                    badger_bytes_store_u32(ref_detail+REF_STIMULUS_RX_MASK,mask|(1u<<index));
                    uint8_t *legacy = ref_detail+REF_SAMPLE_BASE+REF_SAMPLE_BYTES;
                    if (!REF_CLEAN_HANDOFF && index%3u == 0u && !badger_bytes_load_u32(legacy)) {
                        badger_bytes_store_u32(legacy,size);
                        badger_bytes_copy(legacy+16u,packet,size);
                    }
                } else if (!REF_CLEAN_HANDOFF ||
                    ref_handoff_rx_index(packet,size,nonce) == CLEAN_HT_STIMULUS_COUNT)
                    badger_bytes_store_u32(ref_detail+REF_STIMULUS_RX_INVALID,
                        badger_bytes_load_u32(ref_detail+REF_STIMULUS_RX_INVALID)+1u);
                if (REF_CLEAN_HANDOFF && index < CLEAN_HT_STIMULUS_COUNT) return;
            }
            if (CLEAN_RX_HT_COPY_DMA_PROBE) return;
            uint32_t length = size-18u, count = badger_bytes_load_u32(ref_detail+68u);
            uint32_t identity = ref_u16(packet+10u);
            uint32_t advanced = identity & CLEAN_OBSERVER_ADVANCE_TAG;
            identity &= ~CLEAN_OBSERVER_ADVANCE_TAG;
            if (length > REF_SAMPLE_MPDU_BYTES) {
                badger_bytes_store_u32(ref_detail+72u,badger_bytes_load_u32(ref_detail+72u)+1u);
                return;
            }
            uint32_t selected = ref_sample_slot(ref_detail+REF_SAMPLE_BASE,count,length,identity,advanced);
            if (selected < REF_SAMPLES) {
                uint8_t *sample = ref_detail+REF_SAMPLE_BASE+selected*REF_SAMPLE_BYTES;
                badger_bytes_store_u32(sample,length);
                badger_bytes_store_u32(sample+4u,identity);
                badger_bytes_store_u32(sample+8u,packet[16] |
                    (advanced ? REF_ADVANCED_SAMPLE : 0u));
                badger_bytes_store_u32(sample+12u,diag_crc32(packet+18u,length-4u));
                badger_bytes_copy(sample+16u,packet+18u,length);
                if (selected == count) badger_bytes_store_u32(ref_detail+68u,count+1u);
            }
        }
    }
}

void ref_stop(void) {
#if CLEAN_RX_HT_COPY_PROBE
    int fault = active && loader.error == CLEAN_CONTROLLER;
#endif
    active = 0u;
#if CLEAN_RX_HT_COPY_PROBE
    if (fault) {
        /* Isolate stopped fault collection; never clear the receive error. */
        clean_loader_t diagnostic;
        badger_bytes_copy((uint8_t *)&diagnostic,(const uint8_t *)&loader,sizeof(diagnostic));
        diagnostic.error = CLEAN_OK;
        uint8_t *record = ref_detail+REF_COPY_FAULT_BASE;
        uint32_t mac = 0u, paused = 0u, reason = 0u;
        badger_bytes_store_u32(record,REF_COPY_FAULT_MAGIC);
        badger_bytes_store_u32(record+12u,loader.error);
        int captured = clean_read(&diagnostic,0x18001120u,&mac) &&
            clean_write(&diagnostic,0x18001120u,mac&~2u) &&
            clean_read(&diagnostic,0x18001120u,&paused);
        if (captured && (paused&3u) != ((mac&~2u)&3u)) {
            diagnostic.error = CLEAN_READBACK;
            captured = 0;
        }
        if (captured) captured =
            shm_block_with(&diagnostic,CLEAN_OBSERVER_SHM,CLEAN_OBSERVER_WORDS,
                           ref_detail+REF_OBSERVER_DETAIL,0u) &&
            shm_block_with(&diagnostic,CLEAN_OBSERVER_EARLY_SHM,CLEAN_OBSERVER_EARLY_WORDS,
                           ref_detail+REF_EARLY_BASE,0u) &&
            clean_block(&diagnostic,CLEAN_MBOX,ref_detail+128u,128u,0u) &&
            clean_block(&diagnostic,CLEAN_MBOX+REF_MBOX_HEAD_BYTES,ref_detail+REF_MBOX_TAIL,
                        REF_MBOX_BYTES-REF_MBOX_HEAD_BYTES,0u) &&
            clean_block(&diagnostic,CLEAN_MULTI,ref_detail+REF_MULTI_DETAIL,40u,0u);
        if (captured) {
            uint8_t state[4];
            captured = shm_block_with(&diagnostic,0x602u,2u,state,0u);
            if (captured) reason = ref_u16(state);
        }
        badger_bytes_store_u32(record+4u,(uint32_t)captured);
        badger_bytes_store_u32(record+8u,diagnostic.error);
        badger_bytes_store_u32(record+16u,reason);
        badger_bytes_store_u32(record+20u,diagnostic.address);
        badger_bytes_store_u32(record+24u,diagnostic.actual);
        badger_bytes_store_u32(record+28u,loader.rx_consumed);
    }
#endif
    wifi_wire_power_off(wifi_board_wire());
}
