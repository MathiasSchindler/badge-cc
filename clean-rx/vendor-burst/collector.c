#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include "protocol.h"
#include "../src/sdio_ring.h"
#include "build_id.h"
#include "../../src/host/tools/usb_live_transport.h"

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
    for (uint32_t page = 0u; page < WIFI_TRACE_PAGES; ++page) {
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
        fwrite(trace,1u,REF_TRACE_BYTES,out) == REF_TRACE_BYTES;
    if (fclose(out)) written = 0;
    if (!written) { fputs("Reference evidence write failed\n",stderr); return 0; }
    printf("{\"phase\":\"%s\",\"build\":\"%08x\",\"role\":%u,\"stage\":%u,"
           "\"error\":%u,\"last_command\":%u,\"controller_status\":%d}\n",
           suffix,USB_REPORT_BUILD_ID,usb_report_u32(trace+12u),usb_report_u32(trace+4u),
           usb_report_u32(trace+8u),usb_report_u32(trace+16u),(int32_t)usb_report_u32(trace+20u));
    fflush(stdout);
    if (usb_report_u32(trace) != REF_MAGIC || usb_report_u32(trace+12u) != role ||
        usb_report_u32(trace+4u) != phase || usb_report_u32(trace+8u) ||
        usb_report_u32(report+36u)) {
        fputs("Reference device did not complete the requested phase; raw evidence saved\n",stderr);
        return 0;
    }
    return 1;
}

static int summarize(const uint8_t vendor[REF_TRACE_BYTES], const uint8_t observer[REF_TRACE_BYTES],
                      const uint8_t nonce[16]) {
    uint8_t mac[6]; ref_mac(mac,nonce);
    if (memcmp(vendor+32u,mac,6u) || memcmp(observer+32u,mac,6u) ||
        (usb_report_u32(observer+56u)&0x100u) ||
        usb_report_u32(observer+64u) != REF_OBSERVER_ABI) {
        fputs("Reference identity or receive-only configuration mismatch\n",stderr); return 0;
    }
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
    printf("{\"test\":\"vendor-submission-timing\",\"pattern\":\"large-prime-small-pairs\","
           "\"ampdu_mpdu_requested\":%u,\"frames\":%u,\"elapsed_us\":%u,"
           "\"min_write_us\":%u,\"max_write_us\":%u,"
           "\"write_us_buckets\":[%u,%u,%u,%u],\"bucket_upper_us\":[200,400,800,null]}\n",
           REF_AMPDU_LIMIT,timed,elapsed,minimum,maximum,usb_report_u32(timing+16u),
           usb_report_u32(timing+20u),usb_report_u32(timing+24u),usb_report_u32(timing+28u));
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
        int linked = ref_sample_correlated(log,sample,mac);
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
    const uint8_t *early = observer+REF_EARLY_BASE;
    uint32_t early_rxe = ref_u16(early+4u), early_length = early_rxe >= 6u ? early_rxe-6u : 0u;
    int early_linked = ref_early_correlated(log,early,mac);
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
               "\"first_good_hint_rxe_length\":%u,"
               "\"ht_sig_valid\":%s,\"ampdu_format\":%s}\n",
               (unsigned)sig[1]|((unsigned)sig[2]<<8u),sig[0]&127u,
               attributed?"true":"false",ref_u16(r+20u),ref_u16(r+22u),ref_u16(r+28u),
               outcome,usb_report_u32(r+24u),ref_u16(r+30u),ref_u16(r+32u),ref_u16(r+34u),
               ref_u16(r+80u),ref_u16(r+82u),ref_u16(r+84u),
               ref_u16(r+86u),ref_u16(r+88u),ref_u16(r+90u),
               ref_u16(r+92u),
               crc?"true":"false",ampdu?"true":"false");
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
    printf("{\"test\":\"reference-result\",\"frames_submitted\":%u,\"sizes\":[128,256,512,1024],"
           "\"channel\":%u,\"peer_ht_capable\":%s,\"authorized\":%s,"
           "\"observer_tx_enabled\":false,\"saved_headers\":%u,\"overflow\":%u,"
           "\"valid_ht_headers\":%u,\"ampdu_headers\":%u,"
           "\"ampdu_format_observed\":%s,\"correlated_variable_length_mpdus\":%u,"
           "\"correlated_ampdu_format_mpdus\":%u,"
           "\"unaccounted_prefix_exits\":%u,\"active_prefix_at_stop\":%u,"
           "\"early_nonterminal_mpdu_proven\":%s,\"early_multi_candidate_correlated\":%s,"
           "\"phy_header_transmitter_attributed\":%s,"
           "\"vendor_block_ack_activity\":%s,\"complete_multi_mpdu_delivery_proven\":%s,"
           "\"plaintext_over_air_verified\":false,\"sifs_timing_qualified\":false}\n",
           sent,usb_report_u32(vendor+24u),usb_report_u32(vendor+44u)&0x2000u?"true":"false",
           (usb_report_u32(vendor+44u)&0x30u)==0x30u?"true":"false",
           count,overflow,valid,aggregate,aggregate?"true":"false",correlated,correlated_aggregate,
           unaccounted,ref_u16(log+14u),
           early_linked?"true":"false",early_linked && ref_u16(early+20u)==2u?"true":"false",
           correlated || early_linked?"true":"false",ba_activity?"true":"false",
           pair_linked?"true":"false");
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
    if (argc != 5 || !serial_valid(argv[1]) || !serial_valid(argv[2]) || !strcmp(argv[1],argv[2])) {
        fputs("Usage: collector VENDOR_SERIAL OBSERVER_SERIAL PREFIX PRIVATE_SSID_FILE\n",stderr); return 2;
    }
    if (signal(SIGALRM,deadline) == SIG_ERR) { perror("signal"); return 1; }
    alarm(60u);
    uint8_t nonce[16] = {0}, observer_nonce[16], vendor[REF_TRACE_BYTES], observer[REF_TRACE_BYTES];
    FILE *random = fopen("/dev/urandom","rb");
    if (!random) { perror("/dev/urandom"); return 1; }
    int ok = fread(nonce,1u,12u,random) == 12u;
    if (fclose(random)) ok = 0;
    if (!ok) { fputs("Fresh nonce generation failed\n",stderr); return 1; }
    printf("{\"test\":\"configuration\",\"build\":\"%08x\",\"nonce\":\"",USB_REPORT_BUILD_ID);
    for (uint32_t i = 0u; i < sizeof(nonce); ++i) printf("%02x",nonce[i]);
    puts("\",\"mcs_requested\":0}");
    fflush(stdout);
    libusb_context *context = NULL;
    int status = libusb_init(&context);
    if (status) { fprintf(stderr,"USB init: %s\n",libusb_error_name(status)); return 1; }
    libusb_device_handle *sender = open_badge(context,argv[1]), *listener = NULL;
    if (sender) listener = open_badge(context,argv[2]);
    ok = sender && listener && provision(sender,argv[4]) &&
        stage(sender,nonce,1u,1u,argv[3],"vendor-ready",vendor);
    if (ok) {
        memcpy(observer_nonce,nonce,sizeof(nonce));
        observer_nonce[13] = (uint8_t)usb_report_u32(vendor+24u);
        ok = stage(listener,observer_nonce,1u,0u,argv[3],"observer-ready",observer);
    }
    if (ok) {
        int burst = stage(sender,nonce,2u,1u,argv[3],"vendor-burst",vendor);
        int frozen = stage(listener,observer_nonce,2u,0u,argv[3],"observer-capture",observer);
        ok = burst && frozen && summarize(vendor,observer,nonce);
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
