#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "protocol.h"
#include "build_id.h"
#include "../../src/host/tools/usb_live_transport.h"

typedef struct {
    libusb_device_handle *usb;
    const char *serial;
    uint8_t id[8], node;
} device_t;

static uint64_t milliseconds(void) {
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time)) { perror("clock_gettime"); exit(1); }
    return (uint64_t)time.tv_sec * 1000u + (uint64_t)time.tv_nsec / 1000000u;
}

static void hex(const uint8_t *bytes, uint32_t size) {
    for (uint32_t i = 0u; i < size; ++i) printf("%02x", bytes[i]);
}

static int open_device(libusb_context *context, device_t *d, const char *serial) {
    if (strlen(serial) != 16u || strspn(serial, "0123456789ABCDEF") != 16u) {
        fputs("Expected an explicit 16-digit uppercase USB serial\n", stderr);
        return 0;
    }
    d->serial = serial;
    uint64_t id = strtoull(serial, NULL, 16);
    for (uint32_t i = 0u; i < 8u; ++i) d->id[i] = (uint8_t)(id >> (i * 8u));
    d->usb = find_badge(context, serial);
    if (!d->usb) {
        fprintf(stderr, "Radio application not found for serial %s\n", serial);
        return 0;
    }
    int configuration, status = libusb_get_configuration(d->usb, &configuration);
    if (!status && configuration != 1) status = libusb_set_configuration(d->usb, 1);
    if (!status) status = libusb_claim_interface(d->usb, 0);
    uint8_t info[12];
    if (status || !transfer(d->usb, 0xc0u, LIVE_USB_INFO, info, sizeof(info)) ||
        memcmp(info, "U26L\1\0\100\0", 8u) ||
        usb_report_u32(info + 8u) != USB_REPORT_BUILD_ID) {
        fprintf(stderr, "USB claim/protocol/build mismatch on %s: %d\n", serial, status);
        return 0;
    }
    return 1;
}

static int snapshot(device_t *d, uint8_t trace[WIFI_TRACE_SIZE], const uint8_t command[16]) {
    uint8_t request[64], report[64];
    usb_report_request_for_test(request, USB_REPORT_BUILD_ID, command, 3u);
    if (!transfer(d->usb, 0x40u, LIVE_USB_FW_SUBMIT, request, sizeof(request))) return 0;
    int got = 0;
    uint64_t start = milliseconds();
    do {
        int n = libusb_control_transfer(d->usb, 0xc0u, LIVE_USB_FW_REPORT,
                                       0u, 0u, report, sizeof(report), 1000u);
        if (n == sizeof(report)) { got = 1; break; }
        if (n != LIBUSB_ERROR_PIPE) {
            fprintf(stderr, "Report read on %s: %d\n", d->serial, n);
            return 0;
        }
        usleep(10000u);
    } while (milliseconds() - start < 10000u);
    if (!got || usb_report_u32(report) != 0x46363255u ||
        memcmp(report + 4u, "\1\0\100\0", 4u) ||
        usb_report_u32(report + 8u) != USB_REPORT_BUILD_ID ||
        usb_report_u32(report + 12u) != 3u || memcmp(report + 16u, command, 16u) ||
        usb_report_u32(report + 60u) != diag_crc32(report, 60u)) {
        fprintf(stderr, "Invalid/missing nonce-bound report on %s\n", d->serial);
        return 0;
    }
    for (uint32_t page = 0u; page < WIFI_TRACE_PAGES; ++page) {
        uint8_t bytes[64];
        int n = libusb_control_transfer(d->usb, 0xc0u, LIVE_USB_WIFI_TRACE,
                                       (uint16_t)page, 0u, bytes, sizeof(bytes), 1000u);
        if (n != sizeof(bytes) ||
            wifi_trace_page_decode(trace, bytes, sizeof(bytes), report,
                                   (uint8_t)page) != USB_RECORD_OK) {
            fprintf(stderr, "Invalid trace page %u on %s: %d\n", page, d->serial, n);
            return 0;
        }
    }
    uint32_t node = usb_report_u32(trace + 4u);
    if (usb_report_u32(trace) != PR_TRACE_MAGIC || node < 1u || node > 3u ||
        (d->node && node != d->node) || node != usb_report_u32(report + 56u) ||
        memcmp(trace + 96u, d->id, 8u) ||
        usb_report_u32(report + 32u) != usb_report_u32(trace + 84u)) {
        fprintf(stderr, "Radio trace/board/identity mismatch on %s\n", d->serial);
        return 0;
    }
    d->node = (uint8_t)node;
    if (usb_report_u32(trace + 84u)) {
        fprintf(stderr, "Radio command rejected on %s: %u\n",
                d->serial, usb_report_u32(trace + 84u));
        return 0;
    }
    return 1;
}

