#include "usb_live_transport.h"
#include "../../tools/compiler_test/suite.h"
#include "compiler_test_build.h"
#include <unistd.h>
#include <signal.h>

static void deadline(int number) {
    (void)number;
    static const char message[] = "Compiler collector exceeded 60-second deadline\n";
    ssize_t ignored = write(STDERR_FILENO, message, sizeof(message) - 1u);
    (void)ignored;
    _exit(124);
}

static int save_records(const char *path, const uint8_t records[COMPILER_SUITE_BYTES]) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) { perror(path); return 0; }
    int ok = fwrite(records, 1u, COMPILER_SUITE_BYTES, file) == COMPILER_SUITE_BYTES;
    if (fclose(file) != 0) ok = 0;
    if (!ok) fprintf(stderr, "%s: incomplete write\n", path);
    else fprintf(stderr, "%s: saved %u report bytes\n", path, (unsigned)COMPILER_SUITE_BYTES);
    return ok;
}

int main(int argc, char **argv) {
    if (argc != 6 || strlen(argv[1]) != 16u || strlen(argv[2]) != 16u ||
        strcmp(argv[1], argv[2]) == 0) {
        fputs("Usage: compiler_test_host LLVM_SERIAL OWNED_SERIAL REQUEST.bin LLVM.bin OWNED.bin\n", stderr);
        return 2;
    }
    uint8_t request[64];
    FILE *file = fopen(argv[3], "rb");
    if (file == NULL) { perror(argv[3]); return 1; }
    int valid = fread(request, 1u, 64u, file) == 64u && fgetc(file) == EOF && !ferror(file);
    if (fclose(file) != 0) valid = 0;
    if (!valid || usb_report_check_request_for_test(request, 64u, COMPILER_TEST_BUILD_ID,
                                                   COMPILER_SUITE_ID) != USB_RECORD_OK) {
        fputs("Invalid compiler suite request\n", stderr); return 1;
    }
    if (signal(SIGALRM, deadline) == SIG_ERR) { perror("deadline"); return 1; }
    alarm(60u);
    libusb_context *context = NULL;
    int status = libusb_init(&context);
    if (status != 0) { fprintf(stderr, "USB init: %s\n", libusb_error_name(status)); return 1; }
    libusb_device_handle *devices[2] = {NULL, NULL};
    int claimed[2] = {0, 0}, ok = 0;
    libusb_device *rebooting[2] = {NULL, NULL};
    uint8_t records[2][COMPILER_SUITE_BYTES];
    for (unsigned attempt = 0u; attempt < 100u && (!devices[0] || !devices[1]); ++attempt) {
        for (unsigned role = 0u; role < 2u; ++role)
            if (!devices[role]) devices[role] = find_badge(context, argv[role + 1u]);
        if (!devices[0] || !devices[1]) usleep(100000u);
    }
    for (unsigned role = 0u; role < 2u; ++role) {
        if (!devices[role]) { fprintf(stderr, "No live interface for %s\n", argv[role + 1u]); goto cleanup; }
        int configuration = 0;
        status = libusb_get_configuration(devices[role], &configuration);
        if (status == 0 && configuration != 1) status = libusb_set_configuration(devices[role], 1);
        if (status == 0) status = libusb_claim_interface(devices[role], 0);
        if (status != 0) { fprintf(stderr, "USB claim: %s\n", libusb_error_name(status)); goto cleanup; }
        claimed[role] = 1;
        uint8_t info[12];
        const uint8_t prefix[8] = {'U', '2', '6', 'L', 1u, 0u, 64u, 0u};
        if (!transfer(devices[role], 0xc0u, LIVE_USB_INFO, info, 12u) ||
            memcmp(info, prefix, 8u) != 0 || usb_report_u32(info + 8u) != COMPILER_TEST_BUILD_ID) {
            fputs("Incorrect live protocol/build\n", stderr); goto cleanup;
        }
        uint8_t corrupt[64];
        memcpy(corrupt, request, 64u); corrupt[60] ^= 1u;
        status = libusb_control_transfer(devices[role], 0x40u, LIVE_USB_COMPILER_SUBMIT,
                                         0u, 0u, corrupt, 64u, 1000u);
        if (status != LIBUSB_ERROR_PIPE) { fputs("Corrupt suite request not rejected\n", stderr); goto cleanup; }
        status = libusb_control_transfer(devices[role], 0xc0u, LIVE_USB_COMPILER_PAGE,
                                         0u, 0u, corrupt, 64u, 1000u);
        if (status != LIBUSB_ERROR_PIPE) { fputs("Unavailable results not rejected\n", stderr); goto cleanup; }
    }
    for (unsigned role = 0u; role < 2u; ++role) {
        fprintf(stderr, "%s: submitting suite\n", argv[role + 1u]);
        if (!transfer(devices[role], 0x40u, LIVE_USB_COMPILER_SUBMIT, request, 64u)) goto cleanup;
    }
    for (unsigned role = 0u; role < 2u; ++role) {
        int ready = 0;
        fprintf(stderr, "%s: waiting for suite summary\n", argv[role + 1u]);
        for (unsigned attempt = 0u; attempt < 1000u; ++attempt) {
#ifdef COMPILER_TEST_TIMING
            unsigned timeout = 5000u;
#else
            unsigned timeout = 1000u;
#endif
            status = libusb_control_transfer(devices[role], 0xc0u, LIVE_USB_COMPILER_PAGE,
                                             0u, 0u, records[role], 64u, timeout);
            if (status == 64) {
                fprintf(stderr, "%s: suite summary available after %u polls\n", argv[role + 1u], attempt + 1u);
                ready = 1; break;
            }
            if (status != LIBUSB_ERROR_PIPE) {
                fprintf(stderr, "%s: suite summary read after %u polls: %d (%s)\n",
                        argv[role + 1u], attempt + 1u, status,
                        status < 0 ? libusb_error_name(status) : "short transfer");
                goto cleanup;
            }
            usleep(30000u);
        }
        if (!ready) { fputs("Suite did not finish within observation bound\n", stderr); goto cleanup; }
        for (uint16_t page = 1u; page < COMPILER_SUITE_RECORDS; ++page) {
            status = libusb_control_transfer(devices[role], 0xc0u, LIVE_USB_COMPILER_PAGE,
                                             page, 0u, records[role] + page * 64u, 64u, 1000u);
            if (status != 64) { fprintf(stderr, "Suite page %u read: %d\n", page, status); goto cleanup; }
        }
        if (!save_records(argv[role + 4u], records[role])) goto cleanup;
    }
    ok = 1;
cleanup:
    for (unsigned role = 0u; role < 2u; ++role) {
        if (devices[role] == NULL) continue;
        if (claimed[role]) {
            rebooting[role] = libusb_ref_device(libusb_get_device(devices[role]));
            fprintf(stderr, "%s: requesting BOOTSEL\n", argv[role + 1u]);
            int requested = transfer(devices[role], 0x40u, LIVE_USB_BOOTSEL, NULL, 0u);
            if (!requested) ok = 0;
            fprintf(stderr, "%s: releasing interface\n", argv[role + 1u]);
            status = libusb_release_interface(devices[role], 0);
            if (status == LIBUSB_ERROR_NO_DEVICE && requested) {
                fprintf(stderr, "%s: disconnected before interface release; observing requested detach\n", argv[role + 1u]);
            } else if (status != 0) {
                fprintf(stderr, "USB release: %s\n", libusb_error_name(status)); ok = 0;
            }
        }
        fprintf(stderr, "%s: closing handle\n", argv[role + 1u]);
        libusb_close(devices[role]);
        fprintf(stderr, "%s: handle closed\n", argv[role + 1u]);
    }
    /* Finish hotplug removal while the event context is still alive. Destroying
       it concurrently with the requested disconnect can stall libusb_exit. */
    int detached = !rebooting[0] && !rebooting[1];
    for (unsigned attempt = 0u; attempt < 300u && !detached; ++attempt) {
        struct timeval timeout = {0, 100000};
        status = libusb_handle_events_timeout(context, &timeout);
        if (status != 0) { fprintf(stderr, "USB events: %s\n", libusb_error_name(status)); ok = 0; break; }
        libusb_device **list;
        ssize_t count = libusb_get_device_list(context, &list);
        if (count < 0) { fprintf(stderr, "USB detach enumeration: %s\n", libusb_error_name((int)count)); ok = 0; break; }
        detached = 1;
        for (ssize_t i = 0; i < count; ++i)
            for (unsigned role = 0; role < 2; ++role)
                if (rebooting[role] && list[i] == rebooting[role]) detached = 0;
        libusb_free_device_list(list, 1);
    }
    if (!detached) { fputs("USB detach observation failed\n", stderr); ok = 0; }
    for (unsigned role = 0; role < 2; ++role)
        if (rebooting[role]) libusb_unref_device(rebooting[role]);
    if (detached) fputs("Requested live devices detached\n", stderr);
    fputs("USB context teardown\n", stderr);
    libusb_exit(context);
    fputs("USB context closed\n", stderr);
    return ok ? 0 : 1;
}
