#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "firmware/trace.h"
#include "build_id.h"

typedef struct {
    uint8_t *records;
    uint32_t count;
} capture_t;

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

/* Non-report log lines are ignored; anything claiming to be an input must parse. */
static int parse_input(const char *line, uint8_t wire[64], uint32_t sequence, uint8_t feature) {
    if (!strstr(line, "input id=")) return 0;
    if (*line != '+') return -1;
    char *end;
    errno = 0;
    double seconds = strtod(line + 1, &end);
    if (errno || end == line + 1 || !(seconds >= 0.0 && seconds <= 4294967.294) ||
        strncmp(end, " input id=0x", 12u)) return -1;
    const char *at = end + 12u;
    if (!at[0] || !at[1] || hex_digit(at[0]) < 0 || hex_digit(at[1]) < 0) return -1;
    unsigned id = (unsigned)(hex_digit(at[0]) * 16 + hex_digit(at[1]));
    at += 2;
    if (strncmp(at, " length=", 8u)) return -1;
    at += 8u;
    if (*at < '0' || *at > '9') return -1;
    errno = 0;
    unsigned long length = strtoul(at, &end, 10);
    if (errno || !length || length > 32u || strncmp(end, " raw=", 5u)) return -1;
    at = end + 5u;
    uint8_t raw[32];
    for (unsigned i = 0; i < length; ++i) {
        while (space(*at)) ++at;
        if (!at[0] || !at[1]) return -1;
        int hi = hex_digit(at[0]), lo = hex_digit(at[1]);
        if (hi < 0 || lo < 0 || (at[2] && !space(at[2]))) return -1;
        raw[i] = (uint8_t)(hi * 16 + lo);
        at += 2;
    }
    while (space(*at)) ++at;
    if (*at || raw[0] != id) return -1;
    return keypad_trace_record(wire, raw, length, feature,
                               (uint32_t)(seconds * 1000.0 + 0.5), sequence) ? 1 : -1;
}

static int load_capture(const char *path, capture_t *capture) {
    FILE *file = fopen(path, "r");
    if (!file) { perror(path); return 0; }
    char line[2048];
    uint32_t line_number = 0u;
    uint8_t feature = 0u;
    int ok = 1;
    while (fgets(line, sizeof(line), file)) {
        ++line_number;
        if (!strchr(line, '\n') && !feof(file)) {
            fprintf(stderr, "%s:%u: line too long.\n", path, line_number);
            ok = 0; break;
        }
        const char prefix[] = "HID++ feature 0x1b04 discovered at index 0x";
        if (!strncmp(line, prefix, sizeof(prefix) - 1u)) {
            const char *at = line + sizeof(prefix) - 1u;
            char *end;
            errno = 0;
            unsigned long discovered = strtoul(at, &end, 16);
            if (errno || end == at || hex_digit(*at) < 0 ||
                !discovered || discovered > 255u || *end != '.') {
                fprintf(stderr, "%s:%u: invalid navigation feature index.\n", path, line_number);
                ok = 0; break;
            }
            feature = (uint8_t)discovered;
        }
        uint8_t wire[64];
        int parsed = parse_input(line, wire, capture->count + 1u, feature);
        if (parsed < 0) {
            fprintf(stderr, "%s:%u: malformed raw input record.\n", path, line_number);
            ok = 0; break;
        }
        if (!parsed) continue;
        /* Bound host memory and protect size/sequence arithmetic. */
        if (capture->count == 1000000u) {
            fputs("Capture exceeds the one-million-report limit.\n", stderr);
            ok = 0; break;
        }
        uint8_t *grown = realloc(capture->records, ((size_t)capture->count + 1u) * 64u);
        if (!grown) { perror("allocate capture"); ok = 0; break; }
        capture->records = grown;
        memcpy(grown + (size_t)capture->count * 64u, wire, 64u);
        ++capture->count;
    }
    if (ferror(file)) { perror("read capture"); ok = 0; }
    if (fclose(file) != 0) { perror("close capture"); ok = 0; }
    if (ok && !capture->count) { fputs("Capture contains no raw input records.\n", stderr); ok = 0; }
    return ok;
}

#include "../src/host/tools/usb_macos.h"

static IOUSBDeviceInterface500 **open_badge(const char *serial) {
    return usb_macos_open(0xcafe, 0x4026, serial);
}

static int transfer(IOUSBDeviceInterface500 **usb, uint8_t input, uint8_t request,
                     uint8_t wire[64]) {
    return usb_macos_transfer(usb, input, request, wire, 64u);
}

