#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "probe.h"
#include "build_id.h"
#include "../../host/tools/usb_live_transport.h"
#include "../../shared/bytes.h"
#include "../../radio/wifi_usb_report.h"
#include "../diagnostics_logic.h"

#define ARCHIVE_BYTES (128u + WIFI_TRACE_PAGES * 64u)

static volatile sig_atomic_t cancelled;

static void cancel(int number) { (void)number; cancelled = 1; }

static uint64_t milliseconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) { perror("clock_gettime"); exit(1); }
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static int save(const char *path, const uint8_t *bytes, size_t length) {
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) { perror(path); return 0; }
    FILE *file = fdopen(fd, "wb");
    if (!file) { perror("fdopen"); close(fd); return 0; }
    int ok = fwrite(bytes, 1u, length, file) == length;
    if (fclose(file)) ok = 0;
    if (!ok) fprintf(stderr, "Could not write complete evidence: %s\n", path);
    return ok;
}

static int inspect(const uint8_t *archive, const char *serial) {
    const uint8_t *request = archive, *report = archive + 64u;
    uint8_t trace[WIFI_TRACE_SIZE] = {0};
    uint32_t build = usb_report_u32(request + 8u);
    if (usb_report_check_request_for_test(request, 64u, build, WIFI_USB_TEST_ID) != USB_RECORD_OK ||
        usb_report_u32(report) != 0x57363255u || report[4] != 1u || report[5] ||
        report[6] != 64u || report[7] || usb_report_u32(report + 8u) != build ||
        usb_report_u32(report + 12u) != WIFI_USB_TEST_ID ||
        memcmp(request + 16u, report + 16u, 16u) ||
        usb_report_u32(report + 60u) != diag_crc32(report, 60u) ||
        usb_report_u32(report + 48u) != WF_MAGIC ||
        usb_report_u32(report + 52u) != WF_VERSION ||
        usb_report_u32(report + 56u) != WF_REPORT_BYTES) {
        fputs("Invalid request/report identity, shape or CRC\n", stderr);
        return 0;
    }
    for (uint32_t page = 0u; page < WIFI_TRACE_PAGES; ++page) {
        if (wifi_trace_page_decode(trace, archive + 128u + page * 64u, 64u,
                                   report, (uint8_t)page) != USB_RECORD_OK) {
            fprintf(stderr, "Invalid retained evidence page %u\n", page);
            return 0;
        }
    }
    uint32_t count = usb_report_u32(trace + 12u), error = usb_report_u32(report + 32u);
    printf("{\"serial\":\"%s\",\"build\":\"%08x\",\"error\":%u,"
           "\"host_error\":%u,\"host_address\":%u,\"host_actual\":%u,"
           "\"loader_stage\":%u,\"verified_controller_bytes\":%u,\"elapsed_us\":%u,"
           "\"firmware_fault_available\":%s,"
           "\"firmware_error\":%u,\"firmware_address\":%u,\"firmware_actual\":%u,"
           "\"count\":%u,\"reception_proven\":false,\"bandwidth_proven\":false,\"records\":[",
           serial, build, error, usb_report_u32(report + 36u),
           usb_report_u32(report + 40u), usb_report_u32(report + 44u),
           usb_report_u32(trace + WF_REPORT_BYTES + 24u),
           usb_report_u32(trace + WF_REPORT_BYTES + 28u),
           usb_report_u32(trace + WF_REPORT_BYTES + 32u),
           usb_report_u32(trace + WF_REPORT_BYTES + 36u) ? "true" : "false",
           usb_report_u32(trace + WF_REPORT_BYTES + 12u),
           usb_report_u32(trace + WF_REPORT_BYTES + 16u),
           usb_report_u32(trace + WF_REPORT_BYTES + 20u), count);
    int ok = !error && !usb_report_u32(report + 36u) &&
        usb_report_u32(trace) == WF_MAGIC && usb_report_u32(trace + 4u) == WF_VERSION &&
        usb_report_u32(trace + 8u) == (usb_report_u32(request + 16u) | 1u) &&
        usb_report_u32(trace + 16u) == 1u && !usb_report_u32(trace + 20u) &&
        !usb_report_u32(trace + WF_REPORT_BYTES) && count == WF_RECORDS;
    if (count > WF_RECORDS) { count = WF_RECORDS; ok = 0; }
    for (uint32_t i = 0u; i < count; ++i) {
        const uint8_t *r = trace + 64u + i * 64u;
        uint32_t f = usb_report_u32(r), result = usb_report_u32(r + 36u);
        uint32_t divider = usb_report_u32(r + 8u);
        wf_record_t record;
        memcpy(&record, r, sizeof(record));
        int valid = wf_record_valid(&record, i);
        if (!valid) ok = 0;
        printf("%s{\"mhz\":%u,\"baseline_channel\":%u,\"result\":%u,"
               "\"divider\":%u,\"frequency_readback\":%u,\"divider_readback\":%u,"
               "\"vco_before\":%u,\"vco_after\":%u,\"dc_idac\":%u,\"dc_dcoe\":%u,"
               "\"cycles\":%u,\"phy_guard\":%u,\"radio_guard\":%u,\"maccontrol\":%u,"
               "\"phy_control\":%u,\"phy_version\":%u,\"record_valid\":%s}",
               i ? "," : "", f, usb_report_u32(r + 4u), result, divider,
               usb_report_u32(r + 12u), usb_report_u32(r + 16u),
               usb_report_u32(r + 20u), usb_report_u32(r + 24u),
               usb_report_u32(r + 28u), usb_report_u32(r + 32u),
               usb_report_u32(r + 40u), usb_report_u32(r + 44u),
               usb_report_u32(r + 48u), usb_report_u32(r + 52u),
               usb_report_u32(r + 56u), usb_report_u32(r + 60u), valid ? "true" : "false");
    }
    printf("],\"tuning_control_passed\":%s}\n", ok ? "true" : "false");
    if (!ok) fputs("Tuning control incomplete or failed; retain raw evidence\n", stderr);
    return ok;
}

