#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "activity_build.h"
#include "../../host/tools/usb_live_transport.h"
#include "../../radio/wifi_usb_report.h"
#include "../diagnostics_logic.h"

int main(int argc, char **argv) {
    if ((argc != 2 && argc != 3) || strlen(argv[1]) != 16u ||
        strspn(argv[1], "0123456789ABCDEF") != 16u ||
        (argc == 3 && strcmp(argv[2], "--bootsel") && strcmp(argv[2], "--fast") &&
         strcmp(argv[2], "--slow"))) {
        fputs("Usage: activity_collect SERIAL [--bootsel|--fast|--slow]\n", stderr); return 2;
    }
    libusb_context *context = NULL;
    if (libusb_init(&context)) { fputs("USB initialization failed\n", stderr); return 1; }
    libusb_device_handle *device = NULL;
    for (uint32_t i = 0u; i < 100u && !device; ++i) {
        device = find_badge(context, argv[1]);
        if (!device) usleep(20000u);
    }
    if (!device) { fputs("Badge not found\n", stderr); libusb_exit(context); return 1; }
    uint8_t info[12];
    int ok = transfer(device, 0xc0u, LIVE_USB_INFO, info, sizeof(info));
    if (ok && (memcmp(info, "U26L\1\0\100\0", 8u) ||
        usb_report_u32(info + 8u) != ACTIVITY_BUILD_ID)) {
        fputs("Wrong application build\n", stderr); ok = 0;
    }
    int config = 0;
    if (ok && (libusb_get_configuration(device, &config) ||
        (config != 1 && libusb_set_configuration(device, 1)))) {
        fputs("USB configuration failed\n", stderr); ok = 0;
    }
    if (argc == 3 && !strcmp(argv[2], "--bootsel")) {
        if (ok) ok = transfer(device, 0x40u, LIVE_USB_BOOTSEL, NULL, 0u);
        libusb_close(device); libusb_exit(context);
        return ok ? 0 : 1;
    }
    uint32_t initial_sweeps = 0u, initial_frames = 0u, last_sweeps = 0u, last_frames = 0u;
    uint32_t initial_rendered = 0u, last_rendered = 0u;
    uint32_t initial_updates = 0u, last_updates = 0u, last_dwell = 0u;
    uint32_t requested = argc == 3 ? (!strcmp(argv[2], "--fast") ? 50000u : 150000u) : 0u;
    for (uint32_t round = 0u; ok && round < 20u; ++round) {
        uint8_t nonce[16], request[64], reply[64];
        arc4random_buf(nonce, sizeof(nonce));
        if (requested) { nonce[14] = 0x57u; nonce[15] = requested == 50000u ? 1u : 2u; }
        usb_report_request_for_test(request, ACTIVITY_BUILD_ID, nonce, WIFI_USB_TEST_ID);
        ok = transfer(device, 0x40u, LIVE_USB_WIFI_SUBMIT, request, 64u);
        int got = 0;
        for (uint32_t p = 0u; ok && p < 20u; ++p) {
            int n = libusb_control_transfer(device, 0xc0u, LIVE_USB_WIFI_REPORT, 0u, 0u, reply, 64u, 1000u);
            if (n == 64) { got = 1; break; }
            if (n != LIBUSB_ERROR_PIPE) { fprintf(stderr, "Report failure: %d\n", n); ok = 0; }
            usleep(10000u);
        }
        if (!got) { fputs("Missing activity report\n", stderr); ok = 0; break; }
        if (usb_report_u32(reply) != 0x57363255u ||
            memcmp(reply + 4u, request + 4u, 28u) ||
            usb_report_u32(reply + 60u) != diag_crc32(reply, 60u) ||
            usb_report_u32(reply + 56u) != 0x31414657u ||
            usb_report_u32(reply + 48u)) {
            fputs("Invalid or failed activity report\n", stderr); ok = 0; break;
        }
        last_sweeps = usb_report_u32(reply + 32u);
        last_frames = usb_report_u32(reply + 36u);
        last_rendered = usb_report_u32(reply + 44u);
        uint32_t state = usb_report_u32(reply + 52u), channel = state & 255u;
        if (channel > 11u || state & ~0x1ffu || last_rendered < last_sweeps ||
            (round && (last_sweeps < initial_sweeps || last_frames < initial_frames))) {
            fputs("Invalid activity counters/channel\n", stderr); ok = 0; break;
        }
        if (!round) {
            initial_sweeps = last_sweeps; initial_frames = last_frames;
            initial_rendered = last_rendered;
        }
        uint8_t trace[WIFI_TRACE_SIZE] = {0};
        for (uint32_t page = 0u; ok && page < WIFI_TRACE_PAGES; ++page) {
            uint8_t bytes[64];
            int n = libusb_control_transfer(device, 0xc0u, LIVE_USB_WIFI_TRACE,
                                            (uint16_t)page, 0u, bytes, 64u, 1000u);
            if (n != 64 || wifi_trace_page_decode(trace, bytes, 64u, reply,
                                                  (uint8_t)page) != USB_RECORD_OK) {
                fputs("Invalid activity timing trace\n", stderr); ok = 0;
            }
        }
        if (!ok) break;
        last_updates = usb_report_u32(trace);
        last_dwell = usb_report_u32(trace + 8u);
        if (!round) initial_updates = last_updates;
        if ((last_dwell != 50000u && last_dwell != 150000u) ||
            ((state & 0x100u) != 0u) != (last_dwell == 50000u)) {
            fputs("Inconsistent scan-mode report\n", stderr); ok = 0; break;
        }
        printf("{\"build\":\"%08x\",\"sweeps\":%u,\"frames\":%u,\"rejected\":%u,"
               "\"rendered\":%u,\"error\":%u,\"channel\":%u,\"dwell_us\":%u,"
               "\"channel_updates\":%u,\"sweep_us\":%u}\n", ACTIVITY_BUILD_ID,
               last_sweeps, last_frames, usb_report_u32(reply + 40u),
               usb_report_u32(reply + 44u), usb_report_u32(reply + 48u), channel,
               last_dwell, last_updates, usb_report_u32(trace + 4u));
        fflush(stdout);
        usleep(1000000u);
    }
    if (last_sweeps <= initial_sweeps || last_frames <= initial_frames ||
        last_rendered <= initial_rendered || last_updates <= initial_updates ||
        (requested && last_dwell != requested)) {
        fputs("No ongoing sweep/frame/TFT-update progress\n", stderr); ok = 0;
    }
    libusb_close(device); libusb_exit(context);
    return ok ? 0 : 1;
}
