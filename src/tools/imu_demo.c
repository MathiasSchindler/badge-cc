#include "display.h"
#include "i2c_bitbang.h"
#include "lsm6ds3.h"

#define TIMER0_TIMELR (*(volatile uint32_t *)(0x400b0000u + 0x0cu))
#define IMU_SDA 18u
#define IMU_SCL 19u
#define VISUAL_X 164u
#define VISUAL_Y 124u
#define VISUAL_WIDTH 150u
#define VISUAL_HEIGHT 110u
#define VISUAL_ORIGIN_X 75
#define VISUAL_ORIGIN_Y 56
#define FIXED_ONE 1024

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

static lsm6ds3_sample_t sample;
static uint8_t identity;
static int read_ok;
static int filter_initialized;
static int32_t filtered_x;
static int32_t filtered_y;
static int32_t filtered_z;
static int32_t retained_sin_roll;
static int32_t retained_cos_roll = FIXED_ONE;
static uint16_t visual[VISUAL_WIDTH * VISUAL_HEIGHT];
static uint16_t previous_visual[VISUAL_WIDTH * VISUAL_HEIGHT];

typedef struct {
    int32_t x;
    int32_t y;
    int32_t z;
} vector3_t;

typedef struct {
    int32_t x;
    int32_t y;
} point2_t;

static uint32_t time_us(void) {
    return TIMER0_TIMELR;
}

static void wait_us(uint32_t duration) {
    uint32_t start = time_us();
    while (time_us() - start < duration) {}
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
    if (character == '-') return row == 3u ? 14u : 0u;
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

static char hex_digit(uint32_t value) {
    value &= 0x0fu;
    return value < 10u ? (char)('0' + value) :
                         (char)('A' + value - 10u);
}

static void signed_decimal(char output[7], int32_t value) {
    uint32_t magnitude = value < 0 ? (uint32_t)(-value) : (uint32_t)value;
    uint32_t position = 6u;

    if (magnitude > 99999u) magnitude = 99999u;
    for (uint32_t index = 0; index < 6u; ++index) output[index] = ' ';
    do {
        output[--position] = (char)('0' + magnitude % 10u);
        magnitude /= 10u;
    } while (magnitude != 0u);
    if (value < 0 && position != 0u) {
        output[--position] = '-';
    }
    output[6] = '\0';
}

static int32_t accel_mg(int16_t raw) {
    return ((int32_t)raw * 61) / 1000;
}

static uint32_t integer_sqrt(uint32_t value) {
    uint32_t result = 0u;
    uint32_t bit = 1u << 30u;

    while (bit > value) bit >>= 2u;
    while (bit != 0u) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1u) + bit;
        } else {
            result >>= 1u;
        }
        bit >>= 2u;
    }
    return result;
}

static void filter_acceleration(void) {
    if (!filter_initialized) {
        filtered_x = sample.accel_x;
        filtered_y = sample.accel_y;
        filtered_z = sample.accel_z;
        filter_initialized = 1;
        return;
    }
    filtered_x += ((int32_t)sample.accel_x - filtered_x) / 4;
    filtered_y += ((int32_t)sample.accel_y - filtered_y) / 4;
    filtered_z += ((int32_t)sample.accel_z - filtered_z) / 4;
}

static uint32_t visual_index(uint32_t x, uint32_t y) {
    return x * VISUAL_HEIGHT + y;
}

static void visual_put(int32_t x, int32_t y, uint16_t color) {
    if (x < 0 || y < 0 ||
        x >= (int32_t)VISUAL_WIDTH || y >= (int32_t)VISUAL_HEIGHT) {
        return;
    }
    visual[visual_index((uint32_t)x, (uint32_t)y)] = color;
}

static int32_t absolute(int32_t value) {
    return value < 0 ? -value : value;
}

static void visual_line(point2_t start, point2_t end, uint16_t color) {
    int32_t dx = absolute(end.x - start.x);
    int32_t sx = start.x < end.x ? 1 : -1;
    int32_t dy = -absolute(end.y - start.y);
    int32_t sy = start.y < end.y ? 1 : -1;
    int32_t error = dx + dy;

    while (1) {
        visual_put(start.x, start.y, color);
        if (start.x == end.x && start.y == end.y) break;
        int32_t doubled = error * 2;
        if (doubled >= dy) {
            error += dy;
            start.x += sx;
        }
        if (doubled <= dx) {
            error += dx;
            start.y += sy;
        }
    }
}