int main(int argc, char **argv) {
    uint8_t archive[ARCHIVE_BYTES] = {0};
    if (argc == 3 && !strcmp(argv[1], "--inspect")) {
        FILE *file = fopen(argv[2], "rb");
        if (!file) { perror(argv[2]); return 1; }
        int ok = fread(archive, 1u, sizeof(archive), file) == sizeof(archive) &&
            fgetc(file) == EOF && !ferror(file);
        if (fclose(file)) ok = 0;
        if (!ok) { fputs("Invalid archive length\n", stderr); return 1; }
        return inspect(archive, "offline") ? 0 : 1;
    }
    if (argc != 3 || strlen(argv[1]) != 16u ||
        strspn(argv[1], "0123456789ABCDEF") != 16u) {
        fputs("Usage: collect SERIAL NEW_EVIDENCE.bin | collect --inspect EVIDENCE.bin\n", stderr);
        return 2;
    }
    if (access(argv[2], F_OK) == 0) { fputs("Evidence already exists\n", stderr); return 2; }
    signal(SIGINT, cancel); signal(SIGTERM, cancel);
    libusb_context *context = NULL;
    int status = libusb_init(&context);
    if (status) { fprintf(stderr, "libusb_init: %s\n", libusb_error_name(status)); return 1; }
    libusb_device_handle *device = NULL;
    uint64_t deadline = milliseconds() + 5000u;
    while (!cancelled && milliseconds() < deadline && !device) {
        device = find_badge(context, argv[1]);
        if (!device) usleep(20000u);
    }
    if (!device) { fputs("Selected device unavailable\n", stderr); libusb_exit(context); return 1; }
    uint8_t info[12];
    int ok = transfer(device, 0xc0u, LIVE_USB_INFO, info, sizeof(info));
    if (ok && (memcmp(info, "U26L\1\0\100\0", 8u) ||
               usb_report_u32(info + 8u) != USB_REPORT_BUILD_ID)) {
        fputs("Unexpected build or USB interface; not starting radio\n", stderr);
        ok = 0;
    }
    int configuration = 0;
    if (ok && (libusb_get_configuration(device, &configuration) ||
               (configuration != 1 && libusb_set_configuration(device, 1)))) {
        fputs("USB configuration failed\n", stderr); ok = 0;
    }
    uint8_t nonce[16];
    arc4random_buf(nonce, sizeof(nonce));
    usb_report_request_for_test(archive, USB_REPORT_BUILD_ID, nonce, WIFI_USB_TEST_ID);
    if (ok) ok = transfer(device, 0x40u, LIVE_USB_WIFI_SUBMIT, archive, 64u);
    deadline = milliseconds() + 6500u;
    int received = 0;
    while (ok && !cancelled && milliseconds() < deadline) {
        int n = libusb_control_transfer(device, 0xc0u, LIVE_USB_WIFI_REPORT,
                                       0u, 0u, archive + 64u, 64u, 1000u);
        if (n == 64) { received = 1; break; }
        if (n != LIBUSB_ERROR_PIPE) {
            fprintf(stderr, "Report read failed: %d (%s)\n", n,
                    n < 0 ? libusb_error_name(n) : "short report");
            ok = 0; break;
        }
        usleep(10000u);
    }
    if (ok && !received) { fputs("Report deadline/cancellation\n", stderr); ok = 0; }
    for (uint32_t page = 0u; ok && !cancelled && page < WIFI_TRACE_PAGES; ++page) {
        uint8_t *bytes = archive + 128u + page * 64u;
        uint8_t decoded[WIFI_TRACE_SIZE];
        int n = libusb_control_transfer(device, 0xc0u, LIVE_USB_WIFI_TRACE,
                                        (uint16_t)page, 0u, bytes, 64u, 1000u);
        if (n != 64 || wifi_trace_page_decode(decoded, bytes, 64u,
                                              archive + 64u, (uint8_t)page) != USB_RECORD_OK) {
            fprintf(stderr, "Invalid or missing evidence page %u: %d\n", page, n);
            ok = 0;
        }
    }
    if (cancelled) ok = 0;
    int retained = save(argv[2], archive, sizeof(archive));
    int decoded = ok ? inspect(archive, argv[1]) : 0;
    int cleanup = transfer(device, 0x40u, LIVE_USB_BOOTSEL, NULL, 0u);
    libusb_close(device); libusb_exit(context);
    return ok && retained && decoded && cleanup ? 0 : 1;
}
