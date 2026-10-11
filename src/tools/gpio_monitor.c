#include "display.h"

#define IO_BANK0_BASE 0x40028000u
#define PADS_BANK0_BASE 0x40038000u
#define TIMER0_TIMELR (*(volatile uint32_t *)(0x400b0000u + 0x0cu))
#define GPIO_STATUS(gpio) \
    (*(volatile uint32_t *)(IO_BANK0_BASE + (gpio) * 8u))
#define GPIO_INTR(gpio) \
    (*(volatile uint32_t *)(IO_BANK0_BASE + 0x230u + ((gpio) / 8u) * 4u))
#define GPIO_PAD(gpio) \
    (*(volatile uint32_t *)(PADS_BANK0_BASE + 0x04u + (gpio) * 4u))

#define GPIO_STATUS_INFROMPAD (1u << 17u)
#define GPIO_PAD_ISO (1u << 8u)
#define GPIO_PAD_OD (1u << 7u)
#define GPIO_PAD_IE (1u << 6u)
#define GPIO_PAD_PUE (1u << 3u)
#define GPIO_PAD_PDE (1u << 2u)

#define FIRST_GPIO 0u
#define LAST_GPIO 22u
#define GPIO_COUNT (LAST_GPIO - FIRST_GPIO + 1u)
#define ROW_X 8u
#define ROW_Y 47u
#define ROW_HEIGHT 8u
#define ROW_WIDTH 304u

typedef struct {
    uint32_t timestamp_ms;
    uint16_t edge_count;
    uint8_t action;
    uint8_t level;
} pin_state_t;

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

static pin_state_t pins[GPIO_COUNT];
static uint32_t monitor_started_at;
static uint32_t display_coupled_pins;

static uint32_t time_us(void) {
    return TIMER0_TIMELR;
}

static uint32_t gpio_edge_mask(uint32_t gpio) {
    return 0xcu << (4u * (gpio % 8u));
}

static uint32_t gpio_edges(uint32_t gpio) {
    return GPIO_INTR(gpio) & gpio_edge_mask(gpio);
}

static void clear_gpio_edges(uint32_t gpio, uint32_t edges) {
    GPIO_INTR(gpio) = edges;
}

static uint32_t gpio_level(uint32_t gpio) {
    return (GPIO_STATUS(gpio) & GPIO_STATUS_INFROMPAD) != 0u;
}

static void configure_pads(void) {
    for (uint32_t gpio = FIRST_GPIO; gpio <= LAST_GPIO; ++gpio) {
        uint32_t pad = GPIO_PAD(gpio);
        pad |= GPIO_PAD_OD | GPIO_PAD_IE | GPIO_PAD_PUE;
        pad &= ~(GPIO_PAD_ISO | GPIO_PAD_PDE);
        GPIO_PAD(gpio) = pad;
    }
}

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
    if (x < origin_x || y < origin_y) return 0;
    uint32_t local_x = x - origin_x;
    uint32_t local_y = y - origin_y;
    if (local_y >= 7u * scale) return 0;

    uint32_t index = local_x / (6u * scale);
    uint32_t cell_x = (local_x / scale) % 6u;
    for (uint32_t position = 0; position < index; ++position) {
        if (text[position] == '\0') return 0;
    }
    char character = text[index];
    if (character == '\0' || cell_x >= 5u) return 0;
    return (glyph_row(character, local_y / scale) &
            (1u << (4u - cell_x))) != 0u;
}

static void decimal_fixed(char *output, uint32_t value, uint32_t digits) {
    output[digits] = '\0';
    while (digits != 0u) {
        --digits;
        output[digits] = (char)('0' + value % 10u);
        value /= 10u;
    }
}

static void copy_text(char *output, uint32_t offset, const char *text,
                      uint32_t width) {
    for (uint32_t index = 0; index < width; ++index) {
        output[offset + index] =
            text[index] == '\0' ? ' ' : text[index];
    }
}

static void row_text(uint32_t gpio, char output[36]) {
    pin_state_t *state = &pins[gpio - FIRST_GPIO];
    decimal_fixed(output, gpio, 2u);
    output[2] = ' ';
    output[3] = ' ';
    copy_text(output, 4u,
              state->action == 3u ? "BOTH" :
              (state->action == 1u ? "FALL" :
               (state->action == 2u ? "RISE" : "NONE")),
              4u);
    output[8] = ' ';
    output[9] = ' ';
    copy_text(output, 10u, state->level != 0u ? "HIGH" : "LOW", 4u);
    output[14] = ' ';
    output[15] = ' ';
    decimal_fixed(output + 16u, state->edge_count, 4u);
    output[20] = ' ';
    output[21] = ' ';
    decimal_fixed(output + 22u, state->timestamp_ms, 8u);
    output[30] = '\0';
}

