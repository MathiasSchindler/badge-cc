#include "../src/host/tools/usb_macos.h"
#include "../src/shared/bytes.h"
#include "../src/tools/diagnostics_logic.h"
#include "../src/usb/live_usb.h"
#include "board.h"
#include "fs.h"
#include "build_id.h"
#include <errno.h>
#include <stdlib.h>
#include <time.h>

static int parse_seconds(const char *text, unsigned *seconds) {
    unsigned value = 0u;
    if (!text || !text[0]) return 0;
    for (const char *p = text; *p; ++p) {
        if (*p < '0' || *p > '9' || value > 3600u / 10u) return 0;
        value = value * 10u + (unsigned)(*p - '0');
        if (value > 3600u) return 0;
    }
    if (!value) return 0;
    *seconds = value;
    return 1;
}

static int validate_status(const uint8_t bytes[64]) {
    uint32_t samples = badger_bytes_load_u32(bytes + 16u), sum = 0u;
    if (badger_bytes_load_u32(bytes) != LOGI_STATUS_MAGIC ||
        badger_bytes_load_u32(bytes + 4u) != USB_REPORT_BUILD_ID ||
        badger_bytes_load_u32(bytes + 60u) != diag_crc32(bytes, 60u) ||
        bytes[52] > 3u || bytes[53] != LOGI_MODE_PASSIVE ||
        bytes[54] != LOGI_DP || bytes[55] != LOGI_DM ||
        badger_bytes_load_u32(bytes + 12u) > 0xffffffffu / LOGI_SAMPLE_COUNT ||
        badger_bytes_load_u32(bytes + 12u) * LOGI_SAMPLE_COUNT != samples) return 0;
    for (unsigned i = 0u; i < 4u; ++i) {
        uint32_t count = badger_bytes_load_u32(bytes + 24u + i * 4u);
        if (count > samples - sum) return 0;
        sum += count;
    }
    return sum == samples;
}

static void print_status(const uint8_t bytes[64]) {
    printf("uptime=%u ms captures=%u samples=%u changes=%u PIO=%08x\n",
           badger_bytes_load_u32(bytes + 8u), badger_bytes_load_u32(bytes + 12u),
           badger_bytes_load_u32(bytes + 16u), badger_bytes_load_u32(bytes + 20u),
           badger_bytes_load_u32(bytes + 40u));
    printf("  D+ (GPIO12)=%u D- (GPIO13)=%u: %s\n", bytes[52] & 1u,
           bytes[52] >> 1u, logi_line_name(bytes[52]));
    printf("  histogram SE0=%u DP=%u DM=%u SE1=%u; error=%d errors=%u usb_errors=%u\n",
           badger_bytes_load_u32(bytes + 24u), badger_bytes_load_u32(bytes + 28u),
           badger_bytes_load_u32(bytes + 32u), badger_bytes_load_u32(bytes + 36u),
           (int32_t)badger_bytes_load_u32(bytes + 44u), badger_bytes_load_u32(bytes + 56u),
           badger_bytes_load_u32(bytes + 48u));
}

static void usage(FILE *out, const char *program) {
    fprintf(out, "Usage: %s --serial SERIAL [--watch SECONDS | --bootsel | --probe-full-speed | --fs-status]\n"
            "Read passive USB-A line diagnostics (default: one snapshot).\n"
            "--probe-full-speed explicitly DRIVES GPIO12/13: reset, address, device descriptor.\n"
            "Experimental full-speed direct-device only; no low-speed, hubs or HID polling yet.\n"
            "--bootsel checks the matching firmware before requesting recovery.\n", program);
}

static int validate_fs(const uint8_t bytes[64]) {
    if (bytes[34] && (bytes[36] != 18u || bytes[37] != 1u || bytes[43] != bytes[35] ||
        (bytes[35] != 8u && bytes[35] != 16u && bytes[35] != 32u && bytes[35] != 64u))) return 0;
    if (bytes[33] == 6u && (bytes[34] != 18u || !bytes[58] || !(bytes[54] | bytes[55]))) return 0;
    return badger_bytes_load_u32(bytes) == FS_REPORT_MAGIC &&
        badger_bytes_load_u32(bytes + 4u) == USB_REPORT_BUILD_ID &&
        badger_bytes_load_u32(bytes + 60u) == diag_crc32(bytes, 60u) &&
        bytes[32] <= 3u && bytes[33] <= 6u &&
        (bytes[34] == 0u || bytes[34] == 8u || bytes[34] == 18u) && bytes[58] <= 1u;
}

