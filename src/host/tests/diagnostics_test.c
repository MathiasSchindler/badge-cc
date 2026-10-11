#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DIAGNOSTICS_HOST_TEST
#include "../../tools/diagnostics.c"

uint8_t __flash_binary_end;
static uint32_t mock_now;
static uint8_t mock_raw;
static uint8_t mock_leds;
static uint8_t mock_levels[48];
static uint8_t mock_edges[48];
static uint8_t mock_i2c_sda;
static uint8_t mock_i2c_stuck;
static uint8_t mock_imu_fail;
static uint8_t mock_adc_fail;
static uint8_t mock_touch_fail;
static uint8_t mock_touch_wait;
static uint8_t mock_touch_noise;
static uint32_t display_pixels;
static uint32_t region_pixels;
static uint32_t region_expected;
static uint32_t sent_trains;

uint32_t diag_time_us(void) { return mock_now; }
void diag_wait_us(uint32_t duration) { mock_now += duration; }
void diag_gpio_init(void) {}
uint8_t diag_gpio_level(uint32_t pin) { assert(pin < 48u); return mock_levels[pin]; }
uint8_t diag_gpio_take_edges(uint32_t pin) {
    assert(pin < 48u);
    uint8_t value = mock_edges[pin];
    mock_edges[pin] = 0u;
    return value;
}
uint8_t diag_buttons_raw(void) { return mock_raw; }
void diag_leds(uint8_t mask) { mock_leds = mask; }
void diag_backlight(uint8_t on) { (void)on; }
diag_hw_result_t diag_adc_init(void) { return mock_adc_fail ? DIAG_HW_TIMEOUT : DIAG_HW_OK; }
diag_hw_result_t diag_adc_read(uint8_t channel, uint16_t *value) {
    assert(channel == 0u || channel == 2u || channel == 3u);
    *value = channel == 0u ? 2500u : channel == 2u ? 1365u : 2048u;
    return mock_adc_fail ? DIAG_HW_TIMEOUT : DIAG_HW_OK;
}
diag_hw_result_t diag_ir_init(uint32_t *hz) { *hz = 38000u; return DIAG_HW_OK; }
diag_hw_result_t diag_ir_send(void) { ++sent_trains; return DIAG_HW_OK; }
void diag_ir_capture(diag_ir_capture_t *value) {
    *value = (diag_ir_capture_t){24u, 12u, 550u, 620u};
}
const char *diag_hw_result_name(diag_hw_result_t result) {
    return result == DIAG_HW_OK ? "OK" : "HARDWARE TIMEOUT";
}
void badger_display_init(void) {}
void badger_display_begin(void) {}
void badger_display_begin_region(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    assert(width != 0u && height != 0u);
    assert(x + width <= 320u && y + height <= 240u);
    region_pixels = 0u;
    region_expected = width * height;
}
void badger_display_write(uint16_t pixel) {
    (void)pixel;
    ++display_pixels;
    ++region_pixels;
}
void badger_display_end_update(void) { assert(region_pixels == region_expected); }
void badger_display_end(void) {}
void badger_display_set_tearing(int enabled) { (void)enabled; }
badger_i2c_result_t badger_i2c_init_pins(uint32_t sda, uint32_t scl) {
    assert((sda == 4u && scl == 5u) || (sda == 18u && scl == 19u));
    mock_i2c_sda = (uint8_t)sda;
    return sda == 4u && mock_i2c_stuck ? BADGER_I2C_LINES_NOT_HIGH : BADGER_I2C_OK;
}
int badger_i2c_probe_read(uint8_t address, uint8_t *value) {
    assert(mock_i2c_sda == 4u);
    assert(address >= 8u && address <= 0x77u);
    *value = 0u;
    return address == 0x51u;
}
lsm6ds3_result_t lsm6ds3_init(uint8_t *identity) {
    *identity = 0x6au;
    return LSM6DS3_OK;
}
int lsm6ds3_read(lsm6ds3_sample_t *value) {
    *value = (lsm6ds3_sample_t){-1500, 3000, -16000, -32768, 1234, 32767};
    return !mock_imu_fail;
}
cap1208_result_t cap1208_init(uint8_t *product, uint8_t *revision) {
    *product = 0x6bu;
    *revision = 1u;
    return mock_touch_fail ? CAP1208_I2C_ERROR : CAP1208_OK;
}
uint8_t cap1208_get_address(void) { return 0x28u; }
cap1208_result_t cap1208_read(cap1208_sample_t *value) {
    *value = (cap1208_sample_t){0};
    value->touched = 0xa5u;
    value->noise = mock_touch_noise;
    for (uint32_t i = 0u; i < 8u; ++i) value->delta[i] = (int8_t)((int32_t)i * 30 - 100);
    return mock_touch_fail ? CAP1208_I2C_ERROR :
           mock_touch_wait ? CAP1208_NOT_READY : CAP1208_OK;
}
const char *cap1208_result_name(cap1208_result_t result) {
    return result == CAP1208_OK ? "OK" : "I2C ERROR";
}

