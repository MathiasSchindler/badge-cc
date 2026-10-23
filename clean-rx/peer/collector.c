#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include "protected_ap.h"
#include "protocol.h"
#include "frame.h"
#include "lifecycle.h"
#include "network_json.h"
#include "qualification.h"
#include "build_id.h"
#include "../../src/host/tools/usb_live_transport.h"
#include "../../src/shared/bytes.h"

static void deadline(int number) {
    (void)number;
    static const char message[] = "Vendor peer USB deadline exceeded\n";
    ssize_t ignored = write(STDERR_FILENO, message, sizeof(message) - 1u);
    (void)ignored;
    _exit(124);
}

static volatile sig_atomic_t ap_cancel;
static void cancel_ap(int number) {
    (void)number;
    ap_cancel = 1;
}

static int read_trace(libusb_device_handle *device, const uint8_t report[64],
                      uint8_t trace[CLEAN_PEER_TRACE_BYTES]) {
    for (uint8_t page = 0u; page < WIFI_TRACE_PAGES; ++page) {
        uint8_t packet[64];
        int n = libusb_control_transfer(device, 0xc0u, LIVE_USB_WIFI_TRACE, page, 0u,
                                        packet, sizeof(packet), 1000u);
        if (n != sizeof(packet) ||
            wifi_trace_page_decode(trace, packet, sizeof(packet), report, page) != USB_RECORD_OK) {
            fprintf(stderr, "Vendor peer trace page %u invalid\n", page);
            return 0;
        }
    }
    return 1;
}

static int write_evidence(const char *path, const uint8_t report[64],
                          const uint8_t trace[CLEAN_PEER_TRACE_BYTES]) {
    FILE *out = fopen(path, "wb");
    if (!out) { perror(path); return 0; }
    int ok = fwrite(report, 1u, 64u, out) == 64u &&
        fwrite(trace, 1u, CLEAN_PEER_TRACE_BYTES, out) == CLEAN_PEER_TRACE_BYTES;
    if (fclose(out)) ok = 0;
    if (!ok) fputs("Vendor peer evidence write failed\n", stderr);
    return ok;
}

static void print_mac(const uint8_t mac[6]) {
    for (uint32_t i = 0u; i < 6u; ++i) printf("%s%02x", i ? ":" : "", mac[i]);
}

