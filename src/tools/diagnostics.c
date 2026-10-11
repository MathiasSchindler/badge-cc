#include "display.h"
#include "graphics.h"
#include "diagnostic_hw.h"
#include "diagnostics_logic.h"
#include "i2c_bitbang.h"
#include "lsm6ds3.h"
#include "cap1208.h"

#define BG 0x0863u
#define PANEL 0x1946u
#define WHITE 0xffffu
#define MUTED 0x9cf3u
#define GREEN 0x5ecbu
#define AMBER 0xfda8u
#define RED 0xf9c7u
#define BLUE 0x5d9fu

typedef enum {
    PAGE_OVERVIEW, PAGE_BUTTONS, PAGE_TOUCH, PAGE_MOTION, PAGE_POWER,
    PAGE_IR, PAGE_LEDS, PAGE_DISPLAY, PAGE_I2C, PAGE_SYSTEM, PAGE_LIMITS,
    PAGE_COUNT
} diagnostic_page_t;

static const char *const titles[PAGE_COUNT] = {
    "BADGE DIAGNOSTICS", "BUTTONS", "CAPACITIVE TOUCH", "MOTION SENSOR",
    "POWER AND LIGHT", "INFRARED", "CASE LIGHTS", "DISPLAY",
    "EXTERNAL I2C", "SYSTEM MEMORY", "TEST COVERAGE"
};
static const char *const button_names[DIAG_BUTTON_COUNT] = {
    "LEFT  7", "RIGHT 9", "ENTER 10", "UP    11", "DOWN  6", "HOME  22"
};
static const char *const touch_positions[CAP1208_CHANNELS] = {
    "UP", "RIGHT", "DOWN", "LEFT",
    "SLASH RIGHT", "SLASH LEFT", "ROUND RIGHT", "ROUND LEFT"
};

static badger_canvas_t canvas;
static badger_canvas_t previous_canvas;
static uint8_t have_frame;
static diagnostic_page_t page;
static diag_buttons_t buttons;
static uint8_t button_test_active;
static uint8_t suppress_navigation;
static uint8_t exit_holding;
static uint32_t exit_started;
static uint32_t last_sample;
static uint32_t last_frame;
static uint32_t ir_edges;
static uint32_t switch_edges;
static uint32_t te_edges;
static uint32_t ir_last_activity;
static uint32_t ir_carrier;
static uint32_t ir_transmissions;
static diag_ir_capture_t ir_capture;
static uint8_t ir_captured;
static uint8_t ir_busy;
static uint8_t display_pattern;
static uint8_t te_enabled;
static uint8_t led_mask;
static uint8_t led_step;
static uint8_t led_chase;
static uint32_t chase_started;
static uint8_t scan_state;
static uint8_t scan_address;
static uint8_t scan_found[16];
static uint32_t scan_count;
static const char *scan_error;
static uint8_t memory_tested;
static uint8_t memory_ok;
static uint32_t memory_bad_index;
static uint32_t flash_crc;
static uint32_t flash_bytes;
static uint8_t flash_ok;
static volatile uint32_t ram_test[2048];
static lsm6ds3_sample_t imu;
static uint8_t imu_identity;
static lsm6ds3_result_t imu_result;
static cap1208_result_t touch_result;
static uint8_t touch_product;
static uint8_t touch_revision;
static uint8_t touch_mask;
static uint8_t touch_seen;
static uint8_t touch_noise;
static uint8_t touch_status;
static uint8_t touch_calibrating;
static uint8_t touch_base_error;
static uint32_t touch_wait_started;
static int16_t touch_delta[8];
static uint16_t adc_values[3];
static diag_hw_result_t adc_result;
static diag_hw_result_t ir_result;
static badger_i2c_result_t bus_result;

static void text(int32_t x, int32_t y, const char *value,
                  uint32_t scale, uint16_t color) {
    badger_gfx_text(&canvas, x, y, value, scale, color);
}

static void rect(int32_t x, int32_t y, int32_t width,
                  int32_t height, uint16_t color) {
    badger_gfx_rect(&canvas, x, y, width, height, color);
}

static void number(int32_t x, int32_t y, int32_t value,
                    uint32_t scale, uint16_t color) {
    char buffer[12];
    diag_decimal(buffer, value);
    text(x, y, buffer, scale, color);
}

static void hex(int32_t x, int32_t y, uint32_t value,
                 uint32_t digits, uint16_t color) {
    char buffer[9];
    diag_hex(buffer, value, digits);
    text(x, y, buffer, 1u, color);
}

static void row(uint32_t index, const char *name, const char *value,
                 uint16_t color) {
    int32_t y = 46 + (int32_t)index * 18;
    text(10, y, name, 1u, MUTED);
    text(130, y, value, 1u, color);
}