static int status(device_t *d, uint8_t trace[WIFI_TRACE_SIZE]) {
    static uint32_t request_id;
    uint8_t command[16] = {0};
    badger_bytes_store_u32(command + 2u, ++request_id);
    return snapshot(d, trace, command);
}

static void print_status(const device_t *d, const uint8_t *t) {
    printf("{\"serial\":\"%s\",\"node\":%u,\"build_id\":\"%08x\","
           "\"session\":%u,\"radio_ready\":%u,\"radio_boots\":%u,"
           "\"last_fault\":%u,\"fault_address\":%u,\"fault_actual\":%u,"
           "\"tx_requests\":%u,\"tx_replies\":%u,\"rx_requests\":%u,"
           "\"confirmed\":%u,\"failed\":%u,\"duplicates\":%u,\"reply_drops\":%u,"
           "\"raw_rx\":%u,\"rejected\":%u,\"resyncs\":%u,\"active\":%u,\"attempts\":%u,"
           "\"sys_before_khz_q5\":%u,\"sys_after_khz_q5\":%u}\n",
           d->serial, d->node, USB_REPORT_BUILD_ID, usb_report_u32(t + 8u),
           usb_report_u32(t + 16u), usb_report_u32(t + 20u),
           usb_report_u32(t + 24u), usb_report_u32(t + 28u), usb_report_u32(t + 32u),
           usb_report_u32(t + 36u), usb_report_u32(t + 40u), usb_report_u32(t + 44u),
           usb_report_u32(t + 48u), usb_report_u32(t + 52u), usb_report_u32(t + 56u),
           usb_report_u32(t + 60u), usb_report_u32(t + 64u), usb_report_u32(t + 68u),
           usb_report_u32(t + 72u), usb_report_u32(t + 76u), usb_report_u32(t + 80u),
           usb_report_u32(t + 104u), usb_report_u32(t + 108u));
    fflush(stdout);
}

static int matches(const uint8_t *packet, const device_t *source, uint8_t destination,
                   uint8_t kind, uint32_t session, uint32_t sequence, const uint8_t *data) {
    pr_message_t m;
    return pr_metadata(packet) && pr_decode(&m, packet + 18u, PR_MPDU) &&
        m.source == source->node && m.destination == destination && m.kind == kind &&
        m.session == session && m.sequence == sequence &&
        !memcmp(m.sender, source->id, 8u) && !memcmp(m.data, data, PR_DATA);
}

static int exchange(device_t *a, device_t *b, FILE *random) {
    uint8_t command[16] = {1u, b->node}, at[WIFI_TRACE_SIZE], bt[WIFI_TRACE_SIZE];
    if (fread(command + 2u, 1u, PR_DATA, random) != PR_DATA) {
        fputs("Could not read a fresh radio challenge from /dev/urandom\n", stderr);
        return 0;
    }
    if (!status(a, at) || !status(b, bt)) return 0;
    uint32_t boots_a = usb_report_u32(at + 20u), boots_b = usb_report_u32(bt + 20u);
    uint32_t failed_a = usb_report_u32(at + 52u), failed_b = usb_report_u32(bt + 52u);
    uint32_t confirmed = usb_report_u32(at + 48u), received = usb_report_u32(bt + 44u);
    if (!snapshot(a, at, command)) return 0;
    uint32_t session = usb_report_u32(at + 8u), sequence = usb_report_u32(at + 12u);
    uint64_t began = milliseconds();
    do {
        if (!status(a, at) || !status(b, bt)) return 0;
        if (usb_report_u32(at + 20u) != boots_a || usb_report_u32(bt + 20u) != boots_b ||
            usb_report_u32(at + 52u) != failed_a || usb_report_u32(bt + 52u) != failed_b) break;
        if (usb_report_u32(at + 48u) == confirmed + 1u &&
            usb_report_u32(bt + 44u) == received + 1u &&
            matches(bt + 128u, a, b->node, PR_REQUEST, session, sequence, command + 2u) &&
            matches(at + 256u, b, a->node, PR_REPLY, session, sequence, command + 2u)) {
            printf("{\"test\":\"clean-rx-three-node\",\"source\":\"%s\","
                   "\"destination\":\"%s\",\"session\":%u,\"sequence\":%u,"
                   "\"elapsed_ms\":%llu,\"attempts\":%u,\"data_hex\":\"",
                   a->serial, b->serial, session, sequence,
                   (unsigned long long)(milliseconds() - began), usb_report_u32(at + 80u));
            hex(command + 2u, PR_DATA);
            printf("\",\"received_request_hex\":\""); hex(bt + 128u, PR_PACKET);
            printf("\",\"received_reply_hex\":\""); hex(at + 256u, PR_PACKET);
            puts("\",\"verified\":true}");
            fflush(stdout);
            return 1;
        }
        usleep(20000u);
    } while (milliseconds() - began < 5000u);
    fprintf(stderr, "Radio exchange failed: %s -> %s session=%u sequence=%u\n",
            a->serial, b->serial, session, sequence);
    print_status(a, at); print_status(b, bt);
    printf("{\"test\":\"clean-rx-three-node\",\"source\":\"%s\","
           "\"destination\":\"%s\",\"verified\":false}\n", a->serial, b->serial);
    return 0;
}

