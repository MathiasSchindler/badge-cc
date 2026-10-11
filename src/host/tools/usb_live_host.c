#include <libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "../../usb/live_usb.h"
#include "usb_live_transport.h"
#include "../../radio/wifi_usb_report.h"
#if defined(LIVE_USB_UDP)
#include "../../radio/wifi_link_report.h"
#include "../../radio/wifi_udp_report.h"
#include "usb_udp_build.h"
#elif defined(LIVE_USB_LINK)
#include "../../radio/wifi_link_report.h"
#include "usb_link_build.h"
#elif defined(LIVE_USB_FIRMWARE)
#include "../../radio/wifi_firmware.h"
#include "usb_wifi_build.h"
#else
#include "usb_live_build.h"
#endif

static void collector_deadline(int signal_number) {
    (void)signal_number;
    static const char message[] = "USB collector exceeded process deadline\n";
    ssize_t ignored = write(STDERR_FILENO, message, sizeof(message) - 1u);
    (void)ignored;
    _exit(124);
}

static int read_nonce(uint8_t nonce[USB_REPORT_NONCE_SIZE]) {
    FILE *file = fopen("/dev/urandom", "rb");
    if (file == NULL) {
        perror("/dev/urandom");
        return 0;
    }
    int ok = fread(nonce, 1u, USB_REPORT_NONCE_SIZE, file) == USB_REPORT_NONCE_SIZE;
    if (fclose(file) != 0) ok = 0;
    if (!ok) fputs("Could not obtain fresh run nonce\n", stderr);
    return ok;
}