static void footer(const char *first, const char *second) {
    rect(0, 211, 320, 29, PANEL);
    text(8, 215, first, 1u, WHITE);
    text(8, 228, second, 1u, MUTED);
}

static const char *bus_error_name(void) {
    switch (bus_result) {
        case BADGER_I2C_OK: return "BUS OK";
        case BADGER_I2C_LINES_NOT_HIGH: return "BUS HELD LOW";
        case BADGER_I2C_CLOCK_STRETCH_TIMEOUT: return "CLOCK TIMEOUT";
        case BADGER_I2C_INVALID_PINS: return "INVALID BUS PINS";
    }
    return "UNKNOWN BUS ERROR";
}

static const char *imu_error_name(void) {
    if (bus_result != BADGER_I2C_OK) return bus_error_name();
    if (imu_result == LSM6DS3_ID_MISMATCH) return "WRONG WHO AM I";
    if (imu_result != LSM6DS3_OK) return "IMU I2C ERROR";
    return "LIVE SAMPLES";
}

static void count_edges(uint32_t pin, uint32_t *count) {
    uint8_t flags = diag_gpio_take_edges(pin);
    uint32_t amount = (flags & 1u) + ((flags >> 1u) & 1u);
    if (*count < 0x7ffffffdu) *count += amount;
    if (pin == 17u && flags != 0u) ir_last_activity = diag_time_us();
}

static void flush_frame(void) {
    for (uint32_t x = 0u; x < 320u; x += 16u) {
        for (uint32_t y = 0u; y < 240u; y += 16u) {
            uint8_t dirty = !have_frame;
            for (uint32_t dx = 0u; dx < 16u && !dirty; ++dx) {
                for (uint32_t dy = 0u; dy < 16u; ++dy) {
                    uint32_t index = (x + dx) * 240u + y + dy;
                    if (canvas.pixels[index] != previous_canvas.pixels[index]) {
                        dirty = 1u;
                        break;
                    }
                }
            }
            if (!dirty) continue;
            badger_display_begin_region(x, y, 16u, 16u);
            for (uint32_t dx = 0u; dx < 16u; ++dx) {
                for (uint32_t dy = 0u; dy < 16u; ++dy) {
                    uint32_t index = (x + dx) * 240u + y + dy;
                    uint16_t pixel = canvas.pixels[index];
                    badger_display_write(pixel);
                    previous_canvas.pixels[index] = pixel;
                }
            }
            badger_display_end_update();
            count_edges(17u, &ir_edges);
            count_edges(15u, &switch_edges);
            count_edges(21u, &te_edges);
        }
    }
    if (!have_frame) badger_display_end();
    have_frame = 1u;
}

static void initialize_sensors(void) {
    bus_result = badger_i2c_init_pins(18u, 19u);
    imu_identity = 0u;
    touch_product = 0u;
    touch_revision = 0u;
    touch_mask = 0u;
    touch_noise = 0u;
    touch_wait_started = diag_time_us();
    imu_result = LSM6DS3_I2C_ERROR;
    touch_result = CAP1208_I2C_ERROR;
    if (bus_result == BADGER_I2C_OK) {
        imu_result = lsm6ds3_init(&imu_identity);
        touch_result = cap1208_init(&touch_product, &touch_revision);
    }
    adc_result = diag_adc_init();
    ir_result = diag_ir_init(&ir_carrier);
}

static void sample_sensors(void) {
    if (scan_state == 1u) return;
    if (bus_result == BADGER_I2C_OK) {
        if (imu_result == LSM6DS3_OK && !lsm6ds3_read(&imu)) {
            imu_result = LSM6DS3_I2C_ERROR;
        }
        if (touch_result == CAP1208_OK || touch_result == CAP1208_NOT_READY) {
            cap1208_sample_t sample;
            touch_result = cap1208_read(&sample);
            touch_status = sample.general_status;
            touch_calibrating = sample.calibrating;
            touch_base_error = sample.base_count_out_of_limit;
            touch_noise = sample.noise;
            if (touch_result == CAP1208_OK) {
                touch_wait_started = diag_time_us();
                touch_mask = (uint8_t)(sample.touched & (uint8_t)~sample.noise);
                touch_seen |= touch_mask;
                for (uint32_t i = 0; i < 8u; ++i) touch_delta[i] = sample.delta[i];
            } else {
                touch_mask = 0u;
            }
        }
    }
    if (adc_result == DIAG_HW_OK) {
        const uint8_t channels[] = {0u, 2u, 3u};
        for (uint32_t i = 0; i < 3u; ++i) {
            adc_result = diag_adc_read(channels[i], &adc_values[i]);
            if (adc_result != DIAG_HW_OK) break;
        }
    }
}

