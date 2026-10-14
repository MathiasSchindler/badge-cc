#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include "build_id.h"
#include "../../src/host/tools/usb_live_transport.h"
#include "../../src/shared/bytes.h"
#include "../../src/radio/wifi_usb_report.h"
#include "../../src/tools/diagnostics_logic.h"
#include "loader.h"
#include "frames.h"

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
    int raw_valid = rxe >= 6u && clean_frame_valid(detail + 584u, rxe - 6u);
    int f2_valid = f2 >= 18u && f2 <= 522u && clean_frame_valid(detail + 1122u, f2 - 18u);
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
           "\"local_ap_matches\":%u,\"probe_response_fcs_valid\":%s}\n",
           usb_report_u32(detail + 28u), raw_valid ? "true" : "false",
           f2_valid ? "true" : "false", matches,
           f2_valid && detail[1122] == 0x50u &&
           memcmp(detail + 1126u, detail + 1972u, 6u) == 0 ? "true" : "false");
    return raw_valid && f2_valid;
}

static void deadline(int signum) {
    (void)signum;
    static const char text[] = "Collector deadline; badge has independent BOOTSEL recovery\n";
    ssize_t result = write(STDERR_FILENO, text, sizeof(text) - 1u);
    (void)result;
    _exit(124);
}

static int collect(libusb_device_handle *device, const char *path, uint8_t channel, uint8_t tx) {
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
    nonce[15] = channel;
    nonce[14] = tx;
    usb_report_request_for_test(request, USB_REPORT_BUILD_ID, nonce, WIFI_USB_TEST_ID);
    if (!transfer(device, 0x40u, LIVE_USB_WIFI_PIO_SUBMIT, request, sizeof(request))) return 0;
    int got = 0;
    for (unsigned attempt = 0u; attempt < 600u; ++attempt) {
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
    printf("{\"build\":\"%08x\",\"channel\":%u,\"stage\":%u,\"error\":%u,"
           "\"address\":\"%08x\",\"actual\":\"%08x\",\"verified_bytes\":%u,"
           "\"elapsed_ticks\":%u,\"fw_magic\":\"%08x\",\"fw_state\":%u,"
           "\"fw_error\":%u,\"fw_error_address\":\"%08x\",\"fw_error_value\":\"%08x\","
           "\"captured_frames\":%u,\"dma_frames\":%u,\"f2_packets\":%u,\"beacons\":%u,"
           "\"tx_requested\":%u,\"tx_command_completed\":%u,"
           "\"tx_marker\":\"%08x\",\"tx_status\":\"%08x\",\"probe_responses\":%u,\"raw_prefix\":\"",
           USB_REPORT_BUILD_ID, usb_report_u32(detail + 28u),
           usb_report_u32(detail + 4u), usb_report_u32(detail + 8u),
           usb_report_u32(detail + 12u), usb_report_u32(detail + 16u),
           usb_report_u32(detail + 20u), usb_report_u32(detail + 24u),
           usb_report_u32(detail + 32u), usb_report_u32(detail + 36u),
           usb_report_u32(detail + 44u), usb_report_u32(detail + 48u),
           usb_report_u32(detail + 52u), usb_report_u32(detail + 488u),
           usb_report_u32(detail + 508u), usb_report_u32(detail + 1636u),
           usb_report_u32(detail + 1632u), usb_report_u32(detail + 1092u),
           usb_report_u32(detail + 1096u), usb_report_u32(detail + 76u),
           usb_report_u32(detail + 80u), usb_report_u32(detail + 1960u));
    for (unsigned i = 576u; i < 688u; ++i) printf("%02x", detail[i]);
    puts("\"}");
    if (tx == 2u)
        return usb_report_u32(detail + 8u) == CLEAN_CONTROLLER &&
               usb_report_u32(detail + 36u) == 255u &&
               usb_report_u32(detail + 44u) == 0x81u &&
               usb_report_u32(detail + 48u) == CLEAN_MBOX + 24u &&
               usb_report_u32(detail + 1092u) == 1u &&
               usb_report_u32(detail + 1096u) == 0u;
    return (!tx || (usb_report_u32(detail + 1096u) == 1u &&
                   usb_report_u32(detail + 76u) == 0x5555005eu)) &&
           usb_report_u32(detail + 8u) == CLEAN_OK &&
           usb_report_u32(detail + 4u) == 8u &&
           usb_report_u32(detail + 32u) == 0x4f505258u &&
           usb_report_u32(detail + 36u) == 10u;
}

int main(int argc, char **argv) {
    if (argc == 4 && strcmp(argv[1], "--inspect") == 0)
        return inspect(argv[2], argv[3]) ? 0 : 1;
    if ((argc != 4 && argc != 5) || strlen(argv[1]) != 16u ||
        (argc == 5 && strcmp(argv[4], "--tx-one-probe") != 0 &&
                      strcmp(argv[4], "--reject-tx") != 0)) {
        fputs("usage: collector BADGE_SERIAL EVIDENCE_BIN CHANNEL:1-11 [--tx-one-probe|--reject-tx]\n", stderr);
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
    uint8_t mode = argc == 5 ? (strcmp(argv[4], "--reject-tx") == 0 ? 2u : 1u) : 0u;
    if (status == 0) ok = collect(device, argv[2], (uint8_t)channel, mode);
    else fprintf(stderr, "USB claim: %s\n", libusb_error_name(status));
    int rebooted = transfer(device, 0x40u, LIVE_USB_BOOTSEL, NULL, 0u);
    if (status == 0) libusb_release_interface(device, 0);
    libusb_close(device);
    libusb_exit(context);
    return ok && rebooted ? 0 : 1;
}
