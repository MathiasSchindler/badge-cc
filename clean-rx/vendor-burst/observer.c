#include "app.h"
#include "../badge/loader.h"

extern const uint8_t clean_image[], clean_image_end[];
static clean_loader_t loader;
static uint8_t active, packet[CLEAN_RX_PACKET_BYTES], session[13];
static uint32_t idle_polls, rx_summaries;

static int shm_block(uint32_t base, uint32_t words, uint8_t *bytes, uint8_t write) {
    for (uint32_t i = 0u; i < words * 2u; i += 4u) {
        if (!clean_write(&loader, 0x18001160u,
                         0x10000u | ((base + i / 2u) >> 1u))) return 0;
        uint32_t selected;
        if (!clean_read(&loader, 0x18001160u, &selected)) return 0;
        if (selected != (0x10000u | ((base + i / 2u) >> 1u))) {
            loader.error = CLEAN_READBACK; return 0;
        }
        if (write) {
            uint32_t wanted = badger_bytes_load_u32(bytes + i), actual;
            if (!clean_write(&loader, 0x18001164u, wanted) ||
                !clean_read(&loader, 0x18001164u, &actual)) return 0;
            if (actual != wanted) { loader.error = CLEAN_READBACK; return 0; }
        } else {
            uint32_t value;
            if (!clean_read(&loader, 0x18001164u, &value)) return 0;
            badger_bytes_store_u32(bytes + i, value);
        }
    }
    return 1;
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
            ok = shm_block(CLEAN_OBSERVER_SHM,CLEAN_OBSERVER_WORDS,record,1u) &&
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
    } else if (nonce[14] == 2u && active && badger_bytes_equal(session, nonce, 13u)) {
        uint32_t mac, paused;
        ok = clean_write(&loader, CLEAN_MBOX + 24u, 3u) &&
            clean_wait_state(&loader, 10u, 100000u) &&
            clean_read(&loader, 0x18001120u, &mac) &&
            clean_write(&loader, 0x18001120u, mac & ~2u) &&
            clean_read(&loader, 0x18001120u, &paused);
        if (ok && (paused & 3u) != ((mac & ~2u) & 3u)) {
            loader.error = CLEAN_READBACK; ok = 0;
        }
        if (ok) ok = shm_block(CLEAN_OBSERVER_SHM,CLEAN_OBSERVER_WORDS,
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

void ref_stop(void) { active = 0u; wifi_wire_power_off(wifi_board_wire()); }