static void render_overview(void) {
    row(0u, "INTERNAL I2C", bus_error_name(),
        bus_result == BADGER_I2C_OK ? GREEN : RED);
    row(1u, "IMU 6A", imu_error_name(), imu_result == LSM6DS3_OK ? GREEN : RED);
    row(2u, "CAP1208", bus_result == BADGER_I2C_OK ?
        cap1208_result_name(touch_result) : bus_error_name(),
        touch_result == CAP1208_OK ? GREEN :
        touch_result == CAP1208_NOT_READY &&
        diag_time_us() - touch_wait_started < 2000000u ? AMBER : RED);
    row(3u, "ADC", diag_hw_result_name(adc_result),
        adc_result == DIAG_HW_OK ? GREEN : RED);
    row(4u, "IR CARRIER", diag_hw_result_name(ir_result),
        ir_result == DIAG_HW_OK ? BLUE : RED);
    row(5u, "BUTTONS", "OPEN LIVE TEST", BLUE);
    row(6u, "LED / DISPLAY", "VISUAL CHECK REQUIRED", AMBER);
    row(7u, "RADIO / PSRAM", "NOT EXERCISED", AMBER);
    text(10, 194, "GREEN IS A LIVE READ NOT A FULL BOARD PASS", 1u, MUTED);
    footer("LEFT RIGHT PAGES / ENTER RETRY SENSORS", "HOME OVERVIEW / NO SDK OR LIBC");
}

static void render_buttons(void) {
    for (uint32_t i = 0u; i < DIAG_BUTTON_COUNT; ++i) {
        int32_t y = 44 + (int32_t)i * 22;
        uint8_t pressed = buttons.stable & (1u << i);
        text(10, y + 3, button_names[i], 1u, WHITE);
        rect(80, y, 96, 17, pressed ? GREEN : PANEL);
        text(84, y + 5, pressed ? "PRESSED" : "RELEASED", 1u, pressed ? BG : MUTED);
        number(188, y + 4, buttons.presses[i], 1u, WHITE);
        text(240, y + 4, buttons.seen & (1u << i) ? "SEEN" : "WAIT", 1u,
             buttons.seen & (1u << i) ? GREEN : AMBER);
    }
    text(10, 184, "COMBINED GPIO15", 1u, MUTED);
    number(118, 184, diag_gpio_level(15u), 1u, WHITE);
    text(150, 184, "EDGE FLAGS", 1u, MUTED);
    number(220, 184, (int32_t)switch_edges, 1u, WHITE);
    if (button_test_active) {
        text(10, 197, "BUTTON TEST ACTIVE / NAVIGATION PAUSED", 1u, AMBER);
        footer("ALL SIX BUTTONS LIVE / RESET REBOOTS",
               "HOLD LEFT+RIGHT OR HOME 1 SECOND TO END");
    } else {
        text(10, 197, suppress_navigation ?
             "RELEASE BUTTONS TO RESUME NAVIGATION" :
             "BROWSE MODE / ENTER STARTS BUTTON TEST", 1u, MUTED);
        footer("LEFT RIGHT PAGES / ENTER START TEST", "HOME OVERVIEW / RESET REBOOTS");
    }
}

static void render_touch(void) {
    if (touch_result != CAP1208_OK || bus_result != BADGER_I2C_OK) {
        text(10, 54, bus_result != BADGER_I2C_OK ?
             bus_error_name() : cap1208_result_name(touch_result), 1u,
             touch_result == CAP1208_NOT_READY &&
             diag_time_us() - touch_wait_started < 2000000u ? AMBER : RED);
        text(10, 78, touch_result == CAP1208_NOT_READY ?
             "WAITING FOR STABLE SAMPLE / RETRYING" :
             "CHECK SENSOR AND BUS / ENTER REINITIALIZE", 1u, MUTED);
        if (touch_result == CAP1208_NOT_READY || touch_result == CAP1208_SENSOR_FAULT) {
            text(10, 103, "STATUS  CALIBRATING  BASE FAULT", 1u, MUTED);
            hex(10, 122, touch_status, 2u, WHITE);
            hex(58, 122, touch_calibrating, 2u, WHITE);
            hex(130, 122, touch_base_error, 2u, WHITE);
        }
    } else {
        for (uint32_t i = 0u; i < 8u; ++i) {
            int32_t x = 8 + (int32_t)(i % 4u) * 78;
            int32_t y = 44 + (int32_t)(i / 4u) * 64;
            uint8_t active = touch_mask & (1u << i);
            uint8_t noisy = touch_noise & (1u << i);
            rect(x, y, 72, 56, noisy ? AMBER : active ? GREEN : PANEL);
            text(x + 6, y + 4, "CS", 2u, noisy || active ? BG : WHITE);
            number(x + 30, y + 4, (int32_t)i + 1, 2u, noisy || active ? BG : WHITE);
            text(x + 6, y + 21, touch_positions[i], 1u, noisy || active ? BG : WHITE);
            if (noisy) text(x + 6, y + 32, "NOISE", 1u, BG);
            else number(x + 6, y + 32, touch_delta[i], 1u, active ? BG : MUTED);
            text(x + 6, y + 44, touch_seen & (1u << i) ? "SEEN" : "WAIT", 1u,
                 noisy || active ? BG : (touch_seen & (1u << i) ? GREEN : AMBER));
        }
    }
    text(10, 179, "ADDR", 1u, MUTED);
    hex(40, 179, cap1208_get_address(), 2u, WHITE);
    text(64, 179, "ID", 1u, MUTED);
    hex(82, 179, touch_product, 2u, WHITE);
    text(106, 179, "REV", 1u, MUTED);
    hex(130, 179, touch_revision, 2u, WHITE);
    text(158, 179, "ALERT GPIO13", 1u, MUTED);
    number(248, 179, diag_gpio_level(13u), 1u, WHITE);
    text(10, 196, "POSITIONS AS VIEWED FROM FRONT OF BADGE", 1u, MUTED);
    footer("LEFT RIGHT PAGES / ENTER RETRY", "TOUCH EACH PAD / UP CLEAR SEEN HISTORY");
}

