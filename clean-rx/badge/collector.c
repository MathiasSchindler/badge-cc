#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <time.h>
#include "build_id.h"
#include "../../src/host/tools/usb_live_transport.h"
#include "../../src/shared/bytes.h"
#include "../../src/radio/wifi_usb_report.h"
#include "../../src/tools/diagnostics_logic.h"
#include "loader.h"
#include "frames.h"
#include "station.h"
#include "../src/ht20.h"
#include "../peer/frame.h"
#include "../src/wpa2_crypto.h"
#include "station_rx.h"
#include "../src/tx_diag.h"
#include "../src/mac_soak.h"

_Static_assert(!CLEAN_RX_AMPDU_DELIVERY ||
               CLEAN_AMPDU_DMA_PAYLOAD_DETAIL + 3u * CLEAN_HT_AMPDU_PROBE_MPDU <= WIFI_TRACE_SIZE,
               "Aggregate payload evidence must fit the actual USB trace");

static int ap_bssid_decode(const char *text, uint8_t bssid[6]) {
    static const char digits[] = "0123456789abcdef";
    if (strlen(text) != 17u) return 0;
    uint8_t nonzero = 0u;
    for (unsigned i = 0u; i < 6u; ++i) {
        unsigned value = 0u;
        if (i && text[3u * i - 1u] != ':') return 0;
        for (unsigned n = 0u; n < 2u; ++n) {
            unsigned char c = (unsigned char)text[3u * i + n];
            if (c >= 'A' && c <= 'F') c = (unsigned char)(c + ('a' - 'A'));
            const char *digit = strchr(digits, c);
            if (digit == NULL) return 0;
            value = (value << 4u) | (unsigned)(digit - digits);
        }
        bssid[i] = (uint8_t)value;
        nonzero |= bssid[i];
    }
    return nonzero && !(bssid[0] & 1u);
}

static int peer_challenge_decode(const char *hex, uint8_t challenge[14]) {
    if (strlen(hex) != 28u) {
        fputs("Peer challenge must be 28 hexadecimal characters\n", stderr);
        return 0;
    }
    for (unsigned i = 0u; i < 14u; ++i) {
        unsigned value = 0u;
        for (unsigned n = 0u; n < 2u; ++n) {
            char c = hex[2u*i+n];
            unsigned digit;
            if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') digit = (unsigned)(c - 'a') + 10u;
            else if (c >= 'A' && c <= 'F') digit = (unsigned)(c - 'A') + 10u;
            else { fputs("Invalid peer challenge hex\n", stderr); return 0; }
            value = value * 16u + digit;
        }
        challenge[i] = (uint8_t)value;
    }
    return 1;
}

static int inspect_peer(const char *path, const char *hex) {
    uint8_t challenge[14], detail[2048];
    if (!peer_challenge_decode(hex, challenge)) return 0;
    FILE *file = fopen(path, "rb");
    if (file == NULL) { perror(path); return 0; }
    int ok = fread(detail, 1u, sizeof(detail), file) == sizeof(detail) && fgetc(file) == EOF;
    if (fclose(file) != 0) ok = 0;
    if (!ok || usb_report_u32(detail) != CLEAN_DETAIL_MAGIC) {
        fputs("Invalid peer evidence file\n", stderr);
        return 0;
    }
    ok = detail[2000u] == 13u && clean_peer_received(detail, challenge);
    printf("{\"peer_rx_confirmed\":%s,\"peer_frames\":%u,\"channel\":%u}\n",
           ok ? "true" : "false", usb_report_u32(detail + 1996u),
           usb_report_u32(detail + 28u));
    return ok;
}

static void print_tx_diag(const uint8_t detail[2048]) {
    clean_tx_diag_t d;
    memcpy(&d, detail + CLEAN_TX_DIAG_OFFSET, sizeof(d));
    if (d.magic != CLEAN_TX_DIAG_MAGIC) return;
    printf(",\"tx_diagnostic\":{\"duration_ticks\":%u,\"mac_duration_us\":%u,"
           "\"rx_resumed\":%s,",
           d.complete_ticks - d.trigger_ticks, d.mac_complete_us - d.mac_trigger_us,
           d.resume_ticks ? "true" : "false");
    if (d.resume_ticks)
        printf("\"rx_turnaround_ticks\":%u,\"mac_rx_turnaround_us\":%u,",
               d.resume_ticks - d.complete_ticks, d.mac_resume_us - d.mac_complete_us);
    else
        fputs("\"rx_turnaround_ticks\":null,\"mac_rx_turnaround_us\":null,", stdout);
    printf("\"status_before\":\"%08x\",\"status_first\":\"%08x\",\"status_after\":\"%08x\","
           "\"maccontrol\":\"%08x\",\"gci_output\":\"%08x\","
           "\"gci_control\":\"%08x\",\"psm_before\":%u,\"psm_after\":%u,"
           "\"phy_error\":\"%04x\",\"psm_debug\":\"%08x\","
           "\"phy_debug_first\":\"%08x\",\"phy\":{",
           d.status_before, d.status_first, d.status_after,
           d.maccontrol, d.gci_output, d.gci_control, d.psm_before, d.psm_after,
           d.phy_error, d.psm_debug, d.phy_debug_first);
    static const uint16_t phy_regs[CLEAN_TX_PHY_COUNT] = CLEAN_TX_PHY_REGS;
    static const uint16_t radio_regs[8] = CLEAN_TX_RADIO_REGS;
    static const uint16_t ihr_regs[8] = CLEAN_TX_IHR_REGS;
    for (unsigned i = 0u; i < CLEAN_TX_PHY_COUNT; ++i)
        printf("%s\"%03x\":\"%04x\"", i ? "," : "", phy_regs[i], d.phy[i]);
    printf("},\"radio\":{");
    for (unsigned i = 0u; i < 8u; ++i)
        printf("%s\"%02x\":\"%04x\"", i ? "," : "", radio_regs[i], d.radio[i]);
    printf("},\"iq\":[");
    for (unsigned i = 0u; i < 8u; ++i) printf("%s%u", i ? "," : "", d.iq[i]);
    printf("],\"ihr\":{");
    for (unsigned i = 0u; i < 8u; ++i)
        printf("%s\"%03x\":\"%04x\"", i ? "," : "", ihr_regs[i], d.ihr[i]);
    printf("}}");
}

static int inspect(const char *path, const char *credentials) {
    uint8_t detail[WIFI_TRACE_SIZE];
    FILE *file = fopen(path, "rb");
    if (file == NULL) { perror(path); return 0; }
    int ok = fread(detail, 1u, sizeof(detail), file) == sizeof(detail) && fgetc(file) == EOF;
    if (fclose(file) != 0) ok = 0;
    if (!ok || usb_report_u32(detail) != CLEAN_DETAIL_MAGIC) {
        fputs("Invalid evidence file\n", stderr); return 0;
    }
    uint32_t rxe = (uint32_t)detail[522] | ((uint32_t)detail[523] << 8u);
    uint32_t f2 = usb_report_u32(detail + 1088u);
    int raw_valid = rxe >= 6u && rxe <= 510u && clean_frame_valid(detail + 584u, rxe - 6u);
    int f2_valid = f2 >= 18u && f2 <= CLEAN_DETAIL_PACKET_BYTES &&
                   clean_frame_valid(detail + 1122u, f2 - 18u);
    uint8_t ssid[32];
    size_t ssid_length = 0u;
    file = fopen(credentials, "r");
    if (file == NULL) { perror(credentials); return 0; }
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "SSID:", 5u) != 0) continue;
        char *start = line + 5u;
        while (*start == ' ' || *start == '\t') ++start;
        size_t length = strcspn(start, "\r\n");
        while (length && (start[length-1u] == ' ' || start[length-1u] == '\t')) --length;
        if (length == 0u || length > sizeof(ssid)) {
            fputs("Invalid local SSID length\n", stderr); fclose(file); return 0;
        }
        memcpy(ssid, start, length);
        ssid_length = length;
        break;
    }
    int read_error = ferror(file);
    if (fclose(file) != 0) read_error = 1;
    if (read_error || ssid_length == 0u) {
        fputs("Cannot read SSID from local credential file\n", stderr); return 0;
    }
    unsigned matches = 0u;
    uint32_t count = usb_report_u32(detail + 1632u);
    if (count > 8u) { fputs("Invalid beacon count\n", stderr); return 0; }
    for (uint32_t i = 0u; i < count; ++i) {
        const uint8_t *entry = detail + 1640u + 40u*i;
        if (entry[6] == ssid_length && memcmp(entry + 8u, ssid, ssid_length) == 0) ++matches;
    }
    printf("{\"channel\":%u,\"raw_fcs_valid\":%s,\"f2_fcs_valid\":%s,"
           "\"local_ap_matches\":%u,\"probe_response_fcs_valid\":%s,"
           "\"auth_response_fcs_valid\":%s,\"fresh_mac_ack_fcs_valid\":%s,"
           "\"ap_rejection_fcs_valid\":%s,"
           "\"tx_over_air_confirmed\":%s,\"raw_capture_truncated\":%s,"
           "\"f2_capture_truncated\":%s}\n",
           usb_report_u32(detail + 28u), raw_valid ? "true" : "false",
           f2_valid ? "true" : "false", matches,
           f2_valid && detail[1122] == 0x50u &&
           clean_ap_reply(detail + 1122u, f2 - 18u, detail + 1972u,
                                     detail + 1980u, 0u) ? "true" : "false",
           f2_valid && detail[1122] == 0xb0u &&
           clean_ap_reply(detail + 1122u, f2 - 18u, detail + 1972u,
                                     detail + 1980u, 1u) ? "true" : "false",
           f2_valid && clean_ack_reply(detail + 1122u, f2 - 18u, detail + 1972u) ? "true" : "false",
           f2_valid && (detail[1122] == 0xc0u || detail[1122] == 0xa0u) &&
           clean_ap_reply(detail + 1122u, f2 - 18u, detail + 1972u,
                          detail + 1980u, 1u) ? "true" : "false",
           clean_tx_confirmed(detail) ? "true" : "false",
           rxe > 510u ? "true" : "false", f2 > CLEAN_DETAIL_PACKET_BYTES ? "true" : "false");
    return raw_valid && f2_valid;
}

static void print_mac_timing(const uint8_t detail[2048]) {
    static const char *const names[] = {
        "ifs_status", "ifs_backoff", "ifs_idle", "psm_ifs", "psm_tsf", "psm_polls",
        "ack_stage", "ack_frame_control", "ack_rx_status",
        "bad_fcs", "bad_frame_control", "bad_frame_length",
        "ack_elapsed", "ack_window", "response_delay", "ack_dropped",
        "rx_filtered", "rx_capture_max_us"
    };
    _Static_assert(sizeof(clean_mac_trace_status_t) == 40u + 4u * sizeof(names) / sizeof(names[0]),
                   "Trace JSON fields match the shared status layout");
    for (uint32_t i = 0u; i < sizeof(names) / sizeof(names[0]); ++i)
        printf(",\"mac_%s\":%u", names[i],
               usb_report_u32(detail + CLEAN_STATION_TRACE_DETAIL + 40u + 4u * i));
    const uint8_t *completion = detail + CLEAN_TX_COMPLETION_DETAIL;
    printf(",\"mac_tx_completion_report\":%s,\"mac_tx_completion_latched\":%u,"
           "\"mac_tx_completion_preserved\":%u,\"mac_tx_completion_raw\":%u,"
           "\"mac_tx_completion_request\":%u,\"mac_tx_completion_pipeline\":\"%08x\","
           "\"mac_tx_completion_timeouts\":%u",
           usb_report_u32(completion) == CLEAN_TX_COMPLETION_MAGIC ? "true" : "false",
           usb_report_u32(completion + 4u), usb_report_u32(completion + 8u),
           usb_report_u32(completion + 12u), usb_report_u32(completion + 16u),
           usb_report_u32(completion + 20u), usb_report_u32(completion + 24u));
}

static void deadline(int signum) {
    (void)signum;
    static const char text[] = "Collector deadline; badge has independent BOOTSEL recovery\n";
    ssize_t result = write(STDERR_FILENO, text, sizeof(text) - 1u);
    (void)result;
    _exit(124);
}

static int monotonic_us(uint64_t *value) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        perror("Monotonic clock");
        return 0;
    }
    *value = (uint64_t)now.tv_sec * 1000000u + (uint64_t)now.tv_nsec / 1000u;
    return 1;
}

