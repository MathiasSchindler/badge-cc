#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "protocol.h"
#include "frame.h"
#include "lifecycle_usb.h"
#include "protected_ap.h"
#include "../../src/shared/bytes.h"

static void frame_tests(void) {
    live_usb_control_t control;
    live_usb_control_init(&control, 1u, "0102030405060708");
    control.configured = 1u; control.probe_generation = 7u;
    control.probe_inflight = 1u; control.incoming_test = USB_REPORT_TEST_ID + 1u;
    uint8_t report[64] = {1u}, trace[WIFI_TRACE_SIZE] = {1u};
    assert(clean_lifecycle_snapshot(&control, 7u, report, trace, 0u));
    report[0] = trace[0] = 2u;
    live_usb_probe_finish(&control, 7u, report);
    live_usb_trace_finish(&control, 7u, trace);
    assert(control.report[0] == 1u && control.trace[0] == 1u);
    assert(clean_lifecycle_snapshot(&control, 7u, report, trace, 1u));
    assert(control.report[0] == 2u && control.trace[0] == 2u);
    report[0] = trace[0] = 3u;
    assert(!clean_lifecycle_snapshot(&control, 8u, report, trace, 1u));
    control.configured = 0u;
    assert(!clean_lifecycle_snapshot(&control, 7u, report, trace, 1u));
    control.configured = 1u; control.reboot_requested = 1u;
    assert(!clean_lifecycle_snapshot(&control, 7u, report, trace, 1u));
    assert(control.report[0] == 2u && control.trace[0] == 2u);
    for (uint32_t mcs = 0u; mcs < 8u; ++mcs) {
        uint32_t rate = clean_peer_ht_rate(mcs);
        assert(clean_peer_rate_matches(rate, mcs));
        assert(clean_peer_rate_matches(rate | 0x40000000u, mcs));
        assert(clean_peer_rate_matches(rate | 0x80000000u, mcs));
        assert(clean_peer_rate_matches(rate | 0xc0000000u, mcs));
        assert(!clean_peer_rate_matches(rate, mcs ^ 1u));
        assert(!clean_peer_rate_matches(rate ^ 0x03000000u, mcs));
        for (uint32_t bit = 8u; bit < 30u; ++bit)
            assert(!clean_peer_rate_matches(rate ^ (1u << bit), mcs));
    }
    assert(!clean_peer_rate_matches(clean_peer_ht_rate(8u), 8u));
    uint8_t nonce[14] = {1u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,12u,13u,7u};
    for (uint32_t qos = 0u; qos < 2u; ++qos)
        for (uint32_t sequence = 0u; sequence < CLEAN_PEER_FRAME_COUNT; ++sequence) {
            uint8_t ethernet[CLEAN_PEER_ETHERNET_BYTES], mpdu[88] = {0};
            clean_peer_ethernet(ethernet, nonce, (uint8_t)sequence);
            uint32_t header = qos ? 26u : 24u, length = header + CLEAN_PEER_ETHERNET_BYTES - 2u;
            mpdu[0] = qos ? 0x88u : 0x08u;
            mpdu[1] = 2u;
            memcpy(mpdu + 4u, ethernet, 6u);
            memcpy(mpdu + 10u, ethernet + 6u, 6u);
            memcpy(mpdu + 16u, ethernet + 6u, 6u);
            static const uint8_t snap[6] = {0xaau,0xaau,3u,0u,0u,0u};
            memcpy(mpdu + header, snap, sizeof(snap));
            memcpy(mpdu + header + 6u, ethernet + 12u, sizeof(ethernet) - 12u);
            badger_bytes_store_u32(mpdu + length - 4u, diag_crc32(mpdu, length - 4u));
            assert(clean_peer_mpdu_sequence(mpdu, length, nonce) == (int)sequence);
            assert(clean_peer_mpdu_sequence(mpdu, length - 1u, nonce) == -1);
            assert(clean_peer_mpdu_sequence(NULL, length, nonce) == -1);
            nonce[12] ^= 1u;
            assert(clean_peer_mpdu_sequence(mpdu, length, nonce) == -1);
            nonce[12] ^= 1u;
            for (uint32_t i = 4u; i < length - 4u; ++i) {
                if (i >= 22u && i < header) continue;
                mpdu[i] ^= 1u;
                badger_bytes_store_u32(mpdu + length - 4u, diag_crc32(mpdu, length - 4u));
                assert(clean_peer_mpdu_sequence(mpdu, length, nonce) == -1);
                mpdu[i] ^= 1u;
            }
            badger_bytes_store_u32(mpdu + length - 4u, diag_crc32(mpdu, length - 4u));
            mpdu[length - 1u] ^= 1u;
            assert(clean_peer_mpdu_sequence(mpdu, length, nonce) == -1);
        }
}