static void render_motion(void) {
    if (imu_result != LSM6DS3_OK || bus_result != BADGER_I2C_OK) {
        text(10, 52, imu_error_name(), 2u, RED);
        text(10, 82, "WHO AM I", 1u, MUTED);
        hex(82, 82, imu_identity, 2u, WHITE);
    } else {
        int32_t accel[] = {
            (int32_t)imu.accel_x * 61 / 1000,
            (int32_t)imu.accel_y * 61 / 1000,
            (int32_t)imu.accel_z * 61 / 1000
        };
        const char *const axes[] = {"X", "Y", "Z"};
        const uint16_t colors[] = {RED, GREEN, BLUE};
        for (uint32_t i = 0u; i < 3u; ++i) {
            int32_t y = 46 + (int32_t)i * 32;
            text(10, y, axes[i], 2u, colors[i]);
            number(30, y, accel[i], 2u, WHITE);
            text(104, y + 5, "MG", 1u, MUTED);
            rect(144, y + 2, 160, 12, PANEL);
            int32_t amount = accel[i] * 80 / 2000;
            if (amount > 80) amount = 80;
            if (amount < -80) amount = -80;
            rect(amount < 0 ? 224 + amount : 224, y + 2,
                 amount < 0 ? -amount : amount, 12, colors[i]);
            rect(224, y, 1, 16, WHITE);
        }
        uint32_t gravity = diag_sqrt((uint32_t)(accel[0] * accel[0] +
                       accel[1] * accel[1] + accel[2] * accel[2]));
        text(10, 148, "GRAVITY MG", 1u, MUTED);
        number(88, 148, (int32_t)gravity, 1u, WHITE);
        text(170, 148, "IMU INT GPIO20", 1u, MUTED);
        number(274, 148, diag_gpio_level(20u), 1u, WHITE);
        text(10, 171, "GYRO RAW X Y Z", 1u, MUTED);
        number(108, 171, imu.gyro_x, 1u, RED);
        number(178, 171, imu.gyro_y, 1u, GREEN);
        number(248, 171, imu.gyro_z, 1u, BLUE);
        text(10, 194, "FLAT SCREEN UP Z ABOUT -1000 MG", 1u, MUTED);
    }
    footer("LEFT RIGHT PAGES / ENTER RETRY", "TILT EACH AXIS / ROTATE TO TEST GYROSCOPE");
}

static void render_power(void) {
    if (adc_result != DIAG_HW_OK) {
        text(10, 52, diag_hw_result_name(adc_result), 1u, RED);
        text(10, 76, "ADC CLOCK RESET OR CONVERSION FAILED", 1u, MUTED);
    } else {
        const char *const names[] = {"BATTERY MV", "CORE MV", "LIGHT RAW"};
        for (uint32_t i = 0u; i < 3u; ++i) {
            int32_t y = 48 + (int32_t)i * 39;
            text(10, y, names[i], 1u, MUTED);
            uint32_t value = i == 2u ? adc_values[i] :
                diag_adc_mv(adc_values[i], i == 0u ? 2u : 1u);
            number(122, y - 2, (int32_t)value, 2u, WHITE);
            text(226, y, "RAW", 1u, MUTED);
            number(252, y, adc_values[i], 1u, BLUE);
            if (i == 2u) {
                rect(10, y + 18, 294, 8, PANEL);
                rect(10, y + 18, (int32_t)adc_values[i] * 294 / 4095, 8, GREEN);
            }
        }
    }
    row(6u, "USB GPIO12", diag_gpio_level(12u) ? "VBUS DETECTED" : "VBUS LOW", BLUE);
    text(10, 177, "MV ASSUMES 3300 MV ADC REFERENCE", 1u, AMBER);
    text(10, 194, "NOT CALIBRATED / NO BATTERY PERCENT ESTIMATE", 1u, AMBER);
    footer("LEFT RIGHT PAGES / ENTER RETRY ADC", "COVER LIGHT SENSOR / USB UNPLUG MAY REBOOT");
}