static int collect(libusb_device_handle *device, const char *path, uint8_t channel,
                   uint8_t tx, const uint8_t *challenge, const uint8_t *target,
                   const char *serial, const char *credentials) {
    uint8_t info[12], nonce[16], request[64], report[64], detail[WIFI_TRACE_SIZE];
    if (!transfer(device, 0xc0u, LIVE_USB_INFO, info, sizeof(info))) return 0;
    const uint8_t prefix[] = {'U','2','6','L',1,0,64,0};
    if (memcmp(info, prefix, 8u) || usb_report_u32(info + 8u) != USB_REPORT_BUILD_ID) {
        fputs("Wrong protocol or firmware build\n", stderr);
        return 0;
    }
    FILE *random = fopen("/dev/urandom", "rb");
    if (random == NULL) { perror("/dev/urandom"); return 0; }
    int ok = fread(nonce, 1u, sizeof(nonce), random) == sizeof(nonce);
    if (fclose(random) != 0) ok = 0;
    if (!ok) { fputs("Nonce read failed\n", stderr); return 0; }
    if (challenge != NULL) memcpy(nonce, challenge, 14u);
    if (target != NULL) memcpy(nonce, target, 6u);
    nonce[15] = channel;
    nonce[14] = tx;
    if (tx==18u) {
        char ssid[34], password[65];
        uint8_t secret[64], digest[20];
        FILE *file=fopen(credentials,"r");
        if (!file) { perror("Credentials"); return 0; }
        struct stat permissions;
        if (fstat(fileno(file),&permissions)!=0) {
            perror("Credential permissions"); fclose(file); return 0;
        }
        if (!S_ISREG(permissions.st_mode) || (permissions.st_mode&0077u) ||
            permissions.st_uid!=geteuid()) {
            fputs("Credentials must be an owned, private regular file (chmod 600)\n",stderr);
            fclose(file); return 0;
        }
        int valid=fgets(ssid,sizeof(ssid),file)!=NULL && fgets(password,sizeof(password),file)!=NULL &&
                  fgetc(file)==EOF;
        if (fclose(file)) valid=0;
        uint32_t sn=valid?(uint32_t)strcspn(ssid,"\n"):0u;
        uint32_t pn=valid?(uint32_t)strcspn(password,"\n"):0u;
        valid=valid && sn<=32u && pn<=63u && ssid[sn]=='\n' && password[pn]=='\n' &&
              wpa_pmk((const uint8_t *)password,pn,(const uint8_t *)ssid,sn,secret);
        wpa_wipe(password,sizeof(password));
        if (!valid) {
            wpa_wipe(secret,sizeof(secret));
            fputs("Expected SSID and an 8-63-byte passphrase, one line each\n",stderr); return 0;
        }
        FILE *rng=fopen("/dev/urandom","rb");
        if (!rng) { perror("SNonce random source"); wpa_wipe(secret,sizeof(secret)); return 0; }
        valid=fread(secret+32u,1u,32u,rng)==32u;
        if (fclose(rng)) valid=0;
        if (!valid) { fputs("SNonce read failed\n",stderr); wpa_wipe(secret,sizeof(secret)); return 0; }
        wpa_secret_binding(secret,nonce,nonce[15],digest);
        memcpy(nonce+6u,digest,8u);
        valid=transfer(device,0x40u,LIVE_USB_CLEAN_SECRET,secret,64u);
        wpa_wipe(secret,sizeof(secret)); wpa_wipe(digest,sizeof(digest));
        if (!valid) return 0;
    }
    usb_report_request_for_test(request, USB_REPORT_BUILD_ID, nonce, WIFI_USB_TEST_ID);
    int soak = tx == 19u || tx == 20u;
    uint64_t submit_before = 0u, submit_after = 0u, report_at = 0u;
    if (soak && !monotonic_us(&submit_before)) return 0;
    if (!transfer(device, 0x40u, LIVE_USB_WIFI_PIO_SUBMIT, request, sizeof(request))) return 0;
    if (soak && !monotonic_us(&submit_after)) return 0;
    int got = 0;
    for (unsigned attempt = 0u; attempt < (tx >= 19u ? CLEAN_SOAK_POLLS : 800u); ++attempt) {
        int count = libusb_control_transfer(device, 0xc0u, LIVE_USB_WIFI_REPORT, 0, 0,
                                            report, sizeof(report), 1000u);
        if (count == 64) { got = 1; break; }
        if (count != LIBUSB_ERROR_PIPE) {
            fprintf(stderr, "Report read: %d\n", count);
            return 0;
        }
        usleep(tx >= 19u ? CLEAN_SOAK_POLL_US : 10000u);
    }
    if (!got) { fputs("Report deadline\n", stderr); return 0; }
    if (soak && !monotonic_us(&report_at)) return 0;
    if (usb_report_u32(report) != 0x57363255u || report[4] != 1u ||
        report[5] != 0u || report[6] != 64u || report[7] != 0u ||
        usb_report_u32(report + 8u) != USB_REPORT_BUILD_ID ||
        usb_report_u32(report + 12u) != WIFI_USB_TEST_ID ||
        memcmp(report + 16u, nonce, sizeof(nonce)) ||
        usb_report_u32(report + 60u) != diag_crc32(report, 60u)) {
        fputs("Report binding failed\n", stderr);
        return 0;
    }
    for (uint8_t page = 0u; page < WIFI_TRACE_PAGES; ++page) {
        uint8_t packet[64];
        int count = libusb_control_transfer(device, 0xc0u, LIVE_USB_WIFI_TRACE, page, 0,
                                            packet, sizeof(packet), 1000u);
        if (count != 64 ||
            wifi_trace_page_decode(detail, packet, 64u, report, page) != USB_RECORD_OK) {
            fprintf(stderr, "Bound trace page %u failed\n", page);
            return 0;
        }
    }
    if (usb_report_u32(detail) != CLEAN_DETAIL_MAGIC) {
        fputs("Invalid clean-rx detail format\n", stderr);
        return 0;
    }
    FILE *out = fopen(path, "wb");
    if (out == NULL) { perror(path); return 0; }
    ok = fwrite(detail, 1u, sizeof(detail), out) == sizeof(detail);
    if (fclose(out) != 0) ok = 0;
    if (!ok) { fputs("Evidence write failed\n", stderr); return 0; }
    if (tx == CLEAN_HT_AMPDU_PROBE_TX_MODE || tx == CLEAN_HT_AMPDU_PROBE_RX_MODE) {
        clean_ampdu_probe_trace_t trace;
        memcpy(&trace,detail+CLEAN_AMPDU_PROBE_DETAIL,sizeof(trace));
        int sig = trace.magic == CLEAN_AMPDU_PROBE_MAGIC && trace.count &&
            (trace.phy & 3u) == 2u && trace.raw[2u] == nonce[13] &&
            clean_ht_ampdu_probe_sig_valid(trace.raw+2u,CLEAN_HT_AMPDU_PROBE_BYTES);
        uint32_t mask = clean_ht_ampdu_probe_trace_mask(&trace,nonce);
        int single = mask && trace.count == 1u ?
            clean_ht_ampdu_probe_sequence(trace.raw+8u,80u,nonce) : -1;
        int completed = !usb_report_u32(detail+8u) && usb_report_u32(detail+36u)==10u;
        int submitted = completed && usb_report_u32(detail+1092u)==1u &&
            usb_report_u32(detail+1096u)==1u;
        int received = completed && mask;
        uint32_t reported_delivered = usb_report_u32(detail+CLEAN_AMPDU_DMA_DETAIL+4u);
        uint32_t delivered = 0u;
        int delivered_valid = CLEAN_RX_AMPDU_DELIVERY && completed &&
            usb_report_u32(detail+CLEAN_AMPDU_DMA_DETAIL)==CLEAN_AMPDU_DMA_MAGIC;
        if (delivered_valid)
            delivered=clean_ht_ampdu_delivery_mask(detail+CLEAN_AMPDU_DMA_PAYLOAD_DETAIL,
                3u*CLEAN_HT_AMPDU_PROBE_MPDU,reported_delivered,nonce);
        delivered_valid = delivered_valid && reported_delivered==delivered;
        printf("{\"serial\":\"%s\",\"build\":\"%08x\",\"mode\":\"ampdu-probe-%s\","
               "\"channel\":%u,\"mcs\":%u,\"error\":%u,\"fw_error\":%u,"
               "\"fw_error_addr\":%u,\"fw_error_value\":%u,\"local_tx_submission\":%s,"
               "\"fresh_radio_capture\":%s,\"terminal_aggregate_sig\":%s,"
               "\"aggregate_observations\":%u,\"rxe_length\":%u,\"outcome\":%u,"
               "\"status1\":%u,\"status2\":%u,\"phy\":%u,"
               "\"subframe_status2\":[%u,%u,%u],"
               "\"retained_subframe_bitmap\":%u,\"standalone_subframe\":%d,"
               "\"chipcommon_id\":%u,\"actual_multi_mpdu_delivery\":%s,"
               "\"all_subframes_delivered\":%s,"
               "\"dma_experiment_enabled\":%s,\"shm_delivery_enabled\":%s,"
               "\"delivered_subframe_bitmap\":%u,"
               "\"reported_delivered_subframe_bitmap\":%u,\"delivery_payloads_valid\":%s,"
               "\"delivered_subframes\":%u,\"delivered_duplicates\":%u,"
               "\"multi_mpdu_radio_capture\":%s,"
               "\"fixture_bad_fcs_mask\":%u,"
               "\"immediate_ba\":false,\"sifs_timing_qualified\":false,\"ht_qualified\":false",
               serial,USB_REPORT_BUILD_ID,
               tx==CLEAN_HT_AMPDU_PROBE_TX_MODE?"tx":"rx",channel,nonce[13],
               usb_report_u32(detail+8u),usb_report_u32(detail+44u),
               usb_report_u32(detail+48u),usb_report_u32(detail+52u),
               submitted?"true":"false",received?"true":"false",
               sig?"true":"false",trace.count,trace.length,trace.outcome,
               trace.status1,trace.status2,trace.phy,
               trace.raw[264u]|((uint32_t)trace.raw[265u]<<8u),
               trace.raw[266u]|((uint32_t)trace.raw[267u]<<8u),
               trace.raw[268u]|((uint32_t)trace.raw[269u]<<8u),
               mask,single,usb_report_u32(detail+680u),
               delivered_valid && (delivered & (delivered-1u)) ? "true" : "false",
               delivered_valid && delivered==7u ? "true" : "false",
               CLEAN_RX_AMPDU_DMA ? "true" : "false",
               CLEAN_RX_AMPDU_SHM ? "true" : "false",
               delivered,reported_delivered,delivered_valid ? "true" : "false",
               usb_report_u32(detail+CLEAN_AMPDU_DMA_DETAIL+8u),
               usb_report_u32(detail+CLEAN_AMPDU_DMA_DETAIL+12u),
               received && (mask & (mask-1u)) ? "true" : "false",
               CLEAN_RX_AMPDU_BAD_FCS);
#if CLEAN_RX_AMPDU_DMA
        printf(",\"last_dma_publication_delay_us\":%u",
               trace.raw[270u]|((uint32_t)trace.raw[271u]<<8u));
        printf(",\"dma_fault_prefix\":");
        if (usb_report_u32(detail+736u)==CLEAN_AMPDU_DMA_MAGIC) {
            printf("\"");
            for (uint32_t i=0u;i<128u;++i) printf("%02x",detail[1480u+i]);
            printf("\"");
        } else printf("null");
#endif
#if CLEAN_RX_AMPDU_SHM
        const uint8_t *terminal=detail+CLEAN_AMPDU_IF_DETAIL+4u+
            2u*CLEAN_AMPDU_SHM_STATUS_WORD;
        uint16_t terminal_words[CLEAN_AMPDU_SHM_STATUS_WORDS];
        memcpy(terminal_words,terminal,sizeof(terminal_words));
        int terminal_valid=completed &&
            usb_report_u32(detail+CLEAN_AMPDU_IF_DETAIL)==CLEAN_AMPDU_IF_MAGIC &&
            terminal_words[2]==1u && terminal_words[3]==1u &&
            terminal_words[4]==CLEAN_AMPDU_SHM_HELD_STATE &&
            terminal_words[5]==1u;
        printf(",\"shm_terminal\":{\"valid\":%s,\"published_count\":%u,"
               "\"released_count\":%u,\"claim_state\":%u,\"claimed_count\":%u,"
               "\"published_tsf\":%u,\"released_tsf\":%u,\"hold_us\":%u}",
               terminal_valid?"true":"false",terminal_words[2],
               terminal_words[3],terminal_words[4],
               terminal_words[5],usb_report_u32(terminal),
               usb_report_u32(terminal+16u),usb_report_u32(terminal+12u));
#endif
#if CLEAN_RX_AMPDU_BA
        const uint8_t *ba=detail+CLEAN_AMPDU_IF_DETAIL+4u+160u;
        uint16_t ba_words[CLEAN_AMPDU_BA_WORDS];
        memcpy(ba_words,ba,sizeof(ba_words));
        int returned_ba=usb_report_u32(detail+CLEAN_AMPDU_BA_RX_DETAIL)==CLEAN_AMPDU_BA_RX_MAGIC &&
            clean_ht_ampdu_ba_match(detail+CLEAN_AMPDU_BA_RX_DETAIL+8u,32u,nonce);
        printf(",\"ba_experiment\":{\"enabled\":%s,\"bitmap\":%u,\"attempts\":%u,"
               "\"started\":%u,\"errors\":%u,\"observed_eof_tsf\":%u,"
               "\"tx_active_tsf\":%u,\"tx_end_tsf\":%u,\"eof_status\":%u,"
               "\"tx_status\":%u,\"returned_ba\":%s,\"returned_bitmap\":%u}",
               ba_words[0]?"true":"false",ba_words[1],ba_words[2],ba_words[3],ba_words[4],
               usb_report_u32(ba+10u),usb_report_u32(ba+14u),usb_report_u32(ba+18u),
               ba_words[11],ba_words[12],returned_ba?"true":"false",
               returned_ba?detail[CLEAN_AMPDU_BA_RX_DETAIL+28u]:0u);
#endif
#if CLEAN_RX_AMPDU_INTERFACE
        clean_ampdu_interface_t interface;
        memcpy(&interface,detail+CLEAN_AMPDU_IF_DETAIL,sizeof(interface));
        int valid=interface.magic==CLEAN_AMPDU_IF_MAGIC &&
            (((interface.mode==1u || interface.mode==2u) &&
              interface.count==CLEAN_AMPDU_IF_RECORDS) ||
             (interface.mode==3u && interface.count && interface.count<=3u));
        printf(",\"interface\":{\"valid\":%s,\"mode\":%u,\"count\":%u,"
               "\"prefix_tsf\":%u,\"prefix_count\":%u,\"samples\":[",
               valid?"true":"false",interface.mode,interface.count,
               interface.prefix_tsf,interface.prefix_count);
        uint32_t count=interface.magic==CLEAN_AMPDU_IF_MAGIC &&
            interface.count<=CLEAN_AMPDU_IF_RECORDS ? interface.count : 0u;
        for (uint32_t i=0u;i<count;++i) {
            const uint16_t *r=interface.record[i];
            printf("%s[%u,%u,%u,%u,%u,%u,%u,%u]",i?",":"",
                (uint16_t)(r[0]-interface.prefix_tsf),r[1],r[2],r[3],r[4],r[5],r[6],r[7]);
        }
        uint8_t expected[CLEAN_HT_AMPDU_PROBE_MPDU];
        uint32_t available=interface.tail[0]>=6u && interface.tail[0]<=86u ?
            interface.tail[0]-6u : 0u, matched=0u, shifted=0u;
        if (interface.magic==CLEAN_AMPDU_IF_MAGIC && interface.tail[5]==2u &&
            clean_ht_ampdu_probe_mpdu(expected,nonce,2u)) {
            while (matched<available && interface.tail_raw[8u+matched]==expected[matched])
                ++matched;
            while (shifted<available && shifted<76u &&
                   interface.tail_raw[8u+shifted]==expected[4u+shifted]) ++shifted;
        }
        printf("],\"tail\":{\"rxe_length\":%u,\"status1\":%u,\"status2\":%u,"
               "\"phy\":%u,\"elapsed_us\":%u,\"outcome\":%u,"
               "\"matching_sequence2_bytes\":%u,\"matching_sequence2_shift4_bytes\":%u}",
               interface.tail[0],interface.tail[1],interface.tail[2],interface.tail[3],
               interface.tail[5]==2u ?
                   (uint16_t)(interface.tail[4]-interface.prefix_tsf) : 0u,
               interface.tail[5],matched,shifted);
#if CLEAN_RX_AMPDU_HANDOFF == 1
        const uint8_t *evidence=detail+CLEAN_AMPDU_IF_DETAIL+4u;
        printf(",\"handoff\":[");
        for (uint32_t i=0u;i<3u;++i) {
            const uint8_t *p=evidence+2u*(CLEAN_AMPDU_HANDOFF_WORD+
                CLEAN_AMPDU_HANDOFF_WORDS*i);
            uint16_t words[CLEAN_AMPDU_HANDOFF_WORDS];
            memcpy(words,p,sizeof(words));
            uint16_t committed;
            memcpy(&committed,evidence+2u*(CLEAN_AMPDU_HANDOFF_COMMIT+i),
                   sizeof(committed));
            printf("%s{\"committed\":%s,\"elapsed_us\":%u,\"rxe_count\":%u,"
                   "\"base\":%u,\"copy_end\":%u,\"copy_control\":%u,\"fifo\":%u,"
                   "\"rdf\":%u,\"status2\":%u,\"raw_words\":[",
                   i?",":"",interface.magic==CLEAN_AMPDU_IF_MAGIC &&
                   committed==1u?"true":"false",
                   committed==1u?(uint16_t)(words[0]-interface.prefix_tsf):0u,
                   words[1],words[2],words[3],words[4],words[5],words[6],words[7]);
            for (uint32_t j=0u;j<12u;++j) printf("%s%u",j?",":"",words[8u+j]);
            printf("]}");
        }
        printf("]");
#endif
        printf("}");
#endif
        puts("}");
        return tx==CLEAN_HT_AMPDU_PROBE_TX_MODE ? submitted : received;
    }
    if (tx == CLEAN_PEER_RX_MODE) {
        clean_ht_rx_t rx;
        clean_ht_diag_t diagnostic;
        memcpy(&rx, detail + CLEAN_HT_DETAIL, sizeof(rx));
        memcpy(&diagnostic, detail + CLEAN_HT_DIAG_DETAIL, sizeof(diagnostic));
        uint32_t length = rx.rxe_length >= 6u ? rx.rxe_length - 6u : 0u;
        int sig = rx.magic == CLEAN_HT_RX_MAGIC && rx.captures &&
            (rx.phy[0] & 3u) == 2u && rx.sig[0] == nonce[13] &&
            clean_ht20_rx_sig_valid(rx.sig, length);
        int trace = sig && length <= sizeof(rx.mpdu) &&
            clean_peer_mpdu_sequence(rx.mpdu, length, nonce) >= 0;
        uint32_t saved = usb_report_u32(detail + 1088u);
        int packet = saved >= 18u && saved <= CLEAN_DETAIL_PACKET_BYTES &&
            clean_ht_rx_metadata(detail[1119u], detail[1120u], detail[1121u],
                                 saved - 18u, channel, nonce[13]) &&
            clean_peer_mpdu_sequence(detail + 1122u, saved - 18u, nonce) >= 0;
        uint32_t count = usb_report_u32(detail + 1996u);
        int completed = !usb_report_u32(detail + 8u) && usb_report_u32(detail + 36u) == 10u;
        int fresh = completed && count && packet && trace &&
            usb_report_u32(detail + CLEAN_PEER_RX_DETAIL) == CLEAN_PEER_MAGIC;
        printf("{\"serial\":\"%s\",\"build\":\"%08x\",\"mode\":\"vendor-ht-rx\","
               "\"channel\":%u,\"mcs\":%u,\"error\":%u,\"fresh_ht_rx\":%s,"
               "\"fresh_frames\":%u,\"sequence_bitmap\":%u,\"duplicates\":%u,"
               "\"fresh_terminal_trace\":%s,\"terminal_sig_valid\":%s,"
               "\"terminal_length\":%u,\"terminal_captures\":%u,"
               "\"discard_bad_fcs\":%u,\"discard_oversize\":%u,"
               "\"discard_rx_event\":%u,\"discard_plcp\":%u,"
               "\"early_starts\":%u,\"early_outcome\":%u,\"early_final_count\":%u,"
               "\"ht_interoperability_qualified\":false}\n",
               serial, USB_REPORT_BUILD_ID, channel, nonce[13],
               usb_report_u32(detail + 8u), fresh ? "true" : "false", count,
               usb_report_u32(detail + CLEAN_PEER_RX_DETAIL + 8u),
               usb_report_u32(detail + CLEAN_PEER_RX_DETAIL + 12u),
               trace ? "true" : "false", sig ? "true" : "false", length, rx.captures,
               usb_report_u32(detail + 492u), usb_report_u32(detail + 496u),
               usb_report_u32(detail + 500u), usb_report_u32(detail + 504u),
               diagnostic.early.starts, diagnostic.early.outcome, diagnostic.early.final_count);
        return fresh;
    }
    if (tx >= 21u && tx <= 28u) {
        int data_mode = tx >= 24u;
        int ack_mode = tx >= 26u;
        int clocked = tx == 28u;
        uint32_t expected_length = data_mode ? clean_ht_data_length(nonce) : CLEAN_HT_PROBE_MPDU;
        clean_ht20_config_t config = {0};
        if (tx!=23u && !clean_ht20_configure(&config,expected_length,nonce[13],CLEAN_HT20_MCS_MASK)) {
            fputs("Invalid requested HT20 configuration\n",stderr);
            return 0;
        }
        uint8_t expected[CLEAN_HT_DATA_STORAGE];
        uint32_t expected_fcs = 0u;
        if (data_mode) {
            if (!(ack_mode ? clean_ht_ack_frame(expected, nonce) :
                             clean_ht_data_frame(expected, nonce))) {
                fputs("Invalid HT data challenge/profile\n", stderr);
                return 0;
            }
            expected_fcs = usb_report_u32(expected + 14u + expected_length - 4u);
        }
        clean_ht_rx_t rx;
        memcpy(&rx, detail + CLEAN_HT_DETAIL, sizeof(rx));
        int trace_fresh = tx == 22u && clean_ht_probe_rx_valid(&rx, nonce);
        if (data_mode) trace_fresh = tx == 25u && clean_ht_data_trace_valid(&rx, nonce);
        if (ack_mode) trace_fresh = tx == 27u &&
            rx.magic == CLEAN_HT_RX_MAGIC && rx.captures &&
            (rx.phy[0] & 3u) == 2u && rx.rxe_length == expected_length + 6u &&
            rx.sig[0] == nonce[13] && clean_ht20_rx_sig_valid(rx.sig, expected_length) &&
            clean_ht_ack_match(rx.mpdu, expected_length, nonce);
        int fresh = trace_fresh &&
            usb_report_u32(detail + 1996u) &&
            usb_report_u32(detail + 1088u) == 18u + CLEAN_HT_PROBE_MPDU &&
            clean_ht_probe_match(detail + 1122u, CLEAN_HT_PROBE_MPDU, nonce);
        if (data_mode) fresh = (tx == 25u || tx == 27u) &&
            usb_report_u32(detail + 680u) == 0x31524448u &&
            usb_report_u32(detail + 684u) == expected_length &&
            usb_report_u32(detail + 688u) == nonce[13] &&
            usb_report_u32(detail + 692u) == expected_fcs &&
            usb_report_u32(detail + 1996u) &&
            usb_report_u32(detail + 1088u) == 18u + expected_length &&
            clean_ht_rx_metadata(detail[1119u], detail[1120u], detail[1121u],
                                 expected_length, channel, nonce[13]) &&
            !memcmp(detail + 1122u, expected + 14u,
                    expected_length < CLEAN_DETAIL_PACKET_BYTES - 18u ?
                    expected_length : CLEAN_DETAIL_PACKET_BYTES - 18u);
        int completed = !usb_report_u32(detail + 8u) &&
            usb_report_u32(detail + 36u) == 10u;
        int submitted = completed && usb_report_u32(detail + 1096u);
        int returned_ack = ack_mode && tx == 26u && completed &&
            usb_report_u32(detail + 1992u) &&
            usb_report_u32(detail + 1088u) == 32u &&
            clean_ack_reply(detail + 1122u, 14u, expected + 24u);
        clean_ack_rx_t ack_rx = {0};
        clean_unicast_status_t ack_status = {0};
        if (clocked) {
            memcpy(&ack_rx, detail + CLEAN_ACK_RX_DETAIL, sizeof(ack_rx));
            memcpy(&ack_status, detail + 1000u, sizeof(ack_status));
            returned_ack = completed && submitted &&
                ack_status.magic == CLEAN_UNICAST_MAGIC &&
                ack_status.result == CLEAN_UNICAST_ACKED &&
                ack_status.attempts == 1u && ack_status.ack_received == 1u &&
                clean_ht_ack_evidence(&ack_rx, nonce);
        }
        uint32_t mpdu_length = rx.rxe_length >= 6u ? rx.rxe_length - 6u : 0u;
        int sig_valid = rx.magic == CLEAN_HT_RX_MAGIC && rx.captures &&
            (rx.phy[0] & 3u) >= 2u && clean_ht20_rx_sig_valid(rx.sig, mpdu_length);
        int fcs_valid = sig_valid && mpdu_length <= sizeof(rx.mpdu) &&
            clean_frame_valid(rx.mpdu, mpdu_length);
        printf("{\"serial\":\"%s\",\"build\":\"%08x\",\"mode\":\"%s\","
               "\"role\":\"%s\",\"channel\":%u,\"mcs\":%u,\"error\":%u,"
               "\"api_version\":%u,\"expected_mpdu_bytes\":%u,\"full_payload_checked_on_badge\":%s,"
               "\"fw_error\":%u,\"tx_submitted\":%s,\"fresh_ht_rx\":%s,\"fresh_ht_trace\":%s,"
               "\"rx_ht20_sig_valid\":%s,\"rx_mpdu_fcs_valid\":%s,\"rx_mcs\":%u,"
               "\"tx_over_air_confirmed\":false,\"ht_interoperability_qualified\":false,"
               "\"rx_captures\":%u,\"rxe_length\":%u,\"phy\":[%u,%u,%u,%u],\"sig\":\"",
               serial, USB_REPORT_BUILD_ID, ack_mode ? "ht-ack" : data_mode ? "ht-data" : "ht-probe",
               tx == 21u || tx == 24u || tx == 26u || clocked ? "tx" :
               tx == 22u || tx == 25u || tx == 27u ? "rx" : "passive",
               channel, tx == 23u ? 0u : nonce[13],
               usb_report_u32(detail + 8u), clocked ? CLEAN_HT_CLOCKED_VERSION :
                   ack_mode ? CLEAN_HT_ACK_VERSION : data_mode ?
                   (expected_length == CLEAN_AMSDU_MPDU_BYTES ?
                    CLEAN_HT_AMSDU_VERSION : CLEAN_HT_DATA_VERSION) : 0u,
               expected_length, data_mode && fresh ? "true" : "false",
               usb_report_u32(detail + 44u),
               submitted ? "true" : "false", fresh && completed ? "true" : "false",
               trace_fresh && completed ? "true" : "false",
               sig_valid ? "true" : "false", fcs_valid ? "true" : "false", rx.sig[0] & 127u,
               rx.captures, rx.rxe_length, rx.phy[0], rx.phy[1], rx.phy[2], rx.phy[3]);
        for (uint32_t i = 0u; i < 6u; ++i) printf("%02x", rx.sig[i]);
        printf("\",\"challenge\":\"");
        for (uint32_t i = 0u; i < 14u; ++i) printf("%02x", nonce[i]);
        printf("\"");
        if (tx!=23u) {
            printf(",\"ht20_format\":\"mixed\",\"ht20_width_mhz\":%u,"
                   "\"ht20_gi_ns\":%u,\"ht20_streams\":%u,"
                   "\"ht20_rate_kbps\":%u,\"ht20_symbols\":%u,"
                   "\"ht20_expected_airtime_us\":%u,\"ht20_lsig_length\":%u",
                   CLEAN_HT20_WIDTH_MHZ,CLEAN_HT20_GI_NS,CLEAN_HT20_STREAMS,
                   config.rate_kbps,config.symbols,config.airtime_us,config.controls.mmplcp[1]);
        }
        if (ack_mode)
            printf(",\"response_configured\":%s,\"ack_responses_started\":%u,"
                   "\"ack_response_errors\":%u,\"psm_response_delay_us\":%u,\"fresh_return_ack\":%s,"
                   "\"sifs_timing_qualified\":false",
                   usb_report_u32(detail + 704u) ? "true" : "false",
                   usb_report_u32(detail + 696u), usb_report_u32(detail + 700u),
                   usb_report_u32(detail + 708u),
                   returned_ack ? "true" : "false");
        if (clocked) {
            clean_mac_radio_status_t radio;
            clean_mac_trace_status_t trace;
            clean_dcf_trace_t dcf;
            memcpy(&radio, detail + 800u, sizeof(radio));
            memcpy(&trace, detail + 864u, sizeof(trace));
            memcpy(&dcf, detail + CLEAN_DCF_TRACE_DETAIL, sizeof(dcf));
            printf(",\"clocked_tx\":true,\"ack_result\":%u,\"tx_attempts\":%u,"
                   "\"ack_capture_count\":%u,\"ack_rx_time_us\":%u,\"ack_phy\":%u,"
                   "\"access_deferrals\":%u,\"access_cw\":%u,\"access_slots\":%u,"
                   "\"access_nav_until\":%u,\"access_policy_pending\":%u,"
                   "\"access_ifs\":%u,\"access_psm_ifs\":%u,\"access_psm_polls\":%u,"
                   "\"dcf_trace_valid\":%s,\"dcf_draw\":%u,\"dcf_before\":%u,"
                   "\"dcf_after\":%u,\"dcf_clock\":%u,\"dcf_guard_latched\":%u,"
                   "\"dcf_final_slots\":%u,\"ack_mpdu\":\"",
                   ack_status.result, ack_status.attempts, ack_rx.count,
                   (uint32_t)ack_rx.time_lo | ((uint32_t)ack_rx.time_hi << 16u), ack_rx.phy,
                   radio.deferrals, radio.cw, radio.slots, radio.nav_until, radio.policy_pending,
                   trace.ifs_status, trace.psm_ifs, trace.psm_polls,
                   dcf.magic == CLEAN_DCF_TRACE_MAGIC ? "true" : "false",
                   dcf.draw, dcf.before, dcf.after, dcf.clock, dcf.fault, dcf.slots);
            const uint8_t *m = (const uint8_t *)ack_rx.mpdu;
            for (uint32_t i = 0u; i < 14u; ++i) printf("%02x", m[i]);
            printf("\",\"ack_plcp\":\"");
            const uint8_t *plcp = (const uint8_t *)ack_rx.plcp;
            for (uint32_t i = 0u; i < 6u; ++i) printf("%02x", plcp[i]);
            printf("\"");
        }
        if (ack_mode) {
            clean_ht_failure_t failure;
            memcpy(&failure, detail + CLEAN_HT_FAILURE_DETAIL, sizeof(failure));
            const uint8_t *raw = (const uint8_t *)failure.raw;
            const uint8_t *repeated = (const uint8_t *)failure.repeated_raw;
            int valid = failure.magic == CLEAN_HT_FAILURE_MAGIC &&
                        failure.count && failure.length <= 66u;
            int complete = valid && failure.length == 66u;
            int candidate = complete && raw[0] == nonce[13] &&
                clean_ht20_rx_sig_valid(raw, CLEAN_HT_ACK_MPDU) &&
                !memcmp(raw + 10u, expected + 18u, 12u);
            printf(",\"failure_record_valid\":%s,\"failure_count\":%u,\"failure_cause\":%u,"
                   "\"failure_length\":%u,\"failure_phy\":%u,\"failure_status1\":%u,"
                   "\"failure_status2\":%u,\"failure_candidate_match\":%s,"
                   "\"failure_fcs_valid\":%s,\"failure_raw_repeat_match\":%s,"
                   "\"failure_final_length\":%u,\"failure_final_status1\":%u,"
                   "\"failure_final_status2\":%u,\"failure_final_phy\":%u,"
                   "\"failure_time_us\":%u",
                   failure.magic == CLEAN_HT_FAILURE_MAGIC ? "true" : "false",
                   failure.count, failure.cause, failure.length, failure.phy,
                   failure.status1, failure.status2, candidate ? "true" : "false",
                   complete && diag_crc32(raw + 6u, 56u) == usb_report_u32(raw + 62u) ?
                       "true" : "false",
                   valid && failure.length && !memcmp(raw, repeated, failure.length) ?
                       "true" : "false",
                   failure.final_length, failure.final_status1, failure.final_status2,
                   failure.final_phy, (uint32_t)failure.time_lo | ((uint32_t)failure.time_hi << 16u));
            if (complete) {
                uint32_t bit_errors = 0u;
                for (uint32_t i = 0u; i < CLEAN_HT_ACK_MPDU; ++i) {
                    uint8_t different = raw[6u+i] ^ expected[14u+i];
                    for (; different; different &= (uint8_t)(different - 1u)) ++bit_errors;
                }
                printf(",\"failure_fixture_bit_errors\":%u", bit_errors);
            }
            printf(",\"failure_raw\":\"");
            if (valid)
                for (uint32_t i = 0u; i < failure.length; ++i) printf("%02x", raw[i]);
            printf("\",\"failure_repeated_raw\":\"");
            if (valid)
                for (uint32_t i = 0u; i < failure.length; ++i) printf("%02x", repeated[i]);
            printf("\"");
        }
        clean_ht_diag_t diagnostic;
        memcpy(&diagnostic, detail + CLEAN_HT_DIAG_DETAIL, sizeof(diagnostic));
        uint32_t early_length = (uint32_t)diagnostic.early.sig[1] |
                                ((uint32_t)diagnostic.early.sig[2] << 8u);
        printf(",\"ht_diag_valid\":%s,\"early_candidates\":%u,\"early_phy\":%u,"
               "\"early_ht20_sig_valid\":%s,"
               "\"early_initial_count\":%u,\"early_final_count\":%u,"
               "\"early_status1\":%u,\"early_status2\":%u,\"early_outcome\":%u,"
               "\"discard_bad_fcs\":%u,\"discard_oversize\":%u,"
               "\"discard_rx_event\":%u,\"discard_plcp\":%u,"
               "\"tx_controls\":[%u,%u,%u,%u],\"ht_sig_template_crc_zeroed\":%s,\"early_sig\":\"",
               diagnostic.magic == CLEAN_HT_DIAG_MAGIC ? "true" : "false",
               diagnostic.early.starts, diagnostic.early.phy,
               diagnostic.magic == CLEAN_HT_DIAG_MAGIC &&
                   clean_ht20_rx_sig_valid(diagnostic.early.sig, early_length) ? "true" : "false",
               diagnostic.early.initial_count, diagnostic.early.final_count,
               diagnostic.early.status1, diagnostic.early.status2, diagnostic.early.outcome,
               usb_report_u32(detail + 492u), usb_report_u32(detail + 496u),
               usb_report_u32(detail + 500u), usb_report_u32(detail + 504u),
               diagnostic.tx.phyctl, diagnostic.tx.phyctl1,
               diagnostic.tx.mmplcp[0], diagnostic.tx.mmplcp[1],
               diagnostic.template_crc_zeroed ? "true" : "false");
        for (uint32_t i = 0u; i < 6u; ++i) printf("%02x", diagnostic.early.sig[i]);
        printf("\"");
        print_tx_diag(detail);
        int edca_valid = 1;
        if (clocked && CLEAN_RX_EDCA) {
            clean_edca_status_t edca;
            memcpy(&edca, detail + CLEAN_EDCA_DETAIL, sizeof(edca));
            static const uint8_t ac[8] = {0u,1u,1u,0u,2u,2u,3u,3u};
            static const uint16_t minimum[4] = {15u,15u,7u,3u};
            static const uint16_t maximum[4] = {1023u,1023u,15u,7u};
            static const uint16_t aifs[4] = {37u,73u,28u,28u};
            static const uint16_t txop[4] = {0u,0u,94u,47u};
            uint32_t tid = nonce[6] & 15u;
            edca_valid = edca.magic == CLEAN_EDCA_STATUS_MAGIC && edca.enabled == 1u &&
                tid < 8u && edca.tid == tid && edca.ac == ac[tid] &&
                edca.cw_min == minimum[edca.ac] && edca.cw_max == maximum[edca.ac] &&
                edca.aifs_us == aifs[edca.ac] && edca.txop_32us == txop[edca.ac] &&
                !edca.reserved;
            printf(",\"edca_valid\":%s,\"edca_ac\":%u,\"edca_tid\":%u,"
                   "\"edca_cw_min\":%u,\"edca_cw_max\":%u,\"edca_aifs_us\":%u,"
                   "\"edca_txop_32us\":%u",
                   edca_valid ? "true" : "false", edca.ac, edca.tid,
                   edca.cw_min, edca.cw_max, edca.aifs_us, edca.txop_32us);
        }
        puts("}");
        int valid = tx == 21u || tx == 24u || tx == 26u || clocked ? submitted :
            completed && (tx == 22u || tx == 25u || tx == 27u ? fresh : fcs_valid);
        if (tx == 27u) valid = valid && trace_fresh &&
            usb_report_u32(detail + 704u) && usb_report_u32(detail + 696u) == 1u &&
            !usb_report_u32(detail + 700u);
        if (tx == 26u || clocked) {
            valid = valid && returned_ack && edca_valid;
            if (!valid)
                fputs("HT ACK exchange unconfirmed: local TX/response counters do not replace a fresh returned ACK\n",
                      stderr);
        }
        if (!valid)
            fputs("HT probe did not establish the requested local submission or fresh RX evidence\n", stderr);
        return valid;
    }
    if (tx == 19u || tx == 20u) {
        clean_soak_status_t s;
        memcpy(&s, detail + CLEAN_SOAK_DETAIL, sizeof(s));
        clean_soak_fragment_status_t fragments;
        memcpy(&fragments, detail + CLEAN_SOAK_FRAGMENT_DETAIL, sizeof(fragments));
        clean_soak_progress_t progress;
        memcpy(&progress, detail + CLEAN_SOAK_PROGRESS_DETAIL, sizeof(progress));
        clean_ack_test_status_t ack_test;
        memcpy(&ack_test, detail + CLEAN_ACK_TEST_DETAIL, sizeof(ack_test));
        clean_ack_rx_t ack_rx;
        memcpy(&ack_rx, detail + CLEAN_SOAK_ACK_DETAIL, sizeof(ack_rx));
        clean_ack_observe_t observed;
        memcpy(&observed, detail + CLEAN_ACK_OBSERVE_DETAIL, sizeof(observed));
        clean_ack_arith_t arith;
        memcpy(&arith, detail + CLEAN_ACK_ARITH_DETAIL, sizeof(arith));
        clean_unicast_status_t unicast;
        memcpy(&unicast, detail + 1000u, sizeof(unicast));
        uint8_t own[6];
        clean_soak_address(own, nonce, tx == 19u ? 1u : 2u);
        int ack_valid = clean_ack_rx_valid(&ack_rx, own, &unicast,
            clean_soak_phy(nonce) ? 15u : 0x150u, clean_soak_rate(nonce),
            clean_soak_short(nonce));
        int progress_valid = progress.magic == CLEAN_SOAK_PROGRESS_MAGIC &&
            progress.phase == CLEAN_SOAK_FINISHED && progress.ready_us &&
            progress.first_send_us >= progress.ready_us &&
            progress.first_result_us >= progress.first_send_us &&
            progress.first_rx_us >= progress.ready_us &&
            (progress.ref_after & 3u) == 2u && progress.tick_cycles_after == 12u &&
            progress.sys_khz_q5 && !progress.snapshot_stop_error &&
            progress.snapshot_flags == (CLEAN_SOAK_SNAPSHOT_FROZEN |
                CLEAN_SOAK_SNAPSHOT_MAILBOX | CLEAN_SOAK_SNAPSHOT_FULL);
        uint32_t fragment_count = clean_soak_fragments(nonce);
        uint32_t role = tx == 19u ? 1u : 2u;
        int fragment_valid = fragments.magic == CLEAN_SOAK_FRAGMENT_MAGIC &&
            fragments.fragments_per_msdu == fragment_count &&
            fragments.tx_mpdu_acked == s.target * fragment_count;
        if (fragment_count > 1u) {
            fragment_valid = fragment_valid && fragments.reassembled == s.target &&
                fragments.rx_mpdu_checked >= s.target * fragment_count &&
                fragments.last_index == s.target - 1u &&
                fragments.fragment_bitmap == (1u << fragment_count) - 1u &&
                !(fragments.retry_bitmap & ~fragments.fragment_bitmap);
            uint8_t expected[CLEAN_TX_STORAGE_BYTES];
            for (uint32_t f = 0u; f < fragment_count; ++f) {
                uint32_t length = clean_soak_fragment_frame(expected, nonce,
                    (uint8_t)(3u - role), fragments.last_index, f);
                if (length && (fragments.retry_bitmap & (1u << f)) &&
                    !clean_mac_retry(expected + 6u, length - 6u)) length = 0u;
                fragment_valid = fragment_valid && length &&
                    fragments.fragment_fcs[f] == usb_report_u32(expected + length - 4u);
            }
            uint32_t length = clean_soak_fragment_frame(expected, nonce,
                (uint8_t)(3u - role), fragments.last_index, 0u);
            uint8_t duration[2] = {expected[8], expected[9]};
            length = length ? clean_soak_frame(expected, nonce,
                (uint8_t)(3u - role), fragments.last_index) : 0u;
            expected[8] = duration[0]; expected[9] = duration[1];
            fragment_valid = fragment_valid && length &&
                fragments.reassembled_fcs == clean_mac_fcs(expected + 6u, length - 10u) &&
                usb_report_u32(detail + CLEAN_STATION_MAC_DETAIL + 4u) == s.target &&
                usb_report_u32(detail + CLEAN_STATION_MAC_DETAIL + 12u) ==
                    s.target * (fragment_count - 1u) &&
                !usb_report_u32(detail + CLEAN_STATION_MAC_DETAIL + 16u) &&
                !usb_report_u32(detail + CLEAN_STATION_MAC_DETAIL + 20u) &&
                !usb_report_u32(detail + CLEAN_STATION_MAC_DETAIL + 24u) &&
                !usb_report_u32(detail + CLEAN_STATION_MAC_DETAIL + 28u);
        }
        int valid = s.magic == CLEAN_SOAK_STATUS_MAGIC && !usb_report_u32(detail + 8u) &&
            s.target == usb_report_u32(nonce + 8u) && s.tx_acked == s.target &&
            s.received == s.target && !s.failure && !s.corrupt && !s.duplicates && !s.out_of_order &&
            s.rx_phy_checked == s.target && !s.rx_phy_mismatch &&
            usb_report_u32(detail + CLEAN_STATION_TRACE_DETAIL + 100u) == nonce[12] &&
            fragment_valid && progress_valid && clean_ack_test_status_valid(&ack_test) &&
            ack_test.configured == nonce[12] && ack_valid &&
            arith.magic == CLEAN_ACK_ARITH_MAGIC && arith.limit == ack_test.limit &&
            arith.sum == ack_test.limit && !arith.reserved;
        printf("{\"serial\":\"%s\",\"build\":\"%08x\",\"channel\":%u,"
               "\"mode\":\"mac-soak\",\"role\":%u,\"valid\":%s,\"error\":%u,\"actual\":%u,"
               "\"error_addr\":\"%08x\","
               "\"target\":%u,\"tx_acked\":%u,\"tx_attempts\":%u,\"tx_timeouts\":%u,"
               "\"deferrals\":%u,\"received\":%u,\"duplicate_deliveries\":%u,"
               "\"mac_duplicates_suppressed\":%u,\"corrupt\":%u,\"out_of_order\":%u,"
               "\"elapsed_us\":%llu,\"failure\":%u",
               serial, USB_REPORT_BUILD_ID, channel, tx == 19u ? 1u : 2u,
               valid ? "true" : "false", usb_report_u32(detail + 8u),
               usb_report_u32(detail + 16u), usb_report_u32(detail + 12u),
               s.target, s.tx_acked, s.tx_attempts,
               s.tx_timeouts, s.deferrals, s.received, s.duplicates,
               usb_report_u32(detail + 1688u), s.corrupt, s.out_of_order,
               (unsigned long long)clean_soak_elapsed(&s), s.failure);
        printf(",\"progress_report\":%s,\"progress_phase\":%u,\"progress_index\":%u,"
               "\"progress_fragment\":%u,\"progress_attempts\":%u,\"progress_result\":%u,"
               "\"ready_us\":%u,\"first_send_us\":%u,\"first_result_us\":%u,\"first_rx_us\":%u,"
               "\"host_submit_before_us\":%llu,\"host_submit_after_us\":%llu,"
               "\"host_report_us\":%llu",
               progress.magic == CLEAN_SOAK_PROGRESS_MAGIC ? "true" : "false",
               progress.phase, progress.index, progress.fragment, progress.attempts, progress.result,
               progress.ready_us, progress.first_send_us, progress.first_result_us, progress.first_rx_us,
               (unsigned long long)submit_before, (unsigned long long)submit_after,
               (unsigned long long)report_at);
        printf(",\"clock_ref_before\":%u,\"clock_tick_cycles_before\":%u,"
               "\"clock_ref_after\":%u,\"clock_tick_cycles_after\":%u,\"clock_sys_khz_q5\":%u",
               progress.ref_before, progress.tick_cycles_before, progress.ref_after,
               progress.tick_cycles_after, progress.sys_khz_q5);
        printf(",\"snapshot_flags\":%u,\"snapshot_stop_error\":%u,"
               "\"mac_tx_phy_start_handoffs\":%u,\"mac_tx_completion_handoffs\":%u,"
               "\"closing_duplicates_suppressed\":%u",
               progress.snapshot_flags, progress.snapshot_stop_error, progress.phy_start_handoffs,
               progress.completion_handoffs, progress.closing_duplicates);
        printf(",\"ack_test_report\":%s,\"ack_test_healthy\":%s,"
               "\"ack_test_configured\":%u,\"ack_test_limit\":%u,"
               "\"ack_test_remaining\":%u,\"ack_test_dropped\":%u,"
               "\"ack_test_fault\":%u,\"ack_test_phase\":%u,"
               "\"ack_test_before\":%u,\"ack_test_after\":%u,\"ack_test_dropped_before\":%u,"
               "\"ack_test_clock\":%u,\"ack_test_frame_control\":\"%04x\","
               "\"ack_test_sequence\":\"%04x\",\"ack_test_epoch\":%u",
               ack_test.magic == CLEAN_ACK_TEST_MAGIC ? "true" : "false",
               clean_ack_test_status_valid(&ack_test) ? "true" : "false",
               ack_test.configured, ack_test.limit, ack_test.remaining, ack_test.dropped,
               ack_test.fault, ack_test.phase, ack_test.before, ack_test.after,
               ack_test.dropped_before, ack_test.clock, ack_test.frame_control,
               ack_test.sequence, ack_test.epoch);
        printf(",\"ack_rx_valid\":%s,\"ack_rx_count\":%u,\"ack_rx_tsf\":%u,"
               "\"ack_rx_phy\":\"%04x\",\"ack_rx_mpdu\":\"",
               ack_valid ? "true" : "false", ack_rx.count,
               (uint32_t)ack_rx.time_lo | ((uint32_t)ack_rx.time_hi << 16u), ack_rx.phy);
        for (uint32_t i = 0u; i < 7u; ++i)
            printf("%02x%02x", ack_rx.mpdu[i] & 255u, ack_rx.mpdu[i] >> 8u);
        printf("\"");
        printf(",\"ack_observe_report\":%s,\"ack_observe_at\":%u,"
               "\"ack_observe_waiting\":%u,\"ack_observe_due\":%u,"
               "\"ack_observe_started\":%u,\"ack_observe_window\":%u,"
               "\"ack_clear_reason\":%u,\"ack_cleared_at\":%u,\"ack_clear_due\":%u,"
               "\"ack_arm_at\":%u,\"ack_arm_started\":%u,\"ack_arm_window\":%u",
               observed.magic == CLEAN_ACK_OBSERVE_MAGIC ? "true" : "false",
               observed.observed_at, observed.waiting, observed.due,
               observed.started, observed.window, observed.clear_reason,
               observed.cleared_at, observed.clear_due,
               observed.arm_at, observed.arm_started, observed.arm_window);
        printf(",\"ack_arith_report\":%s,\"ack_arith_airtime\":%u,"
               "\"ack_arith_sum\":%u,\"ack_arith_limit\":%u",
               arith.magic == CLEAN_ACK_ARITH_MAGIC ? "true" : "false",
               arith.airtime, arith.sum, arith.limit);
        printf(",\"fw_state\":%u,\"fw_substage\":%u,\"fw_error\":%u,"
               "\"fw_error_addr\":\"%08x\",\"fw_error_val\":\"%08x\"",
               usb_report_u32(detail + 36u), usb_report_u32(detail + 40u),
               usb_report_u32(detail + 44u), usb_report_u32(detail + 48u),
               usb_report_u32(detail + 52u));
        printf(",\"phy\":\"%s\",\"payload\":\"%s\",\"rx_phy_checked\":%u,\"rx_phy_mismatch\":%u,"
               "\"rx_phy_last\":\"%04x\"",
               clean_soak_phy_names[clean_soak_phy(nonce)], clean_soak_payload_names[nonce[13] >> 3u],
               s.rx_phy_checked,
               s.rx_phy_mismatch, s.rx_phy_last);
        printf(",\"fragments_per_msdu\":%u,\"fragment_report_valid\":%s,"
               "\"tx_mpdu_acked\":%u,\"rx_mpdu_checked\":%u,\"reassembled\":%u,"
               "\"fragment_last_index\":%u,\"fragment_bitmap\":%u,\"fragment_retry_bitmap\":%u,"
               "\"reassembled_fcs\":\"%08x\",\"fragment_fcs\":[",
               fragment_count, fragment_valid ? "true" : "false", fragments.tx_mpdu_acked,
               fragments.rx_mpdu_checked, fragments.reassembled, fragments.last_index,
               fragments.fragment_bitmap, fragments.retry_bitmap, fragments.reassembled_fcs);
        for (uint32_t f = 0u; f < fragment_count; ++f)
            printf("%s\"%08x\"", f ? "," : "", fragments.fragment_fcs[f]);
        putchar(']');
        print_mac_timing(detail);
        print_tx_diag(detail);
        fputs(",\"psm_diag\":[", stdout);
        for (unsigned i = 0u; i < 16u; ++i)
            printf("%s%u", i ? "," : "",
                (unsigned)(detail[136u + 2u * i] | ((uint16_t)detail[137u + 2u * i] << 8u)));
        putchar(']');
        fputs(",\"rx_cck_trace\":[", stdout);
        for (unsigned i = 0u; i < CLEAN_RX_TRACE_WORDS; ++i)
            printf("%s%u", i ? "," : "",
                (unsigned)(detail[200u + 2u * i] | ((uint16_t)detail[201u + 2u * i] << 8u)));
        putchar(']');
        printf(",\"cck_tx_tail_us\":%u,\"txe_timeout\":%u,\"tx_template_end\":%u,\"tx_count_end\":%u",
            usb_report_u32(detail + 224u), usb_report_u32(detail + 228u),
            usb_report_u32(detail + 232u), usb_report_u32(detail + 236u));
        printf(",\"phy_465\":%u,\"phy_42c\":%u",
            usb_report_u32(detail + 240u), usb_report_u32(detail + 244u));
        fputs(",\"rx_fifo_fault\":[", stdout);
        for (unsigned i = 0u; i < CLEAN_RX_FAULT_WORDS; ++i)
            printf("%s%u", i ? "," : "",
                (unsigned)(detail[CLEAN_SOAK_FAULT_DETAIL + 2u * i] |
                           ((uint16_t)detail[CLEAN_SOAK_FAULT_DETAIL + 2u * i + 1u] << 8u)));
        puts("]}");
        if (!valid) fputs("Continuous MAC trial did not satisfy endpoint counts/integrity\n", stderr);
        return valid;
    }
    if (tx == 17u) {
        printf("{\"monitor_auth_requests\":%u,\"monitor_assoc_requests\":%u,"
               "\"monitor_ap_acks\":%u,\"monitor_ap_responses\":%u,"
               "\"monitor_auth_first_us\":%u,\"monitor_auth_last_us\":%u,"
               "\"monitor_response_first_us\":%u,\"monitor_response_last_us\":%u,"
               "\"monitor_requests_before_first_response\":%u,"
               "\"monitor_acks_to_ap\":%u,\"error\":%u}\n",
               usb_report_u32(detail + 540u), usb_report_u32(detail + 544u),
               usb_report_u32(detail + 548u), usb_report_u32(detail + 552u),
               usb_report_u32(detail + 520u), usb_report_u32(detail + 524u),
               usb_report_u32(detail + 528u), usb_report_u32(detail + 532u),
               usb_report_u32(detail + 536u),
               usb_report_u32(detail + 556u),
               usb_report_u32(detail + 8u));
        return usb_report_u32(detail + 8u) == 0u;
    }
    if (tx == 16u || tx==18u) {
        int associated = clean_station_evidence(detail);
        int authorized=tx==18u && associated && usb_report_u32(detail+1640u)==0x32575043u &&
            usb_report_u32(detail+1644u)==4u && !usb_report_u32(detail+1648u) &&
            usb_report_u32(detail+1668u)>0u;
        printf("{\"serial\":\"%s\",\"build\":\"%08x\",\"channel\":%u,\"stage\":%u,"
               "\"error\":%u,\"actual\":%u,\"fw_error\":%u,\"ap_profile\":%d,"
               "\"phase\":%u,\"authentication_status\":%d,\"association_status\":%d,"
               "\"aid\":%u,\"auth_attempts\":%u,\"assoc_attempts\":%u,"
               "\"tx_ack_received\":%u,\"ack_responses_scheduled\":%u,"
               "\"ack_schedule_errors\":%u,\"deauth_reason\":%u,"
               "\"associated\":%s,\"wpa2_authorized\":%s,"
               "\"local_mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\","
               "\"own_rx_candidates\":%u,\"association_candidate_bytes\":%u,"
               "\"eapol_m1_received\":%s,\"eapol_m1_count\":%u,"
               "\"wpa_stage\":%u,\"wpa_error\":%u,\"m2_sent\":%u,\"m3_validated\":%u,"
               "\"m4_sent\":%u,\"protected_echoes\":%u,\"gtk_kde\":\"%06x\","
               "\"mac_rx_report\":%s,\"mac_delivered\":%u,\"mac_duplicates\":%u,"
               "\"mac_fragments\":%u,\"mac_malformed\":%u,\"mac_unsupported\":%u,"
               "\"mac_expired\":%u,\"mac_fragment_errors\":%u,"
               "\"mac_radio_report\":%s,\"mac_profile\":%u,\"mac_cw\":%u,"
               "\"mac_slots\":%u,\"mac_deferrals\":%u,\"mac_control_acks\":%u,"
               "\"mac_ack_timeouts\":%u,\"mac_rx_waits\":%u,\"mac_pending_rx\":%u,"
               "\"cts_responses_scheduled\":%u,\"short_responses_scheduled\":%u,"
               "\"mac_policy_pending\":%u,\"mac_prepared_acks\":%u,"
               "\"mac_address_lo\":\"%08x\",\"mac_address_hi\":\"%04x\","
               "\"protected_mac_report\":%s,\"protected_mac_delivered\":%u,"
               "\"protected_mac_duplicates\":%u,\"protected_mac_fragments\":%u,"
               "\"protected_mac_malformed\":%u,\"protected_mac_unsupported\":%u,"
               "\"protected_mac_expired\":%u,\"protected_mac_fragment_errors\":%u,"
               "\"mac_trace_report\":%s,\"mac_response_auth\":%u,\"mac_response_assoc\":%u,"
               "\"mac_response_data\":%u,\"mac_response_other\":%u,"
               "\"mac_published_auth\":%u,\"mac_published_assoc\":%u,"
               "\"mac_published_data\":%u,\"mac_published_other\":%u,\"mac_published_invalid\":%u",
               serial, USB_REPORT_BUILD_ID, usb_report_u32(detail + 28u),
               usb_report_u32(detail + 4u), usb_report_u32(detail + 8u),
               usb_report_u32(detail + 16u), usb_report_u32(detail + 44u),
               (int32_t)usb_report_u32(detail + 572u), usb_report_u32(detail + 524u),
               (int32_t)usb_report_u32(detail + 528u), (int32_t)usb_report_u32(detail + 532u),
               usb_report_u32(detail + 536u), usb_report_u32(detail + 540u),
               usb_report_u32(detail + 544u), usb_report_u32(detail + 548u),
               usb_report_u32(detail + 552u), usb_report_u32(detail + 556u),
               usb_report_u32(detail + 568u), associated ? "true" : "false",authorized?"true":"false",
               detail[1972], detail[1973], detail[1974], detail[1975], detail[1976], detail[1977],
               usb_report_u32(detail + 1096u), usb_report_u32(detail + 796u),
               (tx==18u?usb_report_u32(detail+1652u)>0u:clean_station_eapol_evidence(detail)) ? "true" : "false",
               tx==18u?usb_report_u32(detail+1652u):usb_report_u32(detail + 576u),
               tx==18u?usb_report_u32(detail+1644u):0u,tx==18u?usb_report_u32(detail+1648u):0u,
               tx==18u?usb_report_u32(detail+1656u):0u,tx==18u?usb_report_u32(detail+1660u):0u,
               tx==18u?usb_report_u32(detail+1664u):0u,tx==18u?usb_report_u32(detail+1668u):0u,
               tx==18u?usb_report_u32(detail+1672u):0u,
               usb_report_u32(detail+CLEAN_STATION_MAC_DETAIL)==CLEAN_STATION_MAC_MAGIC?"true":"false",
               usb_report_u32(detail+CLEAN_STATION_MAC_DETAIL+4u),
               usb_report_u32(detail+CLEAN_STATION_MAC_DETAIL+8u),
               usb_report_u32(detail+CLEAN_STATION_MAC_DETAIL+12u),
               usb_report_u32(detail+CLEAN_STATION_MAC_DETAIL+16u),
               usb_report_u32(detail+CLEAN_STATION_MAC_DETAIL+20u),
               usb_report_u32(detail+CLEAN_STATION_MAC_DETAIL+24u),
               usb_report_u32(detail+CLEAN_STATION_MAC_DETAIL+28u),
               usb_report_u32(detail+CLEAN_STATION_MAC_DETAIL+32u)==CLEAN_MAC_RADIO_MAGIC?"true":"false",
               usb_report_u32(detail+CLEAN_STATION_MAC_DETAIL+36u),
               usb_report_u32(detail+CLEAN_STATION_MAC_DETAIL+40u),
               usb_report_u32(detail+CLEAN_STATION_MAC_DETAIL+44u),
               usb_report_u32(detail+CLEAN_STATION_MAC_DETAIL+52u),
               usb_report_u32(detail+CLEAN_STATION_MAC_DETAIL+56u),
               usb_report_u32(detail+CLEAN_STATION_MAC_DETAIL+60u),
               usb_report_u32(detail+CLEAN_STATION_MAC_DETAIL+64u),
               usb_report_u32(detail+CLEAN_STATION_MAC_DETAIL+68u),
               usb_report_u32(detail+CLEAN_STATION_MAC_DETAIL+72u),
               usb_report_u32(detail+CLEAN_STATION_MAC_DETAIL+76u),
               usb_report_u32(detail+CLEAN_STATION_MAC_DETAIL+80u),
               usb_report_u32(detail+CLEAN_STATION_MAC_DETAIL+84u),
               usb_report_u32(detail+CLEAN_STATION_MAC_DETAIL+88u),
               usb_report_u32(detail+CLEAN_STATION_MAC_DETAIL+92u),
               usb_report_u32(detail+CLEAN_STATION_PROTECTED_DETAIL)==CLEAN_STATION_MAC_MAGIC?"true":"false",
               usb_report_u32(detail+CLEAN_STATION_PROTECTED_DETAIL+4u),
               usb_report_u32(detail+CLEAN_STATION_PROTECTED_DETAIL+8u),
               usb_report_u32(detail+CLEAN_STATION_PROTECTED_DETAIL+12u),
               usb_report_u32(detail+CLEAN_STATION_PROTECTED_DETAIL+16u),
               usb_report_u32(detail+CLEAN_STATION_PROTECTED_DETAIL+20u),
               usb_report_u32(detail+CLEAN_STATION_PROTECTED_DETAIL+24u),
               usb_report_u32(detail+CLEAN_STATION_PROTECTED_DETAIL+28u),
               usb_report_u32(detail+CLEAN_STATION_TRACE_DETAIL)==CLEAN_MAC_TRACE_MAGIC?"true":"false",
               usb_report_u32(detail+CLEAN_STATION_TRACE_DETAIL+4u),
               usb_report_u32(detail+CLEAN_STATION_TRACE_DETAIL+8u),
               usb_report_u32(detail+CLEAN_STATION_TRACE_DETAIL+12u),
               usb_report_u32(detail+CLEAN_STATION_TRACE_DETAIL+16u),
               usb_report_u32(detail+CLEAN_STATION_TRACE_DETAIL+20u),
               usb_report_u32(detail+CLEAN_STATION_TRACE_DETAIL+24u),
               usb_report_u32(detail+CLEAN_STATION_TRACE_DETAIL+28u),
               usb_report_u32(detail+CLEAN_STATION_TRACE_DETAIL+32u),
               usb_report_u32(detail+CLEAN_STATION_TRACE_DETAIL+36u));
        print_mac_timing(detail);
        const uint8_t *ht = detail + CLEAN_HT_PEER_DETAIL;
        printf(",\"ht_peer_profile_valid\":%s,\"ht_advertised\":false,"
               "\"ht_legacy_fallback_reasons\":%u,\"ht_peer_rx_mcs_mask\":%u,"
               "\"ht_peer_basic_mcs_mask\":%u,\"ht_peer_primary_channel\":%u,"
               "\"ht_peer_width40\":%s,\"wmm_peer_parameters_present\":%s,"
               "\"wmm_peer_parameter_set\":%u,\"ht_peer_profile_source\":%u,"
               "\"ht_peer_runtime_updates\":%u",
               usb_report_u32(ht) == CLEAN_HT_PEER_MAGIC ? "true" : "false",
               usb_report_u32(ht + 4u), ht[8], ht[9], ht[11], ht[10] ? "true" : "false",
               ht[16] ? "true" : "false", ht[17], ht[19], usb_report_u32(ht + 44u));
        int ht_profile_valid = usb_report_u32(ht) == CLEAN_HT_PEER_MAGIC;
        uint32_t ht_fallback = usb_report_u32(ht + 4u);
        uint32_t ht_candidate_mask = ht_profile_valid &&
            ht_fallback == CLEAN_HT_FALLBACK_MAC ? ht[8] & CLEAN_HT20_MCS_MASK : 0u;
        printf(",\"ht20_active\":false,\"ht20_candidate_tx_mcs_mask\":%u,"
               "\"ht20_candidate_max_mcs\":%d,\"ht20_target_width_mhz\":%u,"
               "\"ht20_target_gi_ns\":%u,\"ht20_target_streams\":%u,"
               "\"ht20_target_rx_mcs_mask\":%u,\"ht20_target_capability_ie\":\"",
               ht_candidate_mask,clean_ht20_select_mcs(ht_candidate_mask,7u),
               CLEAN_HT20_WIDTH_MHZ,CLEAN_HT20_GI_NS,CLEAN_HT20_STREAMS,CLEAN_HT20_MCS_MASK);
        uint8_t ht_capability[CLEAN_HT20_CAPABILITY_BYTES];
        clean_ht20_capability_ie(ht_capability);
        for (uint32_t i=0u;i<sizeof(ht_capability);++i) printf("%02x",ht_capability[i]);
        printf("\"");
        const uint8_t *wmm = detail + CLEAN_WMM_DETAIL;
        const uint8_t *edca = detail + CLEAN_EDCA_DETAIL;
        printf(",\"wmm_report_valid\":%s,\"wmm_offered\":%s,\"wmm_negotiated\":%s,"
               "\"wmm_policy_queued_updates\":%u,\"wmm_policy_committed_updates\":%u,"
               "\"wmm_edca_enabled\":%s,"
               "\"protected_amsdu_frames\":%u,\"protected_amsdu_subframes\":%u",
               usb_report_u32(wmm) == CLEAN_WMM_MAGIC ? "true" : "false",
               usb_report_u32(wmm + 4u) == 1u ? "true" : "false",
               usb_report_u32(wmm + 8u) == 1u ? "true" : "false",
               usb_report_u32(wmm + 12u),
               usb_report_u32(wmm + 12u),
               usb_report_u32(edca) == CLEAN_EDCA_STATUS_MAGIC && edca[14] == 1u ?
                   "true" : "false",
               usb_report_u32(wmm + 16u), usb_report_u32(wmm + 20u));
        clean_auth_failure_t failure;
        memcpy(&failure, detail + CLEAN_AUTH_FAILURE_DETAIL, sizeof(failure));
        int failure_valid = !usb_report_u32(detail + 596u) &&
            failure.magic == CLEAN_AUTH_FAILURE_MAGIC && !failure.reserved &&
            (!failure.count || failure.length == sizeof(failure.raw));
        printf(",\"auth_bad_fcs_report\":%s,\"auth_bad_fcs_count\":%u",
               failure_valid ? "true" : "false", failure_valid ? failure.count : 0u);
        if (failure_valid && failure.count) {
            printf(",\"auth_bad_fcs_phy\":%u,\"auth_bad_fcs_status1\":%u,"
                   "\"auth_bad_fcs_status2\":%u,\"auth_bad_fcs_time_us\":%u,"
                   "\"auth_bad_fcs_fcs_valid\":%s,\"auth_bad_fcs_raw\":\"",
                   failure.phy, failure.status1, failure.status2,
                   (uint32_t)failure.time_lo | ((uint32_t)failure.time_hi << 16u),
                   clean_frame_valid(failure.raw + 6u, 34u) ? "true" : "false");
            for (uint32_t i = 0u; i < sizeof(failure.raw); ++i)
                printf("%02x", failure.raw[i]);
            putchar('"');
        }
        puts("}");
        if (!associated) fputs("No validated successful association exchange\n", stderr);
        if (tx==18u && !authorized) fputs("WPA2 protected round trip not established\n",stderr);
        return tx==18u?authorized:associated;
    }
    printf("{\"serial\":\"%s\",\"build\":\"%08x\",\"channel\":%u,\"stage\":%u,\"error\":%u,"
           "\"address\":\"%08x\",\"actual\":\"%08x\",\"verified_bytes\":%u,"
           "\"elapsed_ticks\":%u,\"fw_magic\":\"%08x\",\"fw_state\":%u,"
           "\"fw_error\":%u,\"fw_error_address\":\"%08x\",\"fw_error_value\":\"%08x\","
           "\"captured_frames\":%u,\"dma_frames\":%u,\"f2_packets\":%u,\"beacons\":%u,"
           "\"tx_requested\":%u,\"tx_command_completed\":%u,"
           "\"tx_marker\":\"%08x\",\"tx_status\":\"%08x\",\"tx_conditions\":%u,"
           "\"ap_responses\":%u,\"fresh_mac_acks\":%u,\"fresh_mac_frames\":%u,"
           "\"fresh_frame_control\":\"%04x\",\"rx_rearm_requested\":%u,"
           "\"rx_match_requested\":%u,\"phy_debug_before_cmd\":\"%08x\","
           "\"rx_before_tx\":%u,\"rx_after_tx\":%u,\"valid_rx_after_tx\":%u,"
           "\"first_rx_after_tx_ticks\":%u,\"ap_frames_after_tx\":%u,"
           "\"ap_beacons_after_tx\":%u,\"rx_delivered_after_tx\":%u,"
           "\"rx_phase_supported\":%s,\"raw_prefix\":\"",
           serial, USB_REPORT_BUILD_ID, usb_report_u32(detail + 28u),
           usb_report_u32(detail + 4u), usb_report_u32(detail + 8u),
           usb_report_u32(detail + 12u), usb_report_u32(detail + 16u),
           usb_report_u32(detail + 20u), usb_report_u32(detail + 24u),
           usb_report_u32(detail + 32u), usb_report_u32(detail + 36u),
           usb_report_u32(detail + 44u), usb_report_u32(detail + 48u),
           usb_report_u32(detail + 52u), usb_report_u32(detail + 488u),
           usb_report_u32(detail + 508u), usb_report_u32(detail + 1636u),
           usb_report_u32(detail + 1632u), usb_report_u32(detail + 1092u),
           usb_report_u32(detail + 1096u), usb_report_u32(detail + 76u),
           usb_report_u32(detail + 80u), usb_report_u32(detail + 40u),
           usb_report_u32(detail + 1960u), usb_report_u32(detail + 1992u),
           usb_report_u32(detail + 1964u), usb_report_u32(detail + 1968u),
           usb_report_u32(detail + 2004u), usb_report_u32(detail + 2008u),
           usb_report_u32(detail + 2012u), usb_report_u32(detail + 2016u),
           usb_report_u32(detail + 2020u), usb_report_u32(detail + 2024u),
           usb_report_u32(detail + 2028u), usb_report_u32(detail + 2032u),
           usb_report_u32(detail + 2036u), usb_report_u32(detail + 2040u),
           usb_report_u32(detail + 2044u) ? "true" : "false");
    for (unsigned i = 576u; i < 688u; ++i) printf("%02x", detail[i]);
    printf("\",\"tx_over_air_confirmed\":%s,\"peer_frames\":%u,"
           "\"peer_rx_confirmed\":%s,\"peer_mode\":%u,\"challenge\":\"",
           clean_tx_confirmed(detail) ? "true" : "false",
           usb_report_u32(detail + 1996u),
           tx == 13u && clean_peer_received(detail, nonce) ? "true" : "false", tx);
    for (unsigned i = 0u; i < 14u; ++i) printf("%02x", nonce[i]);
    printf("\"");
    print_tx_diag(detail);
    if (tx == 15u) {
        printf(",\"requested_bssid\":\"");
        for (unsigned i = 0u; i < 6u; ++i)
            printf("%s%02x", i ? ":" : "", nonce[i]);
        int auth_status = clean_auth_response_status(detail);
        printf("\",\"authentication_response_received\":%s,\"authentication_status\":",
               auth_status >= 0 ? "true" : "false");
        if (auth_status >= 0) printf("%d", auth_status);
        else fputs("null", stdout);
        fputs(",\"associated\":false,\"wpa2_authorized\":false", stdout);
    }
    puts("}");
    if (tx == 15u) {
        if (clean_auth_response_status(detail) != 0) {
            fputs("AP authentication not confirmed: missing/rejected bound authentication response\n", stderr);
            return 0;
        }
        return 1;
    }
    if (tx == 2u)
        return usb_report_u32(detail + 8u) == CLEAN_CONTROLLER &&
               usb_report_u32(detail + 36u) == 255u &&
               usb_report_u32(detail + 44u) == 0x81u &&
               usb_report_u32(detail + 48u) == CLEAN_MBOX + 24u &&
               usb_report_u32(detail + 1092u) == 1u &&
               usb_report_u32(detail + 1096u) == 0u;
    if (tx == 13u) {
        if (!clean_peer_received(detail, nonce))
            fputs("Peer RX not confirmed: no exact fresh-challenge valid-FCS frame\n", stderr);
        return clean_peer_received(detail, nonce);
    }
    if (tx != 0u && tx != 12u && tx != 14u && !clean_tx_confirmed(detail)) {
        fputs("TX not confirmed over the air: no bound valid-FCS AP reply or fresh-MAC ACK\n", stderr);
        return 0;
    }
    return (!tx || (usb_report_u32(detail + 1096u) == 1u &&
                   usb_report_u32(detail + 76u) == (0x55550000u | usb_report_u32(detail + 1988u)) &&
                   usb_report_u32(detail + 40u) == 7u)) &&
           usb_report_u32(detail + 8u) == CLEAN_OK &&
           usb_report_u32(detail + 4u) == 8u &&
           usb_report_u32(detail + 32u) == 0x4f505258u &&
           usb_report_u32(detail + 36u) == 10u;
}

