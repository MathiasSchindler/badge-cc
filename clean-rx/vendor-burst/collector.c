#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include "protocol.h"
#include "monitor.h"
#include "../src/sdio_ring.h"
#include "build_id.h"
#include "../../src/host/tools/usb_live_transport.h"

static uint32_t clock_ticks[2], clock_frames[2];
static uint8_t monitor_archive[REF_MONITOR_FIXED_SLOTS*REF_MONITOR_SLOT_BYTES];
static int summarize_window(const uint8_t trace[REF_TRACE_BYTES]);

static void deadline(int signal_number) {
    (void)signal_number;
    static const char message[] = "Reference experiment USB deadline exceeded\n";
    ssize_t ignored = write(STDERR_FILENO, message, sizeof(message)-1u);
    (void)ignored;
    _exit(124);
}

static int serial_valid(const char *s) {
    if (strlen(s) != 16u) return 0;
    return strspn(s,"0123456789ABCDEF") == 16u;
}

static libusb_device_handle *open_badge(libusb_context *context, const char *serial) {
    libusb_device_handle *device = NULL;
    for (uint32_t i = 0u; !device && i < 60u; ++i) {
        device = find_badge(context,serial);
        if (!device) usleep(50000u);
    }
    if (!device) { fputs("Missing serial-matched reference badge\n",stderr); return NULL; }
    int configuration, status = libusb_get_configuration(device,&configuration);
    if (!status && configuration != 1) status = libusb_set_configuration(device,1);
    if (!status) status = libusb_claim_interface(device,0);
    if (status) {
        fprintf(stderr,"Reference USB claim: %s\n",libusb_error_name(status));
        libusb_close(device); return NULL;
    }
    uint8_t info[12];
    const uint8_t expected[] = {'U','2','6','L',1u,0u,64u,0u};
    if (!transfer(device,0xc0u,LIVE_USB_INFO,info,sizeof(info)) ||
        memcmp(info,expected,sizeof(expected)) ||
        usb_report_u32(info+8u) != USB_REPORT_BUILD_ID) {
        fputs("Reference protocol/build mismatch\n",stderr);
        libusb_release_interface(device,0); libusb_close(device); return NULL;
    }
    return device;
}

static void wipe(uint8_t *bytes, size_t size) {
    for (size_t i = 0u; i < size; ++i) ((volatile uint8_t *)bytes)[i] = 0u;
}

static int provision(libusb_device_handle *device, const char *path) {
    FILE *file = fopen(path,"r");
    if (!file) { perror("Private credentials"); return 0; }
    char line[256];
    uint8_t ssid[64] = {1u}, password[64] = {0};
    int have_ssid = 0, have_password = 0, valid = 1;
    while (fgets(line,sizeof(line),file)) {
        char *value = NULL;
        int is_ssid = !strncmp(line,"SSID:",5u), is_password = !strncmp(line,"Pass:",5u);
        if (!is_ssid && !is_password) continue;
        value = line+5u;
        while (*value == ' ' || *value == '\t') ++value;
        size_t length = strcspn(value,"\r\n");
        if (is_ssid) {
            if (have_ssid || !length || length > 32u) { valid = 0; break; }
            ssid[1] = (uint8_t)length; memcpy(ssid+2u,value,length); have_ssid = 1;
        } else {
            if (have_password || length < 8u || length > 63u) { valid = 0; break; }
            memcpy(password,value,length); have_password = 1;
        }
        wipe((uint8_t *)line,sizeof(line));
    }
    if (ferror(file)) valid = 0;
    if (fclose(file)) valid = 0;
    wipe((uint8_t *)line,sizeof(line));
    int ok = valid && have_ssid && have_password;
    if (ok) ok = transfer(device,0x40u,LIVE_USB_CLEAN_SECRET,ssid,sizeof(ssid));
    if (ok) { usleep(20000u); ok = transfer(device,0x40u,LIVE_USB_CLEAN_SECRET,password,sizeof(password)); }
    usleep(20000u);
    wipe(ssid,sizeof(ssid)); wipe(password,sizeof(password));
    if (!ok) fputs("Credential parsing/provisioning failed; values are not logged\n",stderr);
    return ok;
}

static int stage(libusb_device_handle *device, uint8_t nonce[16], uint8_t phase,
                  uint32_t role, const char *prefix, const char *suffix,
                  uint8_t trace[REF_TRACE_BYTES]) {
    nonce[14] = phase;
    uint8_t request[64], report[64];
    usb_report_request_for_test(request,USB_REPORT_BUILD_ID,nonce,3u);
    if (!transfer(device,0x40u,LIVE_USB_FW_SUBMIT,request,sizeof(request))) return 0;
    int got = 0;
    for (uint32_t i = 0u; i < 1800u; ++i) {
        int n = libusb_control_transfer(device,0xc0u,LIVE_USB_FW_REPORT,0u,0u,report,sizeof(report),1000u);
        if (n == sizeof(report)) { got = 1; break; }
        if (n != LIBUSB_ERROR_PIPE) {
            fprintf(stderr,"Reference report read: %d\n",n); return 0;
        }
        usleep(10000u);
    }
    if (!got || usb_report_u32(report) != 0x46363255u ||
        report[4] != 1u || report[5] || report[6] != 64u || report[7] ||
        usb_report_u32(report+8u) != USB_REPORT_BUILD_ID ||
        usb_report_u32(report+12u) != 3u || memcmp(report+16u,nonce,16u) ||
        usb_report_u32(report+60u) != diag_crc32(report,60u)) {
        fputs("Missing or invalid nonce-bound reference report\n",stderr); return 0;
    }
    for (uint32_t page = 0u; trace && page < WIFI_TRACE_PAGES; ++page) {
        uint8_t packet[64];
        int n = libusb_control_transfer(device,0xc0u,LIVE_USB_WIFI_TRACE,(uint16_t)page,
                                        0u,packet,sizeof(packet),1000u);
        if (n != sizeof(packet) ||
            wifi_trace_page_decode(trace,packet,sizeof(packet),report,(uint8_t)page) != USB_RECORD_OK) {
            fprintf(stderr,"Reference trace page %u invalid\n",page); return 0;
        }
    }
    char path[1024];
    int length = snprintf(path,sizeof(path),"%s-%s.bin",prefix,suffix);
    if (length < 0 || (size_t)length >= sizeof(path)) { fputs("Evidence path too long\n",stderr); return 0; }
    FILE *out = fopen(path,"wbx");
    if (!out) { perror(path); return 0; }
    int written = fwrite(report,1u,sizeof(report),out) == sizeof(report) &&
        (!trace || fwrite(trace,1u,REF_TRACE_BYTES,out) == REF_TRACE_BYTES);
    if (fclose(out)) written = 0;
    if (!written) { fputs("Reference evidence write failed\n",stderr); return 0; }
    printf("{\"phase\":\"%s\",\"build\":\"%08x\",\"role\":%u,\"stage\":%u,"
           "\"error\":%u,\"last_command\":%u,\"controller_status\":%d}\n",
           suffix,USB_REPORT_BUILD_ID,trace ? usb_report_u32(trace+12u) : role,
           usb_report_u32(report+32u),usb_report_u32(report+36u),
           trace ? usb_report_u32(trace+16u) : 0u,
           trace ? (int32_t)usb_report_u32(trace+20u) : 0);
    fflush(stdout);
    {
        uint32_t ticks = usb_report_u32(report+40u), frames = usb_report_u32(report+44u);
        printf("{\"test\":\"timer-sof-calibration\",\"phase\":\"%s\",\"role\":%u,"
               "\"timer_ticks\":%u,\"sof_frames\":%u}\n",suffix,role,ticks,frames);
        if (!ref_timer_calibration_valid(ticks,frames) ||
            (clock_ticks[role] && (clock_ticks[role] != ticks || clock_frames[role] != frames))) {
            fputs("Invalid USB-SOF timer calibration; raw evidence saved\n",stderr); return 0;
        }
        clock_ticks[role] = ticks; clock_frames[role] = frames;
    }
    if (phase == 4u && (!trace || !summarize_window(trace))) return 0;
#if CLEAN_RX_HT_COPY_PROBE
    if (trace && role == 0u &&
        usb_report_u32(trace+REF_COPY_FAULT_BASE) == REF_COPY_FAULT_MAGIC) {
        const uint8_t *fault = trace+REF_COPY_FAULT_BASE, *copy = trace+REF_COPY_BASE;
        printf("{\"test\":\"handoff-copy-fault\",\"snapshot_collected\":%s,"
               "\"diagnostic_error\":%u,\"original_receive_error\":%u,"
               "\"psm_reason\":%u,\"copy_phase\":%u,\"status1\":%u,"
               "\"wait_observed_count\":%u,\"wait_budget_remaining\":%u,"
               "\"wait_tsf_delta\":%u}\n",
               usb_report_u32(fault+4u) == 1u?"true":"false",usb_report_u32(fault+8u),
               usb_report_u32(fault+12u),usb_report_u32(fault+16u),ref_u16(copy+4u),
               ref_u16(copy+22u),ref_u16(copy+80u),ref_u16(copy+82u),
               usb_report_u32(copy+76u)-usb_report_u32(copy+72u));
    }
#endif
    int diagnostic_fault = REF_VENDOR_MONITOR && !REF_CLEAN_HANDOFF &&
        phase >= 5u && phase < 5u+REF_MONITOR_PAGES && trace &&
        usb_report_u32(report+36u) == usb_report_u32(trace+8u);
    if (usb_report_u32(report+32u) != phase || (usb_report_u32(report+36u) && !diagnostic_fault) ||
        (trace && (usb_report_u32(trace) != REF_MAGIC ||
        usb_report_u32(trace+12u) != role || usb_report_u32(trace+4u) != phase ||
        (usb_report_u32(trace+8u) && !diagnostic_fault)))) {
        fputs("Reference device did not complete the requested phase; raw evidence saved\n",stderr);
        return 0;
    }
    if (diagnostic_fault && usb_report_u32(report+36u))
        fprintf(stderr,"Preserved monitor error %u while exporting diagnostic page %u\n",
                usb_report_u32(report+36u),phase-5u);
    return 1;
}

static int host_time(uint64_t *time) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC,&value)) { perror("Monotonic clock"); return 0; }
    *time = (uint64_t)value.tv_sec*1000000u+(uint64_t)value.tv_nsec/1000u;
    return 1;
}