static void render_ir(void) {
    row(0u, "TX GENERATOR", diag_hw_result_name(ir_result),
        ir_result == DIAG_HW_OK ? BLUE : RED);
    text(10, 67, "CARRIER HZ EST", 1u, MUTED);
    number(130, 65, (int32_t)ir_carrier, 2u, WHITE);
    text(10, 92, "TX TRAINS", 1u, MUTED);
    number(82, 92, (int32_t)ir_transmissions, 1u, WHITE);
    text(160, 92, "RX GPIO17", 1u, MUTED);
    number(234, 92, diag_gpio_level(17u), 1u, WHITE);
    text(10, 113, "RX EDGE FLAGS", 1u, MUTED);
    number(108, 113, (int32_t)ir_edges, 1u,
           ir_edges != 0u && diag_time_us() - ir_last_activity < 1000000u ? GREEN : WHITE);
    if (ir_busy) text(10, 138, ir_busy == 1u ? "TRANSMITTING" : "LISTENING 250 MS", 2u, BLUE);
    else if (ir_captured) {
        text(10, 138, "CAPTURE EDGES / LOW PULSES", 1u, MUTED);
        number(192, 138, (int32_t)ir_capture.edges, 1u, GREEN);
        number(250, 138, (int32_t)ir_capture.low_pulses, 1u, GREEN);
        text(10, 158, "LOW PULSE US MIN MAX", 1u, MUTED);
        number(156, 158, (int32_t)ir_capture.shortest_us, 1u, WHITE);
        number(238, 158, (int32_t)ir_capture.longest_us, 1u, WHITE);
    } else text(10, 142, "UP CAPTURES REMOTE BURSTS FOR 250 MS", 1u, AMBER);
    text(10, 183, "TX DOES NOT PROVE OPTICAL DELIVERY", 1u, AMBER);
    text(10, 197, "IDLE EDGE FLAGS ARE A LOWER BOUND", 1u, MUTED);
    footer("ENTER 12 SHORT BURSTS / UP LISTEN", "DOWN CLEAR / POINT AT OTHER BADGE OR REMOTE");
}

static void render_leds(void) {
    for (uint32_t i = 0; i < 4u; ++i) {
        int32_t x = 10 + (int32_t)i * 77;
        uint8_t on = led_mask & (1u << i);
        rect(x, 65, 69, 65, on ? GREEN : PANEL);
        text(x + 8, 75, "GPIO", 1u, on ? BG : MUTED);
        number(x + 24, 98, (int32_t)i, 2u, on ? BG : WHITE);
    }
    text(10, 151, led_chase ? "10 SECOND CHASE RUNNING" : "MANUAL SINGLE LED TEST", 1u, BLUE);
    text(10, 175, "CHECK THE FOUR WHITE CASE LEDS", 1u, WHITE);
    text(10, 194, "ON SCREEN STATE IS A COMMAND NOT FEEDBACK", 1u, AMBER);
    footer("ENTER NEXT LED / UP 10 SECOND CHASE", "DOWN ALL OFF / LEAVING PAGE TURNS LEDS OFF");
}

static void render_display(void) {
    const uint16_t colors[] = {0xf800u, 0x07e0u, 0x001fu, WHITE, 0xffe0u, 0xf81fu};
    if (display_pattern == 0u) {
        for (uint32_t i = 0; i < 6u; ++i) rect(10 + (int32_t)i * 50, 47, 50, 122, colors[i]);
    } else if (display_pattern == 1u) {
        for (int32_t x = 10; x < 310; x += 10) {
            for (int32_t y = 47; y < 167; y += 10) {
                rect(x, y, 10, 10, ((x / 10 + y / 10) & 1) ? WHITE : 0u);
            }
        }
    } else if (display_pattern == 4u) {
        for (int32_t x = 0; x < 300; ++x) {
            uint16_t color = (uint16_t)(((uint32_t)x * 31u / 299u) << 11u |
                ((uint32_t)x * 63u / 299u) << 5u | ((uint32_t)x * 31u / 299u));
            rect(10 + x, 47, 1, 122, color);
        }
    } else if (display_pattern == 5u) {
        for (int32_t x = 10; x < 310; ++x) {
            for (int32_t y = 47; y < 169; ++y) rect(x, y, 1, 1, (x + y) & 1 ? WHITE : 0u);
        }
    } else rect(10, 47, 300, 122, display_pattern == 2u ? WHITE : 0u);
    text(10, 181, "TE", 1u, MUTED);
    text(34, 181, te_enabled ? "ENABLED" : "DISABLED", 1u, BLUE);
    text(112, 181, "EDGE FLAGS", 1u, MUTED);
    number(186, 181, (int32_t)te_edges, 1u, WHITE);
    text(10, 196, "CHECK COLORS PIXELS ORIENTATION AND BORDER", 1u, MUTED);
    footer("ENTER PATTERN / UP TE ON OR OFF", "DOWN BACKLIGHT OFF FOR HALF A SECOND");
}