#ifndef LIVE_USB_LINK
static int reports(libusb_device_handle *device, const char *serial, unsigned count, int wifi) {
    uint8_t info[12];
    if (!transfer(device, 0xc0u, LIVE_USB_INFO, info, sizeof(info))) return 0;
    const uint8_t prefix[] = {'U','2','6','L', 1u, 0u, 64u, 0u};
    if (memcmp(info, prefix, sizeof(prefix)) != 0 || usb_report_u32(info + 8u) != USB_REPORT_BUILD_ID) {
        fprintf(stderr, "Wrong live-report protocol/build: got %08x expected %08x\n",
                (unsigned)usb_report_u32(info + 8u), (unsigned)USB_REPORT_BUILD_ID);
        return 0;
    }
    uint8_t invalid[USB_REPORT_WIRE_SIZE], rejected_reply[USB_REPORT_WIRE_SIZE];
    uint8_t nonce[USB_REPORT_NONCE_SIZE];
    if (!read_nonce(nonce)) return 0;
    usb_report_request(invalid, USB_REPORT_BUILD_ID, nonce);
    invalid[60u] ^= 1u;
    int status = libusb_control_transfer(device, 0x40u, LIVE_USB_SUBMIT, 0u, 0u,
                                         invalid, sizeof(invalid), 1000u);
    if (status != LIBUSB_ERROR_PIPE) {
        fprintf(stderr, "Corrupt request should stall, got %d\n", status);
        return 0;
    }
    status = libusb_control_transfer(device, 0xc0u, LIVE_USB_REPORT, 0u, 0u,
                                     rejected_reply, sizeof(rejected_reply), 1000u);
    if (status != LIBUSB_ERROR_PIPE) {
        fprintf(stderr, "Report without valid request should stall, got %d\n", status);
        return 0;
    }
    if (!transfer(device, 0xc0u, LIVE_USB_INFO, info, sizeof(info))) return 0;
    fputs("Corrupt request and unavailable report rejected; endpoint recovered.\n", stderr);
    for (unsigned run = 0u; run < count; ++run) {
        uint8_t request[USB_REPORT_WIRE_SIZE], report[USB_REPORT_WIRE_SIZE];
        if (!read_nonce(nonce)) return 0;
        usb_report_request_for_test(request, USB_REPORT_BUILD_ID, nonce,
                                    wifi == 3 ? 3u : wifi ? WIFI_USB_TEST_ID : USB_REPORT_TEST_ID);
        if (!transfer(device, 0x40u, wifi == 3 ? LIVE_USB_FW_SUBMIT : wifi == 2 ? LIVE_USB_WIFI_PIO_SUBMIT :
                      wifi ? LIVE_USB_WIFI_SUBMIT : LIVE_USB_SUBMIT,
                      request, sizeof(request))) return 0;
        if (wifi) {
            int received = 0;
            for (unsigned attempt = 0u; attempt < (wifi == 3 ? 500u : 100u); ++attempt) {
                int amount = libusb_control_transfer(device, 0xc0u,
                                                     wifi == 3 ? LIVE_USB_FW_REPORT : LIVE_USB_WIFI_REPORT,
                                                     0u, 0u, report, sizeof(report), 1500u);
                if (amount == sizeof(report)) {
                    received = 1;
                    break;
                }
                if (amount != LIBUSB_ERROR_PIPE) {
                    fprintf(stderr, "Wi-Fi report read failed: %d\n", amount);
                    return 0;
                }
                usleep(20000u);
            }
            if (!received) {
                fputs("Wi-Fi report not ready before host deadline\n", stderr);
                return 0;
            }
        } else if (!transfer(device, 0xc0u, LIVE_USB_REPORT, report, sizeof(report))) return 0;
        wifi_probe_report_t probe;
        usb_record_result_t result;
#ifdef LIVE_USB_FIRMWARE
        wifi_fw_report_t firmware = {0};
        if (wifi == 3) result = wifi_firmware_decode(report, sizeof(report), request, USB_REPORT_BUILD_ID, &firmware);
        else
#endif
        result = wifi ?
            wifi_usb_report_decode(report, sizeof(report), request, USB_REPORT_BUILD_ID, &probe) :
            usb_report_check_response(report, sizeof(report), request, sizeof(request), USB_REPORT_BUILD_ID);
        if (result != USB_RECORD_OK) {
            fprintf(stderr, "Rejected live report: %s\n", usb_report_result_name(result));
            return 0;
        }
        uint8_t trace[WIFI_TRACE_SIZE];
        if (wifi) {
            for (uint8_t page = 0u; page < WIFI_TRACE_PAGES; ++page) {
                uint8_t packet[64];
                int amount = libusb_control_transfer(device, 0xc0u, LIVE_USB_WIFI_TRACE, page, 0u,
                                                     packet, sizeof(packet), 1000u);
                if (amount != 64 || wifi_trace_page_decode(trace, packet, 64u, report, page) != USB_RECORD_OK) {
                    fprintf(stderr, "Trace page %u failed validation/transfer (%d)\n", page, amount);
                    return 0;
                }
            }
            if (wifi != 3 && (usb_report_u32(trace) != WIFI_TRACE_MAGIC || usb_report_u32(trace+8u) > 8u ||
                usb_report_u32(trace+12u) > 20u || usb_report_u32(trace+20u) > 1u)) {
                fputs("Invalid trace version/shape\n", stderr);
                return 0;
            }
        }
#ifdef LIVE_USB_FIRMWARE
        if (wifi == 3) {
            if (usb_report_u32(trace) != 0x31574657u) {
                fputs("Invalid firmware detail format\n", stderr); return 0;
            }
            unsigned version_length = 0u;
            while (version_length < 192u && trace[64u+version_length] != 0u) {
                uint8_t c = trace[64u+version_length++];
                if (c < 0x20u || c > 0x7eu) { fputs("Invalid version characters\n", stderr); return 0; }
            }
            if (version_length == 192u) { fputs("Unterminated version\n", stderr); return 0; }
            if (firmware.error == WIFI_FW_OK &&
                (version_length == 0u || firmware.controller_status != 0u ||
                 (uint64_t)firmware.verified_bytes != (uint64_t)usb_report_u32(trace+4u) + usb_report_u32(trace+12u) ||
                 usb_report_u32(trace+28u) < 3u || usb_report_u32(trace+32u) < 3u)) {
                fputs("Firmware success report lacks complete readback/control evidence\n", stderr); return 0;
            }
            printf("{\"verified\":true,\"test\":\"wifi-firmware\",\"serial\":\"%s\","
                   "\"run\":%u,\"build_id\":\"%08x\",\"nonce\":\"", serial, run+1u, (unsigned)USB_REPORT_BUILD_ID);
            for (unsigned i = 0u; i < sizeof(nonce); ++i) printf("%02x", nonce[i]);
            printf("\",\"firmware_ok\":%s,\"stage\":\"%s\",\"error\":\"%s\","
                   "\"address\":\"%08x\",\"expected\":\"%08x\",\"actual\":\"%08x\","
                   "\"verified_bytes\":%u,\"elapsed_ticks\":%u,\"controller_status\":%u,"
                   "\"firmware_size\":%u,\"firmware_crc\":\"%08x\",\"nvram_size\":%u,"
                   "\"clm_size\":%u,\"tx_packets\":%u,\"rx_packets\":%u,\"version\":\"",
                   firmware.error == WIFI_FW_OK ? "true" : "false",
                   wifi_fw_stage_name(firmware.stage), wifi_fw_error_name(firmware.error),
                   (unsigned)firmware.address, (unsigned)firmware.expected, (unsigned)firmware.actual,
                   (unsigned)firmware.verified_bytes, (unsigned)firmware.elapsed_ticks, (unsigned)firmware.controller_status,
                   (unsigned)usb_report_u32(trace+4u), (unsigned)usb_report_u32(trace+8u),
                   (unsigned)usb_report_u32(trace+12u), (unsigned)usb_report_u32(trace+20u),
                   (unsigned)usb_report_u32(trace+28u), (unsigned)usb_report_u32(trace+32u));
            for (unsigned i = 0u; i < version_length; ++i) {
                if (trace[64u+i] == '"' || trace[64u+i] == '\\') putchar('\\');
                putchar(trace[64u+i]);
            }
            printf("\",\"last_rx_prefix\":\"");
            for (unsigned i = 36u; i < 64u; ++i) printf("%02x", trace[i]);
            printf("\"}\n"); fflush(stdout);
            continue;
        }
#endif
        printf("{\"verified\":true,\"test\":\"%s\",\"transport\":\"live-usb-control\",\"serial\":\"%s\","
               "\"run\":%u,\"build_id\":\"%08x\",\"nonce\":\"",
               wifi ? "wifi-bus-probe" : "known-report", serial, run + 1u, (unsigned)USB_REPORT_BUILD_ID);
        for (unsigned i = 0u; i < sizeof(nonce); ++i) printf("%02x", nonce[i]);
        if (wifi) {
            printf("\",\"probe_ok\":%s,\"stage\":%u,\"stage_name\":\"%s\","
                   "\"error\":%u,\"error_name\":\"%s\",\"reset_pattern\":\"%08x\","
                   "\"normal_pattern\":\"%08x\",\"bus_control\":\"%08x\",\"clock_csr\":\"%02x\","
                   "\"chip_id\":\"%08x\",\"last_value\":\"%08x\",\"attempts\":%u,"
                   "\"passed_reads\":%u,\"elapsed_ticks\":%u,\"powered_at_end\":%u,"
                   "\"crc32\":\"%08x\",\"backend\":\"%s\",\"first_tx\":\"",
                   probe.result == WIFI_BUS_OK ? "true" : "false",
                   (unsigned)probe.stage, wifi_probe_stage_name(probe.stage),
                   (unsigned)probe.result, wifi_bus_result_name(probe.result),
                   (unsigned)probe.reset_pattern, (unsigned)probe.normal_pattern,
                   (unsigned)probe.bus_control, probe.clock_csr, (unsigned)probe.chip_id,
                   (unsigned)probe.last_value, probe.reset_attempts, probe.passed_reads,
                   (unsigned)probe.elapsed_us, probe.powered, (unsigned)usb_report_u32(report + 60u),
                   usb_report_u32(trace+20u) ? "pio" : "software");
            uint32_t tx_length = usb_report_u32(trace+8u), rx_length = usb_report_u32(trace+12u);
            for (uint32_t i = 0u; i < tx_length; ++i) printf("%02x", trace[24u+i]);
            printf("\",\"first_rx\":\"");
            for (uint32_t i = 0u; i < rx_length; ++i) printf("%02x", trace[32u+i]);
            printf("\",\"timer_ticks_over_usb_frames\":%u,\"usb_frames\":%u,\"trace_words\":[",
                   (unsigned)usb_report_u32(trace+52u), (unsigned)usb_report_u32(trace+56u));
            for (uint32_t i = 0u; i < WIFI_TRACE_WORDS; ++i) {
                printf("%s\"%08x\"", i == 0u ? "" : ",", (unsigned)usb_report_u32(trace + 4u*i));
            }
            printf("]}\n");
        } else {
            printf("\",\"value\":\"%08x\",\"crc32\":\"%08x\"}\n",
                   (unsigned)usb_report_u32(report + 36u), (unsigned)usb_report_u32(report + 60u));
        }
        fflush(stdout);
    }
    return 1;
}

