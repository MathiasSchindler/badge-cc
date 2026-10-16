#define _POSIX_C_SOURCE 200809L
#include "usb_live_transport.h"
#include "../../shared/bytes.h"
#include "../../tools/diagnostics_logic.h"
#include "../../../clean-rx/sdio_test.h"
#include "../../tools/micro-signal-ofdm.h"
#include <errno.h>
#include <stdlib.h>
#include <time.h>

static void pause_poll(void) {
    struct timespec delay = {0, 50000000};
    nanosleep(&delay, NULL);
}

static int valid(const uint8_t record[64], const char *serial) {
    uint32_t magic = badger_bytes_load_u32(record);
    if ((magic != 0x5447534du && magic != 0x5554534du &&
         magic != MICRO_OFDM_TEST_MAGIC) ||
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
    int transport = (argc == 5 || argc == 6) && strcmp(argv[4], "--transport") == 0;
    int soak = (argc == 5 || argc == 6) && (strcmp(argv[4], "--soak") == 0 || transport);
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
        fputs("Usage: collect SERIAL_A SERIAL_B OUTPUT_PREFIX [--soak|--transport [SECONDS]]\n", stderr);
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
    uint8_t traces[2][((CLEAN_TRANSPORT_TRACE_BYTES + 23u) / 24u) * 24u] = {{0}};
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
                if (transport) {
                    uint32_t pacing = badger_bytes_load_u32(own + 48u);
                    uint32_t window = badger_bytes_load_u32(own + 40u);
                    uint32_t dma_control = badger_bytes_load_u32(own + 36u);
                    uint32_t pattern = badger_bytes_load_u32(own + 32u);
                    if (badger_bytes_load_u32(own + 28u) < 1000u ||
                        badger_bytes_load_u32(own + 28u) < badger_bytes_load_u32(own + 24u) ||
                        badger_bytes_load_u32(own + 28u) - badger_bytes_load_u32(own + 24u) > 16u ||
                        badger_bytes_load_u32(own + 56u) <= 1200000u ||
                        (pacing != 0u && pacing != CLEAN_SDIO_TEST_PACING_TICKS) ||
                        pacing != badger_bytes_load_u32(peer + 48u) ||
                        window > 16u || window != badger_bytes_load_u32(peer + 40u) ||
                        (dma_control & 1u) == 0u || dma_control == 0xffffffffu ||
                        dma_control != badger_bytes_load_u32(peer + 36u)) ok = 0;
                    if (pattern > 2u || pattern != badger_bytes_load_u32(peer + 32u)) ok = 0;
                    if (pattern == 2u &&
                        badger_bytes_load_u32(own + 28u) < 2u * (CLEAN_MPDU_BYTES - 7u))
                        ok = 0;
                    continue;
                }
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
            if (!ok) fputs(transport ? "Transport counts, runtime, pacing, window, DMA or pattern failed validation\n" :
                                      "Received event does not match the other device's boot session\n", stderr);
            break;
        }
        pause_poll();
    }
    if (!ok) fputs(transport ? "Transport confirmation failed or timed out\n" :
                              "Paired button-event OTA confirmation failed or timed out\n", stderr);
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
            uint16_t pages = transport || badger_bytes_load_u32(records[i]) == 0x5554534du ||
                             badger_bytes_load_u32(records[i]) == MICRO_OFDM_TEST_MAGIC ?
                             (CLEAN_TRANSPORT_TRACE_BYTES + 23u) / 24u : 43u;
            for (uint16_t page = 0u; page < pages; ++page) {
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
    int mtu = badger_bytes_load_u32(records[0]) == 0x5554534du ||
              badger_bytes_load_u32(records[1]) == 0x5554534du;
    int ofdm = badger_bytes_load_u32(records[0]) == MICRO_OFDM_TEST_MAGIC ||
               badger_bytes_load_u32(records[1]) == MICRO_OFDM_TEST_MAGIC;
    if (mtu || ofdm) {
        for (unsigned i = 0u; i < 2u; ++i) {
            if (badger_bytes_load_u32(records[i]) !=
                (ofdm ? MICRO_OFDM_TEST_MAGIC : 0x5554534du)) ok = 0;
            for (unsigned length = 0u; length < (ofdm ? MICRO_OFDM_CASES : CLEAN_MTU_LENGTHS); ++length) {
                if (traces[i][800u + length * 2u] == 0u &&
                    traces[i][801u + length * 2u] == 0u) {
                    if (ofdm)
                        fprintf(stderr, "Badge %u did not receive %u Mbps OFDM, %u MPDU bytes\n",
                                i, clean_ofdm_rates[length % CLEAN_OFDM_RATES].mbps,
                                micro_ofdm_lengths[length / CLEAN_OFDM_RATES]);
                    else
                        fprintf(stderr, "Badge %u did not receive MPDU length %u\n", i, CLEAN_MTU_MIN_BYTES + length);
                    ok = 0;
                }
            }
        }
    }
    printf("{\"bidirectional_button_event_confirmed\":%s,\"mtu_frames_confirmed\":%s,"
           "\"ofdm_rates_confirmed\":%s,\"transport_confirmed\":%s,\"soak\":%s,\"elapsed_ms\":%llu",
           ok && !transport && !mtu && !ofdm ? "true" : "false", ok && mtu ? "true" : "false",
           ok && ofdm ? "true" : "false",
           ok && transport ? "true" : "false",
           soak ? "true" : "false", (unsigned long long)elapsed);
    if (ofdm) {
        printf(",\"ofdm_rates_mbps\":[");
        for (uint32_t rate = 0u; rate < CLEAN_OFDM_RATES; ++rate)
            printf("%s%u", rate ? "," : "", clean_ofdm_rates[rate].mbps);
        printf("],\"ofdm_mpdu_lengths\":[");
        for (uint32_t length = 0u; length < MICRO_OFDM_LENGTHS; ++length)
            printf("%s%u", length ? "," : "", micro_ofdm_lengths[length]);
        putchar(']');
    }
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
        if (transport) {
            printf(",\"transport_delay_ticks_%u\":%u", i,
                   badger_bytes_load_u32(records[i] + 48u));
            printf(",\"transport_window_%u\":%u", i,
                   badger_bytes_load_u32(records[i] + 40u));
            printf(",\"transport_dma_control_%u\":\"%08x\"", i,
                   badger_bytes_load_u32(records[i] + 36u));
            printf(",\"transport_pattern_%u\":%u", i,
                   badger_bytes_load_u32(records[i] + 32u));
            printf(",\"transport_last_buffer_comparable_%u\":%s", i,
                   badger_bytes_load_u32(traces[i] + CLEAN_TRANSPORT_COMPARE_OFFSET) ?
                   "true" : "false");
            static const char *const groups[] = {
                "transport_core", "transport_last_buffer", "transport_slot3", "transport_ring_prefix"
            };
            static const uint32_t offsets[] = {64u, 96u, 128u, 160u};
            static const uint32_t counts[] = {8u, 8u, 8u, 24u};
            for (uint32_t group = 0u; group < 4u; ++group) {
                printf(",\"%s_%u\":[", groups[group], i);
                for (uint32_t word = 0u; word < counts[group]; ++word)
                    printf("%s\"%08x\"", word ? "," : "",
                           badger_bytes_load_u32(traces[i] + offsets[group] + word * 4u));
                putchar(']');
            }
            printf(",\"transport_history_%u\":[", i);
            for (uint32_t word = 0u; word < 52u; ++word)
                printf("%s\"%08x\"", word ? "," : "",
                       badger_bytes_load_u32(traces[i] + CLEAN_TRANSPORT_HISTORY_OFFSET + word * 4u));
            putchar(']');
        }
        if (error != 0u) {
            printf(",\"failed_receive_%u\":\"", i);
            uint32_t saved = transport ? CLEAN_RX_PACKET_BYTES : 544u;
            for (uint32_t byte = 0u; byte < saved; ++byte) printf("%02x", traces[i][256u + byte]);
            putchar('"');
        }
        printf(",\"first_stall_ms_%u\":%llu", i, (unsigned long long)first_stall[i]);
        uint32_t address = badger_bytes_load_u32(records[i] + 8u);
        if (error == 4u && address >= CLEAN_TX_BUFFER_ADDR &&
            address < CLEAN_TX_BUFFER_ADDR + CLEAN_TX_STORAGE_BYTES) {
            printf(",\"staging_expected_%u\":\"", i);
            for (uint32_t byte = 0u; byte < 96u; ++byte) printf("%02x", traces[i][64u + byte]);
            printf("\",\"staging_readback_%u\":\"", i);
            for (uint32_t byte = 0u; byte < 96u; ++byte) printf("%02x", traces[i][160u + byte]);
            putchar('"');
            continue;
        }
        if (transport) continue;
        if (mtu || ofdm) {
            printf(",\"mtu_tx_diag_%u\":[", i);
            for (uint32_t word = 0u; word < 45u; ++word)
                printf("%s\"%08x\"", word ? "," : "",
                       badger_bytes_load_u32(traces[i] + CLEAN_MTU_TX_DIAG_OFFSET + word * 4u));
            putchar(']');
            printf(",\"mtu_fifo_%u\":[", i);
            for (uint32_t word = 0u; word < 8u; ++word)
                printf("%s\"%08x\"", word ? "," : "",
                       badger_bytes_load_u32(traces[i] + CLEAN_MTU_FIFO_OFFSET + word * 4u));
            printf("],\"mtu_erom_%u\":[", i);
            for (uint32_t word = 0u; word < 65u; ++word)
                printf("%s\"%08x\"", word ? "," : "",
                       badger_bytes_load_u32(traces[i] + CLEAN_MTU_EROM_OFFSET + word * 4u));
            putchar(']');
            printf(",\"mtu_rxe_%u\":[", i);
            for (uint32_t word = 0u; word < 7u; ++word)
                printf("%s\"%08x\"", word ? "," : "",
                       badger_bytes_load_u32(traces[i] + 952u + word * 4u));
            putchar(']');
            printf(",\"mtu_rx_fault_%u\":[", i);
            for (uint32_t word = 0u; word < CLEAN_RX_FAULT_WORDS; ++word)
                printf("%s\"%04x\"", word ? "," : "", traces[i][984u + word * 2u] |
                       ((unsigned)traces[i][985u + word * 2u] << 8u));
            putchar(']');
            printf(",\"%s_coverage_%u\":[", ofdm ? "ofdm" : "mtu", i);
            for (uint32_t length = 0u; length < (ofdm ? MICRO_OFDM_CASES : CLEAN_MTU_LENGTHS); ++length)
                printf("%s%u", length ? "," : "", traces[i][800u + length * 2u] |
                       ((unsigned)traces[i][801u + length * 2u] << 8u));
            printf("],\"mtu_capture_length_%u\":%u,\"mtu_capture_fcs_valid_%u\":%u",
                   i,badger_bytes_load_u32(traces[i] + 944u),
                   i,badger_bytes_load_u32(traces[i] + 948u));
            printf(",\"mtu_capture_%u\":\"", i);
            for (uint32_t byte = 0u; byte < CLEAN_RX_PACKET_BYTES; ++byte)
                printf("%02x", traces[i][1024u + byte]);
            putchar('"');
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