int main(int argc, char **argv) {
    if (argc == 4 && strcmp(argv[1], "--inspect-peer") == 0)
        return inspect_peer(argv[2], argv[3]) ? 0 : 1;
    if (argc == 4 && strcmp(argv[1], "--inspect") == 0)
        return inspect(argv[2], argv[3]) ? 0 : 1;
    uint8_t challenge[14];
    uint8_t target[6];
    int wpa_mode=argc==7 && strcmp(argv[4],"--ap-wpa2")==0;
    int ap_mode = wpa_mode || (argc == 6 && (strcmp(argv[4], "--ap-auth") == 0 ||
                                strcmp(argv[4], "--ap-assoc") == 0 ||
                                strcmp(argv[4], "--ap-monitor") == 0));
    if (ap_mode && !ap_bssid_decode(argv[5], target)) {
        fputs("AP BSSID must be a nonzero unicast address in XX:XX:XX:XX:XX:XX format\n", stderr);
        return 2;
    }
    uint8_t peer_mode = 0u;
    int ampdu_probe = argc == 7 && (strcmp(argv[4],"--ht-ampdu-probe-tx")==0 ||
                                   strcmp(argv[4],"--ht-ampdu-probe-rx")==0);
    int vendor_ht = argc == 7 && strcmp(argv[4], "--vendor-ht-rx") == 0;
    int ht_ack = argc == 7 && (strcmp(argv[4], "--ht-ack-tx") == 0 ||
                               strcmp(argv[4], "--ht-ack-clocked-tx") == 0 ||
                               strcmp(argv[4], "--ht-ack-rx") == 0);
    int ht_data = argc == 8 && (strcmp(argv[4], "--ht-data-tx") == 0 ||
                                strcmp(argv[4], "--ht-data-rx") == 0);
    int ht_probe = ampdu_probe || vendor_ht || ht_ack || ht_data || (argc == 7 && (strcmp(argv[4], "--ht-probe-tx") == 0 ||
                                           strcmp(argv[4], "--ht-probe-rx") == 0));
    if (ht_probe) {
        char *end_mcs;
        unsigned long mcs = strtoul(argv[6], &end_mcs, 10);
        if (!peer_challenge_decode(argv[5], challenge) || !*argv[6] || *end_mcs || mcs >= 8u) {
            fputs("HT probe requires a 28-hex challenge and MCS 0..7\n", stderr);
            return 2;
        }
        challenge[13] = (uint8_t)mcs;
        peer_mode = strcmp(argv[4], "--ht-probe-tx") == 0 ? 21u : 22u;
        if (ampdu_probe) {
            if (!CLEAN_RX_AMPDU_PROBE) {
                fputs("A-MPDU hardware probing requires CLEAN_RX_AMPDU_PROBE=1\n",stderr);
                return 2;
            }
            peer_mode = strcmp(argv[4],"--ht-ampdu-probe-tx")==0 ?
                CLEAN_HT_AMPDU_PROBE_TX_MODE : CLEAN_HT_AMPDU_PROBE_RX_MODE;
        }
        if (vendor_ht) peer_mode = CLEAN_PEER_RX_MODE;
        if (ht_ack) {
            if (!CLEAN_RX_HT_RESPONSE) {
                fputs("HT ACK diagnostics require CLEAN_RX_HT_RESPONSE=1\n", stderr);
                return 2;
            }
            challenge[11] = CLEAN_HT_ACK_MPDU; challenge[12] = 0u;
            peer_mode = strcmp(argv[4], "--ht-ack-clocked-tx") == 0 ? 28u :
                strcmp(argv[4], "--ht-ack-tx") == 0 ? 26u : 27u;
        }
        if (ht_data) {
            unsigned long mpdu = strtoul(argv[7], &end_mcs, 10);
            if (!*argv[7] || *end_mcs || mpdu > CLEAN_HT_DATA_MAX_MPDU ||
                !clean_ht_data_size_valid((uint32_t)mpdu)) {
                fputs(CLEAN_RX_HT_RX ?
                    "HT data requires MPDU bytes 28..2332 or the 3869-byte A-MSDU fixture\n" :
                    "HT data requires MPDU bytes 28..2332 including FCS\n", stderr);
                return 2;
            }
            challenge[11] = (uint8_t)mpdu; challenge[12] = (uint8_t)(mpdu >> 8u);
            peer_mode = strcmp(argv[4], "--ht-data-tx") == 0 ? 24u : 25u;
        }
    }
    uint32_t soak_count = 0u;
    int soak = (argc >= 7 && argc <= 10) && (strcmp(argv[4], "--mac-soak-init") == 0 ||
                             strcmp(argv[4], "--mac-soak-reply") == 0);
    if (soak) {
        char *end_count;
        unsigned long count = strtoul(argv[6], &end_count, 10);
        if (!peer_challenge_decode(argv[5], challenge) || !*argv[6] || *end_count ||
            count < 1u || count > CLEAN_SOAK_MAX_FRAMES) {
            fputs("MAC soak requires a 28-hex challenge and frame count 1..100000\n", stderr);
            return 2;
        }
        soak_count = (uint32_t)count;
        unsigned long dropped = 0u;
        if (argc >= 8) {
            dropped = strtoul(argv[7], &end_count, 10);
            if (!*argv[7] || *end_count || dropped > CLEAN_TEST_ACK_MAX) {
                fputs("MAC soak ACK suppression must be 0..16\n", stderr);
                return 2;
            }
        }
        uint32_t phy = 0u;
        if (argc >= 9) {
            for (; phy < CLEAN_SOAK_PHYS; ++phy)
                if (strcmp(argv[8], clean_soak_phy_names[phy]) == 0) break;
            if (phy == CLEAN_SOAK_PHYS) {
                fputs("MAC soak PHY must be ofdm6, 1/2/5.5/11-long or 2/5.5/11-short\n", stderr);
                return 2;
            }
        }
        uint32_t payload = 0u;
        if (argc == 10) {
            for (; payload < CLEAN_SOAK_PAYLOADS; ++payload)
                if (strcmp(argv[9], clean_soak_payload_names[payload]) == 0) break;
            if (payload == CLEAN_SOAK_PAYLOADS) {
                fputs("MAC soak payload must be cycle, 32, 1500, 2304, cck-boundary, frag2 or frag16\n", stderr);
                return 2;
            }
        }
        challenge[12] = (uint8_t)dropped;
        challenge[13] = (uint8_t)(phy | (payload << 3u));
        for (uint32_t i = 0u; i < 4u; ++i) challenge[8u + i] = (uint8_t)(soak_count >> (8u * i));
        peer_mode = strcmp(argv[4], "--mac-soak-init") == 0 ? 19u : 20u;
    }
    if (argc == 6 && (strcmp(argv[4], "--peer-rx") == 0 ||
                      strcmp(argv[4], "--peer-tx") == 0 ||
                      strcmp(argv[4], "--peer-tx-1m") == 0)) {
        if (!peer_challenge_decode(argv[5], challenge)) return 2;
        peer_mode = strcmp(argv[4], "--peer-rx") == 0 ? 13u :
                    strcmp(argv[4], "--peer-tx-1m") == 0 ? 14u : 12u;
    }
    if ((!peer_mode && !ap_mode && argc != 4 && argc != 5) || strlen(argv[1]) != 16u ||
        (argc == 6 && !peer_mode && !ap_mode) ||
        (argc == 5 && strcmp(argv[4], "--tx-auth-short") != 0 &&
                      strcmp(argv[4], "--tx-auth-rearm") != 0 &&
                      strcmp(argv[4], "--tx-auth-match") != 0 &&
                      strcmp(argv[4], "--tx-auth-match-rearm") != 0 &&
                      strcmp(argv[4], "--tx-probe-1m") != 0 &&
                      strcmp(argv[4], "--tx-probe-broadcast-1m") != 0 &&
                      strcmp(argv[4], "--tx-auth-ofdm") != 0 &&
                      strcmp(argv[4], "--tx-auth-1m") != 0 &&
                      strcmp(argv[4], "--tx-one-auth") != 0 &&
                      strcmp(argv[4], "--tx-one-probe") != 0 &&
                      strcmp(argv[4], "--ht-observe") != 0 &&
                      strcmp(argv[4], "--reject-tx") != 0)) {
        fputs("usage: collector BADGE_SERIAL EVIDENCE_BIN CHANNEL:1-11 "
              "[--tx-one-probe|--tx-probe-1m|--tx-probe-broadcast-1m|--tx-one-auth|--tx-auth-1m|--tx-auth-ofdm|--tx-auth-short|--tx-auth-rearm|--tx-auth-match|--tx-auth-match-rearm|--reject-tx]\n",
              stderr);
        fputs("peer usage: collector SERIAL EVIDENCE_BIN CHANNEL "
              "--peer-rx|--peer-tx|--peer-tx-1m CHALLENGE_HEX_28\n", stderr);
        fputs("HT diagnostic only: collector SERIAL EVIDENCE_BIN CHANNEL "
              "--ht-observe OR --ht-probe-tx|--ht-probe-rx CHALLENGE_HEX_28 MCS:0-7\n", stderr);
        fputs("HT data diagnostic: collector SERIAL EVIDENCE_BIN CHANNEL "
              "--ht-data-tx|--ht-data-rx CHALLENGE_HEX_28 MCS:0-7 MPDU_BYTES:28-2332\n", stderr);
        fputs("HT ACK diagnostic: collector SERIAL EVIDENCE_BIN CHANNEL "
              "--ht-ack-tx|--ht-ack-clocked-tx|--ht-ack-rx CHALLENGE_HEX_28 MCS:0-7\n", stderr);
        fputs("A-MPDU hardware diagnostic: collector SERIAL EVIDENCE_BIN CHANNEL "
              "--ht-ampdu-probe-tx|--ht-ampdu-probe-rx CHALLENGE_HEX_28 MCS:0-7\n",stderr);
        fputs("AP authentication only: collector SERIAL EVIDENCE_BIN CHANNEL "
              "--ap-auth|--ap-assoc|--ap-monitor XX:XX:XX:XX:XX:XX\n", stderr);
        fputs("WPA2 protected trial: collector SERIAL EVIDENCE_BIN CHANNEL "
              "--ap-wpa2 XX:XX:XX:XX:XX:XX PRIVATE_CREDENTIAL_FILE\n",stderr);
        fputs("Continuous raw MAC: collector SERIAL EVIDENCE_BIN CHANNEL "
              "--mac-soak-init|--mac-soak-reply CHALLENGE_HEX_28 COUNT:1-100000 [DROP_ACKS:0-16 [PHY [PAYLOAD]]]\n",stderr);
        return 2;
    }
    char *end;
    unsigned long channel = strtoul(argv[3], &end, 10);
    if (*argv[3] == '\0' || *end != '\0' || channel < 1u || channel > 11u) return 2;
    for (unsigned i = 0u; i < 16u; ++i)
        if (!((argv[1][i] >= '0' && argv[1][i] <= '9') ||
              (argv[1][i] >= 'A' && argv[1][i] <= 'F'))) return 2;
    if (signal(SIGALRM, deadline) == SIG_ERR) { perror("signal"); return 1; }
    alarm(soak ? CLEAN_SOAK_HOST_SECONDS : 15u);
    libusb_context *context = NULL;
    int status = libusb_init(&context);
    if (status != 0) { fprintf(stderr, "USB init: %s\n", libusb_error_name(status)); return 1; }
    libusb_device_handle *device = NULL;
    for (unsigned i = 0u; i < 30u && device == NULL; ++i) {
        device = find_badge(context, argv[1]);
        if (device == NULL) usleep(50000u);
    }
    if (device == NULL) {
        fputs("No serial-matched badge\n", stderr);
        libusb_exit(context);
        return 1;
    }
    int configuration;
    status = libusb_get_configuration(device, &configuration);
    if (status == 0 && configuration != 1) status = libusb_set_configuration(device, 1);
    if (status == 0) status = libusb_claim_interface(device, 0);
    int ok = 0;
    uint8_t mode = argc == 5 ? (strcmp(argv[4], "--reject-tx") == 0 ? 2u :
                              strcmp(argv[4], "--ht-observe") == 0 ? 23u :
                              strcmp(argv[4], "--tx-one-auth") == 0 ? 3u :
                              strcmp(argv[4], "--tx-auth-ofdm") == 0 ? 4u :
                              strcmp(argv[4], "--tx-auth-1m") == 0 ? 5u :
                              strcmp(argv[4], "--tx-probe-1m") == 0 ? 7u :
                              strcmp(argv[4], "--tx-auth-rearm") == 0 ? 8u :
                              strcmp(argv[4], "--tx-auth-match") == 0 ? 9u :
                              strcmp(argv[4], "--tx-auth-match-rearm") == 0 ? 10u :
                              strcmp(argv[4], "--tx-probe-broadcast-1m") == 0 ? 11u :
                              strcmp(argv[4], "--tx-auth-short") == 0 ? 6u : 1u) : 0u;
    if (peer_mode) mode = peer_mode;
    if (ap_mode) mode = strcmp(argv[4], "--ap-assoc") == 0 ? 16u :
                        strcmp(argv[4], "--ap-monitor") == 0 ? 17u : 15u;
    if (wpa_mode) mode=18u;
    if (status == 0) ok = collect(device, argv[2], (uint8_t)channel, mode,
                                 peer_mode ? challenge : NULL, ap_mode ? target : NULL, argv[1],
                                 wpa_mode?argv[6]:NULL);
    else fprintf(stderr, "USB claim: %s\n", libusb_error_name(status));
    int rebooted = transfer(device, 0x40u, LIVE_USB_BOOTSEL, NULL, 0u);
    if (status == 0) libusb_release_interface(device, 0);
    libusb_close(device);
    libusb_exit(context);
    return ok && rebooted ? 0 : 1;
}