int main(int argc, char **argv) {
    char serial[17];
    if (argc < 2 || argc > 4 || strlen(argv[1]) != 16u ||
        (argc == 4 && strcmp(argv[3], "wifi") != 0 && strcmp(argv[3], "wifi-pio") != 0
#ifdef LIVE_USB_FIRMWARE
         && strcmp(argv[3], "wifi-fw") != 0
#endif
         )) {
#ifdef LIVE_USB_FIRMWARE
        fputs("Usage: usb_wifi_firmware_host BOOTSEL_SERIAL [COUNT:1-10] [wifi|wifi-pio|wifi-fw]\n", stderr);
#else
        fputs("Usage: usb_live_host BOOTSEL_SERIAL [COUNT:1-10] [wifi|wifi-pio]\n", stderr);
#endif
        return 2;
    }
    /* A disconnected device can wedge synchronous libusb on some hosts.
       The runner still performs independent BOOTSEL recovery checks. */
    if (signal(SIGALRM, collector_deadline) == SIG_ERR) {
        perror("Cannot install collector deadline");
        return 1;
    }
    alarm(30u);
    for (unsigned i = 0u; i < 16u; ++i) {
        char c = argv[1][i];
        if (c >= 'a' && c <= 'f') c = (char)(c - 'a' + 'A');
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return 2;
        serial[i] = c;
    }
    serial[16] = '\0';
    unsigned count = 3u;
    if (argc >= 3) {
        char *end;
        unsigned long parsed = strtoul(argv[2], &end, 10);
        if (*argv[2] == '\0' || *end != '\0' || parsed < 1u || parsed > 10u) return 2;
        count = (unsigned)parsed;
    }
    libusb_context *context = NULL;
    int result = libusb_init(&context);
    if (result != 0) {
        fprintf(stderr, "libusb init: %s\n", libusb_error_name(result));
        return 1;
    }
    libusb_device_handle *device = NULL;
    for (unsigned attempt = 0u; attempt < 50u && device == NULL; ++attempt) {
        device = find_badge(context, serial);
        if (device == NULL) usleep(100000u);
    }
    if (device == NULL) {
        fputs("No matching live-report USB device appeared. Timed BOOTSEL recovery should follow.\n", stderr);
        libusb_exit(context);
        return 1;
    }
    int configuration = 0;
    result = libusb_get_configuration(device, &configuration);
    if (result == 0 && configuration != 1) result = libusb_set_configuration(device, 1);
    if (result == 0) result = libusb_claim_interface(device, 0);
    if (result != 0) {
        fprintf(stderr, "USB configuration/claim: %s\n", libusb_error_name(result));
        libusb_close(device);
        libusb_exit(context);
        return 1;
    }
    int mode = argc == 4 ? (strcmp(argv[3], "wifi-fw") == 0 ? 3 :
                           strcmp(argv[3], "wifi-pio") == 0 ? 2 : 1) : 0;
    int verified = reports(device, serial, count, mode);
    int rebooted = transfer(device, 0x40u, LIVE_USB_BOOTSEL, NULL, 0u);
    libusb_release_interface(device, 0);
    libusb_close(device);
    libusb_exit(context);
    return verified && rebooted ? 0 : 1;
}
#else
static uint8_t paired_nonce[16];

