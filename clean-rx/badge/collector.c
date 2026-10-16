#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include "build_id.h"
#include "../../src/host/tools/usb_live_transport.h"
#include "../../src/shared/bytes.h"
#include "../../src/radio/wifi_usb_report.h"
#include "../../src/tools/diagnostics_logic.h"
#include "loader.h"
#include "frames.h"
#include "station.h"
#include "../wpa2_crypto.h"
#include "../tx_diag.h"

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

static void deadline(int signum) {
    (void)signum;
    static const char text[] = "Collector deadline; badge has independent BOOTSEL recovery\n";
    ssize_t result = write(STDERR_FILENO, text, sizeof(text) - 1u);
    (void)result;
    _exit(124);
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
        wpa_sha1_t sha;
        wpa_sha1_init(&sha); wpa_sha1_update(&sha,secret,64u);
        wpa_sha1_update(&sha,nonce,6u); wpa_sha1_update(&sha,nonce+15u,1u);
        wpa_sha1_final(&sha,digest); memcpy(nonce+6u,digest,8u);
        valid=transfer(device,0x40u,LIVE_USB_CLEAN_SECRET,secret,64u);
        wpa_wipe(secret,sizeof(secret)); wpa_wipe(digest,sizeof(digest));
        if (!valid) return 0;
    }
    usb_report_request_for_test(request, USB_REPORT_BUILD_ID, nonce, WIFI_USB_TEST_ID);
    if (!transfer(device, 0x40u, LIVE_USB_WIFI_PIO_SUBMIT, request, sizeof(request))) return 0;
    int got = 0;
    for (unsigned attempt = 0u; attempt < 800u; ++attempt) {
        int count = libusb_control_transfer(device, 0xc0u, LIVE_USB_WIFI_REPORT, 0, 0,
                                            report, sizeof(report), 1000u);
        if (count == 64) { got = 1; break; }
        if (count != LIBUSB_ERROR_PIPE) {
            fprintf(stderr, "Report read: %d\n", count);
            return 0;
        }
        usleep(10000u);
    }
    if (!got) { fputs("Report deadline\n", stderr); return 0; }
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
    if (tx == 17u) {
        printf("{\"monitor_auth_requests\":%u,\"monitor_assoc_requests\":%u,"
               "\"monitor_ap_acks\":%u,\"monitor_ap_responses\":%u,"
               "\"monitor_acks_to_ap\":%u,\"error\":%u}\n",
               usb_report_u32(detail + 540u), usb_report_u32(detail + 544u),
               usb_report_u32(detail + 548u), usb_report_u32(detail + 552u),
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
               "\"m4_sent\":%u,\"protected_echoes\":%u,\"gtk_kde\":\"%06x\"}\n",
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
               tx==18u?usb_report_u32(detail+1672u):0u);
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
                      strcmp(argv[4], "--reject-tx") != 0)) {
        fputs("usage: collector BADGE_SERIAL EVIDENCE_BIN CHANNEL:1-11 "
              "[--tx-one-probe|--tx-probe-1m|--tx-probe-broadcast-1m|--tx-one-auth|--tx-auth-1m|--tx-auth-ofdm|--tx-auth-short|--tx-auth-rearm|--tx-auth-match|--tx-auth-match-rearm|--reject-tx]\n",
              stderr);
        fputs("peer usage: collector SERIAL EVIDENCE_BIN CHANNEL "
              "--peer-rx|--peer-tx|--peer-tx-1m CHALLENGE_HEX_28\n", stderr);
        fputs("AP authentication only: collector SERIAL EVIDENCE_BIN CHANNEL "
              "--ap-auth|--ap-assoc|--ap-monitor XX:XX:XX:XX:XX:XX\n", stderr);
        fputs("WPA2 protected trial: collector SERIAL EVIDENCE_BIN CHANNEL "
              "--ap-wpa2 XX:XX:XX:XX:XX:XX PRIVATE_CREDENTIAL_FILE\n",stderr);
        return 2;
    }
    char *end;
    unsigned long channel = strtoul(argv[3], &end, 10);
    if (*argv[3] == '\0' || *end != '\0' || channel < 1u || channel > 11u) return 2;
    for (unsigned i = 0u; i < 16u; ++i)
        if (!((argv[1][i] >= '0' && argv[1][i] <= '9') ||
              (argv[1][i] >= 'A' && argv[1][i] <= 'F'))) return 2;
    if (signal(SIGALRM, deadline) == SIG_ERR) { perror("signal"); return 1; }
    alarm(15u);
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