static int summarize_window(const uint8_t trace[REF_TRACE_BYTES]) {
    const uint8_t *window = trace+REF_WINDOW_BASE;
    if (!ref_window_valid(window) || usb_report_u32(window+16u) != clock_ticks[0] ||
        usb_report_u32(window+20u) != clock_frames[0]) {
        fputs("Invalid receive-window encoding; raw evidence saved\n",stderr); return 0;
    }
    uint32_t count = usb_report_u32(window+4u);
    uint32_t ticks = usb_report_u32(window+16u), frames = usb_report_u32(window+20u);
    printf("{\"test\":\"receive-window\",\"records\":%u,\"missed_capacity\":%u,"
           "\"interval_us\":%u,\"cached_fields_are_not_instantaneous\":true}\n",
           count,usb_report_u32(window+8u),usb_report_u32(window+12u));
    printf("{\"test\":\"rearm-decision\",\"enabled\":%u,\"words\":[",
           usb_report_u32(window+24u));
    for (uint32_t i = 0u; i < 16u; ++i)
        printf("%s%u",i?",":"",(unsigned)ref_u16(window+32u+2u*i));
    uint32_t sequence = ref_u16(window+32u);
    printf("],\"psm_snapshot_committed\":%s,\"cached_snapshot_coherent\":%s}\n",
           sequence && !(sequence&1u)?"true":"false",
           ref_window_coherent(window+REF_WINDOW_HEADER_BYTES+
               (count-1u)*REF_WINDOW_RECORD_BYTES)?"true":"false");
    printf("{\"test\":\"rearm-first-fault\",\"recovery_mode\":%u,\"words\":[",
           usb_report_u32(window+28u));
    for (uint32_t i = 0u; i < 16u; ++i)
        printf("%s%u",i?",":"",(unsigned)ref_u16(window+64u+2u*i));
    sequence = ref_u16(window+64u);
    printf("],\"psm_snapshot_committed\":%s}\n",
           sequence && !(sequence&1u)?"true":"false");
    printf("{\"test\":\"rearm-phy-reset\",\"enabled\":%s,\"park_only\":%s,\"words\":[",
           (usb_report_u32(window+28u)&12u) == 4u?"true":"false",
           usb_report_u32(window+28u)&8u?"true":"false");
    for (uint32_t i = 0u; i < 16u; ++i)
        printf("%s%u",i?",":"",(unsigned)ref_u16(window+96u+2u*i));
    puts("]}");
    for (uint32_t i = 0u; i < count; ++i) {
        const uint8_t *r = window+REF_WINDOW_HEADER_BYTES+i*REF_WINDOW_RECORD_BYTES;
        printf("{\"test\":\"receive-window-sample\",\"index\":%u,\"time_ticks\":%u,"
               "\"read_ticks\":%u,\"time_us\":%llu,\"read_us\":%llu,"
               "\"cached_snapshot_coherent\":%s,\"captured\":%u,"
               "\"raw_psm_debug\":%u,\"rx_polls\":%u,\"rx_cycles\":%u,"
               "\"cached_maccontrol\":%u,\"cached_shm_600_60f\":[",
               i,usb_report_u32(r),usb_report_u32(r+4u),
               (unsigned long long)ref_timer_us(usb_report_u32(r),ticks,frames),
               (unsigned long long)ref_timer_us(usb_report_u32(r+4u),ticks,frames),
               ref_window_coherent(r)?"true":"false",usb_report_u32(r+8u),
               usb_report_u32(r+12u),usb_report_u32(r+16u),usb_report_u32(r+20u),
               usb_report_u32(r+24u));
        for (uint32_t j = 0u; j < 16u; ++j)
            printf("%s%u",j?",":"",(unsigned)ref_u16(r+28u+2u*j));
        printf("],\"cached_bad_fcs\":%u,\"cached_length_reject\":%u,"
               "\"cached_rx_event_reject\":%u,\"cached_plcp_reject\":%u,"
               "\"ht_starts\":%u,\"saved_headers\":%u,\"header_overflow\":%u,"
               "\"early_id\":%u,\"psm_restart_attempts\":%u,\"cached_rxctl\":%u,"
               "\"controller_state\":%u,\"controller_error\":%u,"
               "\"controller_error_address\":%u,\"controller_error_value\":%u,"
               "\"first_fault_sequence\":%u,\"phy_reset_state\":%u}\n",
               (unsigned)ref_u16(r+76u),(unsigned)ref_u16(r+78u),
               (unsigned)ref_u16(r+80u),(unsigned)ref_u16(r+82u),
               (unsigned)ref_u16(r+112u),(unsigned)ref_u16(r+132u),
               (unsigned)ref_u16(r+134u),(unsigned)ref_u16(r+136u),
               (unsigned)ref_u16(r+138u),(unsigned)ref_u16(r+92u),
               usb_report_u32(r+100u),usb_report_u32(r+104u),
               usb_report_u32(r+148u),usb_report_u32(r+152u),
               (unsigned)ref_u16(r+156u),(unsigned)ref_u16(r+158u));
    }
    return 1;
}

static int summarize_source(const uint8_t vendor[REF_TRACE_BYTES], const uint8_t nonce[14]) {
    uint8_t expected[CLEAN_HT_STIMULUS_STORAGE];
    if (usb_report_u32(vendor+28u) != CLEAN_HT_STIMULUS_COUNT) {
        fputs("Incomplete fixed stimulus; retained transmission records are not RF qualification\n",stderr);
        return 0;
    }
    for (uint32_t i = 0u; i < CLEAN_HT_STIMULUS_COUNT; ++i) {
        const uint8_t *r = vendor+REF_STIMULUS_RECORD_BASE+i*REF_STIMULUS_RECORD_BYTES;
        if (!ref_stimulus_record_valid(r,nonce,i)) {
            fprintf(stderr,"Invalid fixed stimulus record %u\n",i); return 0;
        }
        printf("{\"test\":\"fixed-stimulus-tx\",\"index\":%u,\"kind\":%u,"
               "\"psdu_bytes\":%u,\"trigger_tsf\":%u,\"complete_tsf\":%u,"
               "\"conditions\":7,\"phy_error\":0,\"template_verified\":true,"
               "\"raw_txe_56a\":%u}\n",
               i,i%3u,usb_report_u32(r+32u)-6u,
               usb_report_u32(r+16u),usb_report_u32(r+20u),(unsigned)ref_u16(r+56u));
    }
    (void)ref_source_frame(expected,nonce,CLEAN_HT_STIMULUS_COUNT-1u);
    if (memcmp(expected,vendor+REF_STIMULUS_PACKET_BASE,sizeof(expected))) {
        fputs("Fixed stimulus last descriptor/bytes mismatch\n",stderr); return 0;
    }
    return 1;
}

static int summarize_monitor(const uint8_t source[REF_TRACE_BYTES],
                              const uint8_t monitor[REF_TRACE_BYTES], const uint8_t nonce[16]) {
    uint8_t bound[14], expected[CLEAN_HT_STIMULUS_STORAGE], mac[6];
    memcpy(bound, nonce, sizeof(bound));
    bound[13] = 6u;
    ref_mac(mac, nonce);
    uint32_t saved = usb_report_u32(monitor + 76u);
    uint32_t packets = usb_report_u32(monitor + 72u), matched = usb_report_u32(monitor + 92u);
    if (usb_report_u32(source + 24u) != 6u || memcmp(source + 32u, mac, 6u) ||
        memcmp(monitor + 32u, mac, 6u) || usb_report_u32(monitor + 64u) != REF_MONITOR_MAGIC ||
        usb_report_u32(monitor + 68u) != 1u ||
        saved > (REF_CLEAN_HANDOFF ? REF_MONITOR_SLOTS : REF_MONITOR_FIXED_SLOTS) ||
        (uint64_t)saved + usb_report_u32(monitor + 80u) + usb_report_u32(monitor + 84u) != matched ||
        (uint64_t)matched + usb_report_u32(monitor + 88u) != packets ||
        usb_report_u32(monitor + 96u) != 9u || usb_report_u32(monitor + 108u) != 1u ||
        usb_report_u32(monitor + 112u) != 6u || usb_report_u32(monitor + 116u) != 6u) {
        fputs("Invalid independent monitor identity, setup or capture accounting\n", stderr);
        return 0;
    }
    uint64_t channels = 0u;
    for (uint32_t i = 0u; i < 16u; ++i) channels += usb_report_u32(monitor + 128u + 4u*i);
    if (channels != packets || usb_report_u32(monitor + 128u)) {
        fputs("Independent monitor SDPCM channel accounting mismatch\n", stderr);
        return 0;
    }
    static const uint32_t commands[] = {263u,86u,2u,30u,108u,107u,29u,108u,107u};
    for (uint32_t i = 0u; i < 9u; ++i) {
        const uint8_t *r = monitor + REF_MONITOR_CONTROLS + i*REF_MONITOR_CONTROL_BYTES;
        uint32_t get = i == 5u || i == 6u || i == 8u;
        if (usb_report_u32(r) != commands[i] || usb_report_u32(r + 4u) != !get ||
            usb_report_u32(r + 8u) != 1u || usb_report_u32(r + 12u) ||
            usb_report_u32(r + 16u) || usb_report_u32(r + 20u) > 16u ||
            (get && usb_report_u32(r + 20u) != (i == 6u ? 12u : 4u)) ||
            (i == 5u && usb_report_u32(r + 24u) != 1u) ||
            (i == 6u && (usb_report_u32(r + 24u) != 6u || usb_report_u32(r + 28u) != 6u)) ||
            (i == 8u && usb_report_u32(r + 24u))) {
            fprintf(stderr, "Independent monitor command %u failed validation\n", i);
            return 0;
        }
    }
    if (!summarize_source(source, bound)) return 0;
    if (!REF_CLEAN_HANDOFF) {
        uint32_t attempts = usb_report_u32(monitor+192u), completed = usb_report_u32(monitor+196u);
        int polling_ok = !usb_report_u32(monitor+200u) && (REF_MONITOR_POLL ?
            attempts && attempts == completed && usb_report_u32(monitor+204u) == 1u &&
                usb_report_u32(monitor+220u) > 0u : !attempts && !completed);
        printf("{\"test\":\"independent-monitor-control-poll\",\"enabled\":%s,"
               "\"interval_ms\":%u,\"attempts\":%u,\"completed\":%u,\"valid\":%s}\n",
               REF_MONITOR_POLL?"true":"false",REF_MONITOR_POLL ? REF_MONITOR_POLL_MS : 0u,
               attempts,completed,polling_ok?"true":"false");
        if (!polling_ok) {
            fputs("Invalid fixed-source monitor control-poll evidence\n",stderr);
            return 0;
        }
    }
    uint32_t masks[4] = {0}, fcs_masks[4] = {0}, duplicates = 0u, invalid = 0u;
    for (uint32_t s = 0u; s < saved; ++s) {
        const uint8_t *slot = REF_CLEAN_HANDOFF ?
            monitor + REF_MONITOR_PACKETS + s*REF_MONITOR_SLOT_BYTES :
            monitor_archive + s*REF_MONITOR_SLOT_BYTES;
        if (!ref_monitor_slot_valid(slot)) {
            fprintf(stderr, "Invalid independent monitor raw packet %u\n", s);
            return 0;
        }
        uint32_t length = usb_report_u32(slot);
        uint32_t matched_bodies = 0u;
        for (uint32_t i = 0u; i < CLEAN_HT_STIMULUS_COUNT; ++i) {
            (void)ref_source_frame(expected, bound, i);
            for (uint32_t body = 0u; body < (i%3u == 2u ? 2u : 1u); ++body) {
                uint32_t bytes = REF_CLEAN_HANDOFF ? clean_ht_handoff_length(bound, body) :
                    CLEAN_HT_STIMULUS_MPDU;
                uint32_t offset = CLEAN_HT_DATA_HEADER + 6u + (i%3u == 2u ? 4u : 0u);
                if (body) offset += 4u + ((ref_source_control_length(bound) + 3u) & ~3u);
                uint32_t kind = i%3u == 2u ? 2u + body : i%3u;
                if (!ref_monitor_contains(slot + 16u, length, expected + offset, bytes - 4u)) continue;
                int fcs = ref_monitor_contains(slot + 16u, length, expected + offset, bytes);
                ++matched_bodies;
                masks[kind] |= 1u << i;
                if (fcs) {
                    if (fcs_masks[kind] & (1u<<i)) ++duplicates;
                    fcs_masks[kind] |= 1u << i;
                } else ++invalid;
                printf("{\"test\":\"independent-raw-body\",\"slot\":%u,\"index\":%u,"
                       "\"kind\":%u,\"body\":%u,\"mpdu_bytes\":%u,\"sdpcm_channel\":%u,"
                       "\"exact_mac_payload\":true,\"exact_fcs_bytes_present\":%s}\n",
                       s,i,i%3u,body,bytes,usb_report_u32(slot + 8u),fcs ? "true" : "false");
            }
        }
        if (!matched_bodies) ++invalid;
    }
    int pair = (masks[2] & masks[3]) != 0u, proven = ref_monitor_qualified(fcs_masks);
    if (!REF_CLEAN_HANDOFF)
        proven = ref_monitor_fixed_qualified(fcs_masks,duplicates,invalid) &&
            !usb_report_u32(monitor+80u) && !usb_report_u32(monitor+84u);
    printf("{\"test\":\"independent-monitor-result\",\"sdpcm_packets\":%u,"
           "\"saved_nonce_packets\":%u,\"matched_packets\":%u,\"selection_full\":%u,"
           "\"oversized_nonce_packets\":%u,\"legacy_mask\":%u,\"single_ht_mask\":%u,"
           "\"aggregate_first_mask\":%u,\"aggregate_second_mask\":%u,"
           "\"pair_payload_bytes_confirmed\":%s,\"pair_fcs_bytes_confirmed\":%s,"
           "\"raw_receive_proven\":%s,\"duplicates\":%u,\"invalid_bodies\":%u,"
           "\"all_32_fixed_bodies_received\":%s,\"vendor_internal_drops_known\":false,"
           "\"ht_interoperability_qualified\":false}\n",
           packets,saved,matched,usb_report_u32(monitor + 80u),usb_report_u32(monitor + 84u),
           masks[0],masks[1],masks[2],masks[3],pair ? "true" : "false",
           (fcs_masks[2] & fcs_masks[3]) ? "true" : "false",proven ? "true" : "false",
           duplicates,invalid,!REF_CLEAN_HANDOFF && proven ? "true" : "false");
    if (!proven) fputs("Independent monitor requires complete exact first/second bodies and both controls, including FCS; raw failures retained\n", stderr);
    return proven;
}