static int parse_serial(const char *input, char serial[17]) {
    if (strlen(input) != 16u) return 0;
    for (unsigned i = 0u; i < 16u; ++i) {
        char c = input[i];
        if (c >= 'a' && c <= 'f') c = (char)(c - 'a' + 'A');
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return 0;
        serial[i] = c;
    }
    serial[16] = '\0';
    return 1;
}

static void print_mac(const uint8_t mac[6]) {
    for (unsigned i = 0u; i < 6u; ++i) printf("%s%02x", i == 0u ? "" : ":", mac[i]);
}

static void print_link(const char *serial, const wifi_link_report_t *report) {
    printf("{\"verified\":true,\"test\":\"wifi-link\",\"build_id\":\"%08x\","
           "\"serial\":\"%s\",\"role\":\"%s\",\"stage\":\"%s\",\"error\":\"%s\","
           "\"last_command\":%u,\"controller_status\":\"%08x\",\"peer_count\":%u,"
           "\"authorized\":%s,\"mac\":\"", (unsigned)USB_REPORT_BUILD_ID, serial,
           report->role == WIFI_LINK_AP ? "ap" : "station",
           wifi_link_stage_name(report->stage), wifi_link_error_name(report->error),
           (unsigned)report->last_command, (unsigned)report->controller_status,
           (unsigned)report->peer_count, report->authorized ? "true" : "false");
    print_mac(report->mac);
    printf("\",\"peer\":\""); print_mac(report->peer); printf("\"}\n"); fflush(stdout);
}

