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

#define GPIO_COUNT 48u
#define SAMPLE_US 5000000u
#define SETTLE_US 100000u
#define RESULT_US 3000000u
#define DEBOUNCE_US 10000u
#define OBSERVE_EXCLUDE_MASK_LOW \
    ((1u << 23u) | (1u << 24u) | (1u << 25u) | (1u << 26u) | \
     (1u << 27u) | (1u << 28u) | (1u << 29u) | (1u << 30u) | \
     (1u << 31u))
#define OBSERVE_EXCLUDE_MASK_HIGH 0x0000ffffu
#define MAX_RESULTS 8u

typedef struct {
    uint8_t gpio;
    uint16_t edges;
} gpio_result_t;

typedef enum {
    PAGE_LIVE,
    PAGE_PROMPT,
    PAGE_RESULT,
    PAGE_SUMMARY,
    PAGE_REALTIME,
} page_t;

#ifdef BUTTON_MAPPER_LOWER
#define CONTROL_COUNT 8u
static const char *const control_names[] = {
    "A", "B", "SELECT", "START", "UP", "DOWN", "LEFT", "RIGHT",
};
#else
#define CONTROL_COUNT 5u
static const char *const control_names[] = {
    "LEFT", "RIGHT", "ENTER", "UP", "DOWN",
};
#endif

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

static page_t page;
static const char *page_control;
static gpio_result_t page_results[MAX_RESULTS];
static uint32_t page_result_count;
static gpio_result_t guided_results[CONTROL_COUNT];
static const char *live_control;
static uint32_t live_gpio;
static uint32_t live_event_count;
static int live_has_event;
static uint32_t live_action;

static uint32_t time_us(void) {
    return TIMER0_TIMELR;
}

#ifndef BUTTON_MAPPER_LIVE
static int pin_is_excluded(uint32_t gpio) {
#ifdef BUTTON_MAPPER_LOWER
    if (gpio == 6u || gpio == 7u || gpio == 9u ||
        gpio == 10u || gpio == 11u) return 1;
#endif
    if (gpio < 32u) {
        return (OBSERVE_EXCLUDE_MASK_LOW & (1u << gpio)) != 0u;
    }
    return (OBSERVE_EXCLUDE_MASK_HIGH & (1u << (gpio - 32u))) != 0u;
}

static void configure_candidate_pads(int pull_up) {
    for (uint32_t gpio = 0; gpio < GPIO_COUNT; ++gpio) {
        if (pin_is_excluded(gpio)) continue;

        uint32_t pad = GPIO_PAD(gpio);
        pad |= GPIO_PAD_OD | GPIO_PAD_IE;
        pad &= ~(GPIO_PAD_ISO | GPIO_PAD_PUE | GPIO_PAD_PDE);
        if (pull_up) pad |= GPIO_PAD_PUE;
        GPIO_PAD(gpio) = pad;
    }
}
#endif

#ifdef BUTTON_MAPPER_LIVE
static void configure_front_button_pads(void) {
    static const uint8_t pins[] = {7u, 9u, 10u, 11u, 6u};
    for (uint32_t index = 0; index < sizeof(pins); ++index) {
        uint32_t pad = GPIO_PAD(pins[index]);
        pad |= GPIO_PAD_OD | GPIO_PAD_IE | GPIO_PAD_PUE;
        pad &= ~(GPIO_PAD_ISO | GPIO_PAD_PDE);
        GPIO_PAD(pins[index]) = pad;
    }
}

static uint32_t gpio_edge_mask(uint32_t gpio) {
    return 0xcu << (4u * (gpio % 8u));
}

static void clear_gpio_edges(uint32_t gpio) {
    GPIO_INTR(gpio) = gpio_edge_mask(gpio);
}

static uint32_t gpio_edges(uint32_t gpio) {
    return GPIO_INTR(gpio) & gpio_edge_mask(gpio);
}
#endif

#ifndef BUTTON_MAPPER_LIVE
static uint32_t read_low_pins(void) {
    uint32_t levels = 0u;
    for (uint32_t gpio = 0; gpio < 32u; ++gpio) {
        if (!pin_is_excluded(gpio) &&
            (GPIO_STATUS(gpio) & GPIO_STATUS_INFROMPAD) != 0u) {
            levels |= 1u << gpio;
        }
    }
    return levels;
}

static uint32_t read_high_pins(void) {
    uint32_t levels = 0u;
    for (uint32_t gpio = 32u; gpio < GPIO_COUNT; ++gpio) {
        if (!pin_is_excluded(gpio) &&
            (GPIO_STATUS(gpio) & GPIO_STATUS_INFROMPAD) != 0u) {
            levels |= 1u << (gpio - 32u);
        }
    }
    return levels;
}
#endif

static void wait_us(uint32_t duration) {
    uint32_t start = time_us();
    while (time_us() - start < duration) {}
}