static int read_fs(IOUSBDeviceInterface500 **usb, uint16_t expected, int check_generation) {
    uint8_t bytes[64];
    if (!usb_macos_transfer(usb, 1u, LIVE_USB_LOGI_FS_STATUS, bytes, 64u)) return 0;
    if (!validate_fs(bytes)) { fputs("Full-speed report protocol/build/CRC mismatch.\n", stderr); return 0; }
    if (check_generation && (bytes[54] | (uint16_t)bytes[55] << 8u) != expected) {
        fputs("Full-speed probe did not publish the expected new result; refusing stale status.\n", stderr);
        return 0;
    }
    int32_t error = (int32_t)badger_bytes_load_u32(bytes + 8u);
    printf("Full-speed probe: stage=%u phase=%u last_pid=%02x error=%d sys_clock_144MHz=%u tx=%u rx=%u timeouts=%u NAKs=%u wire_errors=%u reset_lines=%02x\n",
           bytes[33], bytes[59], bytes[56], error, bytes[58], badger_bytes_load_u32(bytes + 12u),
           badger_bytes_load_u32(bytes + 16u), badger_bytes_load_u32(bytes + 20u),
           badger_bytes_load_u32(bytes + 24u), badger_bytes_load_u32(bytes + 28u), bytes[57]);
    if (bytes[34]) {
        printf("  device descriptor (%u bytes), EP0=%u:", bytes[34], bytes[35]);
        for (unsigned i = 0u; i < bytes[34]; ++i) printf(" %02x", bytes[36u + i]);
        putchar('\n');
        if (bytes[34] == 18u)
            printf("  VID:PID=%04x:%04x\n", bytes[44] | bytes[45] << 8u, bytes[46] | bytes[47] << 8u);
    }
    if (error || bytes[33] != 6u || bytes[34] != 18u) {
        fputs("No successful full-speed device-descriptor probe recorded; see firmware stage/error.\n", stderr);
        return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    const char *serial = NULL;
    unsigned seconds = 0u;
    int bootsel = 0, mode = 0, probe = 0, fs_status = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--serial") && !serial && i + 1 < argc) serial = argv[++i];
        else if (!strcmp(argv[i], "--watch") && !mode && i + 1 < argc) {
            if (!parse_seconds(argv[++i], &seconds)) { usage(stderr, argv[0]); return 2; }
            mode = 1;
        } else if (!strcmp(argv[i], "--bootsel") && !mode) { bootsel = mode = 1; }
        else if (!strcmp(argv[i], "--probe-full-speed") && !mode) { probe = mode = 1; }
        else if (!strcmp(argv[i], "--fs-status") && !mode) { fs_status = mode = 1; }
        else if (!strcmp(argv[i], "--help") && argc == 2) { usage(stdout, argv[0]); return 0; }
        else { usage(stderr, argv[0]); return 2; }
    }
    if (!serial || !serial[0]) { usage(stderr, argv[0]); return 2; }
    IOUSBDeviceInterface500 **usb = usb_macos_open(LIVE_USB_VID, LIVE_USB_PID, serial);
    if (!usb) return 1;
    int ok = 1;
    struct timespec start, tick;
    if (clock_gettime(CLOCK_MONOTONIC, &start)) { perror("monotonic clock"); ok = 0; }
    uint32_t first_samples = 0u, last_samples = 0u;
    unsigned reads = 0u;
    while (ok) {
        uint8_t bytes[64];
        ok = usb_macos_transfer(usb, 1u, LIVE_USB_LOGI_STATUS, bytes, 64u);
        if (!ok) break;
        if (!validate_status(bytes)) {
            fputs("Logi Host protocol/build/CRC or sample-count mismatch.\n", stderr);
            ok = 0; break;
        }
        if (!reads) first_samples = badger_bytes_load_u32(bytes + 16u);
        last_samples = badger_bytes_load_u32(bytes + 16u);
        ++reads;
        print_status(bytes);
        if (badger_bytes_load_u32(bytes + 44u) || badger_bytes_load_u32(bytes + 48u) ||
            badger_bytes_load_u32(bytes + 56u)) {
            fputs("Firmware reports a PIO or USB error.\n", stderr);
            ok = 0; break;
        }
        if (bootsel) {
            ok = usb_macos_transfer(usb, 0u, LIVE_USB_BOOTSEL, NULL, 0u);
            if (ok) puts("BOOTSEL request acknowledged.");
            break;
        }
        if (probe || fs_status) {
            uint16_t expected = 0u;
            if (probe) {
                uint8_t before[64];
                ok = usb_macos_transfer(usb, 1u, LIVE_USB_LOGI_FS_STATUS, before, 64u);
                if (ok && !validate_fs(before)) {
                    fputs("Full-speed report protocol/build/CRC mismatch before arming.\n", stderr);
                    ok = 0;
                }
                if (!ok) break;
                expected = (uint16_t)((before[54] | (uint16_t)before[55] << 8u) + 1u);
                if (!expected) expected = 1u;
                puts("Arming experimental full-speed USB-A reset and descriptor probe.");
                ok = usb_macos_transfer(usb, 0u, LIVE_USB_LOGI_PROBE, NULL, 0u);
                struct timespec delay = {.tv_nsec = 500000000};
                while (ok && nanosleep(&delay, &delay))
                    if (errno != EINTR) { perror("probe wait"); ok = 0; }
            }
            if (ok) ok = read_fs(usb, expected, probe);
            break;
        }
        if (!seconds) break;
        if (clock_gettime(CLOCK_MONOTONIC, &tick)) { perror("monotonic clock"); ok = 0; break; }
        double elapsed = (double)(tick.tv_sec - start.tv_sec) +
                         (double)(tick.tv_nsec - start.tv_nsec) / 1000000000.0;
        if (elapsed >= seconds) break;
        struct timespec delay = {.tv_nsec = 250000000};
        while (nanosleep(&delay, &delay)) {
            if (errno != EINTR) { perror("monitor wait"); ok = 0; break; }
        }
    }
    if (ok && seconds && reads > 1u && last_samples <= first_samples) {
        fputs("PIO sample counter did not advance during the watch.\n", stderr);
        ok = 0;
    }
    if (!usb_macos_close(usb)) ok = 0;
    return ok ? 0 : 1;
}