static void link_diagnostic(libusb_device_handle *device, const char *serial, const uint8_t report[64]) {
    uint8_t detail[WIFI_TRACE_SIZE];
    for (uint8_t page=0u;page<WIFI_TRACE_PAGES;++page) {
        uint8_t packet[64];
        int length=libusb_control_transfer(device,0xc0u,LIVE_USB_WIFI_TRACE,page,0u,packet,64u,1000u);
        if (length!=64 || wifi_trace_page_decode(detail,packet,64u,report,page)!=USB_RECORD_OK) {
            fprintf(stderr,"Failed link diagnostic page %u for %s\n",page,serial); return;
        }
    }
    if (usb_report_u32(detail)!=0x314b4e4cu || detail[6]>31u) {
        fputs("Invalid link diagnostic format\n",stderr); return;
    }
    for (unsigned i=0u;i<detail[6];++i) {
        uint8_t c=detail[8u+i];
        if (!((c>='a'&&c<='z') || (c>='0'&&c<='9') || c==':' || c=='_')) {
            fputs("Invalid link diagnostic name\n",stderr); return;
        }
    }
    printf("{\"test\":\"wifi-link-diagnostic\",\"serial\":\"%s\",\"interface\":%u,\"set\":%u,"
           "\"iovar\":\"%.*s\",\"firmware_error\":%u,\"verified_bytes\":%u,"
           "\"expected\":\"%08x\",\"actual\":\"%08x\",\"firmware_stage\":%u,"
           "\"firmware_elapsed_ticks\":%u,\"firmware_address\":\"%08x\","
           "\"firmware_service_ticks\":%u,\"firmware_transfer_ticks\":%u,"
           "\"service_usb_ticks\":%u,\"service_publish_ticks\":%u,\"service_draw_ticks\":%u,"
           "\"view_phase\":%u,\"view_tile\":%u,\"pio_setup_ticks\":%u,"
           "\"pio_pump_ticks\":%u,\"pio_cleanup_ticks\":%u,\"control_prefix\":\"",
           serial,detail[4],detail[5],detail[6],(const char *)detail+8u,
           (unsigned)usb_report_u32(detail+40u),(unsigned)usb_report_u32(detail+44u),
           (unsigned)usb_report_u32(detail+48u),(unsigned)usb_report_u32(detail+52u),
           (unsigned)usb_report_u32(detail+56u),
           (unsigned)usb_report_u32(detail+60u),(unsigned)usb_report_u32(detail+92u),
           (unsigned)usb_report_u32(detail+96u),(unsigned)usb_report_u32(detail+100u),
           (unsigned)usb_report_u32(detail+104u),(unsigned)usb_report_u32(detail+108u),
           (unsigned)usb_report_u32(detail+112u),(unsigned)usb_report_u32(detail+116u),
           (unsigned)usb_report_u32(detail+120u),(unsigned)usb_report_u32(detail+124u),
           (unsigned)usb_report_u32(detail+128u),(unsigned)usb_report_u32(detail+132u));
    for (unsigned i=64u;i<92u;++i) printf("%02x",detail[i]);
    printf("\"}\n");
    fflush(stdout);
}