static uint16_t screen_pixel(uint32_t x, uint32_t y) {
    uint16_t background = rgb565(9u, 22u, 42u);
    uint16_t white = rgb565(248u, 250u, 252u);
    uint16_t blue = rgb565(9u, 105u, 218u);
    uint16_t green = rgb565(46u, 160u, 67u);
    uint16_t yellow = rgb565(210u, 153u, 34u);

    if (x < 4u || x >= BADGER_DISPLAY_WIDTH - 4u ||
        y < 4u || y >= BADGER_DISPLAY_HEIGHT - 4u) return blue;
    if (text_pixel("GPIO 0 22 EDGE MONITOR", x, y, 34u, 12u, 2u)) {
        return white;
    }
    if (text_pixel("PIN  EDGE  LEVEL COUNT  TIME MS",
                   x, y, ROW_X, 36u, 1u)) return yellow;

    if (y >= ROW_Y && y < ROW_Y + GPIO_COUNT * ROW_HEIGHT) {
        uint32_t gpio = FIRST_GPIO + (y - ROW_Y) / ROW_HEIGHT;
        uint32_t origin_y = ROW_Y + (gpio - FIRST_GPIO) * ROW_HEIGHT;
        char text[36];
        row_text(gpio, text);
        if (text_pixel(text, x, y, ROW_X, origin_y, 1u)) {
            return pins[gpio - FIRST_GPIO].action == 0u ? white : green;
        }
    }
    return background;
}

static void draw_full_screen(void) {
    badger_display_begin();
    for (uint32_t x = 0; x < BADGER_DISPLAY_WIDTH; ++x) {
        for (uint32_t y = 0; y < BADGER_DISPLAY_HEIGHT; ++y) {
            badger_display_write(screen_pixel(x, y));
        }
    }
    badger_display_end();
}

static void draw_gpio_row(uint32_t gpio) {
    uint32_t y_start = ROW_Y + (gpio - FIRST_GPIO) * ROW_HEIGHT;
    badger_display_begin_region(ROW_X, y_start, ROW_WIDTH, ROW_HEIGHT);
    for (uint32_t x = ROW_X; x < ROW_X + ROW_WIDTH; ++x) {
        for (uint32_t y = y_start; y < y_start + ROW_HEIGHT; ++y) {
            badger_display_write(screen_pixel(x, y));
        }
    }
    badger_display_end();
}

static void clear_all_edges(void) {
    for (uint32_t gpio = FIRST_GPIO; gpio <= LAST_GPIO; ++gpio) {
        clear_gpio_edges(gpio, gpio_edge_mask(gpio));
    }
}

static void calibrate_display_coupling(void) {
    display_coupled_pins = 0u;
    for (uint32_t row = FIRST_GPIO; row <= LAST_GPIO; ++row) {
        clear_all_edges();
        draw_gpio_row(row);
        for (uint32_t gpio = FIRST_GPIO; gpio <= LAST_GPIO; ++gpio) {
            if (gpio_edges(gpio) != 0u) {
                display_coupled_pins |= 1u << gpio;
            }
        }
    }
    clear_all_edges();
}

static void clear_display_edges(void) {
    for (uint32_t gpio = FIRST_GPIO; gpio <= LAST_GPIO; ++gpio) {
        if ((display_coupled_pins & (1u << gpio)) == 0u) continue;
        uint32_t edges = gpio_edges(gpio);
        if (edges != 0u) clear_gpio_edges(gpio, edges);
    }
}

void bare_main(void) {
    badger_display_init();
    configure_pads();
    for (uint32_t gpio = FIRST_GPIO; gpio <= LAST_GPIO; ++gpio) {
        pins[gpio - FIRST_GPIO].level = (uint8_t)gpio_level(gpio);
    }
    draw_full_screen();
    calibrate_display_coupling();
    monitor_started_at = time_us();

    while (1) {
        for (uint32_t gpio = FIRST_GPIO; gpio <= LAST_GPIO; ++gpio) {
            uint32_t edges = gpio_edges(gpio);
            if (edges == 0u) continue;

            uint32_t shift = 4u * (gpio % 8u);
            uint32_t action = (edges >> shift) & 0xcu;
            pin_state_t *state = &pins[gpio - FIRST_GPIO];
            clear_gpio_edges(gpio, edges);
            state->timestamp_ms =
                (time_us() - monitor_started_at) / 1000u;
            state->action = (uint8_t)(action >> 2u);
            state->level = (uint8_t)gpio_level(gpio);
            uint32_t increment =
                ((action & 0x4u) != 0u) + ((action & 0x8u) != 0u);
            if (state->edge_count + increment <= 9999u) {
                state->edge_count = (uint16_t)(state->edge_count + increment);
            } else {
                state->edge_count = 9999u;
            }
            draw_gpio_row(gpio);
            clear_display_edges();
        }
    }
}