static point2_t project_vector(vector3_t vector, int32_t scale) {
    point2_t point;
    point.x = VISUAL_ORIGIN_X +
              (vector.x - vector.y) * scale / FIXED_ONE;
    point.y = VISUAL_ORIGIN_Y +
              (vector.x + vector.y) * scale / (2 * FIXED_ONE) -
              vector.z * scale / FIXED_ONE;
    return point;
}

static point2_t add_point(point2_t a, point2_t b, int32_t sign_b) {
    point2_t result = {
        a.x + sign_b * (b.x - VISUAL_ORIGIN_X),
        a.y + sign_b * (b.y - VISUAL_ORIGIN_Y),
    };
    return result;
}

static void visual_arrow(point2_t end, uint16_t color, char label) {
    point2_t origin = {VISUAL_ORIGIN_X, VISUAL_ORIGIN_Y};
    int32_t dx = end.x - origin.x;
    int32_t dy = end.y - origin.y;
    point2_t left = {
        end.x - dx / 5 - dy / 7,
        end.y - dy / 5 + dx / 7,
    };
    point2_t right = {
        end.x - dx / 5 + dy / 7,
        end.y - dy / 5 - dx / 7,
    };

    visual_line(origin, end, color);
    visual_line(end, left, color);
    visual_line(end, right, color);
    int32_t label_x = end.x + (dx >= 0 ? 3 : -7);
    int32_t label_y = end.y + (dy >= 0 ? 2 : -8);
    uint8_t rows[7];
    for (uint32_t row = 0; row < 7u; ++row) {
        rows[row] = glyph_row(label, row);
        for (uint32_t column = 0; column < 5u; ++column) {
            if ((rows[row] & (1u << (4u - column))) != 0u) {
                visual_put(label_x + (int32_t)column,
                           label_y + (int32_t)row, color);
            }
        }
    }
}

static void render_visual(void) {
    uint16_t background = rgb565(9u, 22u, 42u);
    uint16_t grid = rgb565(90u, 105u, 125u);
    uint16_t red = rgb565(235u, 75u, 85u);
    uint16_t green = rgb565(70u, 190u, 95u);
    uint16_t blue = rgb565(70u, 135u, 245u);
    uint32_t total_squared =
        (uint32_t)(filtered_x * filtered_x) +
        (uint32_t)(filtered_y * filtered_y) +
        (uint32_t)(filtered_z * filtered_z);
    uint32_t yz_squared =
        (uint32_t)(filtered_y * filtered_y) +
        (uint32_t)(filtered_z * filtered_z);
    uint32_t total = integer_sqrt(total_squared);
    uint32_t yz = integer_sqrt(yz_squared);
    vector3_t axis_x = {FIXED_ONE, 0, 0};
    vector3_t axis_y = {0, FIXED_ONE, 0};
    vector3_t axis_z = {0, 0, FIXED_ONE};

    for (uint32_t index = 0;
         index < VISUAL_WIDTH * VISUAL_HEIGHT; ++index) {
        visual[index] = background;
    }

    if (total > 64u) {
        int32_t sin_pitch = filtered_x * FIXED_ONE / (int32_t)total;
        int32_t cos_pitch = (int32_t)yz * FIXED_ONE / (int32_t)total;
        if (yz > 64u) {
            retained_sin_roll =
                -filtered_y * FIXED_ONE / (int32_t)yz;
            retained_cos_roll =
                -filtered_z * FIXED_ONE / (int32_t)yz;
        }
        int32_t sin_roll = retained_sin_roll;
        int32_t cos_roll = retained_cos_roll;

        axis_x.x = cos_pitch;
        axis_x.z = -sin_pitch;
        axis_y.x = (int32_t)((int64_t)sin_pitch * sin_roll / FIXED_ONE);
        axis_y.y = cos_roll;
        axis_y.z = (int32_t)((int64_t)cos_pitch * sin_roll / FIXED_ONE);
        axis_z.x = (int32_t)((int64_t)sin_pitch * cos_roll / FIXED_ONE);
        axis_z.y = -sin_roll;
        axis_z.z = (int32_t)((int64_t)cos_pitch * cos_roll / FIXED_ONE);
    }

    point2_t x_plane = project_vector(axis_x, 29);
    point2_t y_plane = project_vector(axis_y, 29);
    point2_t minus_x = {
        2 * VISUAL_ORIGIN_X - x_plane.x,
        2 * VISUAL_ORIGIN_Y - x_plane.y,
    };
    point2_t minus_y = {
        2 * VISUAL_ORIGIN_X - y_plane.x,
        2 * VISUAL_ORIGIN_Y - y_plane.y,
    };
    point2_t corner_pp = add_point(x_plane, y_plane, 1);
    point2_t corner_pm = add_point(x_plane, minus_y, 1);
    point2_t corner_mm = add_point(minus_x, minus_y, 1);
    point2_t corner_mp = add_point(minus_x, y_plane, 1);

    visual_line(corner_pp, corner_pm, grid);
    visual_line(corner_pm, corner_mm, grid);
    visual_line(corner_mm, corner_mp, grid);
    visual_line(corner_mp, corner_pp, grid);
    visual_line(minus_x, x_plane, grid);
    visual_line(minus_y, y_plane, grid);

    visual_arrow(project_vector(axis_x, 43), red, 'X');
    visual_arrow(project_vector(axis_y, 43), green, 'Y');
    visual_arrow(project_vector(axis_z, 43), blue, 'Z');
}