static int paired_reports(libusb_device_handle *devices[2], char serials[2][17]) {
    uint8_t *nonce=paired_nonce;
    uint8_t request[64];
    if (!read_nonce(nonce)) return 0;
    usb_report_request_for_test(request, USB_REPORT_BUILD_ID, nonce, WIFI_LINK_TEST_ID);
    printf("{\"test\":\"wifi-link-session\",\"ssid\":\"U26LAB-%02x%02x%02x%02x\"}\n",
           nonce[0],nonce[1],nonce[2],nonce[3]);
    for (unsigned role = 0u; role < 2u; ++role) {
        uint8_t info[12];
        const uint8_t prefix[] = {'U','2','6','L',1u,0u,64u,0u};
        if (!transfer(devices[role],0xc0u,LIVE_USB_INFO,info,sizeof(info)) ||
            memcmp(info,prefix,sizeof(prefix)) != 0 || usb_report_u32(info+8u) != USB_REPORT_BUILD_ID) {
            fputs("Paired device protocol/build mismatch\n",stderr); return 0;
        }
    }
    for (unsigned role = 0u; role < 2u; ++role) {
        uint8_t command = role == 0u ? LIVE_USB_LINK_AP : LIVE_USB_LINK_STATION;
        if (!transfer(devices[role],0x40u,command,request,sizeof(request))) return 0;
    }
    uint8_t last[2][64] = {{0}};
    wifi_link_report_t reports[2] = {{0}};
    unsigned stable = 0u;
#ifdef LIVE_USB_UDP
    const unsigned observations = 600u;
#else
    const unsigned observations = 240u;
#endif
    for (unsigned attempt = 0u; attempt < observations; ++attempt) {
        int complete = 1;
        for (unsigned role = 0u; role < 2u; ++role) {
            uint8_t bytes[64];
            int length = libusb_control_transfer(devices[role],0xc0u,LIVE_USB_LINK_REPORT,0u,0u,
                                                 bytes,sizeof(bytes),1500u);
            if (length == LIBUSB_ERROR_PIPE) { complete = 0; continue; }
            if (length != 64) {
                fprintf(stderr,"Paired report transfer failed for %s: %d\n",serials[role],length); return 0;
            }
            usb_record_result_t result = wifi_link_report_decode(bytes,USB_REPORT_BUILD_ID,nonce,
                                                                  (uint8_t)role,&reports[role]);
            if (result != USB_RECORD_OK) {
                fprintf(stderr,"Rejected paired report for %s: %s\n",serials[role],usb_report_result_name(result)); return 0;
            }
            if (memcmp(bytes,last[role],64u) != 0) {
                print_link(serials[role],&reports[role]);
                memcpy(last[role],bytes,64u);
            }
            if (reports[role].stage == WIFI_LINK_FAILED) {
                link_diagnostic(devices[role],serials[role],bytes); return 0;
            }
            if (reports[role].stage != WIFI_LINK_CONNECTED || !reports[role].authorized) complete = 0;
        }
        if (complete && memcmp(reports[0].mac,reports[1].peer,6u) == 0 &&
            memcmp(reports[1].mac,reports[0].peer,6u) == 0 &&
            memcmp(reports[0].mac,reports[1].mac,6u) != 0) {
            if (++stable >= 20u) {
                printf("{\"test\":\"wifi-link-pair\",\"association_ok\":true,"
                       "\"both_authorized\":%s,\"stable_observations\":%u}\n",
                       reports[0].authorized && reports[1].authorized ? "true" : "false",stable);
                fflush(stdout); return 1;
            }
        } else stable = 0u;
        usleep(100000u);
    }
    fputs("No matching stable association before paired host deadline\n",stderr);
    return 0;
}

