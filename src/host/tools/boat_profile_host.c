#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "usb_live_transport.h"
#include "../../shared/boat_measure.h"
#include "../../shared/boat_physics.h"
#include "boat_profile_build.h"

static void deadline(int signal_number) {
    (void)signal_number;
    static const char text[] = "Boat collector deadline exceeded\n";
    ssize_t ignored = write(STDERR_FILENO, text, sizeof(text) - 1u);
    (void)ignored;
    _exit(124);
}

int main(int argc, char **argv) {
    if (argc != 2 || strlen(argv[1]) != 16u || strspn(argv[1], "0123456789ABCDEF") != 16u) {
        fputs("Usage: boat_profile_host SERIAL\n", stderr); return 2;
    }
    signal(SIGALRM, deadline);
    alarm(650u);
    libusb_context *context = NULL;
    int status = libusb_init(&context);
    if (status != 0) { fprintf(stderr, "libusb initialization: %s\n", libusb_error_name(status)); return 1; }
    libusb_device_handle *device = NULL;
    for (unsigned i = 0u; i < 50u && device == NULL; ++i) {
        device = find_badge(context, argv[1]);
        if (device == NULL) usleep(100000u);
    }
    int ok = 0;
    if (device == NULL) { fputs("Selected boat diagnostic did not enumerate\n", stderr); goto cleanup; }
    uint8_t info[12];
    const uint8_t prefix[] = {'U','2','6','L',1u,0u,64u,0u};
    if (!transfer(device, 0xc0u, LIVE_USB_INFO, info, 12u)) goto cleanup;
    if (memcmp(info, prefix, 8u) != 0 || usb_report_u32(info + 8u) != USB_REPORT_BUILD_ID) {
        fputs("Wrong boat build/protocol\n", stderr); goto cleanup;
    }
    uint8_t nonce[16], request[64], packet[64];
    FILE *random = fopen("/dev/urandom", "rb");
    if (random == NULL) { perror("/dev/urandom"); goto cleanup; }
    size_t amount = fread(nonce, 1u, sizeof(nonce), random);
    int closed = fclose(random);
    if (amount != sizeof(nonce) || closed != 0) { fputs("Could not obtain a fresh nonce\n", stderr); goto cleanup; }
    usb_report_request_for_test(request, USB_REPORT_BUILD_ID, nonce, NAMETAG_M2_TEST);
    if (!transfer(device, 0x40u, LIVE_USB_M2_SUBMIT, request, 64u)) goto cleanup;
    fprintf(stderr, "Boat %s: ten-minute deterministic tilt/settling workload; real IMU monitored separately\n", argv[1]);
    int ready = 0;
    for (unsigned i = 0u; i < 2520u; ++i) {
        status = libusb_control_transfer(device, 0xc0u, LIVE_USB_M2_PAGE, 0u, 0u, packet, 64u, 1000u);
        if (status == 64) { ready = 1; break; }
        if (status != LIBUSB_ERROR_PIPE) {
            fprintf(stderr, "Boat readiness: %s (%d)\n", libusb_error_name(status), status); goto cleanup;
        }
        usleep(250000u);
    }
    if (!ready) { fputs("Boat report not ready before deadline\n", stderr); goto cleanup; }
    uint32_t words[NAMETAG_M2_WORDS];
    for (uint32_t page = 0u; page < NAMETAG_M2_PAGES; ++page) {
        if (page != 0u) {
            status = libusb_control_transfer(device, 0xc0u, LIVE_USB_M2_PAGE, (uint16_t)page,
                                            0u, packet, 64u, 1000u);
            if (status != 64) { fprintf(stderr, "Boat page %u transfer: %d\n", (unsigned)page, status); goto cleanup; }
        }
        usb_record_result_t result = nametag_m2_page_decode(words, packet, 64u,
                                                          USB_REPORT_BUILD_ID, nonce, page);
        if (result != USB_RECORD_OK) {
            fprintf(stderr, "Boat page %u rejected: %s\n", (unsigned)page, usb_report_result_name(result)); goto cleanup;
        }
    }
    if (words[BM_MAGIC] != BOAT_MEASURE_MAGIC || words[BM_VERSION] != 1u) {
        fputs("Boat payload schema mismatch\n", stderr); goto cleanup;
    }
    int passed = words[BM_ERROR] == 0u && words[BM_STAGE] == 3u &&
        words[BM_STEPS] == BOAT_MEASURE_STEPS && words[BM_VOLUME_ERRORS] == 0u &&
        words[BM_VOLUME] == BOAT_COLUMNS * BOAT_REST_DEPTH &&
        words[BM_INITIAL_VOLUME] == words[BM_VOLUME] &&
        words[BM_DURATION] >= 599000000u && words[BM_DURATION] <= 601000000u &&
        words[BM_PHYSICS_SKIPS] == 0u && words[BM_SOF] > 0u &&
        llabs((long long)words[BM_DURATION] - (long long)words[BM_SOF] * 1000) <= 6000000;
    printf("{\"verified\":true,\"test\":\"boat-profile\",\"serial\":\"%s\","
           "\"build_id\":\"%08x\",\"nonce\":\"", argv[1], (unsigned)USB_REPORT_BUILD_ID);
    for (uint32_t i = 0u; i < 16u; ++i) printf("%02x", nonce[i]);
    printf("\",\"diagnostic_ok\":%s,\"words\":[", passed ? "true" : "false");
    for (uint32_t i = 0u; i < NAMETAG_M2_WORDS; ++i) printf("%s%u", i ? "," : "", (unsigned)words[i]);
    puts("]}");
    fflush(stdout);
    if (!passed) fprintf(stderr, "Boat diagnostic failed: error=%d stage=%u steps=%u physics_skips=%u duration=%u\n",
        (int32_t)words[BM_ERROR], words[BM_STAGE], words[BM_STEPS], words[BM_PHYSICS_SKIPS], words[BM_DURATION]);
    if (!transfer(device, 0x40u, LIVE_USB_BOOTSEL, NULL, 0u)) goto cleanup;
    ok = passed;
cleanup:
    if (device != NULL) libusb_close(device);
    libusb_exit(context);
    return ok ? 0 : 1;
}
