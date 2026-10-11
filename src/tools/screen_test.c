#include "display.h"

static const uint8_t font_letters[26][7] = {
    {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30},
    {14, 17, 16, 16, 16, 17, 14}, {30, 17, 17, 17, 17, 17, 30},
    {31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16},
    {14, 17, 16, 23, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17},
    {14, 4, 4, 4, 4, 4, 14}, {7, 2, 2, 2, 18, 18, 12},
    {17, 18, 20, 24, 20, 18, 17}, {16, 16, 16, 16, 16, 16, 31},
    {17, 27, 21, 21, 17, 17, 17}, {17, 25, 21, 19, 17, 17, 17},
    {14, 17, 17, 17, 17, 17, 14}, {30, 17, 17, 30, 16, 16, 16},
    {14, 17, 17, 17, 21, 18, 13}, {30, 17, 17, 30, 20, 18, 17},
    {15, 16, 16, 14, 1, 1, 30}, {31, 4, 4, 4, 4, 4, 4},
    {17, 17, 17, 17, 17, 17, 14}, {17, 17, 17, 17, 17, 10, 4},
    {17, 17, 17, 21, 21, 21, 10}, {17, 17, 10, 4, 10, 17, 17},
    {17, 17, 10, 4, 4, 4, 4}, {31, 1, 2, 4, 8, 16, 31},
};

static const uint8_t font_digits[10][7] = {
    {14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14},
    {14, 17, 1, 2, 4, 8, 31}, {30, 1, 1, 14, 1, 1, 30},
    {2, 6, 10, 18, 31, 2, 2}, {31, 16, 16, 30, 1, 1, 30},
    {14, 16, 16, 30, 17, 17, 14}, {31, 1, 2, 4, 8, 8, 8},
    {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 1, 14},
};

static uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
    return (uint16_t)(((uint16_t)(red & 0xf8u) << 8) |
                      ((uint16_t)(green & 0xfcu) << 3) |
                      ((uint16_t)blue >> 3));
}

static uint8_t glyph_row(char character, uint32_t row) {
    if (character >= 'A' && character <= 'Z') {
        return font_letters[(uint32_t)(character - 'A')][row];
    }
    if (character >= '0' && character <= '9') {
        return font_digits[(uint32_t)(character - '0')][row];
    }
    return 0u;
}

static int text_pixel(const char *text, uint32_t x, uint32_t y,
                      uint32_t origin_x, uint32_t origin_y,
                      uint32_t scale) {
    uint32_t local_x;
    uint32_t local_y;
    uint32_t index;
    uint32_t cell_x;
    char character;

    if (x < origin_x || y < origin_y) {
        return 0;
    }
    local_x = x - origin_x;
    local_y = y - origin_y;
    if (local_y >= 7u * scale) {
        return 0;
    }

    index = local_x / (6u * scale);
    cell_x = (local_x / scale) % 6u;
    for (uint32_t position = 0; position < index; ++position) {
        if (text[position] == '\0') {
            return 0;
        }
    }
    character = text[index];
    if (character == '\0' || cell_x >= 5u) {
        return 0;
    }

    return (glyph_row(character, local_y / scale) &
            (1u << (4u - cell_x))) != 0u;
}

static uint16_t test_pixel(uint32_t x, uint32_t y) {
    uint16_t navy = rgb565(9u, 22u, 42u);
    uint16_t white = rgb565(248u, 250u, 252u);
    uint16_t green = rgb565(46u, 160u, 67u);
    uint16_t blue = rgb565(9u, 105u, 218u);
    uint16_t purple = rgb565(130u, 80u, 223u);
    uint16_t orange = rgb565(251u, 143u, 68u);

    if (x < 8u || x >= BADGER_DISPLAY_WIDTH - 8u ||
        y < 8u || y >= BADGER_DISPLAY_HEIGHT - 8u) {
        return white;
    }
    if (y >= 176u) {
        if (x < 80u) return green;
        if (x < 160u) return blue;
        if (x < 240u) return purple;
        return orange;
    }
    if (text_pixel("GITHUB UNIVERSE", x, y, 28u, 38u, 3u) ||
        text_pixel("2026", x, y, 124u, 78u, 3u) ||
        text_pixel("DISPLAY OK", x, y, 70u, 122u, 3u) ||
        text_pixel("320 X 240", x, y, 112u, 151u, 1u)) {
        return white;
    }
    return navy;
}

void bare_main(void) {
    badger_display_init();
    badger_display_begin();

    for (uint32_t x = 0; x < BADGER_DISPLAY_WIDTH; ++x) {
        for (uint32_t y = 0; y < BADGER_DISPLAY_HEIGHT; ++y) {
            badger_display_write(test_pixel(x, y));
        }
    }

    badger_display_end();
    while (1) {
        __asm__ volatile ("wfi");
    }
}