#ifdef LIVE_USB_UDP
static int udp_results(libusb_device_handle *device, const char *serial, uint8_t role,
                        wifi_udp_summary_t *summary) {
    uint8_t report[64],detail[WIFI_UDP_DETAIL_SIZE];
    if(!transfer(device,0xc0u,LIVE_USB_UDP_FREEZE,report,64u))return 0;
    if(wifi_udp_report_decode(report,USB_REPORT_BUILD_ID,paired_nonce,role,summary)!=USB_RECORD_OK) {
        fputs("Frozen UDP summary failed validation\n",stderr); return 0;
    }
    for(uint8_t page=0u;page<WIFI_UDP_DETAIL_PAGES;++page) {
        uint8_t bytes[64];
        int length=libusb_control_transfer(device,0xc0u,LIVE_USB_UDP_PAGE,page,0u,bytes,64u,1000u);
        if(length!=64 || wifi_udp_page_decode(detail,bytes,report,page)!=USB_RECORD_OK) {
            fprintf(stderr,"UDP result page %u failed validation\n",page); return 0;
        }
    }
    if(wifi_udp_detail_check(detail,summary)!=USB_RECORD_OK) {
        fputs("UDP result counts/calibration/sample consistency failed\n",stderr); return 0;
    }
    uint32_t sorted[WIFI_UDP_SAMPLES], count=0u;
    for(unsigned i=0u;i<WIFI_UDP_SAMPLES;++i) {
        uint32_t value=usb_report_u32(detail+WIFI_UDP_SAMPLE_OFFSET+i*4u);
        if(!value)continue;
        unsigned j=count;
        while(j && sorted[j-1u]>value) { sorted[j]=sorted[j-1u]; --j; }
        sorted[j]=value; ++count;
    }
    double scale=(double)usb_report_u32(detail+48u)/(double)summary->calibration_ticks;
    printf("{\"verified\":true,\"test\":\"wifi-udp-results\",\"build_id\":\"%08x\",\"serial\":\"%s\",\"role\":\"%s\","
           "\"ip\":\"192.168.77.%u\",\"sent\":%u,\"received\":%u,\"lost\":%u,\"loss_percent\":%.2f,"
           "\"echoed\":%u,\"dropped\":%u,\"duplicates\":%u,\"arp_tx\":%u,\"arp_rx\":%u,"
           "\"tx_frames\":%u,\"rx_frames\":%u,\"radio_drops\":%u,\"last_drop\":%u,"
           "\"calibration_ticks\":%u,\"usb_frames\":%u,\"elapsed_ticks\":%u,",
           (unsigned)USB_REPORT_BUILD_ID,serial,role==0u?"ap":"station",role+1u,(unsigned)summary->sent,(unsigned)summary->received,
           (unsigned)summary->lost,summary->sent?100.0*summary->lost/summary->sent:0.0,
           (unsigned)summary->echoed,(unsigned)summary->dropped,(unsigned)usb_report_u32(detail+32u),
           (unsigned)usb_report_u32(detail+36u),(unsigned)usb_report_u32(detail+40u),
           (unsigned)usb_report_u32(detail+56u),(unsigned)usb_report_u32(detail+60u),
           (unsigned)usb_report_u32(detail+64u),(unsigned)usb_report_u32(detail+80u),
           (unsigned)summary->calibration_ticks,(unsigned)usb_report_u32(detail+48u),
           (unsigned)summary->elapsed_ticks);
    if(count) {
        printf("\"rtt_ms\":{\"min\":%.3f,\"mean\":%.3f,\"p50\":%.3f,\"p95\":%.3f,\"p99\":%.3f,\"max\":%.3f},",
               sorted[0]*scale,(double)usb_report_u32(detail+76u)*scale/count,
               sorted[(count*50u+99u)/100u-1u]*scale,sorted[(count*95u+99u)/100u-1u]*scale,
               sorted[(count*99u+99u)/100u-1u]*scale,sorted[count-1u]*scale);
    } else printf("\"rtt_ms\":null,");
    printf("\"samples_ticks\":[");
    for(unsigned i=0u;i<WIFI_UDP_SAMPLES;++i) {
        printf("%s%u",i?",":"",(unsigned)usb_report_u32(detail+WIFI_UDP_SAMPLE_OFFSET+i*4u));
    }
    printf("]}\n"); fflush(stdout);
    return 1;
}

