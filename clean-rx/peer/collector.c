#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include "protocol.h"
#include "frame.h"
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

static int collect(libusb_device_handle *device, const char *path, const uint8_t *challenge) {
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
    nonce[14] = 0u;
    if (challenge) {
        memcpy(nonce, challenge, 14u);
        nonce[14] = 1u;
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
    if (!clean_peer_trace_valid(trace)) {
        fputs("Invalid vendor peer evidence layout\n", stderr);
        return 0;
    }
    FILE *out = fopen(path, "wb");
    if (!out) { perror(path); return 0; }
    ok = fwrite(report, 1u, sizeof(report), out) == sizeof(report) &&
         fwrite(trace, 1u, sizeof(trace), out) == sizeof(trace);
    if (fclose(out)) ok = 0;
    if (!ok) { fputs("Vendor peer evidence write failed\n", stderr); return 0; }
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
           "\"firmware_crc\":\"%08x\",\"radio_tx_requested\":false,\"queries\":[",
           USB_REPORT_BUILD_ID, flags & 255u, flags >> 8u, usb_report_u32(trace + 4u));
    for (uint32_t i = 0u; i < completed; ++i) {
        const uint8_t *r = trace + CLEAN_PEER_RECORD_BASE + i * CLEAN_PEER_RECORD_BYTES;
        printf("%s{\"name\":\"%s\",\"ok\":%s,\"error\":%u,\"controller_status\":%d,"
               "\"length\":%u,\"value\":%u}", i ? "," : "", clean_peer_variables[i],
               usb_report_u32(r + 4u) ? "true" : "false", usb_report_u32(r + 8u),
               (int32_t)usb_report_u32(r + 12u), usb_report_u32(r + 16u),
               usb_report_u32(r + 20u));
    }
    puts("]}");
    return (flags & 255u) == 11u && !(flags >> 8u) && completed == CLEAN_PEER_RECORDS;
}

int main(int argc, char **argv) {
    uint8_t challenge[14];
    int tx = argc == 5 && !strcmp(argv[3], "--tx-multicast");
    if ((argc != 3 && !tx) || strlen(argv[1]) != 16u) {
        fputs("Usage: collector SERIAL EVIDENCE_BIN [--tx-multicast CHALLENGE_HEX_28]\n", stderr);
        return 2;
    }
    if (tx) {
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
        if (challenge[13] >= 8u) { fputs("Challenge MCS must be 0..7\n", stderr); return 2; }
    }
    for (uint32_t i = 0u; i < 16u; ++i)
        if (!((argv[1][i] >= '0' && argv[1][i] <= '9') ||
              (argv[1][i] >= 'A' && argv[1][i] <= 'F'))) return 2;
    if (signal(SIGALRM, deadline) == SIG_ERR) { perror("signal"); return 1; }
    alarm(18u);
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
    if (!status) ok = collect(device, argv[2], tx ? challenge : NULL);
    else fprintf(stderr, "USB claim: %s\n", libusb_error_name(status));
    if (fflush(stdout)) { perror("Vendor peer JSON flush"); ok = 0; }
    int rebooted = transfer(device, 0x40u, LIVE_USB_BOOTSEL, NULL, 0u);
    if (!status) libusb_release_interface(device, 0);
    libusb_close(device);
    libusb_exit(context);
    return ok && rebooted ? 0 : 1;
}