static int summarize(const uint8_t vendor[REF_TRACE_BYTES], const uint8_t observer[REF_TRACE_BYTES],
                      const uint8_t nonce[16]) {
    if (REF_VENDOR_MONITOR) return summarize_monitor(vendor, observer, nonce);
    uint8_t mac[6]; ref_mac(mac,nonce);
    if (memcmp(vendor+32u,mac,6u) || memcmp(observer+32u,mac,6u) ||
        (usb_report_u32(observer+56u)&0x100u) ||
        usb_report_u32(observer+64u) != REF_OBSERVER_ABI) {
        fputs("Reference identity or receive-only configuration mismatch\n",stderr); return 0;
    }
    if (!REF_CLEAN_STIMULUS) {
    const uint8_t *timing = vendor+REF_SUBMIT_STATS;
    uint32_t timed = usb_report_u32(timing), elapsed = usb_report_u32(timing+4u);
    uint32_t minimum = usb_report_u32(timing+8u), maximum = usb_report_u32(timing+12u), total = 0u;
    for (uint32_t i = 0u; i < 4u; ++i) {
        uint32_t bucket = usb_report_u32(timing+16u+4u*i);
        if (bucket > REF_FRAME_COUNT) { fputs("Invalid submission timing bucket\n",stderr); return 0; }
        total += bucket;
    }
    if (timed != REF_FRAME_COUNT || timed != total || !elapsed ||
        minimum > maximum || maximum > elapsed) {
        fputs("Invalid bounded submission timing report\n",stderr); return 0;
    }
    uint32_t first_eight = usb_report_u32(timing+32u), next_eight = usb_report_u32(timing+36u);
    if (!first_eight || !next_eight || first_eight > elapsed ||
        next_eight > elapsed-first_eight ||
        usb_report_u32(timing+40u) != (nonce[11] ? REF_EARLY_PREPARED : 0u) ||
        (nonce[11] ? !usb_report_u32(timing+44u) : usb_report_u32(timing+44u) != 0u)) {
        fputs("Invalid prepared-fixture timing report\n",stderr); return 0;
    }
    printf("{\"test\":\"vendor-submission-timing\",\"pattern\":\"%s\","
           "\"ampdu_mpdu_requested\":%u,\"frames\":%u,\"elapsed_us\":%llu,"
           "\"min_write_us\":%llu,\"max_write_us\":%llu,"
           "\"write_timer_tick_buckets\":[%u,%u,%u,%u],\"bucket_upper_timer_ticks\":[200,400,800,null],"
           "\"first_eight_us\":%llu,\"next_eight_us\":%llu,\"prepared_frames\":%u,\"prepare_us\":%llu}\n",
           nonce[11] ? "eight-prepared-constant-early-size" : "large-prime-small-pairs",
           REF_AMPDU_LIMIT,timed,
           (unsigned long long)ref_timer_us(elapsed,clock_ticks[1],clock_frames[1]),
           (unsigned long long)ref_timer_us(minimum,clock_ticks[1],clock_frames[1]),
           (unsigned long long)ref_timer_us(maximum,clock_ticks[1],clock_frames[1]),
           usb_report_u32(timing+16u),
           usb_report_u32(timing+20u),usb_report_u32(timing+24u),usb_report_u32(timing+28u),
           (unsigned long long)ref_timer_us(first_eight,clock_ticks[1],clock_frames[1]),
           (unsigned long long)ref_timer_us(next_eight,clock_ticks[1],clock_frames[1]),
           usb_report_u32(timing+40u),
           (unsigned long long)ref_timer_us(usb_report_u32(timing+44u),clock_ticks[1],clock_frames[1]));
    printf("{\"test\":\"vendor-queries\",\"queries\":[");
    for (uint32_t i = 0u; i < REF_QUERY_COUNT; ++i) {
        const uint8_t *q = vendor+REF_QUERY_BASE+i*REF_QUERY_BYTES;
        printf("%s{\"name\":\"%s\",\"after\":%s,\"attempted\":%s,\"supported\":%s,"
               "\"error\":%u,\"controller_status\":%d,\"bytes\":%u,\"word0\":%u}",
               i?",":"",ref_queries[i],i==14u || i==16u || i==18u?"true":"false",
               usb_report_u32(q)?"true":"false",usb_report_u32(q+4u)?"true":"false",
               usb_report_u32(q+8u),(int32_t)usb_report_u32(q+12u),
               usb_report_u32(q+16u),usb_report_u32(q+20u));
    }
    puts("]}");
    }
    const uint8_t *phy_snapshot = observer+REF_PHY_SNAPSHOT;
    if (!ref_phy_snapshot_valid(phy_snapshot)) {
        fputs("Invalid stopped PHY snapshot commit\n",stderr); return 0;
    }
    printf("{\"test\":\"stopped-phy-controls\",\"enabled\":%s",
           CLEAN_RX_HT_PHY_SNAPSHOT ? "true" : "false");
#if CLEAN_RX_HT_PHY_SNAPSHOT
    static const uint16_t phy_indices[CLEAN_OBSERVER_PHY_REGS] = CLEAN_OBSERVER_PHY_INDICES;
    printf(",\"indices\":[");
    for (uint32_t i = 0u; i < CLEAN_OBSERVER_PHY_REGS; ++i)
        printf("%s%u",i?",":"",(unsigned)phy_indices[i]);
    for (uint32_t phase = 0u; phase < 2u; ++phase) {
        printf("],\"%s\":[",phase ? "after_rx" : "before_rx");
        for (uint32_t i = 0u; i < CLEAN_OBSERVER_PHY_REGS; ++i)
            printf("%s%u",i?",":"",(unsigned)ref_u16(phy_snapshot+8u+
                2u*(phase*CLEAN_OBSERVER_PHY_REGS+i)));
    }
    printf("]");
#endif
    puts("}");
    static const char *const hardware_names[REF_HARDWARE_WORDS] = {
        "chipcommon_id","cc_clock","d11_clock","pmu_status","pmu_resources","pmu_pending"};
    static const char *const calibration_names[6] = {
        "rc_lpf","rc_adc","rc_dacbuf","vco_status","dc_idac","dc_dcoe"};
    printf("{\"test\":\"controller-calibration\"");
    for (uint32_t i = 0u; i < 6u; ++i)
        printf(",\"%s\":%u",calibration_names[i],usb_report_u32(observer+128u+36u+4u*i));
    puts("}");
    uint32_t chip = usb_report_u32(observer+REF_HARDWARE_BASE);
    if ((chip&0xffffu) != 43439u ||
        chip != usb_report_u32(observer+REF_HARDWARE_BASE+4u*REF_HARDWARE_WORDS)) {
        fputs("Invalid or changing stopped radio identity\n",stderr); return 0;
    }
    for (uint32_t phase = 0u; phase < 2u; ++phase) {
        printf("{\"test\":\"stopped-hardware\",\"phase\":\"%s\"",
               phase ? "after-rx" : "before-rx");
        for (uint32_t i = 0u; i < REF_HARDWARE_WORDS; ++i)
            printf(",\"%s\":%u",hardware_names[i],
                   usb_report_u32(observer+REF_HARDWARE_BASE+4u*(phase*REF_HARDWARE_WORDS+i)));
        puts("}");
    }
    printf("{\"test\":\"stopped-psm\",\"raw_debug\":%u,\"shm_600_60b\":[",
           usb_report_u32(observer+REF_STOPPED_PSM));
    for (uint32_t i = 0u; i < REF_STOPPED_PSM_WORDS; ++i)
        printf("%s%u",i?",":"",(unsigned)ref_u16(observer+REF_STOPPED_PSM+4u+2u*i));
    puts("]}");
    const uint8_t *log = observer+REF_OBSERVER_DETAIL;
    uint32_t count = (uint32_t)log[2] | ((uint32_t)log[3]<<8u);
    uint32_t overflow = (uint32_t)log[4] | ((uint32_t)log[5]<<8u);
    printf("{\"test\":\"observer-diagnostics\",\"header_hook_calls\":%u,"
           "\"prefix_mismatches\":%u,\"early_ht_starts\":%u,"
           "\"ordinary_f2_frames\":%u,\"vendor_f2_frames\":%u,\"oversize_samples\":%u}\n",
           (unsigned)log[12]|((unsigned)log[13]<<8u),
           (unsigned)log[22]|((unsigned)log[23]<<8u),
           (unsigned)observer[260]|((unsigned)observer[261]<<8u),
           usb_report_u32(observer+60u),usb_report_u32(observer+76u),usb_report_u32(observer+72u));
    const uint8_t *f2 = observer+REF_MBOX_TAIL+444u-REF_MBOX_HEAD_BYTES;
    const uint8_t *multi = observer+REF_MULTI_DETAIL, *loader = observer+REF_LOADER_DETAIL;
    uint32_t posted = usb_report_u32(f2+8u), consumed = usb_report_u32(f2+4u);
    uint32_t f2_counts = usb_report_u32(f2), summaries = usb_report_u32(loader+28u);
    if (!ref_delivery_accounted(observer)) {
        fputs("Invalid frozen observer delivery accounting\n",stderr); return 0;
    }
    if (CLEAN_RX_HT_OBSERVER_FIFO) {
        uint32_t pause = usb_report_u32(observer+REF_FIFO_PAUSE_STATE);
        uint32_t peak = usb_report_u32(observer+REF_FIFO_PEAK);
        uint32_t held = usb_report_u32(observer+REF_FIFO_HELD_SAMPLES);
        int valid = REF_OBSERVER_PAUSE ?
            pause == (REF_OBSERVER_STOP_FULL ? 3u : 2u) && peak == 8u && held :
            !pause && !peak && !held;
        printf("{\"test\":\"observer-fifo\",\"slots\":8,\"pause_enabled\":%s,"
               "\"pause_until_stop\":%s,"
               "\"pause_state\":%u,\"observed_peak_queued\":%u,\"full_held_samples\":%u,"
               "\"stress_ownership_proven\":%s}\n",
               REF_OBSERVER_PAUSE?"true":"false",REF_OBSERVER_STOP_FULL?"true":"false",pause,peak,held,
               REF_OBSERVER_PAUSE && valid?"true":"false");
        if (!valid) {
            fputs("Invalid observer FIFO pause/ownership evidence\n",stderr);
            return 0;
        }
    }
    printf("{\"test\":\"observer-delivery-state\",\"controller_state\":%u,"
           "\"controller_error\":%u,\"rx_polls\":%u,\"rx_cycles\":%u,"
           "\"capture_target\":%u,\"captured\":%u,\"bad_fcs\":%u,\"oversize\":%u,"
           "\"rx_event_discard\":%u,\"plcp_discard\":%u,\"dma_frames\":%u,"
           "\"dma_status0\":%u,\"dma_status1\":%u,\"f2_posted\":%u,\"f2_consumed\":%u,"
           "\"f2_pending\":%u,\"f2_posted_saturated\":%u,\"f2_dropped_saturated\":%u,"
           "\"host_rx_status\":%u,\"host_resyncs\":%u,\"host_stalled\":%u,"
           "\"host_retries\":%u,\"host_credit\":%u,\"host_idle_polls\":%u}\n",
           usb_report_u32(observer+132u),usb_report_u32(observer+140u),
           usb_report_u32(observer+188u),usb_report_u32(observer+192u),
           usb_report_u32(multi+4u),usb_report_u32(multi+8u),
           usb_report_u32(multi+12u),usb_report_u32(multi+16u),
           usb_report_u32(multi+20u),usb_report_u32(multi+24u),usb_report_u32(multi+28u),
           usb_report_u32(multi+32u),usb_report_u32(multi+36u),
           posted,consumed,posted-consumed,f2_counts&0xffffu,f2_counts>>16u,
           usb_report_u32(loader+4u),usb_report_u32(loader+8u),usb_report_u32(loader+12u),
           usb_report_u32(loader+16u),usb_report_u32(loader+20u),usb_report_u32(loader+24u));
    for (uint32_t i = 0u; i < summaries; ++i) {
        const uint8_t *r = observer+REF_RX_SUMMARIES+i*REF_RX_SUMMARY_BYTES;
        uint32_t length = usb_report_u32(r);
        if (length < 4u || length > CLEAN_MPDU_BYTES ||
            usb_report_u32(r+56u) != i+1u) {
            fputs("Invalid ordinary receive summary\n",stderr); return 0;
        }
        printf("{\"test\":\"ordinary-rx-summary\",\"ordinal\":%u,\"mpdu_bytes\":%u,"
               "\"bdc_version\":%u,\"raw_rate\":%u,\"phy_metadata\":%u,\"ht_version\":%u,"
               "\"frame_control\":%u,\"fcs_valid\":%s}\n",
               i+1u,length,r[4],r[5],r[6],r[7],ref_u16(r+16u),
               usb_report_u32(r+8u) == usb_report_u32(r+12u)?"true":"false");
    }
    if (count > CLEAN_OBSERVER_RECORDS || memcmp(log+6u,mac,6u)) {
        fputs("Invalid observer session log\n",stderr); return 0;
    }
    uint32_t samples = usb_report_u32(observer+68u), correlated = 0u, correlated_aggregate = 0u;
    if (samples > REF_SAMPLES) { fputs("Invalid observer sample count\n",stderr); return 0; }
    if (CLEAN_RX_HT_COPY_DMA_PROBE &&
        (samples || !ref_dma_probe_valid(observer+REF_SAMPLE_BASE,observer+REF_EARLY_BASE))) {
        fputs("Invalid completed DMA diagnostic\n",stderr); return 0;
    }
    const uint8_t *early = observer+REF_EARLY_BASE;
    int early_delivered = 0;
    for (uint32_t i = 0u; i < samples; ++i) {
        const uint8_t *sample = observer+REF_SAMPLE_BASE+i*REF_SAMPLE_BYTES;
        uint32_t length = usb_report_u32(sample);
        if (length < 44u || length > REF_SAMPLE_MPDU_BYTES) {
            fputs("Invalid observer MPDU sample length\n",stderr); return 0;
        }
        for (uint32_t j = 0u; j < i; ++j)
            if (length == usb_report_u32(observer+REF_SAMPLE_BASE+j*REF_SAMPLE_BYTES)) {
                fputs("Duplicate observer sample size\n",stderr); return 0;
            }
        int retained_early = (usb_report_u32(sample+8u)&REF_EARLY_SAMPLE) != 0u;
        int linked = retained_early ?
            ref_early_f2_correlated(log,early,sample,mac,usb_report_u32(observer+24u)) :
            ref_sample_correlated(log,sample,mac);
        if (retained_early && linked) early_delivered = 1;
        if (linked) {
            ++correlated;
            uint32_t id = usb_report_u32(sample+4u);
            const uint8_t *record = log+2u*CLEAN_OBSERVER_HEADER_WORDS+
                (id-1u)*2u*CLEAN_OBSERVER_RECORD_WORDS;
            if (record[15]&8u) ++correlated_aggregate;
        }
        printf("{\"test\":\"correlated-mpdu\",\"index\":%u,\"mpdu_bytes\":%u,"
               "\"prefix_id\":%u,\"fcs_valid\":%s,\"fresh_prefix_mac_f2_match\":%s}\n",
               i,length,usb_report_u32(sample+4u),
               diag_crc32(sample+16u,length-4u) == usb_report_u32(sample+16u+length-4u)?"true":"false",
               linked?"true":"false");
    }
    uint32_t early_rxe = ref_u16(early+4u), early_length = early_rxe >= 6u ? early_rxe-6u : 0u;
    int early_linked = ref_early_correlated(log,early,mac);
    int stream_control_proven = !CLEAN_RX_HT_COPY_DEFER_STREAM;
    int stream_phase_proven = !CLEAN_RX_HT_COPY_STREAM_PHASE;
#if CLEAN_RX_HT_ADVANCE
    const uint8_t *handoff = observer+REF_HANDOFF_BASE;
    uint32_t handoff_id = ref_u16(handoff), phases = ref_u16(handoff+4u);
    if (handoff_id > CLEAN_OBSERVER_RECORDS ||
        (handoff_id && phases != 1u && phases != 3u && phases != 7u) ||
        (!handoff_id && phases) || ref_u16(handoff+6u)) {
        fputs("Invalid handoff trace header\n",stderr); return 0;
    }
    static const char *const phase_names[] = {
        "before-retain","before-eoi","after-arm"};
    if (CLEAN_RX_HT_COPY_DEFER_STREAM)
        stream_control_proven = ref_stream_defer_verified(handoff,early);
    for (uint32_t i = 0u; i < CLEAN_OBSERVER_HANDOFF_RECORDS; ++i) {
        if (!(phases & (1u<<i))) continue;
        const uint8_t *r = handoff+2u*CLEAN_OBSERVER_HANDOFF_HEADER+
            2u*i*CLEAN_OBSERVER_HANDOFF_RECORD_WORDS;
        printf("{\"test\":\"handoff-state\",\"phase\":\"%s\",\"prefix_id\":%u,"
               "\"epoch\":%u,\"matches_early_owner\":%s,\"tsf\":%u,\"rxe_length\":%u,"
               "\"status1\":%u,\"status2\":%u,\"copy_control\":%u,\"rx_base\":%u,"
               "\"copy_end\":%u,\"next_length\":%u,\"raw_1002\":%u,"
               "\"raw_1024\":%u,\"raw_1078\":%u}\n",
               phase_names[i],handoff_id,ref_u16(handoff+2u),
               handoff_id == ref_u16(early) && ref_u16(handoff+2u) == ref_u16(early+2u) ?
                   "true":"false",
               usb_report_u32(r),ref_u16(r+4u),ref_u16(r+6u),ref_u16(r+8u),
               ref_u16(r+10u),ref_u16(r+12u),ref_u16(r+14u),ref_u16(r+16u),
               ref_u16(r+18u),ref_u16(r+20u),ref_u16(r+22u));
    }
    const uint8_t *second = observer+REF_SECOND_BASE;
#if CLEAN_RX_HT_COPY_PROBE
    const uint8_t *copy = observer+REF_COPY_BASE;
    if (!ref_copy_probe_valid(copy,early,second)) {
        fputs("Invalid receive-copy probe ownership, phase or seed readback\n",stderr);
        return 0;
    }
    const uint8_t *scope = early+2u*(CLEAN_COPY_SCOPE_SHM-CLEAN_OBSERVER_EARLY_SHM);
    if (CLEAN_RX_HT_COPY_SCOPE)
        stream_phase_proven = ref_copy_scope_valid(scope,copy,early,CLEAN_RX_HT_COPY_SCOPE) &&
            ref_u16(scope) != 20u;
    else if (CLEAN_RX_HT_COPY_STREAM_PHASE)
        stream_phase_proven = ref_stream_phase_verified(handoff,early,copy,
            CLEAN_RX_HT_COPY_STREAM_PHASE);
    if (CLEAN_RX_HT_COPY_STREAM_REARM && !CLEAN_RX_HT_COPY_STREAM_STALL)
        stream_phase_proven = stream_phase_proven &&
            ref_stream_rearm_verified(copy,early,CLEAN_RX_HT_COPY_STREAM_REARM);
    if (CLEAN_RX_HT_COPY_STREAM_STALL && !CLEAN_RX_HT_COPY_SCOPE)
        stream_phase_proven = stream_phase_proven &&
            ref_stream_stall_valid(copy,early,CLEAN_RX_HT_COPY_STREAM_REARM) &&
            ref_u16(copy+96u) != 5u && ref_u16(copy+96u) != 6u;
    uint32_t retained = 0u;
    if (ref_u16(copy+4u) == 3u)
        for (uint32_t i = 0u; i < 4u; ++i)
            if (ref_u16(copy+40u+2u*i) == CLEAN_OBSERVER_COPY_CANARY) retained |= 1u<<i;
    printf("{\"test\":\"handoff-copy-probe\",\"prefix_id\":%u,\"epoch\":%u,"
           "\"phase\":%u,\"seed_readback_valid\":%s,\"retained_canary_mask\":%u,"
           "\"terminal_sample_committed\":%s,\"copy_end_plus6_enabled\":%s,"
           "\"stream_quiet_enabled\":%s,\"stream_control_before\":%u,"
           "\"stream_control_after\":%u,"
           "\"terminal_rxe_length\":%u,\"status1\":%u,\"status2\":%u,"
           "\"copy_control\":%u,\"rx_base\":%u,\"copy_end\":%u,\"next_length\":%u,"
           "\"raw_1002\":%u,\"raw_1024\":%u,\"raw_1078\":%u,"
           "\"terminal_prefix_stable\":%s,\"terminal_words\":[%u,%u,%u,%u],"
           "\"fresh_count_wait_enabled\":%s,\"wait_start_tsf\":%u,\"wait_end_tsf\":%u,"
           "\"fresh_count\":%u,\"wait_budget_remaining\":%u}\n",
           ref_u16(copy),ref_u16(copy+2u),ref_u16(copy+4u),
           ref_u16(copy)?"true":"false",retained,
           ref_u16(copy+4u) == 3u?"true":"false",CLEAN_RX_HT_COPY_END_PLUS6?"true":"false",
           CLEAN_RX_HT_COPY_QUIET?"true":"false",
           CLEAN_RX_HT_COPY_QUIET ? ref_u16(copy+84u) : 0u,
           CLEAN_RX_HT_COPY_QUIET ? ref_u16(copy+86u) : 0u,
           ref_u16(copy+20u),
           ref_u16(copy+22u),ref_u16(copy+24u),ref_u16(copy+26u),
           ref_u16(copy+28u),ref_u16(copy+30u),ref_u16(copy+32u),
           ref_u16(copy+34u),ref_u16(copy+36u),ref_u16(copy+38u),
           ref_u16(copy+4u) == 3u && !memcmp(copy+48u,second+24u,24u)?"true":"false",
           ref_u16(copy+40u),ref_u16(copy+42u),ref_u16(copy+44u),ref_u16(copy+46u),
           CLEAN_RX_HT_COPY_WAIT?"true":"false",
           CLEAN_RX_HT_COPY_WAIT ? usb_report_u32(copy+72u) : 0u,
           CLEAN_RX_HT_COPY_WAIT ? usb_report_u32(copy+76u) : 0u,
           CLEAN_RX_HT_COPY_WAIT ? ref_u16(copy+80u) : 0u,
           CLEAN_RX_HT_COPY_WAIT ? ref_u16(copy+82u) : 0u);
    if (CLEAN_RX_HT_COPY_STREAM_PHASE)
        printf("{\"test\":\"handoff-stream-activation\",\"committed\":%s,"
               "\"epoch\":%u,\"matches_early_epoch\":%s,\"rxe_before\":%u,"
               "\"rxe_after\":%u,\"control_readback\":%u,\"status1\":%u,\"tsf\":%u}\n",
               ref_u16(copy+84u) == 1u?"true":"false",ref_u16(copy+86u),
               ref_u16(copy+84u) == 1u && ref_u16(copy+86u) == ref_u16(early+2u)?"true":"false",
               ref_u16(copy+72u),ref_u16(copy+74u),ref_u16(copy+76u),ref_u16(copy+78u),
               usb_report_u32(copy+80u));
    if (CLEAN_RX_HT_COPY_STREAM_REARM && !CLEAN_RX_HT_COPY_STREAM_STALL)
        printf("{\"test\":\"handoff-stream-rearm\",\"mode\":%u,\"readback_verified\":%s,"
               "\"control_before_stream\":%u,\"base_before\":%u,\"end_before\":%u,"
               "\"next_before\":%u,\"context_before\":%u,\"count_after\":%u,"
               "\"control_after\":%u,\"base_after\":%u,\"end_after\":%u,"
               "\"context_after\":%u}\n",
               CLEAN_RX_HT_COPY_STREAM_REARM,
               ref_stream_rearm_verified(copy,early,CLEAN_RX_HT_COPY_STREAM_REARM)?"true":"false",
               ref_u16(copy+88u),ref_u16(copy+90u),ref_u16(copy+92u),ref_u16(copy+94u),
               ref_u16(copy+96u),ref_u16(copy+98u),ref_u16(copy+100u),ref_u16(copy+102u),
               ref_u16(copy+104u),ref_u16(copy+106u));
    if (CLEAN_RX_HT_COPY_SCOPE) {
        uint32_t commit = ref_u16(scope);
        printf("{\"test\":\"copy-scope\",\"mode\":%u,\"diagnostic_valid\":%s,"
               "\"commit\":%u,\"epoch\":%u,\"executed\":%s,"
               "\"replay_suppressed\":%s,\"replay_command_field\":%u,\"native_length_guard\":%s,"
               "\"destination_shift_words\":%u}\n",
               CLEAN_RX_HT_COPY_SCOPE,
               ref_copy_scope_valid(scope,copy,early,CLEAN_RX_HT_COPY_SCOPE)?"true":"false",
               commit,ref_u16(scope+2u),commit?"true":"false",
               CLEAN_RX_HT_COPY_SCOPE_NO_REPLAY?"true":"false",
               CLEAN_RX_HT_COPY_SCOPE_REPLAY_FIELD,
               CLEAN_RX_HT_COPY_SCOPE_REPLAY_FIELD != 0x83u?"true":"false",
               CLEAN_STREAM_DEST_SHIFT ? CLEAN_STREAM_DEST_WORDS : 0u);
        for (uint32_t n = 0u; commit && n < 3u; ++n) {
            const uint8_t *s = scope+4u+52u*n;
            printf("{\"test\":\"copy-scope-record\",\"index\":%u,\"tsf_low\":%u,"
                   "\"count\":%u,\"status1\":%u,\"status2\":%u,\"control\":%u,"
                   "\"base\":%u,\"end\":%u,\"next\":%u,\"fifo\":%u,\"context\":%u,"
                   "\"source128_window\":\"",
                   n,ref_u16(s),ref_u16(s+2u),ref_u16(s+4u),ref_u16(s+6u),
                   ref_u16(s+8u),ref_u16(s+10u),ref_u16(s+12u),ref_u16(s+14u),
                   ref_u16(s+16u),ref_u16(s+18u));
            for (uint32_t i = 20u; i < 36u; ++i) printf("%02x",s[i]);
            fputs("\",\"source160_window\":\"",stdout);
            for (uint32_t i = 36u; i < 52u; ++i) printf("%02x",s[i]);
            fputs("\"}\n",stdout);
        }
    }
    if (CLEAN_RX_HT_COPY_STREAM_STALL && !CLEAN_RX_HT_COPY_SCOPE) {
        int valid = ref_stream_stall_valid(copy,early,CLEAN_RX_HT_COPY_STREAM_REARM);
        printf("{\"test\":\"handoff-post-stall\",\"mode\":%u,\"readback_valid\":%s,"
               "\"reason\":%u,\"stable_interval_verified\":%s,\"command_issued\":%s,"
               "\"observed_count\":%u,\"budget_remaining\":%u,\"start_tsf_low\":%u,"
               "\"stable_us\":%u,\"count_after\":%u,\"control_after\":%u,"
               "\"base_after\":%u,\"end_after\":%u,\"context_after\":%u,"
               "\"destination_probe\":%s,\"base_shift_words\":%u,"
               "\"endpoint_probe\":%s,\"endpoint_subtract\":%u}\n",
               CLEAN_RX_HT_COPY_STREAM_REARM,valid?"true":"false",ref_u16(copy+96u),
               valid && ref_u16(copy+96u) == 1u?"true":"false",
               valid && ref_u16(copy+96u) == 1u && CLEAN_RX_HT_COPY_STREAM_REARM >= 4?"true":"false",
               ref_u16(copy+88u),ref_u16(copy+90u),ref_u16(copy+92u),ref_u16(copy+94u),
               ref_u16(copy+98u),ref_u16(copy+100u),ref_u16(copy+102u),
               ref_u16(copy+104u),ref_u16(copy+106u),CLEAN_STREAM_DEST_PROBE?"true":"false",
               valid && ref_u16(copy+96u) == 1u && CLEAN_STREAM_DEST_SHIFT ?
                   CLEAN_STREAM_DEST_WORDS : 0u,CLEAN_STREAM_END_PROBE?"true":"false",
               valid && ref_u16(copy+96u) == 1u && CLEAN_RX_HT_COPY_STREAM_REARM == 8 ?
                   CLEAN_STREAM_END_BYTES : 0u);
    }
#endif
    uint32_t second_rxe = ref_u16(second+4u);
    uint32_t second_length = second_rxe >= 6u ? second_rxe-6u : 0u;
    int second_fcs = CLEAN_RX_HT_ADVANCE && ref_u16(second) &&
        second_length >= 44u && second_length <= CLEAN_OBSERVER_SECOND_MAX &&
        diag_crc32(second+CLEAN_OBSERVER_SECOND_HEADER*2u,second_length-4u) ==
            usb_report_u32(second+CLEAN_OBSERVER_SECOND_HEADER*2u+second_length-4u);
    printf("{\"test\":\"continuation-snapshot\",\"prefix_id\":%u,\"epoch\":%u,"
           "\"rxe_length\":%u,\"status1\":%u,\"status2\":%u,\"outcome\":%u,"
           "\"rx_base\":%u,\"fcs_valid\":%s}\n",
           ref_u16(second),ref_u16(second+2u),second_rxe,ref_u16(second+6u),
           ref_u16(second+8u),ref_u16(second+20u),ref_u16(second+22u),second_fcs?"true":"false");
#endif
    int pair_linked = 0;
    for (uint32_t i = 0u; i < samples; ++i)
        if (ref_pair_correlated(log,early,observer+REF_SAMPLE_BASE+i*REF_SAMPLE_BYTES,mac))
            pair_linked = 1;
    int early_fcs = early_length >= 44u && early_length <= CLEAN_OBSERVER_EARLY_MAX &&
        diag_crc32(early+CLEAN_OBSERVER_EARLY_HEADER*2u,early_length-4u) ==
        usb_report_u32(early+CLEAN_OBSERVER_EARLY_HEADER*2u+early_length-4u);
    printf("{\"test\":\"early-snapshot\",\"prefix_id\":%u,\"rx_epoch\":%u,"
           "\"rxe_length\":%u,\"mpdu_bytes\":%u,\"status1\":%u,\"status2\":%u,"
           "\"tsf\":%u,\"candidate_kind\":%u,\"oversize_candidates\":%u,"
           "\"fcs_valid\":%s,\"fresh_prefix_fcs_match\":%s,"
           "\"advance_state\":%u,\"advance_next_length\":%u,\"pair_correlated\":%s}\n",
           ref_u16(early),ref_u16(early+2u),early_rxe,early_length,
           ref_u16(early+6u),ref_u16(early+8u),usb_report_u32(early+10u),
           ref_u16(early+20u),ref_u16(early+22u),
           early_fcs?"true":"false",early_linked?"true":"false",
           CLEAN_RX_HT_ADVANCE ? ref_u16(early+24u) : 0u,
           CLEAN_RX_HT_ADVANCE ? ref_u16(early+26u) : 0u,pair_linked?"true":"false");
    uint32_t valid = 0u, aggregate = 0u, unaccounted = 0u;
    for (uint32_t i = 0u; i < count; ++i) {
        const uint8_t *r = log+2u*CLEAN_OBSERVER_HEADER_WORDS+i*2u*CLEAN_OBSERVER_RECORD_WORDS;
        const uint8_t *sig = r+12u;
        const char *outcome = ref_observer_outcome_name(ref_u16(r+22u));
        if (!outcome) {
            fputs("Invalid observer exit code\n",stderr); return 0;
        }
        int crc = ref_sig_valid(sig);
        int ampdu = crc && (sig[3]&8u);
        if (crc) ++valid;
        if (ampdu) ++aggregate;
        if (crc && !ref_u16(r+22u) && ref_u16(log+14u) != i+1u) ++unaccounted;
        printf("{\"test\":\"radio-header\",\"index\":%u,\"tsf\":%u,\"phy\":%u,"
               "\"rxe_length\":%u,\"ht_sig\":\"",i,usb_report_u32(r),
               (unsigned)r[4]|((unsigned)r[5]<<8u),(unsigned)r[6]|((unsigned)r[7]<<8u));
        for (uint32_t j = 0u; j < 6u; ++j) printf("%02x",sig[j]);
        int attributed = early_linked && ref_u16(early) == i+1u;
        for (uint32_t j = 0u; j < samples; ++j) {
            const uint8_t *sample = observer+REF_SAMPLE_BASE+j*REF_SAMPLE_BYTES;
            if (usb_report_u32(sample+4u) == i+1u && ref_sample_correlated(log,sample,mac))
                attributed = 1;
        }
        printf("\",\"psdu_bytes\":%u,\"mcs\":%u,\"transmitter_attributed\":%s,"
               "\"rx_epoch\":%u,\"terminal_outcome\":%u,\"terminal_rxe_length\":%u,"
               "\"exit_reason\":\"%s\",\"terminal_tsf\":%u,\"terminal_phy\":%u,"
               "\"terminal_status1\":%u,\"terminal_status2\":%u,"
               "\"progress\":%u,\"raw_1004\":%u,\"raw_1002\":%u,"
               "\"raw_102a\":%u,\"raw_102b\":%u,\"raw_10af\":%u,"
               "\"first_good_hint_rxe_length\":%u,\"early_capture_reason\":\"%s\","
               "\"ht_sig_valid\":%s,\"ampdu_format\":%s}\n",
               (unsigned)sig[1]|((unsigned)sig[2]<<8u),sig[0]&127u,
               attributed?"true":"false",ref_u16(r+20u),ref_u16(r+22u),ref_u16(r+28u),
               outcome,usb_report_u32(r+24u),ref_u16(r+30u),ref_u16(r+32u),ref_u16(r+34u),
               ref_u16(r+80u),ref_u16(r+82u),ref_u16(r+84u),
               ref_u16(r+86u),ref_u16(r+88u),ref_u16(r+90u),
               ref_u16(r+92u),ref_early_reason(r,early,i+1u),
               crc?"true":"false",ampdu?"true":"false");
    }
    if (REF_CLEAN_STIMULUS) {
        uint8_t bound_nonce[14], expected[CLEAN_HT_STIMULUS_STORAGE];
        memcpy(bound_nonce,nonce,sizeof(bound_nonce));
        bound_nonce[13] = (uint8_t)usb_report_u32(vendor+24u);
        if (!summarize_source(vendor,bound_nonce)) return 0;
        (void)ref_source_frame(expected,bound_nonce,CLEAN_HT_STIMULUS_COUNT-1u);
        if (REF_CLEAN_HANDOFF) {
            uint32_t first = clean_ht_handoff_length(bound_nonce,0u);
            if (CLEAN_RX_HT_COPY_DMA_PROBE) {
                const uint8_t *dma = observer+REF_SAMPLE_BASE;
                uint32_t saved = usb_report_u32(dma+8u);
                printf("{\"test\":\"completed-dma-probe\",\"saved\":%u,\"matched\":%u,"
                       "\"inspected\":%u,\"max_nonzero_bytes\":%u,\"max_dma_header_length\":%u,"
                       "\"unmatched\":%u,\"qos_nonce_prefixes\":%u,\"nonce_readback_matches\":%s,"
                       "\"qos_first_prefixes\":%u,\"qos_second_prefixes\":%u,"
                       "\"qos_first_valid_fcs\":%u,\"qos_second_valid_fcs\":%u,"
                       "\"stopped_dma_status\":%u,\"stopped_dma_control\":%u,\"first_dma_header\":\"",
                       saved,usb_report_u32(dma+12u),usb_report_u32(dma+16u),
                       usb_report_u32(dma+20u),usb_report_u32(dma+24u),
                       usb_report_u32(dma+28u),usb_report_u32(dma+76u),usb_report_u32(dma+16u) &&
                           !memcmp(dma+32u,bound_nonce,14u)?"true":"false",
                       usb_report_u32(dma+104u),usb_report_u32(dma+108u),
                       usb_report_u32(dma+112u),usb_report_u32(dma+116u),
                       usb_report_u32(dma+120u),usb_report_u32(dma+124u));
                for (uint32_t i = 0u; i < 24u; ++i) printf("%02x",dma[80u+i]);
                printf("\","
                       "\"diagnostic_only\":true,\"unrelated_dma_owner_not_assumed\":true}\n");
                for (uint32_t slot = 0u; slot < saved; ++slot) {
                    const uint8_t *r = dma+CLEAN_OBSERVER_DMA_HEADER+slot*CLEAN_OBSERVER_DMA_RECORD;
                    const uint8_t *bytes = r+32u;
                    printf("{\"test\":\"completed-dma-buffer\",\"slot\":%u,"
                           "\"dma_completions_at_sample\":%u,\"consumer\":%u,\"producer\":%u,"
                           "\"status0\":%u,\"status1\":%u,\"handoff_context\":%u,"
                           "\"held_state\":%u,\"record_flags\":%u,\"frozen_ring_slot\":%s,\"window_start\":%u,"
                           "\"prefix_bytes\":%u,\"dma_header_length\":%u}\n",
                           slot,usb_report_u32(r),usb_report_u32(r+4u),usb_report_u32(r+8u),
                           usb_report_u32(r+12u),usb_report_u32(r+16u),usb_report_u32(r+20u),
                           ref_u16(r+24u),usb_report_u32(r+24u)&0xe0000000u,
                           usb_report_u32(r+24u)&0x80000000u?"true":"false",
                           usb_report_u32(r+28u)>>16u,CLEAN_OBSERVER_DMA_PREFIX,ref_u16(r+28u));
                    for (uint32_t index = 0u; index < CLEAN_HT_STIMULUS_COUNT; ++index) {
                        (void)clean_ht_handoff_frame(expected,bound_nonce,index);
                        for (uint32_t part = 0u; part < (index%3u == 2u ? 2u : 1u); ++part) {
                            uint32_t length = clean_ht_handoff_length(bound_nonce,part);
                            uint32_t offset = part ? 18u+4u+((first+3u)&~3u) :
                                index%3u == 2u ? 18u : 14u;
                            for (uint32_t at = 0u; at+length <= CLEAN_OBSERVER_DMA_PREFIX; ++at) {
                                if (memcmp(bytes+at,expected+offset,length)) continue;
                                printf("{\"test\":\"completed-dma-exact-body\",\"slot\":%u,"
                                       "\"source_index\":%u,\"kind\":%u,\"body\":%u,\"offset\":%u,"
                                       "\"bytes\":%u,\"fcs_valid\":%s}\n",slot,index,index%3u,part,
                                       at+(usb_report_u32(r+28u)>>16u),length,
                                       diag_crc32(bytes+at,length-4u) ==
                                           usb_report_u32(bytes+at+length-4u)?"true":"false");
                            }
                        }
                    }
                }
            }
            uint32_t mode = bound_nonce[10], early_index = CLEAN_HT_STIMULUS_COUNT;
            int exact_first = 0, exact_pair = 0;
            if (early_linked && early_length == first && ref_u16(early+15u) == CLEAN_HT_HANDOFF_PSDU) {
                const uint8_t *body = early+CLEAN_OBSERVER_EARLY_HEADER*2u;
                uint32_t sequence = ref_u16(body+22u)>>4u;
                early_index = sequence/2u;
                if (!(sequence&1u) && early_index%3u == 2u &&
                    clean_ht_handoff_frame(expected,bound_nonce,early_index))
                    exact_first = !memcmp(body,expected+18u,first);
                for (uint32_t i = 0u; exact_first && i < samples; ++i) {
                    const uint8_t *sample = observer+REF_SAMPLE_BASE+i*REF_SAMPLE_BYTES;
                    uint32_t at = 18u+4u+((first+3u)&~3u);
                    if (ref_pair_correlated(log,early,sample,mac) &&
                        !memcmp(sample+16u,expected+at,clean_ht_handoff_length(bound_nonce,1u)))
                        exact_pair = 1;
                }
            }
            uint32_t negative_outcome = 0u, negative_length = 0u, negative_id = 0u;
            (void)clean_ht_handoff_frame(expected,bound_nonce,2u);
            for (uint32_t i = 0u; i < count; ++i) {
                const uint8_t *record = log+2u*CLEAN_OBSERVER_HEADER_WORDS+
                    i*2u*CLEAN_OBSERVER_RECORD_WORDS;
                if (!memcmp(record+12u,expected+8u,6u) && !memcmp(record+36u,expected+18u,40u)) {
                    negative_id = i+1u;
                    negative_outcome = ref_u16(record+22u);
                    negative_length = ref_u16(record+28u);
                }
            }
            if (exact_first && (!mode || early_index == 2u)) {
                negative_id = ref_u16(early);
                const uint8_t *record = log+2u*CLEAN_OBSERVER_HEADER_WORDS+
                    (negative_id-1u)*2u*CLEAN_OBSERVER_RECORD_WORDS;
                negative_outcome = ref_u16(record+22u);
                negative_length = ref_u16(record+28u);
            }
            uint32_t mask = usb_report_u32(observer+REF_STIMULUS_RX_MASK), required = 0u;
            for (uint32_t i = 0u; i < CLEAN_HT_STIMULUS_COUNT; ++i)
                if (i%3u != 2u) required |= 1u<<i;
            int clean = !(mask&~required) && !usb_report_u32(observer+REF_STIMULUS_RX_DUPLICATES) &&
                !usb_report_u32(observer+REF_STIMULUS_RX_INVALID) && !unaccounted;
            uint32_t after = (1u<<3u)|(1u<<4u)|(1u<<21u)|(1u<<22u);
            int recovered = (mask&after) == after;
            int exact_bad_second = ref_handoff_bad_second(log,early,observer+REF_SECOND_BASE,bound_nonce);
            int negative = mode == 1u ? negative_id && negative_outcome == 2u &&
                exact_pair && early_index > 2u :
                mode == 2u ? exact_first && early_index == 2u && !exact_pair &&
                    exact_bad_second && negative_outcome == 2u : 0;
            printf("{\"test\":\"controlled-handoff-result\",\"case\":%u,\"first_bytes\":%u,"
                   "\"second_bytes\":%u,\"guard_body\":%s,\"psdu_bytes\":%u,"
                   "\"stream_prefix_threshold\":%u,\"stream_defer_enabled\":%s,"
                   "\"stream_defer_readback_verified\":%s,"
                   "\"stream_phase\":%u,\"continuation_stream_threshold\":%u,"
                   "\"stream_phase_readback_verified\":%s,"
                   "\"stream_rearm_mode\":%u,\"diagnostic_only\":%s,"
                   "\"transmissions\":24,\"exact_first\":%s,"
                   "\"exact_pair\":%s,\"early_source_index\":%u,\"advance\":%u,"
                   "\"case_prefix_id\":%u,\"case_terminal_outcome\":%u,\"case_terminal_length\":%u,"
                   "\"exact_bad_second\":%s,"
                   "\"recovery_controls_received\":%s,\"negative_case_proven\":%s,"
                   "\"received_control_mask\":%u,\"expected_control_mask\":%u,"
                   "\"all_controls_received\":%s,\"delivery_integrity\":%s,"
                   "\"ht_station_qualified\":false}\n",
                   mode,first,clean_ht_handoff_length(bound_nonce,1u),
                   CLEAN_RX_HT_HANDOFF_GUARD?"true":"false",CLEAN_HT_HANDOFF_PSDU,
                   CLEAN_RX_STREAM_PREFIX_BYTES,CLEAN_RX_HT_COPY_DEFER_STREAM?"true":"false",
                   CLEAN_RX_HT_COPY_DEFER_STREAM && stream_control_proven?"true":"false",
                   CLEAN_RX_HT_COPY_STREAM_PHASE,CLEAN_RX_CONTINUATION_STREAM_BYTES,
                   CLEAN_RX_HT_COPY_STREAM_PHASE && stream_phase_proven?"true":"false",
                   CLEAN_RX_HT_COPY_STREAM_REARM,
                   CLEAN_STREAM_DEST_PROBE?"true":"false",
                   exact_first?"true":"false",exact_pair?"true":"false",early_index,
                   ref_u16(early+24u),negative_id,negative_outcome,negative_length,
                   exact_bad_second?"true":"false",
                   recovered?"true":"false",negative && recovered && clean?"true":"false",
                   mask,required,mask == required?"true":"false",clean?"true":"false");
            int passed = !CLEAN_RX_HT_COPY_DMA_PROBE && !CLEAN_STREAM_DEST_PROBE &&
                clean && stream_control_proven &&
                stream_phase_proven &&
                (mode ? negative && recovered : exact_pair && mask == required);
            if (!passed) fputs("Controlled handoff qualification not proven; raw failed trial retained\n",stderr);
            return passed;
        }
        int exact_early = 0;
        if (early_linked && early_length == CLEAN_HT_STIMULUS_MPDU &&
            ref_u16(early+20u) == 2u && ref_u16(early+15u) == CLEAN_HT_STIMULUS_PSDU) {
            const uint8_t *body = early+CLEAN_OBSERVER_EARLY_HEADER*2u;
            uint32_t sequence = ref_u16(body+22u)>>4u, index = sequence/2u;
            if (!(sequence&1u) && index%3u == 2u &&
                clean_ht_stimulus_frame(expected,bound_nonce,index))
                exact_early = !memcmp(body,expected+CLEAN_HT_DATA_HEADER+10u,
                                     CLEAN_HT_STIMULUS_MPDU);
        }
        uint32_t rx_mask = usb_report_u32(observer+REF_STIMULUS_RX_MASK);
        const uint8_t *legacy = observer+REF_SAMPLE_BASE+REF_SAMPLE_BYTES;
        uint32_t legacy_index = ref_stimulus_rx_index(legacy+16u,usb_report_u32(legacy),bound_nonce);
        int legacy_proof = legacy_index < CLEAN_HT_STIMULUS_COUNT && legacy_index%3u == 0u;
        uint32_t ht_mask = 0u, legacy_mask = 0u;
        for (uint32_t i = 0u; i < CLEAN_HT_STIMULUS_COUNT; i += 3u) {
            legacy_mask |= 1u<<i; ht_mask |= 1u<<(i+1u);
        }
        if (rx_mask&~(ht_mask|legacy_mask)) {
            fputs("Invalid fixed stimulus receive bitmap\n",stderr); return 0;
        }
        uint32_t prefix_mask = 0u;
        for (uint32_t i = 0u; i < count; ++i) {
            const uint8_t *r = log+2u*CLEAN_OBSERVER_HEADER_WORDS+i*2u*CLEAN_OBSERVER_RECORD_WORDS;
            uint32_t index = ref_stimulus_prefix_index(r,bound_nonce);
            if (index < CLEAN_HT_STIMULUS_COUNT) prefix_mask |= 1u<<index;
        }
        for (uint32_t i = 0u; i < CLEAN_HT_STIMULUS_COUNT; ++i)
            printf("{\"test\":\"fixed-stimulus-observed\",\"index\":%u,\"kind\":%u,"
                   "\"complete_control_delivered\":%s,\"single_ht_prefix_bytes_match\":%s,"
                   "\"prefix_log_complete\":%s}\n",i,i%3u,
                   rx_mask&(1u<<i)?"true":"false",prefix_mask&(1u<<i)?"true":"false",
                   overflow?"false":"true");
        int controls = (rx_mask&(ht_mask|legacy_mask)) == (ht_mask|legacy_mask) &&
            legacy_proof && !usb_report_u32(observer+REF_STIMULUS_RX_DUPLICATES) &&
            !usb_report_u32(observer+REF_STIMULUS_RX_INVALID);
        printf("{\"test\":\"fixed-stimulus-result\",\"transmissions\":%u,"
               "\"legacy_controls\":8,\"single_ht_controls\":8,\"two_body_attempts\":8,"
               "\"valid_ht_headers\":%u,\"ampdu_headers\":%u,"
               "\"exact_554_byte_nonterminal_body\":%s,\"unaccounted_prefix_exits\":%u,"
               "\"received_control_mask\":%u,\"expected_control_mask\":%u,"
               "\"control_duplicates\":%u,\"control_invalid\":%u,"
               "\"independent_legacy_bytes\":%s,\"all_controls_received\":%s,"
               "\"single_ht_prefix_mask\":%u,\"single_ht_missing_prefix_mask\":%u,"
               "\"single_ht_prefix_without_delivery_mask\":%u,\"prefix_log_complete\":%s,"
               "\"independent_third_receiver\":false,\"ht_station_qualified\":false}\n",
               CLEAN_HT_STIMULUS_COUNT,valid,aggregate,exact_early?"true":"false",unaccounted,
               rx_mask,ht_mask|legacy_mask,usb_report_u32(observer+REF_STIMULUS_RX_DUPLICATES),
               usb_report_u32(observer+REF_STIMULUS_RX_INVALID),legacy_proof?"true":"false",
               controls?"true":"false",prefix_mask,ht_mask&~prefix_mask,
               prefix_mask&~rx_mask,overflow?"false":"true");
        if (CLEAN_RX_HT_EARLY_FIFO) {
            uint32_t delivered = usb_report_u32(observer+REF_EARLY_RX_COUNT);
            uint32_t mask = usb_report_u32(observer+REF_EARLY_RX_MASK), expected_mask = 0u;
            uint32_t duplicates = usb_report_u32(observer+REF_EARLY_RX_DUPLICATES);
            for (uint32_t i = 2u; i < CLEAN_HT_STIMULUS_COUNT; i += 3u) expected_mask |= 1u<<i;
            printf("{\"test\":\"early-fifo-result\",\"exact_early_body_delivered\":%s,"
                   "\"early_deliveries\":%u,\"received_early_mask\":%u,"
                   "\"expected_early_mask\":%u,\"early_duplicates\":%u,\"all_controls_received\":%s,"
                   "\"reliability_qualified\":false,\"ht_station_qualified\":false}\n",
                   early_delivered?"true":"false",delivered,mask,expected_mask,duplicates,controls?"true":"false");
            if (!early_delivered || delivered != CLEAN_HT_STIMULUS_COUNT/3u ||
                mask != expected_mask || duplicates) {
                fputs("Early FIFO requires all eight fresh FCS-valid first bodies delivered byte-exact through F2 without duplication\n",stderr);
                return 0;
            }
        }
        if (!exact_early || unaccounted || !controls)
            fputs("Fixed stimulus requires an exact 554-byte nonterminal body, all legacy/single-HT controls without duplicate/corrupt delivery and accounted exits; not proven\n",stderr);
        return exact_early && !unaccounted && controls;
    }
    uint32_t sent = usb_report_u32(vendor+28u), digest = REF_DIGEST_INITIAL;
    uint8_t frame[REF_FRAME_MAX];
    for (uint32_t i = 0u; i < sent && i < REF_FRAME_COUNT; ++i) {
        uint32_t size = ref_frame(frame,nonce,vendor+38u,i);
        digest = ref_digest(digest,frame,size);
    }
    if (sent != REF_FRAME_COUNT || digest != usb_report_u32(vendor+52u)) {
        fputs("Bounded vendor fixture count/digest mismatch\n",stderr); return 0;
    }
    static const char *const names[] = {"txframe","txerror","txallfrm","rxback","txfail","txretry","txfrmsnt"};
    static const uint32_t offsets[] = {4u,16u,196u,416u,432u,436u,472u};
    uint32_t delta;
    int decoded = ref_counter_delta(vendor+REF_COUNTER_BEFORE,vendor+REF_COUNTER_AFTER,4u,&delta);
    printf("{\"test\":\"vendor-counter-deltas\",\"decoded_v10_length848\":%s",
           decoded?"true":"false");
    if (decoded)
        for (uint32_t i = 0u; i < sizeof(offsets)/sizeof(offsets[0]); ++i) {
            if (!ref_counter_delta(vendor+REF_COUNTER_BEFORE,vendor+REF_COUNTER_AFTER,offsets[i],&delta))
                return 0;
            printf(",\"%s\":%u",names[i],delta);
        }
    puts("}");
    uint32_t block_ack_delta = 0u;
    int ba_activity = decoded && ref_counter_delta(vendor+REF_COUNTER_BEFORE,
        vendor+REF_COUNTER_AFTER,416u,&block_ack_delta) && block_ack_delta != 0u;
    uint32_t fixture = ref_fixture_size(nonce[11]);
    const char *sizes = fixture == 128u ? "[128]" : fixture == 256u ? "[256]" :
        fixture == 512u ? "[512]" : "[128,256,512,1024]";
    int focused_early = fixture && early_linked && ref_u16(early+20u) == 2u &&
        early_length == fixture+42u;
    printf("{\"test\":\"reference-result\",\"frames_submitted\":%u,\"sizes\":%s,"
           "\"channel\":%u,\"peer_ht_capable\":%s,\"authorized\":%s,"
           "\"observer_tx_enabled\":false,\"saved_headers\":%u,\"overflow\":%u,"
           "\"valid_ht_headers\":%u,\"ampdu_headers\":%u,"
           "\"ampdu_format_observed\":%s,\"correlated_variable_length_mpdus\":%u,"
           "\"correlated_ampdu_format_mpdus\":%u,"
           "\"unaccounted_prefix_exits\":%u,\"active_prefix_at_stop\":%u,"
           "\"early_nonterminal_mpdu_proven\":%s,\"early_multi_candidate_correlated\":%s,"
           "\"phy_header_transmitter_attributed\":%s,"
           "\"vendor_block_ack_activity\":%s,\"complete_multi_mpdu_delivery_proven\":%s,"
           "\"focused_early_capture_proven\":%s,"
           "\"plaintext_over_air_verified\":false,\"sifs_timing_qualified\":false}\n",
           sent,sizes,usb_report_u32(vendor+24u),usb_report_u32(vendor+44u)&0x2000u?"true":"false",
           (usb_report_u32(vendor+44u)&0x30u)==0x30u?"true":"false",
           count,overflow,valid,aggregate,aggregate?"true":"false",correlated,correlated_aggregate,
           unaccounted,ref_u16(log+14u),
           early_linked?"true":"false",early_linked && ref_u16(early+20u)==2u?"true":"false",
           correlated || early_linked?"true":"false",ba_activity?"true":"false",
           pair_linked?"true":"false",focused_early?"true":"false");
    if (fixture) {
        if (!focused_early || !aggregate || !ba_activity || unaccounted)
            fputs("Focused early capture requires the requested-size nonterminal multi candidate, fresh prefix, independent FCS, vendor BA activity and accounted exits; not proven\n",stderr);
        return focused_early && aggregate && ba_activity && !unaccounted;
    }
    if (!aggregate || !ba_activity || correlated < 2u || !correlated_aggregate || unaccounted)
        fputs("Reference requires vendor BA activity, two fresh-prefix-linked FCS-valid MPDU lengths, a linked A-MPDU-format reception and accounted prefix exits; evidence is incomplete\n",stderr);
    if (!early_linked)
        fputs("Early snapshot requires complete FCS-valid bytes bound to a fresh nonterminal aggregate prefix; not proven\n",stderr);
    if (CLEAN_RX_HT_ADVANCE && !pair_linked)
        fputs("Bounded advance requires two distinct complete FCS-valid bodies under one fresh PPDU; not proven\n",stderr);
    return aggregate != 0u && ba_activity && correlated >= 2u && correlated_aggregate != 0u &&
        !unaccounted && early_linked && (!CLEAN_RX_HT_ADVANCE || pair_linked);
}