#ifndef BUTTON_MAPPER_LIVE
static uint32_t collect_events(uint32_t duration_us, uint16_t edges[GPIO_COUNT]) {
    uint32_t start = time_us();
    uint32_t previous_low = read_low_pins();
    uint32_t previous_high = read_high_pins();
    uint32_t changed_count = 0u;

    for (uint32_t gpio = 0; gpio < GPIO_COUNT; ++gpio) {
        edges[gpio] = 0u;
    }

    while (time_us() - start < duration_us) {
        uint32_t current_low = read_low_pins();
        uint32_t current_high = read_high_pins();
        uint32_t changed_low = current_low ^ previous_low;
        uint32_t changed_high = current_high ^ previous_high;

        for (uint32_t gpio = 0; gpio < 32u; ++gpio) {
            if ((changed_low & (1u << gpio)) != 0u &&
                edges[gpio] != 0xffffu) {
                if (edges[gpio] == 0u) ++changed_count;
                ++edges[gpio];
            }
        }
        for (uint32_t gpio = 32u; gpio < GPIO_COUNT; ++gpio) {
            if ((changed_high & (1u << (gpio - 32u))) != 0u &&
                edges[gpio] != 0xffffu) {
                if (edges[gpio] == 0u) ++changed_count;
                ++edges[gpio];
            }
        }
        previous_low = current_low;
        previous_high = current_high;
    }
    return changed_count;
}

static uint32_t button_score(uint16_t edges) {
    if (edges == 0u || edges > 32u) return 0u;
    return 1000u - (edges > 2u ? edges - 2u : 2u - edges);
}

static uint32_t rank_results(const uint16_t edges[GPIO_COUNT],
                             gpio_result_t results[MAX_RESULTS]) {
    uint32_t result_count = 0u;

    for (uint32_t gpio = 0; gpio < GPIO_COUNT; ++gpio) {
        if (edges[gpio] == 0u || pin_is_excluded(gpio)) continue;
        uint32_t score = button_score(edges[gpio]);
        if (score == 0u) continue;
        if (result_count == MAX_RESULTS &&
            score <= button_score(results[MAX_RESULTS - 1u].edges)) continue;

        uint32_t insert = result_count;
        if (insert >= MAX_RESULTS) insert = MAX_RESULTS - 1u;
        while (insert > 0u &&
               button_score(results[insert - 1u].edges) < score) {
            if (insert < MAX_RESULTS) results[insert] = results[insert - 1u];
            --insert;
        }
        if (insert < MAX_RESULTS) {
            results[insert].gpio = (uint8_t)gpio;
            results[insert].edges = edges[gpio];
            if (result_count < MAX_RESULTS) ++result_count;
        }
    }
    return result_count;
}
#endif

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

static void decimal2(char output[3], uint32_t value) {
    output[0] = (char)('0' + (value / 10u) % 10u);
    output[1] = (char)('0' + value % 10u);
    output[2] = '\0';
}

static void result_line(char output[13], const gpio_result_t *result) {
    char gpio[3];
    char edges[3];
    decimal2(gpio, result->gpio);
    decimal2(edges, result->edges);
    output[0] = 'G';
    output[1] = 'P';
    output[2] = 'I';
    output[3] = 'O';
    output[4] = ' ';
    output[5] = gpio[0];
    output[6] = gpio[1];
    output[7] = ' ';
    output[8] = 'E';
    output[9] = edges[0];
    output[10] = edges[1];
    output[11] = '\0';
}