static void render_i2c(void) {
    text(10, 47, "QWST GPIO4 SDA GPIO5 SCL", 1u, WHITE);
    text(10, 63, scan_state == 0u ? "NOT SCANNED" :
         scan_state == 1u ? "SCANNING / INTERNAL SENSORS PAUSED" :
         scan_state == 2u ? "SCAN COMPLETE" : scan_error,
         1u, scan_state == 3u ? RED : BLUE);
    if (scan_state != 0u) {
        for (uint32_t address = 8u; address <= 0x77u; ++address) {
            uint32_t index = address - 8u;
            uint8_t found = scan_found[address / 8u] & (1u << (address % 8u));
            hex(10 + (int32_t)(index % 16u) * 19,
                86 + (int32_t)(index / 16u) * 14, address, 2u,
                found ? GREEN : (address < scan_address ? MUTED : PANEL));
        }
    }
    text(10, 190, "ACK ADDRESSES", 1u, MUTED);
    number(108, 190, (int32_t)scan_count, 1u, GREEN);
    text(148, 190, "EMPTY BUS IS NORMAL", 1u, AMBER);
    footer("ENTER SCAN ADDRESSES 08 TO 77", "READ PROBES / ONBOARD SENSORS ARE NOT HERE");
}

static void render_system(void) {
    row(0u, "CPU", "RP2350 ARM M33", WHITE);
    row(1u, "SRAM 8 KIB", !memory_tested ? "NOT RUN" :
        memory_ok ? "PATTERNS PASS" : "PATTERN FAILURE",
        !memory_tested ? AMBER : memory_ok ? GREEN : RED);
    if (memory_tested && !memory_ok) {
        text(10, 87, "BAD WORD INDEX", 1u, RED);
        number(130, 87, (int32_t)memory_bad_index, 1u, RED);
    }
    row(3u, "FLASH XIP", !memory_tested ? "NOT READ" :
        flash_ok ? "IMAGE READ ONLY" : "INVALID IMAGE RANGE",
        !memory_tested ? AMBER : flash_ok ? BLUE : RED);
    if (memory_tested && flash_ok) {
        text(10, 121, "IMAGE CRC32", 1u, MUTED);
        hex(130, 121, flash_crc, 8u, WHITE);
        text(10, 139, "IMAGE BYTES", 1u, MUTED);
        number(130, 139, (int32_t)flash_bytes, 1u, WHITE);
    }
    text(10, 166, "RAM TEST ONLY OWNS ITS 8 KIB BUFFER", 1u, AMBER);
    text(10, 184, "CRC IS A FINGERPRINT NOT FLASH VALIDATION", 1u, AMBER);
    footer("ENTER RUN NONDESTRUCTIVE MEMORY CHECK", "NO FLASH WRITES / NO FULL CAPACITY CLAIM");
}

static void render_limits(void) {
    row(0u, "RADIO", "NOT TESTED / NEEDS SDK", AMBER);
    row(1u, "CHARGE STATUS", "RADIO GPIO2 / NOT READ", AMBER);
    row(2u, "PSRAM", "NOT INITIALIZED", AMBER);
    row(3u, "USB DATA", "BOOTLOADER ONLY", AMBER);
    row(4u, "FLASH WRITES", "NOT EXERCISED", AMBER);
    row(5u, "RESET", "PHYSICAL REBOOT TEST", BLUE);
    row(6u, "IMU INTERRUPT", "LEVEL ONLY / NOT SETUP", AMBER);
    row(7u, "BATTERY", "NO CAPACITY ESTIMATE", AMBER);
    text(10, 194, "THIS APP DOES NOT ISSUE A WHOLE BOARD PASS", 1u, WHITE);
    footer("LEFT RIGHT PAGES / HOME OVERVIEW", "NO RADIO GPIO OR SHARED POWER TOGGLING");
}