int main(int argc, char **argv) {
    int test = argc >= 2 && !strcmp(argv[1], "test");
    int single = argc == 3 && (!strcmp(argv[1], "status") || !strcmp(argv[1], "bootsel"));
    if ((!test && !single) || (test && argc != 5 && argc != 6)) {
        fputs("Usage: collector status|bootsel SERIAL\n"
              "       collector test PICO_SERIAL BADGE_A_SERIAL BADGE_B_SERIAL [ROUNDS:1..100]\n",
              stderr);
        return 2;
    }
    unsigned long rounds = 5u;
    if (test && argc == 6) {
        char *end;
        rounds = strtoul(argv[5], &end, 10);
        if (!*argv[5] || *end || rounds < 1u || rounds > 100u) {
            fputs("Rounds must be 1..100\n", stderr); return 2;
        }
    }
    if (test && (!strcmp(argv[2], argv[3]) || !strcmp(argv[2], argv[4]) ||
                 !strcmp(argv[3], argv[4]))) {
        fputs("Three distinct explicit device serials are required\n", stderr); return 2;
    }
    libusb_context *context = NULL;
    int result = libusb_init(&context);
    if (result) { fprintf(stderr, "libusb_init: %s\n", libusb_error_name(result)); return 1; }
    device_t devices[3] = {{0}};
    uint32_t count = test ? 3u : 1u, opened = 0u;
    int ok = 1;
    for (uint32_t i = 0u; i < count; ++i) {
        if (test) devices[i].node = (uint8_t)(i + 1u);
        if (!open_device(context, &devices[i], argv[i + 2u])) { ok = 0; break; }
        ++opened;
        uint8_t trace[WIFI_TRACE_SIZE];
        if (!status(&devices[i], trace)) { ok = 0; break; }
        print_status(&devices[i], trace);
        if (test) {
            uint64_t began = milliseconds();
            while (!usb_report_u32(trace + 16u) && milliseconds() - began < 20000u) {
                usleep(100000u);
                if (!status(&devices[i], trace)) { ok = 0; break; }
            }
            if (!ok || !usb_report_u32(trace + 16u)) {
                print_status(&devices[i], trace);
                fprintf(stderr, "Radio not ready on %s\n", devices[i].serial);
                ok = 0; break;
            }
        }
    }
    if (ok && single && !strcmp(argv[1], "bootsel")) {
        uint8_t unused = 0;
        ok = transfer(devices[0].usb, 0x40u, LIVE_USB_BOOTSEL, &unused, 0u);
    }
    if (ok && test) {
        FILE *random = fopen("/dev/urandom", "rb");
        if (!random) { perror("/dev/urandom"); ok = 0; }
        uint32_t passed = 0u;
        for (uint32_t round = 0u; round < rounds && ok; ++round)
            for (uint32_t a = 0u; a < 3u && ok; ++a)
                for (uint32_t b = 0u; b < 3u && ok; ++b) if (a != b) {
                    ok = exchange(&devices[a], &devices[b], random);
                    if (ok) ++passed;
                }
        for (uint32_t i = 0u; i < 3u; ++i) {
            uint8_t trace[WIFI_TRACE_SIZE];
            if (status(&devices[i], trace)) print_status(&devices[i], trace);
            else ok = 0;
        }
        printf("{\"summary\":\"three-node-exchanges\",\"passed\":%u,\"expected\":%lu,"
               "\"verified\":%s}\n", passed, rounds * 6u, ok ? "true" : "false");
        if (random) fclose(random);
    }
    for (uint32_t i = 0u; i < count; ++i) if (devices[i].usb) {
        if (i < opened) (void)libusb_release_interface(devices[i].usb, 0);
        libusb_close(devices[i].usb);
    }
    libusb_exit(context);
    return ok ? 0 : 1;
}