static int read_status(IOUSBDeviceInterface500 **usb, uint8_t status[64]) {
    if (!transfer(usb, 1u, KEYPAD_TRACE_STATUS, status)) return 0;
    if (badger_bytes_load_u32(status) != KEYPAD_STATUS_MAGIC ||
        badger_bytes_load_u32(status + 60u) != diag_crc32(status, 60u) ||
        badger_bytes_load_u32(status + 32u) != USB_REPORT_BUILD_ID) {
        fputs("Badge trace protocol/CRC/build mismatch; use the matching UF2 and replay tool.\n", stderr);
        return 0;
    }
    return 1;
}

static int submit(IOUSBDeviceInterface500 **usb, uint8_t wire[64], const keypad_trace_t *expected) {
    if (!transfer(usb, 0u, KEYPAD_TRACE_SUBMIT, wire)) return 0;
    for (unsigned attempt = 0; attempt < 100u; ++attempt) {
        uint8_t status[64];
        if (!read_status(usb, status)) return 0;
        uint32_t total = badger_bytes_load_u32(status + 4u);
        if (badger_bytes_load_u32(status + 28u)) {
            fprintf(stderr, "Badge rejected event: error %u.\n", badger_bytes_load_u32(status + 28u));
            return 0;
        }
        if (total == expected->total &&
            badger_bytes_load_u32(status + 8u) == expected->count &&
            badger_bytes_load_u32(status + 12u) == expected->total - expected->count &&
            badger_bytes_load_u32(status + 16u) == expected->unknown) return 1;
        struct timespec delay = {.tv_nsec = 5000000};
        while (nanosleep(&delay, &delay) != 0) {
            if (errno != EINTR) { perror("wait for badge status"); return 0; }
        }
    }
    fputs("Badge did not confirm the expected event/counts; delivery is unverified.\n", stderr);
    return 0;
}

static void usage(FILE *out, const char *program) {
    fprintf(out, "Usage: %s --validate CAPTURE\n"
                 "       %s --replay CAPTURE --badge SERIAL\n"
                 "Replay clears badge history, then sends raw reports immediately.\n"
                 "Original timestamps are retained. The badge must run the matching trace UF2.\n",
            program, program);
}

int main(int argc, char **argv) {
    const char *path = NULL, *serial = NULL;
    int replay = 0;
    for (int i = 1; i < argc; ++i) {
        if ((!strcmp(argv[i], "--validate") || !strcmp(argv[i], "--replay")) &&
            !path && i + 1 < argc) {
            replay = !strcmp(argv[i], "--replay");
            path = argv[++i];
        } else if (!strcmp(argv[i], "--badge") && !serial && i + 1 < argc) {
            serial = argv[++i];
        } else if (!strcmp(argv[i], "--help") && argc == 2) {
            usage(stdout, argv[0]); return 0;
        } else { usage(stderr, argv[0]); return 2; }
    }
    if (!path || (replay && (!serial || !serial[0])) || (!replay && serial)) {
        usage(stderr, argv[0]); return 2;
    }
    capture_t capture = {0};
    if (!load_capture(path, &capture)) { free(capture.records); return 1; }
    keypad_trace_t expected;
    keypad_trace_init(&expected);
    IOUSBDeviceInterface500 **usb = replay ? open_badge(serial) : NULL;
    int ok = !replay || usb != NULL;
    if (ok && replay) {
        uint8_t status[64], clear[64];
        ok = read_status(usb, status);
        if (ok) {
            keypad_trace_clear_wire(clear);
            ok = submit(usb, clear, &expected);
        }
    }
    for (uint32_t i = 0; ok && i < capture.count; ++i) {
        uint8_t *wire = capture.records + (size_t)i * 64u;
        ok = keypad_trace_accept(&expected, wire);
        if (!ok) fputs("Internal capture sequence validation failed.\n", stderr);
        if (ok && replay) ok = submit(usb, wire, &expected);
    }
    if (usb) {
        if (!usb_macos_close(usb)) ok = 0;
    }
    if (ok) printf("%s %u raw reports: %u decoded, %u unknown/command, %u retained, %u overwritten.\n",
                   replay ? "Badge confirmed" : "Validated", expected.total,
                   expected.total - expected.unknown, expected.unknown, expected.count,
                   expected.total - expected.count);
    free(capture.records);
    return ok ? 0 : 1;
}