void diagnostics_render(void) {
    badger_gfx_clear(&canvas, BG);
    rect(0, 0, 320, 34, PANEL);
    text(8, 10, titles[page], 2u, WHITE);
    number(286, 13, (int32_t)page + 1, 1u, BLUE);
    text(299, 13, "/11", 1u, MUTED);
    switch (page) {
        case PAGE_OVERVIEW: render_overview(); break;
        case PAGE_BUTTONS: render_buttons(); break;
        case PAGE_TOUCH: render_touch(); break;
        case PAGE_MOTION: render_motion(); break;
        case PAGE_POWER: render_power(); break;
        case PAGE_IR: render_ir(); break;
        case PAGE_LEDS: render_leds(); break;
        case PAGE_DISPLAY: render_display(); break;
        case PAGE_I2C: render_i2c(); break;
        case PAGE_SYSTEM: render_system(); break;
        case PAGE_LIMITS: render_limits(); break;
        case PAGE_COUNT: break;
    }
}

static void finish_scan(uint8_t result) {
    scan_state = result;
    bus_result = badger_i2c_init_pins(18u, 19u);
    if (bus_result != BADGER_I2C_OK) {
        scan_state = 3u;
        scan_error = "INTERNAL BUS RESTORE ERROR";
        imu_result = LSM6DS3_I2C_ERROR;
        touch_result = CAP1208_I2C_ERROR;
    }
}

static void scan_step(void) {
    uint8_t value;
    if (badger_i2c_probe_read(scan_address, &value)) {
        scan_found[scan_address / 8u] |= (uint8_t)(1u << (scan_address % 8u));
        ++scan_count;
    }
    if (!diag_gpio_level(4u) || !diag_gpio_level(5u)) {
        scan_error = "EXTERNAL BUS HELD LOW";
        finish_scan(3u);
        return;
    }
    ++scan_address;
    if (scan_address > 0x77u) finish_scan(2u);
}

static void start_scan(void) {
    for (uint32_t i = 0; i < sizeof(scan_found); ++i) scan_found[i] = 0u;
    scan_count = 0u;
    scan_address = 8u;
    scan_error = "EXTERNAL BUS ERROR";
    badger_i2c_result_t result = badger_i2c_init_pins(4u, 5u);
    if (result != BADGER_I2C_OK) {
        scan_error = "EXTERNAL BUS NOT IDLE";
        finish_scan(3u);
    } else {
        scan_state = 1u;
    }
}

static void change_page(diagnostic_page_t next) {
    diag_leds(0u);
    led_mask = 0u;
    led_chase = 0u;
    if (te_enabled) {
        badger_display_set_tearing(0);
        te_enabled = 0u;
    }
    if (scan_state == 1u) {
        scan_error = "SCAN CANCELLED";
        finish_scan(3u);
    }
    page = next;
    button_test_active = 0u;
    exit_holding = 0u;
}

static void memory_check(void) {
    memory_tested = 1u;
    memory_ok = 1u;
    for (uint32_t pass = 0u; pass < 35u; ++pass) {
        for (uint32_t i = 0u; i < 2048u; ++i) {
            uint32_t pattern = pass < 32u ? 1u << pass :
                pass == 32u ? 0xaaaaaaaau : pass == 33u ? 0x55555555u : i ^ 0x6d3a510fu;
            ram_test[i] = pattern;
        }
        for (uint32_t i = 0u; i < 2048u; ++i) {
            uint32_t pattern = pass < 32u ? 1u << pass :
                pass == 32u ? 0xaaaaaaaau : pass == 33u ? 0x55555555u : i ^ 0x6d3a510fu;
            if (ram_test[i] != pattern) {
                memory_ok = 0u;
                memory_bad_index = i;
                break;
            }
        }
        if (!memory_ok) break;
    }
    extern uint8_t __flash_binary_end;
    uintptr_t end = (uintptr_t)&__flash_binary_end;
    flash_ok = end > 0x10000000u && end <= 0x10400000u;
    if (flash_ok) {
        flash_bytes = (uint32_t)(end - 0x10000000u);
        flash_crc = diag_crc32((const uint8_t *)0x10000000u, flash_bytes);
    }
}