static int collect_protected(libusb_device_handle *device, const char *path,
        uint8_t nonce[16], uint32_t channel, uint32_t seconds,
        const char *credentials, const char *serial, int group) {
    uint8_t secret[64], digest[20], request[64], report[64], trace[CLEAN_PEER_TRACE_BYTES];
    char password[33], ssid[24], text[64];
    FILE *rng=fopen("/dev/urandom","rb");
    if (!rng) { perror("AP credential randomness"); return 0; }
    int ok=fread(secret,1u,sizeof(secret),rng)==sizeof(secret);
    if (fclose(rng)) ok=0;
    if (!ok) { wpa_wipe(secret,sizeof(secret)); fputs("AP randomness read failed\n",stderr); return 0; }
    for (uint32_t i=0u;i<16u;++i) snprintf(password+2u*i,3u,"%02x",secret[i]);
    int sn=snprintf(ssid,sizeof(ssid),"U26LAB-%02x%02x%02x%02x",nonce[0],nonce[1],nonce[2],nonce[3]);
    ok=sn==15 && wpa_psk((const uint8_t *)password,32u,(const uint8_t *)ssid,15u,secret);
    int size=snprintf(text,sizeof(text),"%s\n%s\n",ssid,password);
    wpa_wipe(password,sizeof(password));
    int fd=open(credentials,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
    if (fd<0) { perror("Create private AP credentials"); ok=0; }
    else {
        if (!ok || size<=0 || size>=(int)sizeof(text) || write(fd,text,(size_t)size)!=size) {
            fputs("Write AP credentials failed\n",stderr); ok=0;
        }
        if (close(fd)) { perror("Close AP credentials"); ok=0; }
    }
    wpa_wipe(text,sizeof(text));
    nonce[5]=(uint8_t)seconds;
    nonce[14]=group?CLEAN_GROUP_AP_MODE:CLEAN_PROTECTED_AP_MODE; nonce[15]=(uint8_t)channel;
    wpa_secret_binding(secret,nonce,nonce[15],digest); memcpy(nonce+6u,digest,8u);
    if (ok) ok=transfer(device,0x40u,LIVE_USB_CLEAN_SECRET,secret,sizeof(secret));
    wpa_wipe(secret,sizeof(secret)); wpa_wipe(digest,sizeof(digest));
    if (!ok) return 0;
    usb_report_request_for_test(request,USB_REPORT_BUILD_ID,nonce,3u);
    if (!transfer(device,0x40u,LIVE_USB_FW_SUBMIT,request,sizeof(request))) return 0;
    clean_protected_ap_report_t ap;
    memset(&ap,0,sizeof(ap));
    uint8_t last_report[64], last_trace[CLEAN_PEER_TRACE_BYTES];
    int announced=0, finished=0, observed=0;
    for (uint32_t i=0u;i<(seconds+20u)*20u;++i) {
        if (ap_cancel) { fputs("Protected AP cancelled; requesting BOOTSEL\n",stderr); return 0; }
        int n=libusb_control_transfer(device,0xc0u,LIVE_USB_FW_REPORT,0u,0u,report,64u,1000u);
        if (n==LIBUSB_ERROR_PIPE) { usleep(50000u); continue; }
        if (n!=64 || !clean_peer_report_valid(report,nonce,USB_REPORT_BUILD_ID) ||
            !read_trace(device,report,trace)) {
            fprintf(stderr,"Protected AP report binding/read failed: bytes=%d, bound=%d\n",
                    n,n==64 && clean_peer_report_valid(report,nonce,USB_REPORT_BUILD_ID));
            if (observed && !write_evidence(path,last_report,last_trace))
                fputs("Last valid protected AP snapshot could not be retained\n",stderr);
            return 0;
        }
        memcpy(&ap,trace+CLEAN_PROTECTED_AP_DETAIL,sizeof(ap));
        observed=1;
        memcpy(last_report,report,sizeof(last_report)); memcpy(last_trace,trace,sizeof(last_trace));
        if (usb_report_u32(trace)!=CLEAN_PEER_MAGIC ||
            usb_report_u32(trace+20u)!=nonce[14] ||
            ap.magic!=CLEAN_PROTECTED_AP_MAGIC) {
            fputs("Protected AP evidence layout invalid\n",stderr); return 0;
        }
        if (!announced && ap.state==1u) {
            printf("{\"test\":\"%s\",\"channel\":%u,\"ssid\":\"%s\",\"bssid\":\"",
                   group?"e2-ap-ready":"e1-ap-ready",channel,ssid);
            print_mac(ap.mac); puts("\"}");
            if (fflush(stdout)) { perror("Protected AP readiness flush"); return 0; }
            announced=1;
        }
        if (ap.state!=1u) { finished=1; break; }
        usleep(50000u);
    }
    if (!observed) { fputs("Protected AP produced no evidence before deadline\n",stderr); return 0; }
    if (!write_evidence(path,report,trace)) return 0;
    int configured=usb_report_u32(trace+28u)==channel && usb_report_u32(trace+32u)==channel &&
        usb_report_u32(trace+36u)==clean_qualification_nmode(channel) &&
        usb_report_u32(trace+40u)==4u && ap.wpa_auth==0x80u && ap.channel==channel;
    int valid=finished && configured && ap.state==2u && !ap.error &&
        ap.authorized && ap.received && ap.received==ap.echoed &&
        (!group || (ap.broadcast_tx && ap.multicast_tx));
    printf("{\"test\":\"%s\",\"serial\":\"%s\",\"build\":\"%08x\",\"valid\":%s,"
           "\"state\":%u,\"error\":%u,\"operation\":%u,\"configuration_verified\":%s,"
           "\"channel\":%u,\"nmode\":%u,\"wsec\":%u,\"wpa_auth\":%u,"
           "\"authorized\":%s,\"received\":%u,\"echoed\":%u,"
           "\"broadcast_tx\":%u,\"multicast_tx\":%u,"
           "\"gtk_rekey_status\":%d,\"ptk_rekey_status\":%d,\"bssid\":\"",
           group?"e2-ap-result":"e1-ap-result",serial,USB_REPORT_BUILD_ID,valid?"true":"false",ap.state,ap.error,ap.operation,configured?"true":"false",
           ap.channel,usb_report_u32(trace+36u),usb_report_u32(trace+40u),ap.wpa_auth,
           ap.authorized?"true":"false",ap.received,ap.echoed,ap.broadcast_tx,ap.multicast_tx,
           ap.gtk_rekey_status,ap.ptk_rekey_status);
    print_mac(ap.mac); fputs("\",\"peer\":\"",stdout); print_mac(ap.peer); puts("\"}");
    if (!valid) fputs("Protected AP qualification failed; raw evidence retained\n",stderr);
    return valid;
}

static int collect_ap(libusb_device_handle *device, const char *path,
        const uint8_t nonce[16], uint8_t report[64], uint8_t trace[CLEAN_PEER_TRACE_BYTES],
        const char *serial) {
    int announced = 0;
    uint32_t elapsed = 0u;
    clean_lifecycle_ap_report_t ap;
    for (uint32_t poll = 0u; poll < ((uint32_t)nonce[13] + 8u) * 20u; ++poll) {
        if (ap_cancel) { fputs("Lifecycle AP cancelled; requesting BOOTSEL\n", stderr); return 0; }
        if (!read_trace(device, report, trace)) return 0;
        memcpy(&ap, trace + CLEAN_LIFECYCLE_AP_DETAIL, sizeof(ap));
        if (usb_report_u32(trace) != CLEAN_PEER_MAGIC ||
            usb_report_u32(trace + 20u) != nonce[14] ||
            ap.magic != CLEAN_LIFECYCLE_AP_MAGIC) {
            fputs("Invalid lifecycle AP evidence layout\n", stderr); return 0;
        }
        if (ap.state != 1u) break;
        if (ap.elapsed_us != elapsed) {
            printf("{\"test\":\"b1-ap-progress\",\"elapsed_us\":%u,\"operation\":%u,"
                   "\"cycles\":%u,\"echoed\":%u}\n",
                   ap.elapsed_us, ap.operation, ap.cycles, ap.echoed);
            if (fflush(stdout)) { perror("AP progress flush"); return 0; }
            elapsed = ap.elapsed_us;
        }
        if (!announced) {
            fputs("{\"test\":\"b1-ap-ready\",\"bssid\":\"", stdout);
            print_mac(ap.mac);
            printf("\",\"startup_us\":%u}\n", ap.startup_us);
            if (fflush(stdout)) { perror("AP readiness flush"); return 0; }
            announced = 1;
        }
        usleep(50000u);
        int n = libusb_control_transfer(device, 0xc0u, LIVE_USB_FW_REPORT, 0u, 0u,
                                        report, 64u, 1000u);
        if (n != 64 || !clean_peer_report_valid(report, nonce, USB_REPORT_BUILD_ID)) {
            fputs("Lifecycle AP report binding failed\n", stderr); return 0;
        }
    }
    if (!write_evidence(path, report, trace)) return 0;
    int configured = nonce[14] != CLEAN_QUALIFICATION_AP_MODE ||
        (ap.channel == nonce[15] && usb_report_u32(trace + 28u) == nonce[15] &&
         usb_report_u32(trace + 32u) == nonce[15] &&
         usb_report_u32(trace + 36u) == clean_qualification_nmode(nonce[15]) &&
         !usb_report_u32(trace + 40u));
    int valid = configured && ap.state == 2u && !ap.error && ap.cycles == nonce[12] &&
        ap.received && ap.received == ap.echoed && ap.associations >= ap.cycles + 1u &&
        ap.associations <= 16u;
    printf("{\"test\":\"b1-ap-result\",\"serial\":\"%s\",\"build\":\"%08x\",\"valid\":%s,"
           "\"state\":%u,\"error\":%u,\"cycles\":%u,\"data_epochs\":%u,"
           "\"received\":%u,\"echoed\":%u,\"channel\":%u,\"nmode\":%u,"
           "\"configuration_verified\":%s,\"bssid\":\"",
           serial, USB_REPORT_BUILD_ID, valid ? "true" : "false", ap.state, ap.error, ap.cycles,
           ap.associations, ap.received, ap.echoed, ap.channel,
           usb_report_u32(trace + 36u),
           nonce[14] == CLEAN_QUALIFICATION_AP_MODE && configured ? "true" : "false");
    print_mac(ap.mac); fputs("\",\"peer\":\"", stdout); print_mac(ap.peer);
    fputs("\",\"session\":\"", stdout);
    for (uint32_t i = 0u; i < 6u; ++i) printf("%02x", ap.session[i]);
    fputs("\",\"epochs\":[", stdout);
    for (uint32_t i = 0u; i < ap.associations && i < 16u; ++i)
        printf("%s%u", i ? "," : "", ap.epochs[i]);
    printf("],\"operation\":%u,\"firmware_stage\":%u,\"address\":%u,\"expected\":%u,"
           "\"actual\":%u,\"link_error\":%u,\"command\":%u,\"controller_status\":%u,"
           "\"rx_header\":%u,\"startup_us\":%u,\"elapsed_us\":%u}\n",
           ap.operation, ap.firmware_stage, ap.address, ap.expected, ap.actual,
           ap.link_error, ap.command, ap.controller_status, ap.rx_header, ap.startup_us, ap.elapsed_us);
    if (!valid) fputs("Vendor AP lacks a fresh data epoch for every requested lifecycle\n", stderr);
    return valid;
}

static int collect_network(libusb_device_handle *device, const char *path,
        const uint8_t nonce[16], uint8_t report[64], uint8_t trace[CLEAN_PEER_TRACE_BYTES]) {
    uint32_t seconds = nonce[12] | (uint32_t)nonce[13] << 8u, host_stalls = 0u;
    clean_network_report_t n;
    int announced = 0;
    for (uint32_t i = 0u; i < (seconds + 25u) * 5u; ++i) {
        if (ap_cancel) { fputs("B2 AP cancelled; requesting BOOTSEL\n",stderr); return 0; }
        if (!read_trace(device, report, trace)) return 0;
        memcpy(&n, trace + CLEAN_NETWORK_AP_DETAIL, sizeof(n));
        if (usb_report_u32(trace) != CLEAN_PEER_MAGIC ||
            usb_report_u32(trace + 20u) != CLEAN_NETWORK_AP_MODE ||
            n.magic != CLEAN_NETWORK_MAGIC) {
            fputs("B2 AP evidence layout mismatch\n",stderr); return 0;
        }
        if (n.state != 1u) break;
        if (!announced) {
            fputs("{\"test\":\"b2-ap-ready\",\"bssid\":\"",stdout); print_mac(n.mac);
            puts("\"}"); if (fflush(stdout)) { perror("B2 readiness"); return 0; }
            announced = 1;
        }
        if (i % 25u == 20u) { ++host_stalls; usleep(2000000u); }
        usleep(200000u);
        int count = libusb_control_transfer(device,0xc0u,LIVE_USB_FW_REPORT,0u,0u,report,64u,1000u);
        if (count != 64 || !clean_peer_report_valid(report,nonce,USB_REPORT_BUILD_ID)) {
            fputs("B2 AP bound report read failed\n",stderr); return 0;
        }
    }
    if (!write_evidence(path,report,trace)) return 0;
    int valid = clean_network_valid(&n);
    printf("{\"test\":\"b2-ap-result\",\"valid\":%s,\"build\":\"%08x\","
           "\"host_stalls\":%u,\"network\":",valid?"true":"false",USB_REPORT_BUILD_ID,host_stalls);
    clean_network_print(&n); puts("}");
    if (!valid) fputs("B2 AP retained corruption, loss, unsettled data or radio failure\n",stderr);
    return valid;
}

static int collect(libusb_device_handle *device, const char *path, const uint8_t *challenge,
                   int monitor, uint32_t ap_seconds, uint32_t ap_cycles, int network,
                   uint32_t ap_channel, const char *serial, const char *protected_credentials,
                   int group) {
    uint8_t info[12], nonce[16], request[64], report[64], trace[CLEAN_PEER_TRACE_BYTES];
    const uint8_t prefix[] = {'U','2','6','L',1u,0u,64u,0u};
    if (!transfer(device, 0xc0u, LIVE_USB_INFO, info, sizeof(info))) return 0;
    if (memcmp(info, prefix, sizeof(prefix)) ||
        usb_report_u32(info + 8u) != USB_REPORT_BUILD_ID) {
        fputs("Vendor peer protocol/build mismatch\n", stderr);
        return 0;
    }
    FILE *random = fopen("/dev/urandom", "rb");
    if (!random) { perror("/dev/urandom"); return 0; }
    int ok = fread(nonce, 1u, sizeof(nonce), random) == sizeof(nonce);
    if (fclose(random)) ok = 0;
    if (!ok) { fputs("Cannot read a fresh peer nonce\n", stderr); return 0; }
    if (protected_credentials)
        return collect_protected(device,path,nonce,ap_channel,ap_seconds,protected_credentials,serial,group);
    nonce[14] = monitor ? 2u : 0u;
    if (challenge) {
        memcpy(nonce, challenge, 14u);
        nonce[14] = 1u;
    }
    if (ap_seconds) {
        nonce[12] = network ? (uint8_t)ap_seconds : (uint8_t)ap_cycles;
        nonce[13] = network ? (uint8_t)(ap_seconds >> 8u) : (uint8_t)ap_seconds;
        nonce[14] = network ? CLEAN_NETWORK_AP_MODE : CLEAN_LIFECYCLE_AP_MODE;
        if (ap_channel) {
            nonce[14] = CLEAN_QUALIFICATION_AP_MODE;
            nonce[15] = (uint8_t)ap_channel;
        }
    }
    usb_report_request_for_test(request, USB_REPORT_BUILD_ID, nonce, 3u);
    if (!transfer(device, 0x40u, LIVE_USB_FW_SUBMIT, request, sizeof(request))) return 0;
    int received = 0;
    for (uint32_t i = 0u; i < 500u; ++i) {
        int n = libusb_control_transfer(device, 0xc0u, LIVE_USB_FW_REPORT, 0u, 0u,
                                        report, sizeof(report), 1000u);
        if (n == sizeof(report)) { received = 1; break; }
        if (n != LIBUSB_ERROR_PIPE) {
            fprintf(stderr, "Vendor peer report read failed: %d\n", n);
            return 0;
        }
        usleep(20000u);
    }
    if (!received || !clean_peer_report_valid(report, nonce, USB_REPORT_BUILD_ID)) {
        fputs("Missing or invalid nonce-bound vendor peer report\n", stderr);
        return 0;
    }
    if (network) return collect_network(device,path,nonce,report,trace);
    if (ap_seconds) return collect_ap(device, path, nonce, report, trace,serial);
    if (!read_trace(device, report, trace)) return 0;
    if (!clean_peer_trace_valid(trace) ||
        usb_report_u32(trace + 20u) != (monitor ? 2u : 1u)) {
        fputs("Invalid vendor peer evidence layout\n", stderr);
        return 0;
    }
    if (!write_evidence(path, report, trace)) return 0;
    uint32_t flags = usb_report_u32(report + 32u), completed = usb_report_u32(trace + 24u);
    if (challenge) {
        const uint8_t *t = trace + CLEAN_PEER_TX_DETAIL;
        printf("{\"build\":\"%08x\",\"firmware_error\":%u,\"radio_tx_requested\":true,"
               "\"stage\":%u,\"requested_rate\":%u,\"readback_rate\":%u,"
               "\"frames_submitted\":%u,\"error\":%u,\"controller_status\":%d,"
               "\"last_setup_command\":%u,\"setup_error\":%u,\"channel\":%u,"
               "\"tx_over_air_confirmed\":false}\n",
               USB_REPORT_BUILD_ID, flags >> 8u, usb_report_u32(t), usb_report_u32(t + 4u),
               usb_report_u32(t + 8u), usb_report_u32(t + 12u), usb_report_u32(t + 16u),
               (int32_t)usb_report_u32(t + 20u), usb_report_u32(t + 24u),
               usb_report_u32(t + 28u), usb_report_u32(t + 32u));
        return flags == 11u && usb_report_u32(t) == 5u &&
            usb_report_u32(t + 12u) == CLEAN_PEER_FRAME_COUNT && !usb_report_u32(t + 16u);
    }
    printf("{\"build\":\"%08x\",\"firmware_stage\":%u,\"firmware_error\":%u,"
           "\"firmware_crc\":\"%08x\",\"radio_tx_requested\":false,"
           "\"monitor_probe\":%s,\"monitor_enable_readback_supported\":%s,"
           "\"raw_receive_proven\":false,\"queries\":[",
           USB_REPORT_BUILD_ID, flags & 255u, flags >> 8u, usb_report_u32(trace + 4u),
           monitor ? "true" : "false",
           monitor && clean_peer_monitor_supported(trace) ? "true" : "false");
    for (uint32_t i = 0u; i < completed; ++i) {
        const uint8_t *r = trace + CLEAN_PEER_RECORD_BASE + i * CLEAN_PEER_RECORD_BYTES;
        printf("%s{\"name\":\"%s\",\"ok\":%s,\"error\":%u,\"controller_status\":%d,"
               "\"length\":%u,\"value\":%u}", i ? "," : "",
               monitor ? clean_peer_monitor_operations[i] : clean_peer_variables[i],
               usb_report_u32(r + 4u) ? "true" : "false", usb_report_u32(r + 8u),
               (int32_t)usb_report_u32(r + 12u), usb_report_u32(r + 16u),
               usb_report_u32(r + 20u));
    }
    puts("]}");
    return (flags & 255u) == 11u && !(flags >> 8u) && completed == CLEAN_PEER_RECORDS &&
        (!monitor || clean_peer_monitor_supported(trace));
}

int main(int argc, char **argv) {
    uint8_t challenge[14];
    int tx = argc == 5 && !strcmp(argv[3], "--tx-multicast");
    int monitor = argc == 4 && !strcmp(argv[3], "--monitor-probe");
    int network = argc == 6 && !strcmp(argv[3], "--network-ap");
    int qualification = argc == 7 && !strcmp(argv[3], "--qualification-ap");
    int group = argc == 7 && !strcmp(argv[3], "--protected-traffic-ap");
    int protected = group || (argc == 7 && !strcmp(argv[3], "--protected-ap"));
    int ap = protected || network || qualification || (argc == 7 && !strcmp(argv[3], "--lifecycle-ap"));
    uint32_t ap_seconds = 0u, ap_cycles = 0u, ap_channel = 0u;
    if ((argc != 3 && !tx && !monitor && !ap) || strlen(argv[1]) != 16u) {
        fputs("Usage: collector SERIAL EVIDENCE_BIN [--tx-multicast CHALLENGE_HEX_28 | --monitor-probe | --lifecycle-ap CHALLENGE_HEX_28 SECONDS:8-120 CYCLES:0-5]\n", stderr);
        fputs("Continuous AP: collector SERIAL EVIDENCE_BIN --network-ap CHALLENGE_HEX_28 SECONDS:10-3900\n",stderr);
        fputs("B3 AP: collector SERIAL EVIDENCE_BIN --qualification-ap CHALLENGE_HEX_28 SECONDS:8-120 CHANNEL:1,6,11\n",stderr);
        fputs("E1 AP: collector SERIAL EVIDENCE_BIN --protected-ap CHANNEL:1,6,11 SECONDS:12-120 PRIVATE_CREDENTIAL_OUT\n",stderr);
        fputs("E2 traffic AP: replace --protected-ap with --protected-traffic-ap (does not force rekeys)\n",stderr);
        return 2;
    }
    if ((tx || ap) && !protected) {
        if (strlen(argv[4]) != 28u) return 2;
        for (uint32_t i = 0u; i < 14u; ++i) {
            unsigned value = 0u;
            for (uint32_t j = 0u; j < 2u; ++j) {
                char c = argv[4][2u * i + j];
                if (c >= '0' && c <= '9') value = value * 16u + (unsigned)(c - '0');
                else if (c >= 'a' && c <= 'f') value = value * 16u + (unsigned)(c - 'a') + 10u;
                else { fputs("Challenge must be lowercase hexadecimal\n", stderr); return 2; }
            }
            challenge[i] = (uint8_t)value;
        }
        if (tx && challenge[13] >= 8u) { fputs("Challenge MCS must be 0..7\n", stderr); return 2; }
    }
    if (ap) {
        char *end;
        unsigned long seconds = strtoul(argv[5], &end, 10);
        if (!*argv[5] || *end || seconds < (network?10u:8u) || seconds > (network?3900u:120u)) {
            fputs("AP duration must be lifecycle 8..120 or network 10..3900 seconds\n", stderr); return 2;
        }
        unsigned long cycles = network || protected ? 0u : strtoul(argv[6], &end, 10);
        if (protected) {
            unsigned long channel=strtoul(argv[4],&end,10);
            if (!*argv[4] || *end || channel>11u || !clean_qualification_channel((uint32_t)channel) ||
                seconds<12u) { fputs("E1 AP requires channel 1/6/11 and 12..120 seconds\n",stderr); return 2; }
            ap_channel=(uint32_t)channel;
        }
        if (qualification) {
            if (!*argv[6] || *end || !clean_qualification_channel((uint32_t)cycles) ||
                cycles > 11u) {
                fputs("B3 AP channel must be 1, 6 or 11\n",stderr); return 2;
            }
            ap_channel = (uint32_t)cycles; cycles = 0u;
        }
        if (!network && !qualification && !protected && (!*argv[6] || *end || cycles > 5u ||
            (cycles && seconds < 11u * cycles + 10u))) {
            fputs("Lifecycle AP requires 0..5 outages and time for all cycles\n", stderr); return 2;
        }
        ap_seconds = (uint32_t)seconds; ap_cycles = (uint32_t)cycles;
    }
    for (uint32_t i = 0u; i < 16u; ++i)
        if (!((argv[1][i] >= '0' && argv[1][i] <= '9') ||
              (argv[1][i] >= 'A' && argv[1][i] <= 'F'))) return 2;
    if (signal(SIGALRM, deadline) == SIG_ERR) { perror("signal"); return 1; }
    if (ap && (signal(SIGINT, cancel_ap) == SIG_ERR || signal(SIGTERM, cancel_ap) == SIG_ERR)) {
        perror("signal"); return 1;
    }
    alarm(ap ? ap_seconds + 45u : 18u);
    libusb_context *context = NULL;
    int status = libusb_init(&context);
    if (status) { fprintf(stderr, "USB init: %s\n", libusb_error_name(status)); return 1; }
    libusb_device_handle *device = NULL;
    for (uint32_t i = 0u; i < 30u && !device; ++i) {
        device = find_badge(context, argv[1]);
        if (!device) usleep(50000u);
    }
    if (!device) { fputs("No serial-matched vendor peer\n", stderr); libusb_exit(context); return 1; }
    int configuration;
    status = libusb_get_configuration(device, &configuration);
    if (!status && configuration != 1) status = libusb_set_configuration(device, 1);
    if (!status) status = libusb_claim_interface(device, 0);
    int ok = 0;
    if (!status) ok = collect(device, argv[2], tx || (ap && !protected) ? challenge : NULL,
                             monitor, ap_seconds, ap_cycles, network, ap_channel, argv[1],
                             protected?argv[6]:NULL,group);
    else fprintf(stderr, "USB claim: %s\n", libusb_error_name(status));
    if (fflush(stdout)) { perror("Vendor peer JSON flush"); ok = 0; }
    int rebooted = transfer(device, 0x40u, LIVE_USB_BOOTSEL, NULL, 0u);
    if (!status) libusb_release_interface(device, 0);
    libusb_close(device);
    libusb_exit(context);
    return ok && rebooted ? 0 : 1;
}