static int udp_reports(libusb_device_handle *devices[2], char serials[2][17]) {
    uint8_t request[64];
    usb_report_request_for_test(request,USB_REPORT_BUILD_ID,paired_nonce,WIFI_LINK_TEST_ID);
    if(!transfer(devices[0],0x40u,LIVE_USB_UDP_START,request,64u) ||
       !transfer(devices[1],0x40u,LIVE_USB_UDP_START,request,64u))return 0;
    wifi_udp_summary_t summaries[2]={{0}};
    uint32_t last_bucket[2]={0xffffffffu,0xffffffffu};
    uint8_t last_stage[2]={255u,255u};
    for(unsigned attempt=0u;attempt<600u;++attempt) {
        int complete=1;
        for(uint8_t role=0u;role<2u;++role) {
            uint8_t report[64];
            int length=libusb_control_transfer(devices[role],0xc0u,LIVE_USB_UDP_REPORT,0u,0u,report,64u,1500u);
            if(length==LIBUSB_ERROR_PIPE) { complete=0; continue; }
            if(length!=64) { fprintf(stderr,"UDP report transfer %s: %d\n",serials[role],length); return 0; }
            usb_record_result_t result=wifi_udp_report_decode(report,USB_REPORT_BUILD_ID,paired_nonce,role,&summaries[role]);
            if(result!=USB_RECORD_OK) { fprintf(stderr,"UDP report rejected: %s\n",usb_report_result_name(result)); return 0; }
            wifi_udp_summary_t *s=&summaries[role];
            if(s->stage!=last_stage[role] || s->sent/10u!=last_bucket[role]) {
                printf("{\"verified\":true,\"test\":\"wifi-udp-progress\",\"serial\":\"%s\",\"role\":\"%s\","
                       "\"stage\":\"%s\",\"error\":\"%s\",\"sent\":%u,\"received\":%u,\"lost\":%u,\"dropped\":%u}\n",
                       serials[role],role==0u?"ap":"station",wifi_udp_stage_name((wifi_udp_stage_t)s->stage),
                       wifi_udp_error_name((wifi_udp_error_t)s->error),(unsigned)s->sent,
                       (unsigned)s->received,(unsigned)s->lost,(unsigned)s->dropped);
                fflush(stdout);
                last_stage[role]=s->stage; last_bucket[role]=s->sent/10u;
            }
            if(s->stage==WIFI_UDP_FAILED) {
                udp_results(devices[role],serials[role],role,s);
                uint8_t link_report[64];
                if(transfer(devices[role],0xc0u,LIVE_USB_LINK_REPORT,link_report,64u))
                    link_diagnostic(devices[role],serials[role],link_report);
                return 0;
            }
            if(s->stage!=WIFI_UDP_DONE)complete=0;
        }
        if(complete) {
            if(!udp_results(devices[0],serials[0],0u,&summaries[0]) ||
               !udp_results(devices[1],serials[1],1u,&summaries[1]))return 0;
            int ok=summaries[0].received>0u && summaries[1].received>0u &&
                   summaries[0].received<=summaries[1].echoed && summaries[1].received<=summaries[0].echoed;
            printf("{\"test\":\"wifi-udp-pair\",\"data_ok\":%s,\"attempts_each\":%u}\n",
                   ok?"true":"false",WIFI_UDP_SAMPLES); fflush(stdout);
            return ok;
        }
        usleep(100000u);
    }
    fputs("UDP benchmark did not finish within the host observation window\n",stderr);
    return 0;
}
#endif

int main(int argc, char **argv) {
    char serials[2][17];
    if (argc != 3 || !parse_serial(argv[1],serials[0]) || !parse_serial(argv[2],serials[1]) ||
        strcmp(serials[0],serials[1]) == 0) {
        fputs("Usage: usb_wifi_link_host AP_SERIAL STATION_SERIAL (distinct chip serials)\n",stderr); return 2;
    }
    if (signal(SIGALRM,collector_deadline) == SIG_ERR) { perror("Collector deadline"); return 1; }
#ifdef LIVE_USB_UDP
    alarm(90u);
#else
    alarm(60u);
#endif
    libusb_context *context = NULL;
    int error = libusb_init(&context);
    if (error != 0) { fprintf(stderr,"USB init: %s\n",libusb_error_name(error)); return 1; }
    libusb_device_handle *devices[2] = {NULL,NULL};
    int claimed[2] = {0,0}, ok = 0;
    for (unsigned attempt = 0u; attempt < 50u && (!devices[0] || !devices[1]); ++attempt) {
        for (unsigned role = 0u; role < 2u; ++role) {
            if (!devices[role]) devices[role] = find_badge(context,serials[role]);
        }
        if (!devices[0] || !devices[1]) usleep(100000u);
    }
    for (unsigned role = 0u; role < 2u; ++role) {
        if (!devices[role]) { fprintf(stderr,"No live interface for %s\n",serials[role]); goto cleanup; }
        int configuration = 0;
        error = libusb_get_configuration(devices[role],&configuration);
        if (error == 0 && configuration != 1) error = libusb_set_configuration(devices[role],1);
        if (error == 0) error = libusb_claim_interface(devices[role],0);
        if (error != 0) { fprintf(stderr,"USB claim %s: %s\n",serials[role],libusb_error_name(error)); goto cleanup; }
        claimed[role] = 1;
    }
    ok = paired_reports(devices,serials);
#ifdef LIVE_USB_UDP
    if(ok)ok=udp_reports(devices,serials);
#endif
cleanup:
    for (unsigned role = 0u; role < 2u; ++role) {
        if (!devices[role]) continue;
        if (claimed[role]) {
            if (!transfer(devices[role],0x40u,LIVE_USB_BOOTSEL,NULL,0u)) ok = 0;
            libusb_release_interface(devices[role],0);
        }
        libusb_close(devices[role]);
    }
    libusb_exit(context);
    return ok ? 0 : 1;
}
#endif
