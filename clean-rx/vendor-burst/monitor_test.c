#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "monitor.c"

uint8_t ref_detail[REF_TRACE_BYTES];
uint32_t ref_generation, ref_clock_ticks = 16000u, ref_clock_frames = 32u;
static uint32_t now, begins, stops;

uint32_t ref_now(void) { return now; }
int ref_service(void *context) { (void)context; return 1; }
const wifi_wire_t *wifi_board_wire(void) { return 0; }
const wifi_fw_assets_t *wifi_firmware_assets(void) { return 0; }
int wifi_firmware_begin(const wifi_wire_t *wire, const wifi_fw_assets_t *assets,
        wifi_fw_report_t *report, wifi_fw_service_t callback, void *context) {
    (void)wire; (void)assets; (void)report; (void)callback; (void)context;
    ++begins;
    return 0;
}
int wifi_firmware_raw_enable(wifi_fw_raw_receive_t callback, void *context) {
    (void)callback; (void)context;
    assert(0);
    return 0;
}
int wifi_firmware_control(uint32_t cmd, uint8_t set, uint8_t interface,
        const uint8_t *request, uint32_t size, uint8_t *reply, uint32_t capacity,
        uint32_t *received) {
    (void)interface; (void)request; (void)size; (void)reply; (void)capacity;
    assert(cmd == 107u && !set);
    *received = 0u;
    firmware.error = WIFI_FW_TIMEOUT;
    return 0;
}
int wifi_firmware_raw_poll(void) { return 0; }
void wifi_firmware_stop(void) { ++stops; }

int main(void) {
    uint8_t nonce[16] = {1u,2u,3u}, packet[64];
    nonce[10] = 0xedu; nonce[11] = 3u; nonce[13] = 6u; nonce[14] = 1u;
    ref_run(nonce);
    assert(begins == 1u && !active);
    for (uint32_t count = 0u; count <= REF_MONITOR_FIXED_SLOTS; ++count) {
        memset(ref_detail,0,sizeof(ref_detail));
        memset(archive,0,sizeof(archive));
        memset(&firmware,0,sizeof(firmware));
        memcpy(session,nonce,sizeof(session));
        active = 1u; stopped = 0u;
        uint32_t length = wifi_data_frame(packet,0u,0u,nonce,14u);
        for (uint32_t i = 0u; i < count; ++i) {
            now = 100u+i;
            receive(0,packet,length);
        }
        firmware.error = WIFI_FW_TIMEOUT;
        firmware.tx_packets = 73u; firmware.rx_packets = 99u;
        firmware.expected = 0x1234u; firmware.actual = 0x5678u;
        badger_bytes_store_u32(ref_detail+8u,firmware.error);
        uint32_t before = stops;
        ref_stop();
        assert(stops == before+1u && !active && stopped && archived == count);
        assert(badger_bytes_load_u32(ref_detail+120u) == 73u);
        assert(badger_bytes_load_u32(ref_detail+124u) == 99u);
        assert(badger_bytes_load_u32(ref_detail+224u) == WIFI_FW_TIMEOUT);
        assert(badger_bytes_load_u32(ref_detail+232u) == 0x1234u);
        assert(badger_bytes_load_u32(ref_detail+236u) == 0x5678u);
        assert(!memcmp(ref_detail+REF_MONITOR_PACKETS,archive,
                       REF_MONITOR_SLOTS*REF_MONITOR_SLOT_BYTES));
        nonce[14] = 2u;
        ref_run(nonce);
        assert(badger_bytes_load_u32(ref_detail+4u) == 2u);
        for (uint32_t page = 0u; page < REF_MONITOR_PAGES; ++page) {
            nonce[14] = (uint8_t)(5u+page);
            ref_run(nonce);
            assert(badger_bytes_load_u32(ref_detail+4u) == nonce[14]);
            assert(badger_bytes_load_u32(ref_detail+8u) == WIFI_FW_TIMEOUT);
            assert(ref_monitor_page_valid(ref_detail+REF_MONITOR_PAGE_BASE,page,count));
        }
    }
#if REF_MONITOR_POLL
    active = 1u; stopped = 0u; polled_at = 0u; armed_at = 0u;
    memset(ref_detail,0,sizeof(ref_detail));
    badger_bytes_store_u32(ref_detail+220u,10u);
    now = 11u;
    ref_idle();
    assert(!active && stopped);
    assert(badger_bytes_load_u32(ref_detail+8u) == WIFI_FW_TIMEOUT);
    assert(badger_bytes_load_u32(ref_detail+192u) == 1u);
    assert(badger_bytes_load_u32(ref_detail+196u) == 0u);
    assert(badger_bytes_load_u32(ref_detail+200u) == 1u);
#endif
    puts("Monitor lifecycle: random fixed-source nonce, bounded partial archives, timeout preservation and post-fault pages pass; NOT RF");
    return 0;
}