static uint16_t page_pixel(uint32_t x, uint32_t y) {
    uint16_t background = rgb565(9u, 22u, 42u);
    uint16_t white = rgb565(248u, 250u, 252u);
    uint16_t blue = rgb565(9u, 105u, 218u);
    uint16_t green = rgb565(46u, 160u, 67u);
    uint16_t yellow = rgb565(210u, 153u, 34u);

    if (x < 6u || x >= BADGER_DISPLAY_WIDTH - 6u ||
        y < 6u || y >= BADGER_DISPLAY_HEIGHT - 6u) return blue;

    if (page == PAGE_LIVE &&
        text_pixel("LIVE GPIO EVENTS", x, y, 40u, 20u, 2u)) return white;
    if (page == PAGE_PROMPT) {
        if (text_pixel("NEXT 5 SECONDS", x, y, 52u, 30u, 2u)) return white;
        if (text_pixel("PRESS", x, y, 112u, 75u, 2u)) return yellow;
        if (text_pixel(page_control, x, y, 92u, 115u, 4u)) return white;
        if (text_pixel("PRESS AND RELEASE", x, y, 52u, 170u, 2u)) return green;
    }
    if (page == PAGE_RESULT) {
        if (text_pixel(page_control, x, y, 25u, 20u, 2u)) return yellow;
        if (text_pixel("OBSERVED", x, y, 150u, 20u, 2u)) return white;
    }
    if (page == PAGE_SUMMARY &&
        text_pixel(
#ifdef BUTTON_MAPPER_LOWER
            "LOWER SUMMARY",
#else
            "GUIDED SUMMARY",
#endif
            x, y, 58u, 16u, 2u)) return white;
    if (page == PAGE_REALTIME) {
        if (text_pixel("REALTIME BUTTONS", x, y, 40u, 18u, 2u)) return white;
        if (!live_has_event &&
            text_pixel("READY", x, y, 104u, 90u, 4u)) return green;
        if (live_has_event) {
            char gpio[3];
            char count[3];
            decimal2(gpio, live_gpio);
            decimal2(count, live_event_count);
            if (text_pixel(live_control, x, y, 75u, 65u, 4u)) return white;
            const char *action = live_action == 3u ? "TAP" :
                (live_action == 1u ? "PRESSED" : "RELEASED");
            if (text_pixel(action,
                           x, y, 65u, 112u, 3u)) return green;
            if (text_pixel("GPIO", x, y, 80u, 164u, 2u)) return white;
            if (text_pixel(gpio, x, y, 165u, 164u, 2u)) return yellow;
            if (text_pixel("EVENT", x, y, 80u, 195u, 2u)) return white;
            if (text_pixel(count, x, y, 185u, 195u, 2u)) return yellow;
        }
    }

    if (page == PAGE_LIVE || page == PAGE_RESULT) {
        for (uint32_t row = 0; row < page_result_count; ++row) {
            char line[13];
            result_line(line, &page_results[row]);
            if (text_pixel(line, x, y, 75u, 55u + row * 21u, 2u)) {
                return row == 0u ? green : white;
            }
        }
        if (page_result_count == 0u &&
            text_pixel("NO EVENTS", x, y, 83u, 100u, 3u)) return white;
    }

    if (page == PAGE_SUMMARY) {
        for (uint32_t row = 0; row < CONTROL_COUNT; ++row) {
            char gpio[3];
            decimal2(gpio, guided_results[row].gpio);
            if (text_pixel(control_names[row], x, y, 25u,
                           48u + row * 23u, 2u)) return white;
            if (guided_results[row].edges != 0u &&
                text_pixel("GPIO", x, y, 165u,
                           48u + row * 23u, 2u)) return green;
            if (guided_results[row].edges != 0u &&
                text_pixel(gpio, x, y, 245u,
                           48u + row * 23u, 2u)) return green;
            if (guided_results[row].edges == 0u &&
                text_pixel("NONE", x, y, 185u,
                           48u + row * 23u, 2u)) return yellow;
        }
    }
    return background;
}

static void show_page(void) {
    badger_display_begin();
    for (uint32_t x = 0; x < BADGER_DISPLAY_WIDTH; ++x) {
        for (uint32_t y = 0; y < BADGER_DISPLAY_HEIGHT; ++y) {
            badger_display_write(page_pixel(x, y));
        }
    }
    badger_display_end();
}

void bare_main(void) {
    badger_display_init();

#ifdef BUTTON_MAPPER_LIVE
    static const uint8_t live_pins[] = {7u, 9u, 10u, 11u, 6u};
    configure_front_button_pads();
    for (uint32_t index = 0; index < sizeof(live_pins); ++index) {
        clear_gpio_edges(live_pins[index]);
    }
    page = PAGE_REALTIME;
    live_has_event = 0;
    show_page();

    while (1) {
        for (uint32_t control = 0; control < CONTROL_COUNT; ++control) {
            uint32_t gpio = live_pins[control];
            uint32_t edges = gpio_edges(gpio);
            if (edges == 0u) continue;

            GPIO_INTR(gpio) = edges;
            wait_us(DEBOUNCE_US);
            live_control = control_names[control];
            live_gpio = gpio;
            uint32_t shift = 4u * (gpio % 8u);
            live_action = (edges >> shift) & 0xcu;
            uint32_t edge_count =
                ((live_action & 0x4u) != 0u) +
                ((live_action & 0x8u) != 0u);
            live_action >>= 2u;
            if (live_event_count + edge_count < 100u) {
                live_event_count += edge_count;
            } else {
                live_event_count = 99u;
            }
            live_has_event = 1;
            show_page();
        }
    }
#else
    uint16_t edges[GPIO_COUNT];
    configure_candidate_pads(1);
    wait_us(SETTLE_US);
    for (uint32_t control = 0; control < CONTROL_COUNT; ++control) {
        page = PAGE_PROMPT;
        page_control = control_names[control];
        show_page();
        wait_us(SETTLE_US);
        collect_events(SAMPLE_US, edges);
        page_result_count = rank_results(edges, page_results);

        guided_results[control].gpio =
            page_result_count == 0u ? 0u : page_results[0].gpio;
        guided_results[control].edges =
            page_result_count == 0u ? 0u : page_results[0].edges;

        page = PAGE_RESULT;
        show_page();
        wait_us(RESULT_US);
    }

    page = PAGE_SUMMARY;
    show_page();
    while (1) {
        __asm__ volatile ("wfi");
    }
#endif
}