int main(int argc, char **argv) {
    if ((argc != 5 && argc != 6 && argc != 7) || !serial_valid(argv[1]) || !serial_valid(argv[2]) || !strcmp(argv[1],argv[2])) {
        fputs("Usage: collector VENDOR_SERIAL OBSERVER_SERIAL PREFIX PRIVATE_SSID_FILE [mixed|128|256|512] [HANDOFF_CASE]\n",stderr); return 2;
    }
    uint8_t profile = 0u, handoff_case = 0u;
    if (argc == 7) {
        static const char *const names[] = {"good","bad-first","bad-last","bad-delimiter","truncated-body"};
        uint32_t i;
        for (i = 0u; i < CLEAN_HT_HANDOFF_CASES; ++i)
            if (!strcmp(argv[6],names[i])) { handoff_case = (uint8_t)i; break; }
        if (!REF_CLEAN_HANDOFF || i == CLEAN_HT_HANDOFF_CASES) {
            fputs("HANDOFF_CASE requires reference-handoff and a supported case name\n",stderr); return 2;
        }
    }
    if (argc >= 6 && strcmp(argv[5],"mixed")) {
        profile = !strcmp(argv[5],"128") ? 1u : !strcmp(argv[5],"256") ? 2u :
            !strcmp(argv[5],"512") ? 3u : 0u;
        if (!profile || (CLEAN_RX_HT_ADVANCE && !REF_CLEAN_HANDOFF)) {
            fputs("Focused sizes require 128, 256 or 512 and an early-only build (reference-early/phy/window/rearm)\n",stderr); return 2;
        }
        if (REF_CLEAN_STIMULUS && !REF_CLEAN_HANDOFF && profile != 3u) {
            fputs("Fixed stimulus requires the explicit 512 profile; no AP credentials are used\n",stderr);
            return 2;
        }
    }
    if (REF_CLEAN_HANDOFF && profile != 1u && profile != 2u) {
        fputs("Controlled handoff requires explicit 128 (170/298) or 256 (298/170)\n",stderr);
        return 2;
    }
    if (CLEAN_RX_HT_HANDOFF_GUARD && handoff_case) {
        fputs("Guard-body boundary control supports only good; two-body negative-case semantics do not apply\n",stderr);
        return 2;
    }
    if (REF_VENDOR_MONITOR && handoff_case) {
        fputs("Independent raw monitor currently requires the unchanged good two-body source\n", stderr);
        return 2;
    }
    if (signal(SIGALRM,deadline) == SIG_ERR) { perror("signal"); return 1; }
    alarm(60u);
    uint8_t nonce[16] = {0}, observer_nonce[16], vendor[REF_TRACE_BYTES], observer[REF_TRACE_BYTES];
    FILE *random = fopen("/dev/urandom","rb");
    if (!random) { perror("/dev/urandom"); return 1; }
    int ok = fread(nonce,1u,12u,random) == 12u;
    if (fclose(random)) ok = 0;
    if (!ok) { fputs("Fresh nonce generation failed\n",stderr); return 1; }
    nonce[11] = profile;
    if (REF_CLEAN_HANDOFF) nonce[10] = handoff_case;
    printf("{\"test\":\"configuration\",\"build\":\"%08x\",\"nonce\":\"",USB_REPORT_BUILD_ID);
    for (uint32_t i = 0u; i < sizeof(nonce); ++i) printf("%02x",nonce[i]);
    puts("\",\"mcs_requested\":0}");
    fflush(stdout);
    libusb_context *context = NULL;
    int status = libusb_init(&context);
    if (status) { fprintf(stderr,"USB init: %s\n",libusb_error_name(status)); return 1; }
    libusb_device_handle *sender = open_badge(context,argv[1]), *listener = NULL;
    if (sender) listener = open_badge(context,argv[2]);
    ok = sender && listener && (REF_CLEAN_STIMULUS || provision(sender,argv[4])) &&
        stage(sender,nonce,1u,1u,argv[3],"vendor-ready",vendor);
    if (ok) {
        memcpy(observer_nonce,nonce,sizeof(nonce));
        observer_nonce[13] = (uint8_t)usb_report_u32(vendor+24u);
        ok = stage(listener,observer_nonce,1u,0u,argv[3],"observer-ready",observer);
    }
    if (ok) {
        uint64_t arm_before = 0u, arm_after = 0u, burst_before = 0u, burst_after = 0u;
        if (CLEAN_RX_HT_WINDOW && !REF_VENDOR_MONITOR)
            ok = host_time(&arm_before) &&
                stage(listener,observer_nonce,3u,0u,argv[3],"observer-armed",NULL) &&
                host_time(&arm_after);
        if (ok && CLEAN_RX_HT_WINDOW && !REF_VENDOR_MONITOR) ok = host_time(&burst_before);
        int burst = ok && stage(sender,nonce,2u,1u,argv[3],"vendor-burst",vendor);
        int timed = !CLEAN_RX_HT_WINDOW || REF_VENDOR_MONITOR || host_time(&burst_after);
        int frozen = stage(listener,observer_nonce,2u,0u,argv[3],"observer-capture",observer);
        int window_ok = 1;
        if (CLEAN_RX_HT_WINDOW && !REF_VENDOR_MONITOR) {
            uint8_t exported[REF_TRACE_BYTES];
            window_ok = stage(listener,observer_nonce,4u,0u,argv[3],"observer-window",exported);
            if (burst && timed) {
                printf("{\"test\":\"receive-window-host-bracket\","
                       "\"arm_ack_latency_us\":%llu,\"vendor_phase_begin_min_us\":%llu,"
                       "\"vendor_phase_begin_max_us\":%llu,\"vendor_phase_end_max_us\":%llu,"
                       "\"not_exact_air_timing\":true}\n",
                       (unsigned long long)(arm_after-arm_before),
                       (unsigned long long)(burst_before-arm_after),
                       (unsigned long long)(burst_before-arm_before),
                       (unsigned long long)(burst_after-arm_before));
            }
        }
        int monitor_ok = 1;
        if (REF_VENDOR_MONITOR && !REF_CLEAN_HANDOFF) {
            uint32_t total = usb_report_u32(observer+76u);
            for (uint32_t page = 0u; page < REF_MONITOR_PAGES && monitor_ok; ++page) {
                uint8_t exported[REF_TRACE_BYTES];
                char suffix[40];
                int length = snprintf(suffix,sizeof(suffix),"observer-raw-page-%u",page);
                monitor_ok = length > 0 && (size_t)length < sizeof(suffix) &&
                    stage(listener,observer_nonce,(uint8_t)(5u+page),0u,argv[3],suffix,exported) &&
                    ref_monitor_page_valid(exported+REF_MONITOR_PAGE_BASE,page,total);
                if (monitor_ok)
                    memcpy(monitor_archive+page*REF_MONITOR_SLOTS*REF_MONITOR_SLOT_BYTES,
                           exported+REF_MONITOR_PAGE_BASE+REF_MONITOR_PAGE_HEADER,
                           REF_MONITOR_SLOTS*REF_MONITOR_SLOT_BYTES);
            }
            printf("{\"test\":\"independent-monitor-archive\",\"valid\":%s,"
                   "\"saved_packets\":%u,\"pages\":%u}\n",
                   monitor_ok?"true":"false",total,REF_MONITOR_PAGES);
            if (!monitor_ok) fputs("Invalid or incomplete fixed-source raw packet archive\n",stderr);
        }
        int early_ok = 1;
        if (CLEAN_RX_HT_EARLY_FIFO) {
            uint8_t exported[REF_TRACE_BYTES], bound_nonce[14];
            memcpy(bound_nonce,nonce,sizeof(bound_nonce)); bound_nonce[13] = 6u;
            uint32_t mask = 0u;
            early_ok = stage(listener,observer_nonce,5u,0u,argv[3],"observer-early-packets",exported);
            const uint8_t *records = exported+REF_EARLY_EXPORT_BASE;
            if (early_ok) early_ok = ref_early_export_valid(records,bound_nonce,
                usb_report_u32(observer+REF_MULTI_DETAIL+8u),&mask) &&
                usb_report_u32(records+8u) == usb_report_u32(observer+REF_EARLY_RX_COUNT) &&
                mask == usb_report_u32(observer+REF_EARLY_RX_MASK) &&
                !usb_report_u32(records+12u);
            printf("{\"test\":\"early-fifo-raw-export\",\"valid\":%s,"
                   "\"packet_mask\":%u,\"complete_bodies_preserved\":%s,"
                   "\"reliability_qualified\":false}\n",early_ok?"true":"false",mask,early_ok?"true":"false");
            if (!early_ok) fputs("Invalid or incomplete early FIFO packet archive\n",stderr);
        }
        ok = burst && timed && frozen && window_ok && early_ok && monitor_ok &&
            summarize(vendor,observer,nonce);
    }
    if (fflush(stdout)) { perror("Reference JSON flush"); ok = 0; }
    if (listener) {
        if (!transfer(listener,0x40u,LIVE_USB_BOOTSEL,NULL,0u)) ok = 0;
        libusb_release_interface(listener,0); libusb_close(listener);
    }
    if (sender) {
        if (!transfer(sender,0x40u,LIVE_USB_BOOTSEL,NULL,0u)) ok = 0;
        libusb_release_interface(sender,0); libusb_close(sender);
    }
    libusb_exit(context);
    return ok ? 0 : 1;
}