static void monitor_tests(void) {
    uint8_t request[CLEAN_PEER_CONTROL_BYTES], set, trace[CLEAN_PEER_TRACE_BYTES] = {0};
    uint32_t command;
    static const uint32_t commands[] = {107u, 2u, 30u, 108u, 107u, 108u, 107u};
    static const uint8_t setters[] = {0u, 1u, 1u, 1u, 0u, 1u, 0u};
    for (uint32_t i = 0u; i < CLEAN_PEER_RECORDS; ++i) {
        assert(clean_peer_prepare_monitor(i, request, &command, &set) == 4u);
        assert(command == commands[i] && set == setters[i]);
        assert(usb_report_u32(request) == (i == 2u ? 6u : i == 3u ? 1u : 0u));
        for (uint32_t j = 4u; j < sizeof(request); ++j) assert(!request[j]);
    }
    assert(!clean_peer_prepare_monitor(CLEAN_PEER_RECORDS, request, &command, &set));
    badger_bytes_store_u32(trace, CLEAN_PEER_MAGIC);
    badger_bytes_store_u32(trace + 16u, CLEAN_PEER_RECORDS);
    badger_bytes_store_u32(trace + 20u, 2u);
    badger_bytes_store_u32(trace + 24u, CLEAN_PEER_RECORDS);
    for (uint32_t i = 0u; i < CLEAN_PEER_RECORDS; ++i) {
        uint8_t *r = trace + CLEAN_PEER_RECORD_BASE + i * CLEAN_PEER_RECORD_BYTES;
        badger_bytes_store_u32(r, i);
        badger_bytes_store_u32(r + 4u, 1u);
        badger_bytes_store_u32(r + 16u, 4u);
        badger_bytes_store_u32(r + 20u, i == 4u ? 1u : 0u);
    }
    assert(clean_peer_monitor_supported(trace));
    for (uint32_t i = 0u; i < CLEAN_PEER_RECORDS; ++i) {
        uint8_t *r = trace + CLEAN_PEER_RECORD_BASE + i * CLEAN_PEER_RECORD_BYTES;
        badger_bytes_store_u32(r + 4u, 0u);
        badger_bytes_store_u32(r + 8u, 8u);
        badger_bytes_store_u32(r + 12u, (uint32_t)-23);
        assert(clean_peer_trace_valid(trace) && !clean_peer_monitor_supported(trace));
        badger_bytes_store_u32(r + 4u, 1u);
        badger_bytes_store_u32(r + 8u, 0u);
        badger_bytes_store_u32(r + 12u, 0u);
        if (i == 0u || i == 4u || i == 6u) {
            for (uint32_t size = 0u; size < 8u; ++size) {
                badger_bytes_store_u32(r + 16u, size);
                assert(clean_peer_monitor_supported(trace) == (size == 4u));
            }
            badger_bytes_store_u32(r + 16u, 4u);
            r[20u] ^= 1u;
            assert(!clean_peer_monitor_supported(trace));
            r[20u] ^= 1u;
        }
    }
    badger_bytes_store_u32(trace + 24u, CLEAN_PEER_RECORDS - 1u);
    assert(!clean_peer_monitor_supported(trace));
    badger_bytes_store_u32(trace + 24u, CLEAN_PEER_RECORDS);
    badger_bytes_store_u32(trace + 20u, 1u);
    assert(!clean_peer_monitor_supported(trace));
}

static void protected_secret_tests(void) {
    live_usb_control_t usb;
    uint8_t secret[64], pmk[32], nonce[16]={1u}, digest[20], taken[16], request[64];
    nonce[5]=16u; nonce[14]=CLEAN_PROTECTED_AP_MODE; nonce[15]=6u;
    memset(secret,0xa5,sizeof(secret));
    wpa_secret_binding(secret,nonce,nonce[15],digest); memcpy(nonce+6u,digest,8u);
    const uint8_t provision[8]={0x40u,LIVE_USB_CLEAN_SECRET,0u,0u,0u,0u,64u,0u};
    const uint8_t submit[8]={0x40u,LIVE_USB_FW_SUBMIT,0u,0u,0u,0u,64u,0u};
    const uint8_t poll[8]={0xc0u,LIVE_USB_FW_REPORT,0u,0u,0u,0u,64u,0u};
    for (uint32_t early=0u;early<2u;++early) {
        live_usb_control_init(&usb,123u,"0102030405060708"); usb.configured=1u;
        live_usb_control_setup(&usb,provision);
        live_usb_control_complete(&usb,0u,secret,sizeof(secret));
        live_usb_control_complete(&usb,1u,0,0u);
        assert(usb.clean_secret_ready);
        usb_report_request_for_test(request,123u,nonce,3u);
        live_usb_control_setup(&usb,submit);
        live_usb_control_complete(&usb,0u,request,sizeof(request));
        live_usb_control_complete(&usb,1u,0,0u);
        assert(live_usb_probe_take(&usb,taken));
        if (early) {
            assert(clean_protected_ap_take_secret(pmk,usb.clean_secret,&usb.clean_secret_ready,taken));
            live_usb_control_setup(&usb,poll);
            assert(usb.phase==USB_PHASE_STALL);
            assert(!memcmp(pmk,secret,32u));
        } else {
            live_usb_control_setup(&usb,poll);
            assert(usb.phase==USB_PHASE_STALL);
            assert(!clean_protected_ap_take_secret(pmk,usb.clean_secret,&usb.clean_secret_ready,taken));
            for (uint32_t i=0u;i<32u;++i) assert(!pmk[i]);
        }
        assert(!usb.clean_secret_ready);
        for (uint32_t i=0u;i<64u;++i) assert(!usb.clean_secret[i]);
        assert(!clean_protected_ap_take_secret(pmk,usb.clean_secret,&usb.clean_secret_ready,taken));
        for (uint32_t i=0u;i<32u;++i) assert(!pmk[i]);
    }
    for (uint32_t fault=0u;fault<4u;++fault) {
        memcpy(usb.clean_secret,secret,64u); usb.clean_secret_ready=1u;
        memcpy(taken,nonce,16u);
        taken[fault==0u?6u:fault==1u?14u:fault==2u?15u:5u]^=1u;
        assert(!clean_protected_ap_take_secret(pmk,usb.clean_secret,&usb.clean_secret_ready,taken));
        for (uint32_t i=0u;i<32u;++i) assert(!pmk[i]);
    }
    puts("E1 AP secret: real provisioning/submit/not-ready-stall sequence, early consumption, single-use binding and failed-input wiping pass");
}

