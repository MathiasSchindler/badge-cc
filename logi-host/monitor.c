#include "../src/host/tools/usb_macos.h"
#include "../src/shared/bytes.h"
#include "../src/tools/diagnostics_logic.h"
#include "../src/usb/live_usb.h"
#include "board.h"
#include "fs.h"
#include "fs_lcd.h"
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
    fprintf(out, "Usage: %s --serial SERIAL [--watch SECONDS | --bootsel | --probe-full-speed | --fs-status | --keypad-watch SECONDS | --lcd-test SECONDS | --lcd-toggle SECONDS]\n"
            "Read passive USB-A line diagnostics (default: one snapshot).\n"
            "--probe-full-speed explicitly DRIVES GPIO12/13: reset, address, device descriptor.\n"
            "--keypad-watch captures HID input for 1..30 seconds and queries a feature read-only.\n"
            "--lcd-test SECONDS replaces all nine icons with numbered test images; holds for 1..30 seconds.\n"
            "--lcd-toggle SECONDS toggles each key's normal/inverted image on press for 1..30 seconds.\n"
            "Experimental full-speed direct-device only; no low-speed or hubs.\n"
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

static unsigned trace_sample(const uint8_t trace[64], unsigned i) {
    uint32_t word = badger_bytes_load_u32(trace + 16u + (i / 16u) * 4u);
    return (word >> (2u * (i % 16u))) & 3u;
}

static int trace_response_gap(const uint8_t trace[64]) {
    unsigned n = (unsigned)trace[8] * 16u, i = 0u;
    if (!n || n > 128u || trace_sample(trace, 0u) != 0u) return -1;
    while (i < n && trace_sample(trace, i) == 0u) ++i;
    unsigned start = i;
    while (i < n && trace_sample(trace, i) == 1u) ++i;
    if (i == start || i == n || trace_sample(trace, i) != 2u) return -1;
    return (int)(i - start);
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
    uint8_t trace[64];
    if (!usb_macos_transfer(usb, 1u, LIVE_USB_LOGI_FS_TRACE, trace, 64u)) return 0;
    if (badger_bytes_load_u32(trace) != FS_TRACE_MAGIC ||
        badger_bytes_load_u32(trace + 4u) != USB_REPORT_BUILD_ID ||
        badger_bytes_load_u32(trace + 60u) != diag_crc32(trace, 60u) || trace[8] > 8u ||
        trace[10] != bytes[54] || trace[11] != bytes[55]) {
        fputs("Full-speed trace protocol/build/CRC/generation mismatch.\n", stderr);
        return 0;
    }
    printf("  physical post-EOP trace (24 MHz, first DATA PID=%02x): ", trace[9]);
    for (unsigned i = 0u; i < (unsigned)trace[8] * 16u; ++i)
        putchar("0JKX"[trace_sample(trace, i)]);
    putchar('\n');
    int gap = trace_response_gap(trace);
    if (gap < 0) {
        puts("  Response timing unavailable: capture lacks an SE0/J/K transition.");
    } else {
        printf("  First response edge: %.1f bit times after SE0-to-J "
               "(sampling uncertainty +/-0.5; USB host maximum 7.5).\n", gap / 2.0);
        if (gap + 1 > 15)
            fputs("  WARNING: captured response does not establish the USB host timing limit.\n", stderr);
    }
    if (error || bytes[33] != 6u || bytes[34] != 18u) {
        fputs("No successful full-speed device-descriptor probe recorded; see firmware stage/error.\n", stderr);
        return 0;
    }
    return 1;
}

static int validate_keypad_record(const uint8_t bytes[64], uint32_t magic) {
    return badger_bytes_load_u32(bytes) == magic &&
        badger_bytes_load_u32(bytes + 4u) == USB_REPORT_BUILD_ID &&
        badger_bytes_load_u32(bytes + 60u) == diag_crc32(bytes, 60u);
}

static int keypad_watch(IOUSBDeviceInterface500 **usb, unsigned seconds, int lcd_mode) {
    uint8_t bytes[64];
    if (!usb_macos_transfer(usb, 1u, LIVE_USB_LOGI_KEYPAD_STATUS, bytes, 64u)) return 0;
    if (!validate_keypad_record(bytes, FS_KEYPAD_MAGIC) || bytes[14] == 2u) {
        fputs("Keypad status mismatch or a session is already running.\n", stderr); return 0;
    }
    uint16_t generation = (uint16_t)(keypad_u16(bytes + 12u) + 1u);
    if (!generation) generation = 1u;
    if (lcd_mode) puts("LCD mode replaces existing icons (no image backup); uploading numbered keys 1..9.");
    if (lcd_mode == FS_LCD_TOGGLE) puts("Press a key to toggle its image; release does not toggle. Wait for 9/9 images.");
    uint8_t request = lcd_mode == FS_LCD_TOGGLE ? LIVE_USB_LOGI_LCD_TOGGLE :
                      lcd_mode ? LIVE_USB_LOGI_LCD_TEST : LIVE_USB_LOGI_KEYPAD;
    if (!usb_macos_transfer_value(usb, 0u, request,
                                  (uint16_t)seconds, NULL, 0u)) return 0;
    puts("Keypad session armed: configuration, verified HID and input capture.");
    struct timespec start, tick;
    if (clock_gettime(CLOCK_MONOTONIC, &start)) { perror("keypad clock"); return 0; }
    uint32_t consumed = 0u;
    unsigned images = 0u;
    for (unsigned loops = 0u; loops < 7000u; ++loops) {
        if (!usb_macos_transfer(usb, 1u, LIVE_USB_LOGI_KEYPAD_STATUS, bytes, 64u)) return 0;
        if (!validate_keypad_record(bytes, FS_KEYPAD_MAGIC) || keypad_u16(bytes + 12u) != generation ||
            bytes[14] > 4u || bytes[58] != (unsigned)lcd_mode || bytes[50] > 9u) {
            fputs("Keypad status protocol/build/CRC/generation mismatch.\n", stderr); return 0;
        }
        if (lcd_mode && bytes[50] != images) {
            images = bytes[50];
            printf("  LCD images accepted: %u/9 (%u acknowledged fragments).\n", images, bytes[51]);
        }
        uint32_t count = badger_bytes_load_u32(bytes + 16u);
        if (count < consumed || count - consumed > FS_KEYPAD_EVENTS) {
            fputs("Keypad capture overrun or invalid report sequence; reports were lost.\n", stderr); return 0;
        }
        while (consumed < count) {
            uint8_t event[64];
            if (!usb_macos_transfer_value(usb, 1u, LIVE_USB_LOGI_KEYPAD_EVENT,
                (uint16_t)(consumed % FS_KEYPAD_EVENTS), event, 64u)) return 0;
            if (!validate_keypad_record(event, FS_KEYPAD_EVENT_MAGIC) ||
                keypad_u16(event + 12u) != generation ||
                badger_bytes_load_u32(event + 8u) != consumed + 1u || !event[14] || event[14] > 32u) {
                fputs("Keypad event mismatch/overwrite; refusing stale or corrupt input.\n", stderr); return 0;
            }
            printf("  input #%u at %u us:", ++consumed, badger_bytes_load_u32(event + 48u));
            for (unsigned i = 0u; i < event[14]; ++i) printf(" %02x", event[16u + i]);
            int key = decode_button(event[16], event + 16u, event[14]);
            int navigation = decode_navigation(event[15], event[16], event + 16u, event[14]);
            if (key == 0) printf("  LCD key release");
            else if (key > 0) printf("  LCD key=%d row=%d column=%d", key, (key - 1) / 3 + 1, (key - 1) % 3 + 1);
            else if (navigation >= 0) printf("  navigation state=%d", navigation);
            else printf("  command reply or unknown report");
            putchar('\n');
        }
        if (bytes[14] == 3u || bytes[14] == 4u) {
            int32_t error = (int32_t)badger_bytes_load_u32(bytes + 8u);
            printf("Keypad session: state=%u phase=%u error=%d cleanup_error=%d reports=%u LCD_states=%u "
                   "polls=%u NAKs=%u duplicates=%u HID_CRC=%08x feature=%02x query_reply=%u\n",
                   bytes[14], bytes[15], error, (int32_t)badger_bytes_load_u32(bytes + 44u),
                   count, badger_bytes_load_u32(bytes + 52u), badger_bytes_load_u32(bytes + 20u),
                   badger_bytes_load_u32(bytes + 24u), badger_bytes_load_u32(bytes + 28u),
                   badger_bytes_load_u32(bytes + 32u), bytes[43], bytes[56]);
            printf("  IN=%02x/%u bytes/%u ms OUT=%02x/%u bytes/%u ms interface=%u\n",
                   bytes[36], bytes[37], bytes[38], bytes[39], bytes[40], bytes[41], bytes[42]);
            printf("  Declared configuration demand: %u mA (not measured; power path unqualified).\n",
                   keypad_u16(bytes + 48u));
            if (lcd_mode) printf("  LCD images=%u/9 fragments=%u state=%u; hold complete=%u.\n",
                                 bytes[50], bytes[51], bytes[59], bytes[59] == FS_LCD_DONE);
            if (!badger_bytes_load_u32(bytes + 52u))
                puts("  No physical LCD key states observed; button operation is not yet verified.");
            int phy_ok = read_fs(usb, 0u, 0);
            return bytes[14] == 3u && !error && !badger_bytes_load_u32(bytes + 44u) &&
                bytes[56] && !bytes[57] &&
                (!lcd_mode || (bytes[58] == (unsigned)lcd_mode && bytes[50] == 9u && bytes[59] == FS_LCD_DONE)) &&
                phy_ok;
        }
        if (clock_gettime(CLOCK_MONOTONIC, &tick)) { perror("keypad clock"); return 0; }
        if ((double)(tick.tv_sec - start.tv_sec) +
            (double)(tick.tv_nsec - start.tv_nsec) / 1000000000.0 >= seconds + (lcd_mode ? 34.0 : 3.0)) break;
        struct timespec delay = {.tv_nsec = 10000000};
        while (nanosleep(&delay, &delay))
            if (errno != EINTR) { perror("keypad wait"); return 0; }
    }
    fputs("Keypad session did not finish within its bounded deadline.\n", stderr);
    return 0;
}

int main(int argc, char **argv) {
    const char *serial = NULL;
    unsigned seconds = 0u;
    unsigned keypad_seconds = 0u;
    int lcd_mode = 0;
    int bootsel = 0, mode = 0, probe = 0, fs_status = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--serial") && !serial && i + 1 < argc) serial = argv[++i];
        else if (!strcmp(argv[i], "--watch") && !mode && i + 1 < argc) {
            if (!parse_seconds(argv[++i], &seconds)) { usage(stderr, argv[0]); return 2; }
            mode = 1;
        } else if ((!strcmp(argv[i], "--keypad-watch") || !strcmp(argv[i], "--lcd-test") ||
                    !strcmp(argv[i], "--lcd-toggle")) &&
                   !mode && i + 1 < argc) {
            lcd_mode = !strcmp(argv[i], "--lcd-toggle") ? FS_LCD_TOGGLE :
                       !strcmp(argv[i], "--lcd-test") ? FS_LCD_STATIC : 0;
            if (!parse_seconds(argv[++i], &keypad_seconds) || keypad_seconds > 30u) {
                usage(stderr, argv[0]); return 2;
            }
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
        if (keypad_seconds) {         ok = keypad_watch(usb, keypad_seconds, lcd_mode); break; }
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
