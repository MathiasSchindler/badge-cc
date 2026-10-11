#include "../../../src/platform/badger/wifi_pio_fifo.h"

extern void badger_display_write_pixels_hw(const uint16_t *, uint32_t,
                                            volatile uint32_t *);

static volatile uint32_t status, tx, rx, timer, sio[20], pio[64], gpio[64];
static uint8_t source[2058], output[2058];
static uint16_t pixels[258];

int main(void) {
    const wifi_pio_fifo_t fifo = {&status, &tx, &rx, &timer};
    const uint32_t counts[] = {0u, 1u, 4u, 16u, 17u, 514u};
    for (uint32_t i = 0u; i < sizeof(source); ++i) source[i] = (uint8_t)(i * 37u + 11u);
    for (uint32_t k = 0u; k < 6u; ++k) {
        uint32_t words = counts[k], length = words * 4u;
        status = timer = 0u;
        tx = 0x87654321u;
        if (!wifi_pio_write_words(&fifo, source + 1u, words, 0u)) return 1;
        uint32_t expected = 0x87654321u;
        if (words != 0u) {
            expected = (uint32_t)source[length - 3u] << 24u |
                       (uint32_t)source[length - 2u] << 16u |
                       (uint32_t)source[length - 1u] << 8u | source[length];
        }
        if (tx != expected) return 2;
        for (uint32_t i = 0u; i < sizeof(output); ++i) output[i] = 0xa5u;
        rx = 0x12345678u;
        if (!wifi_pio_read_words(&fifo, output + 1u, words, 0u)) return 3;
        if (output[0] != 0xa5u || output[length + 1u] != 0xa5u) return 4;
        for (uint32_t i = 0u; i < length; ++i) {
            if (output[i + 1u] != (uint8_t)(0x12345678u >> (24u - (i % 4u) * 8u))) return 5;
        }
    }
    if (!wifi_pio_write_words(0, 0, 0u, 0u) || !wifi_pio_read_words(0, 0, 0u, 0u)) return 6;
    timer = 100000u;
    tx = 0x87654321u;
    output[1] = 0xa5u;
    if (wifi_pio_write_words(&fifo, source + 1u, 1u, 0u) ||
        wifi_pio_read_words(&fifo, output + 1u, 1u, 0u)) return 7;
    if (tx != 0x87654321u || output[1] != 0xa5u) return 8;
    timer = 99999u;
    if (!wifi_pio_write_words(&fifo, source + 1u, 1u, 0u)) return 9;
    timer = 17u;
    if (!wifi_pio_read_words(&fifo, output + 1u, 1u, 0xffffffffu - 17u)) return 10;
    timer = 0u;
    status = 1u << 16u;
    tx = 0x87654321u;
    if (wifi_pio_write_words(&fifo, source + 1u, 1u, 0u) || tx != 0x87654321u) return 11;
    status = 1u << 8u;
    output[1] = 0xa5u;
    if (wifi_pio_read_words(&fifo, output + 1u, 1u, 0u) || output[1] != 0xa5u) return 12;
    for (uint32_t i = 0u; i < 258u; ++i) pixels[i] = (uint16_t)(i * 257u + 123u);
    for (uint32_t i = 0u; i < 12u; ++i) sio[i] = 0xa55aa55au;
    badger_display_write_pixels_hw(0, 0u, 0);
    for (uint32_t k = 0u; k < 3u; ++k) {
        uint32_t count = k == 0u ? 1u : k == 1u ? 16u : 256u;
        badger_display_write_pixels_hw(pixels + 1u, count, sio);
        if (sio[6] != (1u << 30u) || sio[7] != (pixels[count] & 255u) ||
            sio[8] != (1u << 30u) || sio[9] != 255u) return 13;
        for (uint32_t i = 0u; i < 12u; ++i) {
            if ((i < 6u || i > 9u) && sio[i] != 0xa55aa55au) return 14;
        }
    }
    for (uint32_t mode = 0u; mode < 2u; ++mode) {
        for (uint32_t i = 0u; i < 64u; ++i) pio[i] = gpio[i] = 0xa55aa55au;
        uint32_t receive = mode == 0u ? 0u : 80u;
        wifi_pio_prepare_hw(pio, gpio, 68u, receive);
        if (pio[18] != 0x6001u || pio[19] != 0x1040u || pio[20] != 0xe080u ||
            pio[21] != (mode ? 0x5001u : 0xc000u) ||
            pio[22] != (mode ? 0x0083u : 4u) || pio[23] != 0xc000u || pio[24] != 6u)
            return 15;
        if (pio[50] != (8u << 16u) || pio[51] != (6u << 12u) ||
            pio[52] != (3u << 16u) || pio[55] != 0x241c7718u ||
            pio[54] != 0u || pio[14] != (1u << 24u) ||
            pio[4] != (mode ? receive * 8u - 1u : 0u) || gpio[59] != 6u)
            return 16;
        for (uint32_t i = 0u; i < 64u; ++i) {
            if (!(i == 4u || i == 14u || (i >= 18u && i <= 24u) ||
                  (i >= 50u && i <= 52u) || i == 54u || i == 55u) &&
                pio[i] != 0xa55aa55au) return 17;
            if (i != 59u && gpio[i] != 0xa55aa55au) return 18;
        }
        for (uint32_t i = 0u; i < 20u; ++i) sio[i] = 0xa55aa55au;
        wifi_pio_finish_hw(pio, sio, gpio);
        if (pio[0] != 0u || pio[54] != 0xe080u || sio[16] != (1u << 24u) ||
            sio[8] != (1u << 29u) || sio[6] != (1u << 25u) ||
            gpio[49] != 5u || gpio[59] != 5u) return 19;
        for (uint32_t i = 0u; i < 20u; ++i) {
            if (i != 6u && i != 8u && i != 16u && sio[i] != 0xa55aa55au) return 20;
        }
    }
    return 0;
}