static uint32_t crc_reference(uint32_t crc, const uint8_t *bytes, uint32_t size) {
    for (uint32_t i = 0u; i < size; ++i) {
        crc ^= bytes[i];
        for (uint32_t bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return crc;
}

static void test_crc(void) {
    static uint8_t bytes[65540];
    const uint32_t sizes[] = {0u, 1u, 3u, 4u, 255u, 256u, 257u, 511u, 512u, 4096u, 65540u};
    const uint32_t chunks[] = {1u, 3u, 256u, 4096u};
    uint32_t random = 0x12345678u;
    for (uint32_t i = 0u; i < sizeof(bytes); ++i) {
        random = random * 1664525u + 1013904223u;
        bytes[i] = i < 256u ? (uint8_t)i : (uint8_t)(random >> 24u);
    }
    assert(diag_crc32_update(0x12345678u, 0, 0u) == 0x12345678u);
    for (uint32_t k = 0u; k < sizeof(sizes) / sizeof(sizes[0]); ++k) {
        uint32_t size = sizes[k], expected = ~crc_reference(0xffffffffu, bytes, size);
        assert(diag_crc32(bytes, size) == expected);
        assert(diag_crc32_update(0x12345678u, bytes, size) == crc_reference(0x12345678u, bytes, size));
        for (uint32_t j = 0u; j < sizeof(chunks) / sizeof(chunks[0]); ++j) {
            uint32_t crc = 0xffffffffu;
            for (uint32_t offset = 0u; offset < size;) {
                uint32_t length = size - offset;
                if (length > chunks[j]) length = chunks[j];
                crc = diag_crc32_update(crc, bytes + offset, length);
                offset += length;
            }
            assert(~crc == expected);
        }
    }
    for (uint32_t split = 0u; split <= 257u; ++split) {
        uint32_t crc = diag_crc32_update(0xffffffffu, bytes, split);
        crc = diag_crc32_update(crc, bytes + split, 257u - split);
        assert(~crc == diag_crc32(bytes, 257u));
    }
}

static void test_logic(void) {
    test_crc();
    char decimal[12], hexadecimal[9];
    diag_decimal(decimal, INT32_MIN); assert(strcmp(decimal, "-2147483648") == 0);
    diag_decimal(decimal, INT32_MAX); assert(strcmp(decimal, "2147483647") == 0);
    diag_decimal(decimal, 0); assert(strcmp(decimal, "0") == 0);
    diag_hex(hexadecimal, 0x89abcdefu, 8u); assert(strcmp(hexadecimal, "89ABCDEF") == 0);
    diag_hex(hexadecimal, 0x1au, 2u); assert(strcmp(hexadecimal, "1A") == 0);
    assert(diag_adc_mv(0u, 2u) == 0u);
    assert(diag_adc_mv(2048u, 1u) == 1650u);
    assert(diag_adc_mv(2048u, 2u) == 3300u);
    assert(diag_adc_mv(4095u, 2u) == 6598u);
    assert(diag_crc32((const uint8_t *)"123456789", 9u) == 0xcbf43926u);
    assert(diag_crc32((const uint8_t *)"", 0u) == 0u);
    assert(diag_sqrt(0u) == 0u);
    assert(diag_sqrt(999999u) == 999u);
    assert(diag_sqrt(1000000u) == 1000u);
    assert(diag_sqrt(UINT32_MAX) == 65535u);

    diag_buttons_t state = {0};
    assert(diag_buttons_update(&state, DIAG_ENTER, 100u) == 0u);
    assert(diag_buttons_update(&state, 0u, 110u) == 0u);
    assert(diag_buttons_update(&state, DIAG_ENTER, 120u) == 0u);
    assert(diag_buttons_update(&state, DIAG_ENTER, 20119u) == 0u);
    assert(diag_buttons_update(&state, DIAG_ENTER, 20120u) == DIAG_ENTER);
    assert(diag_buttons_update(&state, DIAG_ENTER, 99999u) == 0u);
    assert(state.presses[2] == 1u);
    assert(state.seen == DIAG_ENTER);
    state = (diag_buttons_t){0};
    assert(diag_buttons_update(&state, 0x3fu, UINT32_MAX - 9999u) == 0u);
    assert(diag_buttons_update(&state, 0x3fu, 10000u) == 0x3fu);
    assert(state.stable == 0x3fu);
    for (uint32_t i = 0; i < 6u; ++i) assert(state.presses[i] == 1u);
    state = (diag_buttons_t){0};
    state.presses[0] = 65535u;
    diag_buttons_update(&state, DIAG_LEFT, 0u);
    diag_buttons_update(&state, DIAG_LEFT, 20000u);
    assert(state.presses[0] == 65535u);
}

static void test_buttons_browsing(void) {
    change_page(PAGE_OVERVIEW);
    for (uint32_t i = 0u; i < PAGE_COUNT; ++i) {
        actions(DIAG_RIGHT, i);
        assert(page == (diagnostic_page_t)((i + 1u) % PAGE_COUNT));
    }
    for (uint32_t i = 0u; i < PAGE_COUNT; ++i) {
        actions(DIAG_LEFT, i);
        assert(page == (diagnostic_page_t)((PAGE_COUNT - 1u - i) % PAGE_COUNT));
    }
    change_page(PAGE_BUTTONS);
    actions(DIAG_HOME, 0u);
    assert(page == PAGE_OVERVIEW);
}

static void button_input(uint8_t raw, uint32_t now) {
    uint8_t pressed = diag_buttons_update(&buttons, raw, now);
    actions(pressed, now);
}

static void test_button_test_mode(void) {
    buttons = (diag_buttons_t){0};
    suppress_navigation = 0u;
    change_page(PAGE_BUTTONS);
    assert(!button_test_active);
    button_input(DIAG_ENTER, 0u);
    button_input(DIAG_ENTER, 20000u);
    assert(button_test_active && suppress_navigation);
    assert(buttons.seen == 0u && buttons.presses[2] == 0u);
    button_input(DIAG_ENTER, 100000u);
    assert(button_test_active && suppress_navigation);
    button_input(0u, 110000u);
    button_input(0u, 130000u);
    assert(!suppress_navigation);

    for (uint32_t i = 0u; i < DIAG_BUTTON_COUNT; ++i) {
        uint32_t now = 200000u + i * 100000u;
        uint8_t bit = (uint8_t)(1u << i);
        button_input(bit, now);
        button_input(bit, now + 20000u);
        assert(page == PAGE_BUTTONS && button_test_active);
        assert(buttons.presses[i] == 1u && (buttons.seen & bit) != 0u);
        button_input(0u, now + 40000u);
        button_input(0u, now + 60000u);
    }
    assert(buttons.seen == 0x3fu);

    uint32_t start = 1000000u;
    button_input(DIAG_HOME, start);
    button_input(DIAG_HOME, start + 20000u);
    button_input(DIAG_HOME, start + 1019999u);
    assert(button_test_active);
    button_input(DIAG_HOME, start + 1020000u);
    assert(page == PAGE_BUTTONS && !button_test_active && suppress_navigation);
    assert(buttons.seen == 0x3fu);
    button_input(DIAG_HOME | DIAG_RIGHT, start + 1030000u);
    button_input(DIAG_HOME | DIAG_RIGHT, start + 1050000u);
    assert(page == PAGE_BUTTONS && suppress_navigation);
    button_input(0u, start + 1070000u);
    button_input(0u, start + 1090000u);
    assert(!suppress_navigation);
    button_input(DIAG_RIGHT, start + 1110000u);
    button_input(DIAG_RIGHT, start + 1130000u);
    assert(page == PAGE_TOUCH);
    assert(buttons.seen == 0x3fu);
    button_input(0u, start + 1150000u);
    button_input(0u, start + 1170000u);
    button_input(DIAG_LEFT, start + 1190000u);
    button_input(DIAG_LEFT, start + 1210000u);
    assert(page == PAGE_BUTTONS && !button_test_active);
    assert(buttons.seen == 0x3fu);

    buttons = (diag_buttons_t){0};
    button_input(DIAG_ENTER, 0u);
    button_input(DIAG_ENTER, 20000u);
    button_input(0u, 40000u);
    button_input(0u, 60000u);
    assert(button_test_active && !suppress_navigation && buttons.seen == 0u);
    uint8_t chord = DIAG_LEFT | DIAG_RIGHT;
    button_input(chord, 100000u);
    button_input(chord, 120000u);
    button_input(chord, 700000u);
    button_input(0u, 710000u);
    button_input(0u, 730000u);
    assert(button_test_active && !exit_holding);

    start = UINT32_MAX - 500000u;
    button_input(chord, start);
    button_input(chord, start + 20000u);
    button_input(chord, start + 1019999u);
    assert(button_test_active);
    button_input(chord, start + 1020000u);
    assert(page == PAGE_BUTTONS && !button_test_active && suppress_navigation);
    button_input(0u, start + 1040000u);
    button_input(0u, start + 1060000u);
    button_input(DIAG_LEFT, start + 1080000u);
    button_input(DIAG_LEFT, start + 1100000u);
    assert(page == PAGE_OVERVIEW && !button_test_active);
    buttons = (diag_buttons_t){0};
}

static void test_application(void) {
    for (uint32_t i = 0; i < 48u; ++i) mock_levels[i] = 1u;
    diagnostics_init();
    assert(display_pixels >= 76800u);
    assert(touch_mask == 0xa5u);
    assert(imu_result == LSM6DS3_OK && adc_result == DIAG_HW_OK);
    mock_touch_wait = 1u;
    sample_sensors();
    assert(touch_result == CAP1208_NOT_READY && touch_mask == 0u);
    mock_touch_wait = 0u;
    sample_sensors();
    assert(touch_result == CAP1208_OK && touch_mask == 0xa5u);
    mock_touch_noise = 1u;
    sample_sensors();
    assert(touch_mask == 0xa4u && touch_noise == 1u);
    mock_touch_noise = 0u;
    sample_sensors();
    diagnostics_render();
    flush_frame();
    display_pixels = 0u;
    diagnostics_render();
    flush_frame();
    assert(display_pixels == 0u);

    change_page(PAGE_LEDS);
    actions(DIAG_ENTER, 0u);
    assert(mock_leds == 1u);
    change_page(PAGE_OVERVIEW);
    assert(mock_leds == 0u);
    change_page(PAGE_LEDS);
    actions(DIAG_UP, 100u);
    mock_now = 10000100u;
    diagnostics_tick();
    assert(mock_leds == 0u && !led_chase);

    mock_i2c_stuck = 1u;
    start_scan();
    assert(scan_state == 3u && mock_i2c_sda == 18u);
    mock_i2c_stuck = 0u;
    start_scan();
    assert(scan_state == 1u);
    mock_touch_fail = 1u;
    sample_sensors();
    assert(touch_result == CAP1208_OK);
    while (scan_state == 1u) scan_step();
    assert(scan_state == 2u && scan_count == 1u && mock_i2c_sda == 18u);
    assert(scan_found[0x51u / 8u] & (1u << (0x51u % 8u)));
    start_scan();
    change_page(PAGE_OVERVIEW);
    assert(scan_state == 3u && mock_i2c_sda == 18u);

    mock_imu_fail = 1u;
    mock_adc_fail = 1u;
    sample_sensors();
    assert(imu_result == LSM6DS3_I2C_ERROR);
    assert(touch_result == CAP1208_I2C_ERROR && touch_mask == 0u);
    assert(adc_result == DIAG_HW_TIMEOUT);
    mock_imu_fail = mock_adc_fail = mock_touch_fail = 0u;
    sample_sensors();
    assert(imu_result == LSM6DS3_I2C_ERROR && touch_result == CAP1208_I2C_ERROR);
    actions(DIAG_ENTER, mock_now);
    assert(imu_result == LSM6DS3_OK && touch_result == CAP1208_OK);

    change_page(PAGE_IR);
    actions(DIAG_ENTER, mock_now);
    assert(sent_trains == 1u && ir_transmissions == 1u);
    ir_result = DIAG_HW_CLOCK_ERROR;
    actions(DIAG_ENTER, mock_now);
    assert(sent_trains == 1u);
    actions(DIAG_UP, mock_now);
    assert(ir_captured && ir_capture.low_pulses == 12u);
    memory_check();
    assert(memory_tested && memory_ok);
    assert(!flash_ok);
    initialize_sensors();
    sample_sensors();

    for (uint32_t p = 0; p < PAGE_COUNT; ++p) {
        page = (diagnostic_page_t)p;
        diagnostics_render();
        flush_frame();
    }
    page = PAGE_DISPLAY;
    display_pattern = 0u;
    diagnostics_render();
    assert(canvas.pixels[10u * 240u + 47u] == 0xf800u);
    assert(canvas.pixels[60u * 240u + 47u] == 0x07e0u);
    assert(canvas.pixels[110u * 240u + 47u] == 0x001fu);
    display_pattern = 4u;
    diagnostics_render();
    assert(canvas.pixels[10u * 240u + 47u] == 0u);
    assert(canvas.pixels[309u * 240u + 47u] == 0xffffu);
    display_pattern = 5u;
    diagnostics_render();
    assert(canvas.pixels[10u * 240u + 47u] != canvas.pixels[11u * 240u + 47u]);
    assert(canvas.pixels[10u * 240u + 47u] != canvas.pixels[10u * 240u + 48u]);
    display_pattern = 0u;
    badger_gfx_rect(&canvas, -30, -30, 40, 40, WHITE);
    badger_gfx_rect(&canvas, 318, 238, 30, 30, WHITE);
    badger_gfx_text(&canvas, 310, 233, "CLIPPED TEXT", 4u, WHITE);
}

static void test_touch_positions(void) {
    static badger_canvas_t expected;
    static const char *const positions[] = {
        "UP", "RIGHT", "DOWN", "LEFT",
        "SLASH RIGHT", "SLASH LEFT", "ROUND RIGHT", "ROUND LEFT"
    };
    page = PAGE_TOUCH;
    bus_result = BADGER_I2C_OK;
    touch_result = CAP1208_OK;
    touch_noise = 0u;
    for (uint32_t channel = 0u; channel < CAP1208_CHANNELS; ++channel) {
        touch_mask = (uint8_t)(1u << channel);
        touch_seen = touch_mask;
        diagnostics_render();
        for (uint32_t tile = 0u; tile < CAP1208_CHANNELS; ++tile) {
            assert(strcmp(touch_positions[tile], positions[tile]) == 0);
            assert(strlen(positions[tile]) * 6u <= 66u);
            int32_t x = 8 + (int32_t)(tile % 4u) * 78;
            int32_t y = 44 + (int32_t)(tile / 4u) * 64;
            uint16_t background = tile == channel ? GREEN : PANEL;
            assert(canvas.pixels[x * 240 + y] == background);
            badger_gfx_rect(&expected, x, y, 72, 56, background);
            badger_gfx_text(&expected, x + 6, y + 21, positions[tile],
                            1u, tile == channel ? BG : WHITE);
            for (int32_t dx = 0; dx < 72; ++dx) {
                for (int32_t dy = 21; dy < 28; ++dy) {
                    uint32_t index = (uint32_t)((x + dx) * 240 + y + dy);
                    assert(canvas.pixels[index] == expected.pixels[index]);
                }
            }
        }
    }
    sample_sensors();
}

static void previews(const char *prefix) {
    for (uint32_t p = 0; p < PAGE_COUNT; ++p) {
        char path[1024];
        int count = snprintf(path, sizeof(path), "%s-%u.ppm", prefix, p);
        assert(count > 0 && (size_t)count < sizeof(path));
        FILE *file = fopen(path, "wb");
        assert(file != NULL);
        page = (diagnostic_page_t)p;
        diagnostics_render();
        assert(fprintf(file, "P6\n320 240\n255\n") > 0);
        for (uint32_t y = 0; y < 240u; ++y) {
            for (uint32_t x = 0; x < 320u; ++x) {
                uint16_t pixel = canvas.pixels[x * 240u + y];
                unsigned char rgb[] = {
                    (unsigned char)(((pixel >> 11u) & 31u) * 255u / 31u),
                    (unsigned char)(((pixel >> 5u) & 63u) * 255u / 63u),
                    (unsigned char)((pixel & 31u) * 255u / 31u)
                };
                assert(fwrite(rgb, 1u, 3u, file) == 3u);
            }
        }
        assert(fclose(file) == 0);
    }
}

int main(int argc, char **argv) {
    test_logic();
    test_buttons_browsing();
    test_application();
    test_button_test_mode();
    test_touch_positions();
    if (argc == 2) previews(argv[1]);
    puts("diagnostics: logic, navigation, errors, bus restore, memory and rendering PASS");
    return 0;
}
