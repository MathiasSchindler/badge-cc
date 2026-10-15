#define _POSIX_C_SOURCE 200809L
#include "usb_live_transport.h"
#include "../../shared/bytes.h"
#include "../../tools/diagnostics_logic.h"
#include <errno.h>
#include <stdlib.h>
#include <time.h>

static void pause_poll(void) {
    struct timespec delay = {0, 50000000};
    nanosleep(&delay, NULL);
}

static int valid(const uint8_t record[64], const char *serial) {
    if (badger_bytes_load_u32(record) != 0x5447534du ||
        badger_bytes_load_u32(record + 60u) != diag_crc32(record, 60u)) return 0;
    char actual[17];
    diag_hex(actual, badger_bytes_load_u32(record + 16u), 8u);
    diag_hex(actual + 8u, badger_bytes_load_u32(record + 12u), 8u);
    return strcmp(actual, serial) == 0;
}

static int monotonic_ms(uint64_t *value) {
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0) {
        perror("clock_gettime");
        return 0;
    }
    *value = (uint64_t)time.tv_sec * 1000u + (uint64_t)time.tv_nsec / 1000000u;
    return 1;
}

int main(int argc, char **argv) {
    int soak = (argc == 5 || argc == 6) && strcmp(argv[4], "--soak") == 0;
    unsigned long seconds = 60u;
    if (soak && argc == 6) {
        char *end;
        errno = 0;
        seconds = strtoul(argv[5], &end, 10);
        if (errno != 0 || end == argv[5] || *end != '\0' || seconds < 30u || seconds > 600u) {
            fputs("Soak duration must be 30..600 seconds\n", stderr);
            return 2;
        }
    }
    if ((argc != 4 && !soak) || strlen(argv[1]) != 16u || strlen(argv[2]) != 16u ||
        strcmp(argv[1], argv[2]) == 0) {
        fputs("Usage: collect SERIAL_A SERIAL_B OUTPUT_PREFIX [--soak [SECONDS]]\n", stderr);
        return 2;
    }
    uint64_t started;
    if (!monotonic_ms(&started)) return 1;
    libusb_context *context = NULL;
    int result = libusb_init(&context);
    if (result != 0) {
        fprintf(stderr, "USB initialization: %s\n", libusb_error_name(result));
        return 1;
    }
    libusb_device_handle *devices[2] = {NULL, NULL};
    uint8_t records[2][64] = {{0}};
    uint8_t traces[2][264] = {{0}};
    int ready[2] = {0,0}, ok = 0;
    uint32_t last_sequence[2] = {0u,0u};
    uint64_t progressed[2] = {0u,0u}, elapsed = 0u;
    uint64_t first_stall[2] = {0u,0u};
    uint64_t duration = (uint64_t)seconds * 1000u;
    uint64_t deadline = soak ? duration + 10000u : 10000u;
    for (uint64_t attempt = 0u; attempt <= deadline / 50u; ++attempt) {
        uint64_t time;
        if (!monotonic_ms(&time)) goto finish;
        elapsed = time - started;
        if (elapsed > deadline) break;
        int failed = 0;
        for (unsigned i = 0u; i < 2u; ++i) {
            if (devices[i] == NULL) devices[i] = find_badge(context, argv[i + 1u]);
            if (devices[i] == NULL) continue;
            uint8_t record[64];
            if (!transfer(devices[i], 0xc0u, LIVE_USB_WIFI_REPORT, record, sizeof(record)))
                goto finish;
            if (!valid(record, argv[i + 1u])) {
                fputs("Invalid or wrongly bound micro-signal test record\n", stderr);
                goto finish;
            }
            memcpy(records[i], record, sizeof(record));
            if (badger_bytes_load_u32(record + 4u) != 0u ||
                badger_bytes_load_u32(record + 52u) != 0u) failed = 1;
            ready[i] = badger_bytes_load_u32(record + 24u) == 6u &&
                       badger_bytes_load_u32(record + 28u) == 2u;
            if (soak) {
                uint32_t sequence = badger_bytes_load_u32(record + 44u);
                if (sequence != last_sequence[i]) {
                    progressed[i] = elapsed;
                    last_sequence[i] = sequence;
                }
                if (elapsed > 15000u && last_sequence[i] != 0u &&
                    elapsed - progressed[i] >= 5000u && first_stall[i] == 0u)
                    first_stall[i] = elapsed;
                ready[i] = elapsed >= duration && elapsed - progressed[i] < 5000u &&
                           badger_bytes_load_u32(record + 24u) >= 30u &&
                           badger_bytes_load_u32(record + 28u) >= 10u &&
                           (badger_bytes_load_u32(record + 8u) & 65535u) >= 64u;
            }
        }
        if (failed) {
            fputs("Device reported an application or USB error\n", stderr);
            goto finish;
        }
        if (ready[0] && ready[1]) {
            ok = 1;
            /* Sequential USB snapshots may straddle the first repeat of a new event. */
            for (unsigned i = 0u; i < 2u; ++i) {
                const uint8_t *own = records[i], *peer = records[1u-i];
                if (memcmp(own + 32u, peer + 12u, 8u) != 0 ||
                    badger_bytes_load_u32(own + 40u) != badger_bytes_load_u32(peer + 20u) ||
                    (!soak && (badger_bytes_load_u32(own + 44u) != 2u ||
                               badger_bytes_load_u32(own + 48u) != 5u)) ||
                    (soak && (badger_bytes_load_u32(own + 44u) >
                                  (badger_bytes_load_u32(peer + 24u) + 2u) / 3u + 1u ||
                              badger_bytes_load_u32(own + 28u) >
                                  badger_bytes_load_u32(own + 44u))) ||
                    badger_bytes_load_u32(own + 56u) <= 1200000u) ok = 0;
            }
            if (!ok) fputs("Received event does not match the other device's boot session\n", stderr);
            break;
        }
        pause_poll();
    }
    if (!ok) fputs("Paired button-event OTA confirmation failed or timed out\n", stderr);
finish:
    if (soak && (first_stall[0] != 0u || first_stall[1] != 0u)) {
        fputs("Peer sequence stopped advancing for at least five seconds\n", stderr);
        ok = 0;
    }
    for (unsigned i = 0u; i < 2u; ++i) {
        char path[1024];
        int length = snprintf(path, sizeof(path), "%s-%s.bin", argv[3], argv[i + 1u]);
        if (length < 0 || (size_t)length >= sizeof(path)) {
            fputs("Evidence output path is too long\n", stderr);
            ok = 0;
        } else {
            FILE *file = fopen(path, "wb");
            if (file == NULL) { perror(path); ok = 0; }
            else {
                if (fwrite(records[i], 1u, 64u, file) != 64u) {
                    perror(path); ok = 0;
                }
                if (fclose(file) != 0) { perror(path); ok = 0; }
            }
        }
        if (devices[i] != NULL) {
            for (uint16_t page = 0u; page < 11u; ++page) {
                uint8_t bytes[64];
                int read = libusb_control_transfer(devices[i], 0xc0u, LIVE_USB_WIFI_TRACE,
                                                  page, 0u, bytes, sizeof(bytes), 1000u);
                if (read != 64 || badger_bytes_load_u32(bytes + 60u) != diag_crc32(bytes, 60u) ||
                    bytes[32] != page) {
                    fprintf(stderr, "Cannot read test diagnostic page %u: %d\n", page, read);
                    ok = 0;
                    break;
                }
                memcpy(traces[i] + 24u * page, bytes + 36u, 24u);
            }
            if (!transfer(devices[i], 0x40u, LIVE_USB_BOOTSEL, NULL, 0u)) ok = 0;
            libusb_close(devices[i]);
        }
    }
    libusb_exit(context);
    printf("{\"bidirectional_button_event_confirmed\":%s,\"soak\":%s,\"elapsed_ms\":%llu",
           ok ? "true" : "false", soak ? "true" : "false", (unsigned long long)elapsed);
    for (unsigned i = 0u; i < 2u; ++i) {
        uint32_t error = badger_bytes_load_u32(records[i] + 4u);
        uint32_t f2_counts = error ? 0u : badger_bytes_load_u32(records[i] + 8u);
        printf(",\"%s\":{\"error\":%u,\"detail\":\"%08x\",\"tx_frames\":%u,"
               "\"rx_events\":%u,\"f2_posted\":%u,\"f2_dropped\":%u}",
               argv[i + 1u], error, badger_bytes_load_u32(records[i] + 8u),
               badger_bytes_load_u32(records[i] + 24u),
               badger_bytes_load_u32(records[i] + 28u),
               f2_counts & 65535u, f2_counts >> 16u);
        printf(",\"diagnostic_%u\":[", i);
        for (uint32_t word = 0u; word < 16u; ++word)
            printf("%s\"%08x\"", word ? "," : "", badger_bytes_load_u32(traces[i] + word * 4u));
        putchar(']');
        printf(",\"first_stall_ms_%u\":%llu", i, (unsigned long long)first_stall[i]);
        uint32_t address = badger_bytes_load_u32(records[i] + 8u);
        if (error == 4u && address >= 0x38400u && address < 0x38460u) {
            printf(",\"staging_expected_%u\":\"", i);
            for (uint32_t byte = 0u; byte < 96u; ++byte) printf("%02x", traces[i][64u + byte]);
            printf("\",\"staging_readback_%u\":\"", i);
            for (uint32_t byte = 0u; byte < 96u; ++byte) printf("%02x", traces[i][160u + byte]);
            putchar('"');
            continue;
        }
        static const char *const names[] = {"psm_mailbox", "psm_diag", "rxe_sample", "rxe_post"};
        static const uint32_t offsets[] = {64u, 96u, 128u, 144u};
        for (uint32_t group = 0u; group < 4u; ++group) {
            printf(",\"%s_%u\":[", names[group], i);
            uint32_t count = group >= 2u ? 8u : 16u;
            for (uint32_t word = 0u; word < count; ++word) {
                const uint8_t *bytes = traces[i] + offsets[group] + word * 2u;
                printf("%s\"%04x\"", word ? "," : "",
                       (unsigned)bytes[0] | ((unsigned)bytes[1] << 8u));
            }
            putchar(']');
        }
        printf(",\"sample_polls_%u\":%u,\"sample_ticks_%u\":%u", i,
               badger_bytes_load_u32(traces[i] + 160u),
               i, badger_bytes_load_u32(traces[i] + 164u));
    }
    puts("}");
    return ok ? 0 : 1;
}