static uint16_t screen_pixel(uint32_t x, uint32_t y) {
    uint16_t background = rgb565(9u, 22u, 42u);
    uint16_t white = rgb565(248u, 250u, 252u);
    uint16_t green = rgb565(46u, 160u, 67u);
    uint16_t blue = rgb565(9u, 105u, 218u);
    uint16_t yellow = rgb565(210u, 153u, 34u);
    uint16_t red = rgb565(207u, 34u, 46u);
    char value[7];
    char id[3] = {hex_digit(identity >> 4u), hex_digit(identity), '\0'};

    if (x < 5u || x >= BADGER_DISPLAY_WIDTH - 5u ||
        y < 5u || y >= BADGER_DISPLAY_HEIGHT - 5u) return blue;
    if (x >= VISUAL_X && x < VISUAL_X + VISUAL_WIDTH &&
        y >= VISUAL_Y && y < VISUAL_Y + VISUAL_HEIGHT) {
        return visual[visual_index(x - VISUAL_X, y - VISUAL_Y)];
    }
    if (text_pixel("LSM6DS3TR C", x, y, 61u, 12u, 3u)) return white;
    if (text_pixel("ID", x, y, 126u, 39u, 1u) ||
        text_pixel(id, x, y, 144u, 39u, 1u)) return green;

    if (!read_ok) {
        if (text_pixel("SENSOR READ ERROR", x, y, 58u, 105u, 2u)) return red;
        return background;
    }

    if (text_pixel("ACCEL MG", x, y, 19u, 58u, 2u)) return yellow;
    signed_decimal(value, accel_mg(sample.accel_x));
    if (text_pixel("X", x, y, 15u, 82u, 2u) ||
        text_pixel(value, x, y, 39u, 82u, 2u)) return white;
    signed_decimal(value, accel_mg(sample.accel_y));
    if (text_pixel("Y", x, y, 15u, 103u, 2u) ||
        text_pixel(value, x, y, 39u, 103u, 2u)) return white;
    signed_decimal(value, accel_mg(sample.accel_z));
    if (text_pixel("Z", x, y, 15u, 124u, 2u) ||
        text_pixel(value, x, y, 39u, 124u, 2u)) return white;

    if (text_pixel("GYRO RAW", x, y, 15u, 162u, 1u)) return yellow;
    signed_decimal(value, sample.gyro_x);
    if (text_pixel("X", x, y, 15u, 180u, 1u) ||
        text_pixel(value, x, y, 27u, 180u, 1u)) return white;
    signed_decimal(value, sample.gyro_y);
    if (text_pixel("Y", x, y, 15u, 195u, 1u) ||
        text_pixel(value, x, y, 27u, 195u, 1u)) return white;
    signed_decimal(value, sample.gyro_z);
    if (text_pixel("Z", x, y, 15u, 210u, 1u) ||
        text_pixel(value, x, y, 27u, 210u, 1u)) return white;
    if (text_pixel("TILT", x, y, 226u, 108u, 1u)) return yellow;
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

static void draw_region(uint32_t x_start, uint32_t y_start,
                        uint32_t width, uint32_t height) {
    badger_display_begin_region(x_start, y_start, width, height);
    for (uint32_t x = x_start; x < x_start + width; ++x) {
        for (uint32_t y = y_start; y < y_start + height; ++y) {
            badger_display_write(screen_pixel(x, y));
        }
    }
    badger_display_end_update();
}

static void draw_values(void) {
    draw_region(39u, 82u, 72u, 14u);
    draw_region(39u, 103u, 72u, 14u);
    draw_region(39u, 124u, 72u, 14u);
    draw_region(27u, 180u, 36u, 7u);
    draw_region(27u, 195u, 36u, 7u);
    draw_region(27u, 210u, 36u, 7u);
}

static void remember_visual(void) {
    for (uint32_t index = 0;
         index < VISUAL_WIDTH * VISUAL_HEIGHT; ++index) {
        previous_visual[index] = visual[index];
    }
}

static void draw_visual_changes(void) {
    for (uint32_t x = 0; x < VISUAL_WIDTH; ++x) {
        uint32_t first = VISUAL_HEIGHT;
        uint32_t last = 0u;
        for (uint32_t y = 0; y < VISUAL_HEIGHT; ++y) {
            uint32_t index = visual_index(x, y);
            if (visual[index] == previous_visual[index]) continue;
            if (first == VISUAL_HEIGHT) first = y;
            last = y;
        }
        if (first == VISUAL_HEIGHT) continue;

        badger_display_begin_region(VISUAL_X + x, VISUAL_Y + first,
                                    1u, last - first + 1u);
        for (uint32_t y = first; y <= last; ++y) {
            uint32_t index = visual_index(x, y);
            badger_display_write(visual[index]);
            previous_visual[index] = visual[index];
        }
        badger_display_end_update();
    }
}

static void show_error(const char *line1, const char *line2) {
    badger_display_begin();
    for (uint32_t x = 0; x < BADGER_DISPLAY_WIDTH; ++x) {
        for (uint32_t y = 0; y < BADGER_DISPLAY_HEIGHT; ++y) {
            uint16_t color = rgb565(9u, 22u, 42u);
            if (x < 5u || x >= BADGER_DISPLAY_WIDTH - 5u ||
                y < 5u || y >= BADGER_DISPLAY_HEIGHT - 5u) {
                color = rgb565(207u, 34u, 46u);
            } else if (text_pixel("IMU ERROR", x, y, 79u, 55u, 3u) ||
                       text_pixel(line1, x, y, 40u, 112u, 2u) ||
                       text_pixel(line2, x, y, 40u, 145u, 2u)) {
                color = rgb565(248u, 250u, 252u);
            }
            badger_display_write(color);
        }
    }
    badger_display_end();
}

void bare_main(void) {
    badger_display_init();
    wait_us(10000u);

    badger_i2c_result_t bus = badger_i2c_init_pins(IMU_SDA, IMU_SCL);
    if (bus != BADGER_I2C_OK) {
        show_error("I2C BUS FAILED", "GPIO 18 19");
        while (1) __asm__ volatile ("wfi");
    }

    lsm6ds3_result_t sensor = lsm6ds3_init(&identity);
    if (sensor != LSM6DS3_OK) {
        char actual_id[] = {
            'G', 'O', 'T', ' ', 'I', 'D', ' ',
            hex_digit(identity >> 4u), hex_digit(identity), '\0',
        };
        show_error(sensor == LSM6DS3_ID_MISMATCH ?
                   actual_id : "NO SENSOR ACK",
                   "EXPECT ID 6A");
        while (1) __asm__ volatile ("wfi");
    }

    wait_us(20000u);
    read_ok = lsm6ds3_read(&sample);
    if (!read_ok) {
        show_error("SENSOR READ ERROR", "CHECK I2C BUS");
        while (1) __asm__ volatile ("wfi");
    }
    filter_acceleration();
    render_visual();
    draw_full_screen();
    remember_visual();
    while (1) {
        wait_us(10000u);
        read_ok = lsm6ds3_read(&sample);
        if (!read_ok) {
            show_error("SENSOR READ ERROR", "CHECK I2C BUS");
            while (1) __asm__ volatile ("wfi");
        }
        filter_acceleration();
        render_visual();
        draw_values();
        draw_visual_changes();
    }
}
