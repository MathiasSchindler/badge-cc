#include "app.h"
#include "../badge/loader.h"

extern const uint8_t clean_image[], clean_image_end[];
#if REF_CLEAN_HANDOFF
const char ref_role[] = "HT HANDOFF STIMULUS";
#else
const char ref_role[] = "FIXED HT STIMULUS";
#endif
static clean_loader_t loader;
static uint8_t active, session[14], packet[CLEAN_HT_STIMULUS_STORAGE];

static int transmit(uint32_t index) {
    uint32_t cmd = ref_source_frame(packet,session,index);
    uint32_t total = clean_tx_length(cmd);
    uint8_t *bytes = packet+(index%3u == 0u ? CLEAN_HT_DATA_HEADER : 0u);
    uint32_t size = total+(index%3u == 0u ? 0u : CLEAN_HT_DATA_HEADER);
    if (REF_CLEAN_HANDOFF && index%3u == 2u) size = sizeof(packet);
    uint8_t readback[64];
    uint8_t *r = ref_detail+REF_STIMULUS_RECORD_BASE+index*REF_STIMULUS_RECORD_BYTES;
    badger_bytes_store_u32(r,index);
    badger_bytes_store_u32(r+4u,cmd);
    badger_bytes_store_u32(r+8u,ref_now());
    if (!cmd) { loader.error = CLEAN_INPUT; return 0; }
    for (uint32_t offset = 0u; offset < size; offset += sizeof(readback)) {
        uint32_t count = size-offset < sizeof(readback) ? size-offset : sizeof(readback);
        if (!clean_block(&loader,CLEAN_TX_BUFFER_ADDR+offset,bytes+offset,count,1u) ||
            !clean_block(&loader,CLEAN_TX_BUFFER_ADDR+offset,readback,count,0u)) return 0;
        if (!badger_bytes_equal(bytes+offset,readback,count)) {
            loader.error = CLEAN_READBACK; loader.address = CLEAN_TX_BUFFER_ADDR+offset;
            return 0;
        }
    }
    clean_tx_diag_t diag;
    uint32_t conditions;
    if (!clean_write(&loader,CLEAN_MBOX+24u,cmd) ||
        !clean_wait_command(&loader,cmd,100000u) ||
        !clean_block(&loader,CLEAN_MBOX+264u,(uint8_t *)&diag,sizeof(diag),0u) ||
        !clean_read(&loader,CLEAN_MBOX+8u,&conditions)) return 0;
    badger_bytes_store_u32(r+12u,ref_now());
    badger_bytes_store_u32(r+16u,diag.mac_trigger_us);
    badger_bytes_store_u32(r+20u,diag.mac_complete_us);
    badger_bytes_store_u32(r+24u,diag.phy_error);
    badger_bytes_store_u32(r+28u,conditions);
    badger_bytes_store_u32(r+32u,diag.length);
    badger_bytes_store_u32(r+36u,diag_crc32(packet,sizeof(packet)));
    badger_bytes_store_u32(r+40u,diag.magic);
    badger_bytes_copy(r+44u,packet+CLEAN_HT_DATA_HEADER,6u);
    badger_bytes_copy(r+50u,(const uint8_t *)diag.ihr,8u);
    badger_bytes_store_u32(r+60u,diag.channel);
    if (diag.magic != CLEAN_TX_DIAG_MAGIC || diag.length != total ||
        diag.channel != session[13] || diag.phy_error || conditions != 7u ||
        diag.mac_complete_us-diag.mac_trigger_us > 100000u) {
        loader.error = CLEAN_READBACK; return 0;
    }
    if (index%3u == 2u)
        badger_bytes_copy(ref_detail+REF_STIMULUS_PACKET_BASE,packet,sizeof(packet));
    badger_bytes_store_u32(ref_detail+28u,index+1u);
    return 1;
}

void ref_run(const uint8_t nonce[16]) {
    int ok = 0;
    if (nonce[14] == 1u && !active && nonce[12] == 0u &&
        (REF_CLEAN_HANDOFF ? (nonce[11] == 1u || nonce[11] == 2u) &&
                            nonce[10] < CLEAN_HT_HANDOFF_CASES : nonce[11] == 3u)) {
        badger_bytes_zero(ref_detail,REF_TRACE_BYTES);
        badger_bytes_copy(session,nonce,sizeof(session));
        session[13] = 6u;
        badger_bytes_store_u32(ref_detail,REF_MAGIC);
        badger_bytes_store_u32(ref_detail+12u,1u);
        badger_bytes_store_u32(ref_detail+24u,session[13]);
        ref_mac(ref_detail+32u,nonce);
        clean_loader_init(&loader,wifi_board_wire(),ref_service,0);
        loader.budget_us = 45000000u;
        ok = clean_boot(&loader,clean_image,(uint32_t)(clean_image_end-clean_image),
                         session[13],ref_generation,1u) &&
            clean_write(&loader,0x18002024u,0xf0u) &&
            clean_write(&loader,CLEAN_MBOX+24u,2u) &&
            clean_wait_state(&loader,9u,100000u);
        if (ok) { active = 1u; badger_bytes_store_u32(ref_detail+4u,1u); }
    } else if (nonce[14] == 2u && active && badger_bytes_equal(session,nonce,13u)) {
        ok = ref_timer_calibration_valid(ref_clock_ticks,ref_clock_frames);
        if (!ok) loader.error = CLEAN_INPUT;
        for (uint32_t i = 0u; ok && i < CLEAN_HT_STIMULUS_COUNT; ++i) {
            ok = transmit(i);
            uint32_t start = ref_now(), ticks = ref_clock_ticks*40u/ref_clock_frames;
            while (ok && ref_now()-start < ticks) ok = ref_service(0);
            if (!ok && !loader.error) loader.error = CLEAN_CANCELLED;
        }
        if (ok) badger_bytes_store_u32(ref_detail+4u,2u);
        ref_stop();
    } else loader.error = CLEAN_INPUT;
    if (!ok) {
        badger_bytes_store_u32(ref_detail+8u,loader.error ? loader.error : CLEAN_INPUT);
        badger_bytes_store_u32(ref_detail+16u,loader.address);
        badger_bytes_store_u32(ref_detail+20u,loader.actual);
        ref_stop();
    }
}

void ref_idle(void) {}
void ref_stop(void) { active = 0u; wifi_wire_power_off(wifi_board_wire()); }