static void actions(uint8_t pressed, uint32_t now) {
    if (suppress_navigation) {
        if (buttons.stable == 0u && buttons.candidate == 0u) suppress_navigation = 0u;
        return;
    }
    if (page == PAGE_BUTTONS && button_test_active) {
        uint8_t exit = (buttons.stable & (DIAG_LEFT | DIAG_RIGHT)) ==
                          (DIAG_LEFT | DIAG_RIGHT) ||
                      (buttons.stable & DIAG_HOME) != 0u;
        if (!exit) exit_holding = 0u;
        else if (!exit_holding) {
            exit_holding = 1u;
            exit_started = now;
        } else if (now - exit_started >= 1000000u) {
            button_test_active = 0u;
            exit_holding = 0u;
            suppress_navigation = 1u;
        }
        return;
    }
    if (pressed & DIAG_HOME) change_page(PAGE_OVERVIEW);
    else if (pressed & DIAG_LEFT) change_page((diagnostic_page_t)((page + PAGE_COUNT - 1u) % PAGE_COUNT));
    else if (pressed & DIAG_RIGHT) change_page((diagnostic_page_t)((page + 1u) % PAGE_COUNT));
    else if (page == PAGE_BUTTONS && (pressed & DIAG_ENTER)) {
        button_test_active = 1u;
        buttons.seen = 0u;
        for (uint32_t i = 0; i < DIAG_BUTTON_COUNT; ++i) buttons.presses[i] = 0u;
        exit_holding = 0u;
        suppress_navigation = 1u;
    }
    else if (page == PAGE_OVERVIEW || page == PAGE_TOUCH || page == PAGE_MOTION || page == PAGE_POWER) {
        if (pressed & DIAG_ENTER) {
            initialize_sensors();
            sample_sensors();
        }
        if (page == PAGE_TOUCH && (pressed & DIAG_UP)) touch_seen = 0u;
    } else if (page == PAGE_IR) {
        if ((pressed & DIAG_ENTER) && ir_result == DIAG_HW_OK) {
            ir_busy = 1u;
            diagnostics_render();
            flush_frame();
            ir_result = diag_ir_send();
            if (ir_result == DIAG_HW_OK && ir_transmissions < 0x7fffffffu) ++ir_transmissions;
            ir_busy = 0u;
        } else if (pressed & DIAG_UP) {
            ir_busy = 2u;
            diagnostics_render();
            flush_frame();
            diag_ir_capture(&ir_capture);
            ir_captured = 1u;
            ir_busy = 0u;
        } else if (pressed & DIAG_DOWN) {
            ir_edges = 0u;
            ir_transmissions = 0u;
            ir_captured = 0u;
        }
    } else if (page == PAGE_LEDS) {
        if (pressed & DIAG_ENTER) {
            led_chase = 0u;
            led_step = (uint8_t)((led_step + 1u) % 5u);
            led_mask = led_step == 0u ? 0u : (uint8_t)(1u << (led_step - 1u));
        } else if (pressed & DIAG_UP) {
            led_chase = 1u;
            chase_started = now;
        } else if (pressed & DIAG_DOWN) {
            led_chase = 0u;
            led_mask = 0u;
        }
        diag_leds(led_mask);
    } else if (page == PAGE_DISPLAY) {
        if (pressed & DIAG_ENTER) display_pattern = (uint8_t)((display_pattern + 1u) % 6u);
        else if (pressed & DIAG_UP) {
            te_enabled ^= 1u;
            te_edges = 0u;
            badger_display_set_tearing(te_enabled);
        } else if (pressed & DIAG_DOWN) {
            diag_backlight(0u);
            diag_wait_us(500000u);
            diag_backlight(1u);
        }
    } else if (page == PAGE_I2C && (pressed & DIAG_ENTER) && scan_state != 1u) start_scan();
    else if (page == PAGE_SYSTEM && (pressed & DIAG_ENTER)) memory_check();
}

void diagnostics_init(void) {
    badger_display_init();
    diag_gpio_init();
    badger_gfx_clear(&canvas, BG);
    text(10, 80, "BADGE DIAGNOSTICS", 2u, WHITE);
    text(10, 115, "INITIALIZING PERIPHERALS", 1u, BLUE);
    flush_frame();
    diag_wait_us(15000u);
    initialize_sensors();
    sample_sensors();
    buttons.candidate = diag_buttons_raw();
    buttons.stable = buttons.candidate;
    suppress_navigation = buttons.stable != 0u;
    last_sample = diag_time_us();
    last_frame = last_sample;
    diagnostics_render();
    flush_frame();
}

void diagnostics_tick(void) {
    uint32_t now = diag_time_us();
    uint8_t pressed = diag_buttons_update(&buttons, diag_buttons_raw(), now);
    actions(pressed, now);
    count_edges(17u, &ir_edges);
    count_edges(15u, &switch_edges);
    count_edges(21u, &te_edges);
    if (scan_state == 1u) scan_step();
    if (led_chase) {
        uint32_t elapsed = now - chase_started;
        if (elapsed >= 10000000u) {
            led_chase = 0u;
            led_mask = 0u;
        } else led_mask = (uint8_t)(1u << ((elapsed / 400000u) % 4u));
        diag_leds(led_mask);
    }
    if (now - last_sample >= 50000u) {
        sample_sensors();
        last_sample = now;
    }
    if (pressed != 0u || now - last_frame >= 100000u) {
        diagnostics_render();
        flush_frame();
        last_frame = now;
    }
}

#ifndef DIAGNOSTICS_HOST_TEST
void bare_main(void) {
    diagnostics_init();
    while (1) diagnostics_tick();
}
#endif