int main(void) {
    protected_secret_tests();
    frame_tests();
    monitor_tests();
    uint8_t control[CLEAN_PEER_CONTROL_BYTES], set;
    uint32_t command;
    for (uint32_t i = 0u; i < CLEAN_PEER_RECORDS; ++i) {
        uint32_t size = clean_peer_prepare_query(i, control, &command, &set);
        if (i < 5u) {
            assert(size == sizeof(control) && command == 262u && !set);
            assert(!strcmp((const char *)control, clean_peer_variables[i]));
            for (uint32_t j = (uint32_t)strlen(clean_peer_variables[i]) + 1u;
                 j < size; ++j) assert(!control[j]);
        } else if (i == 5u) {
            assert(size == 39u && command == 263u && set == 1u);
            assert(!strcmp((const char *)control, "pkteng") && control[7] == 2u);
            for (uint32_t j = 8u; j < sizeof(control); ++j) assert(!control[j]);
        } else {
            assert(size == 4u && command == 107u && !set);
            for (uint32_t j = 0u; j < sizeof(control); ++j) assert(!control[j]);
        }
    }
    assert(!clean_peer_prepare_query(CLEAN_PEER_RECORDS, control, &command, &set));
    uint8_t nonce[16] = {1u}, report[64], trace[CLEAN_PEER_TRACE_BYTES] = {0};
    usb_report_header(report, 0x46363255u, 123u, nonce, 3u);
    badger_bytes_store_u32(report + 60u, diag_crc32(report, 60u));
    assert(clean_peer_report_valid(report, nonce, 123u));
    for (uint32_t i = 0u; i < sizeof(report); ++i) {
        report[i] ^= 1u;
        assert(!clean_peer_report_valid(report, nonce, 123u));
        report[i] ^= 1u;
    }
    assert(!clean_peer_report_valid(report, nonce, 124u));
    nonce[1] = 1u;
    assert(!clean_peer_report_valid(report, nonce, 123u));
    assert(!clean_peer_trace_valid(trace));
    badger_bytes_store_u32(trace, CLEAN_PEER_MAGIC);
    badger_bytes_store_u32(trace + 16u, CLEAN_PEER_RECORDS);
    badger_bytes_store_u32(trace + 20u, 1u);
    badger_bytes_store_u32(trace + 24u, CLEAN_PEER_RECORDS);
    for (uint32_t i = 0u; i < CLEAN_PEER_RECORDS; ++i) {
        uint8_t *r = trace + CLEAN_PEER_RECORD_BASE + i * CLEAN_PEER_RECORD_BYTES;
        badger_bytes_store_u32(r, i);
        badger_bytes_store_u32(r + 8u, 8u);
        badger_bytes_store_u32(r + 12u, (uint32_t)-23);
    }
    assert(clean_peer_trace_valid(trace));
    uint8_t *r = trace + CLEAN_PEER_RECORD_BASE;
    badger_bytes_store_u32(r + 4u, 1u);
    assert(!clean_peer_trace_valid(trace));
    badger_bytes_store_u32(r + 8u, 0u);
    badger_bytes_store_u32(r + 12u, 0u);
    assert(clean_peer_trace_valid(trace));
    badger_bytes_store_u32(r + 16u, 1025u);
    assert(!clean_peer_trace_valid(trace));
    puts("Vendor peer report binding and explicit failure tests passed");
    return 0;
}
