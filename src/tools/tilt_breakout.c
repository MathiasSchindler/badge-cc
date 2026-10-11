#include "display.h"
#include "i2c_bitbang.h"
#include "lsm6ds3.h"

#define TIMER0_TIMELR (*(volatile uint32_t *)(0x400b0000u + 0x0cu))

#define IMU_SDA 18u
#define IMU_SCL 19u
#define FRAME_US 16667u

#define PLAY_LEFT 7
#define PLAY_RIGHT 312
#define PLAY_TOP 30
#define PLAY_BOTTOM 233

#define PADDLE_Y 220
#define PADDLE_WIDTH 54
#define PADDLE_HEIGHT 7
#define BALL_RADIUS 3

#define BRICK_COLUMNS 8u
#define BRICK_ROWS 4u
#define BRICK_COUNT (BRICK_COLUMNS * BRICK_ROWS)
#define BRICK_X 11
#define BRICK_Y 43
#define BRICK_WIDTH 34
#define BRICK_HEIGHT 10
#define BRICK_X_STEP 38
#define BRICK_Y_STEP 15

#define FIXED_SHIFT 8
#define FIXED_ONE (1 << FIXED_SHIFT)
#define NO_BRICK 0xffffffffu

/* Gameplay tuning, in Q8 pixels per frame unless marked as raw IMU units. */
#define PADDLE_DEAD_ZONE_RAW 800
#define PADDLE_FULL_TILT_RAW 12000
#define PADDLE_MIN_SPEED 64
#define PADDLE_MAX_SPEED 2304
#define LAUNCH_ARM_Y_RAW (-2000)
#define LAUNCH_TRIGGER_Y_RAW (-5000)
#define BALL_START_X_SPEED 480
#define BALL_START_Y_SPEED 700
#define BALL_MIN_X_SPEED 160
#define BALL_MAX_X_SPEED 1050
#define BALL_MAX_Y_SPEED 1100
#define BALL_SPEEDUP_X 6
#define BALL_SPEEDUP_Y 10

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
static int32_t filtered_x;
static int32_t filtered_y;
static int filter_initialized;

static int32_t paddle_x;
static int32_t paddle_position;
static int32_t paddle_velocity;
static int32_t ball_x;
static int32_t ball_y;
static int32_t ball_vx;
static int32_t ball_vy;
static uint32_t bricks;
static uint32_t score;
static uint32_t lives;
static int waiting_for_launch;
static int launch_armed;
static char score_text[11] = "SCORE 0000";
static char lives_text[8] = "LIVES 3";

static uint32_t time_us(void) {
    return TIMER0_TIMELR;
}

static void wait_frame(uint32_t started_at) {
    while (time_us() - started_at < FRAME_US) {}
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

static void update_labels(void) {
    uint32_t value = score > 9999u ? 9999u : score;
    for (uint32_t index = 0; index < 4u; ++index) {
        score_text[9u - index] = (char)('0' + value % 10u);
        value /= 10u;
    }
    lives_text[6] = (char)('0' + lives);
}

static int inside(int32_t value, int32_t start, int32_t length) {
    return value >= start && value < start + length;
}

static uint16_t brick_color(uint32_t row) {
    if (row == 0u) return rgb565(235u, 75u, 85u);
    if (row == 1u) return rgb565(251u, 143u, 68u);
    if (row == 2u) return rgb565(210u, 153u, 34u);
    return rgb565(70u, 190u, 95u);
}

static uint16_t game_pixel(uint32_t x, uint32_t y) {
    uint16_t background = rgb565(9u, 22u, 42u);
    uint16_t white = rgb565(248u, 250u, 252u);
    uint16_t blue = rgb565(9u, 105u, 218u);
    uint16_t purple = rgb565(130u, 80u, 223u);
    uint16_t yellow = rgb565(210u, 153u, 34u);

    if (x < 5u || x >= BADGER_DISPLAY_WIDTH - 5u ||
        y < 5u || y >= BADGER_DISPLAY_HEIGHT - 5u) return blue;
    if (text_pixel(score_text, x, y, 12u, 11u, 1u) ||
        text_pixel("TILT BREAKOUT", x, y, 97u, 10u, 1u) ||
        text_pixel(lives_text, x, y, 261u, 11u, 1u)) return white;

    for (uint32_t index = 0; index < BRICK_COUNT; ++index) {
        if ((bricks & (1u << index)) == 0u) continue;
        uint32_t column = index % BRICK_COLUMNS;
        uint32_t row = index / BRICK_COLUMNS;
        int32_t brick_x = BRICK_X + (int32_t)column * BRICK_X_STEP;
        int32_t brick_y = BRICK_Y + (int32_t)row * BRICK_Y_STEP;
        if (inside((int32_t)x, brick_x, BRICK_WIDTH) &&
            inside((int32_t)y, brick_y, BRICK_HEIGHT)) {
            return brick_color(row);
        }
    }

    if (waiting_for_launch &&
        text_pixel("TILT FORWARD TO LAUNCH", x, y, 28u, 188u, 2u)) {
        return yellow;
    }

    if (inside((int32_t)x, paddle_x, PADDLE_WIDTH) &&
        inside((int32_t)y, PADDLE_Y, PADDLE_HEIGHT)) return purple;

    int32_t center_x = ball_x >> FIXED_SHIFT;
    int32_t center_y = ball_y >> FIXED_SHIFT;
    int32_t dx = (int32_t)x - center_x;
    int32_t dy = (int32_t)y - center_y;
    if (dx * dx + dy * dy <= BALL_RADIUS * BALL_RADIUS) return white;
    return background;
}

static void draw_region(int32_t x, int32_t y,
                        int32_t width, int32_t height) {
    if (x < 5) {
        width -= 5 - x;
        x = 5;
    }
    if (y < 5) {
        height -= 5 - y;
        y = 5;
    }
    if (x + width > 315) width = 315 - x;
    if (y + height > 235) height = 235 - y;
    if (width <= 0 || height <= 0) return;

    badger_display_begin_region((uint32_t)x, (uint32_t)y,
                                (uint32_t)width, (uint32_t)height);
    for (int32_t px = x; px < x + width; ++px) {
        for (int32_t py = y; py < y + height; ++py) {
            badger_display_write(game_pixel((uint32_t)px, (uint32_t)py));
        }
    }
    badger_display_end_update();
}

static void draw_full_screen(void) {
    badger_display_begin();
    for (uint32_t x = 0; x < BADGER_DISPLAY_WIDTH; ++x) {
        for (uint32_t y = 0; y < BADGER_DISPLAY_HEIGHT; ++y) {
            badger_display_write(game_pixel(x, y));
        }
    }
    badger_display_end();
}

static int32_t minimum(int32_t a, int32_t b) {
    return a < b ? a : b;
}

static int32_t maximum(int32_t a, int32_t b) {
    return a > b ? a : b;
}

static void draw_moving_object(int32_t old_x, int32_t old_y,
                               int32_t new_x, int32_t new_y,
                               int32_t radius) {
    int32_t left = minimum(old_x, new_x) - radius - 1;
    int32_t top = minimum(old_y, new_y) - radius - 1;
    int32_t right = maximum(old_x, new_x) + radius + 2;
    int32_t bottom = maximum(old_y, new_y) + radius + 2;
    draw_region(left, top, right - left, bottom - top);
}

static void filter_input(void) {
    if (!filter_initialized) {
        filtered_x = sample.accel_x;
        filtered_y = sample.accel_y;
        filter_initialized = 1;
        return;
    }
    filtered_x += ((int32_t)sample.accel_x - filtered_x) / 3;
    filtered_y += ((int32_t)sample.accel_y - filtered_y) / 3;
}

static void update_paddle(void) {
    int32_t previous_position = paddle_position;
    int32_t magnitude = filtered_x < 0 ? -filtered_x : filtered_x;
    if (magnitude <= PADDLE_DEAD_ZONE_RAW) {
        paddle_velocity = 0;
    } else {
        int32_t range = PADDLE_FULL_TILT_RAW - PADDLE_DEAD_ZONE_RAW;
        int32_t excess = magnitude - PADDLE_DEAD_ZONE_RAW;
        if (excess > range) excess = range;
        int32_t speed = PADDLE_MIN_SPEED +
            (int32_t)((int64_t)excess * excess *
                      (PADDLE_MAX_SPEED - PADDLE_MIN_SPEED) /
                      ((int64_t)range * range));
        paddle_velocity = filtered_x < 0 ? -speed : speed;
    }
    paddle_position += paddle_velocity;
    int32_t left_limit = PLAY_LEFT << FIXED_SHIFT;
    int32_t right_limit =
        (PLAY_RIGHT - PADDLE_WIDTH) << FIXED_SHIFT;
    if (paddle_position < left_limit) paddle_position = left_limit;
    if (paddle_position > right_limit) paddle_position = right_limit;
    paddle_velocity = paddle_position - previous_position;
    paddle_x = paddle_position >> FIXED_SHIFT;
}

static int32_t speed_up_component(int32_t velocity, int32_t increment,
                                  int32_t maximum_speed) {
    if (velocity > 0) {
        velocity += increment;
        if (velocity > maximum_speed) velocity = maximum_speed;
    } else if (velocity < 0) {
        velocity -= increment;
        if (velocity < -maximum_speed) velocity = -maximum_speed;
    }
    return velocity;
}

static void attach_ball(void) {
    ball_x = (paddle_x + PADDLE_WIDTH / 2) << FIXED_SHIFT;
    ball_y = (PADDLE_Y - BALL_RADIUS - 2) << FIXED_SHIFT;
}

static int32_t clamp(int32_t value, int32_t low, int32_t high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static int circle_overlaps_rectangle(int32_t center_x, int32_t center_y,
                                     int32_t left, int32_t top,
                                     int32_t right, int32_t bottom) {
    int32_t nearest_x = clamp(center_x, left, right);
    int32_t nearest_y = clamp(center_y, top, bottom);
    int32_t dx = center_x - nearest_x;
    int32_t dy = center_y - nearest_y;
    return dx * dx + dy * dy <= BALL_RADIUS * BALL_RADIUS;
}

static uint32_t update_ball(void) {
    int32_t previous_x = ball_x >> FIXED_SHIFT;
    int32_t previous_y = ball_y >> FIXED_SHIFT;
    ball_x += ball_vx;
    ball_y += ball_vy;
    int32_t x = ball_x >> FIXED_SHIFT;
    int32_t y = ball_y >> FIXED_SHIFT;

    if (x - BALL_RADIUS < PLAY_LEFT) {
        ball_x = (PLAY_LEFT + BALL_RADIUS) << FIXED_SHIFT;
        ball_vx = ball_vx < 0 ? -ball_vx : ball_vx;
        x = ball_x >> FIXED_SHIFT;
    } else if (x + BALL_RADIUS >= PLAY_RIGHT) {
        ball_x = (PLAY_RIGHT - BALL_RADIUS - 1) << FIXED_SHIFT;
        ball_vx = ball_vx > 0 ? -ball_vx : ball_vx;
        x = ball_x >> FIXED_SHIFT;
    }
    if (y - BALL_RADIUS < PLAY_TOP) {
        ball_y = (PLAY_TOP + BALL_RADIUS) << FIXED_SHIFT;
        ball_vy = ball_vy < 0 ? -ball_vy : ball_vy;
        y = ball_y >> FIXED_SHIFT;
    }

    if (ball_vy > 0 &&
        previous_y + BALL_RADIUS < PADDLE_Y &&
        y + BALL_RADIUS >= PADDLE_Y &&
        x >= paddle_x - BALL_RADIUS &&
        x <= paddle_x + PADDLE_WIDTH + BALL_RADIUS) {
        int32_t offset =
            x - (paddle_x + PADDLE_WIDTH / 2);
        int32_t vertical_speed =
            ball_vy < 0 ? -ball_vy : ball_vy;
        if (vertical_speed < BALL_START_Y_SPEED) {
            vertical_speed = BALL_START_Y_SPEED;
        }
        ball_y = (PADDLE_Y - BALL_RADIUS - 1) << FIXED_SHIFT;
        ball_vy = -vertical_speed;
        ball_vx = offset * 24 + paddle_velocity / 5;
        if (ball_vx > BALL_MAX_X_SPEED) ball_vx = BALL_MAX_X_SPEED;
        if (ball_vx < -BALL_MAX_X_SPEED) ball_vx = -BALL_MAX_X_SPEED;
        if (ball_vx > 0 && ball_vx < BALL_MIN_X_SPEED) {
            ball_vx = BALL_MIN_X_SPEED;
        } else if (ball_vx < 0 && ball_vx > -BALL_MIN_X_SPEED) {
            ball_vx = -BALL_MIN_X_SPEED;
        } else if (ball_vx == 0) {
            ball_vx = offset >= 0 ?
                      BALL_MIN_X_SPEED : -BALL_MIN_X_SPEED;
        }
        y = ball_y >> FIXED_SHIFT;
    }

    for (uint32_t index = 0; index < BRICK_COUNT; ++index) {
        if ((bricks & (1u << index)) == 0u) continue;
        uint32_t column = index % BRICK_COLUMNS;
        uint32_t row = index / BRICK_COLUMNS;
        int32_t brick_x = BRICK_X + (int32_t)column * BRICK_X_STEP;
        int32_t brick_y = BRICK_Y + (int32_t)row * BRICK_Y_STEP;
        int32_t brick_right = brick_x + BRICK_WIDTH - 1;
        int32_t brick_bottom = brick_y + BRICK_HEIGHT - 1;
        if (!circle_overlaps_rectangle(x, y, brick_x, brick_y,
                                       brick_right, brick_bottom)) {
            continue;
        }
        bricks &= ~(1u << index);
        score += 10u;
        if (previous_y + BALL_RADIUS < brick_y) {
            ball_y = (brick_y - BALL_RADIUS - 1) << FIXED_SHIFT;
            ball_vy = ball_vy > 0 ? -ball_vy : ball_vy;
        } else if (previous_y - BALL_RADIUS > brick_bottom) {
            ball_y = (brick_bottom + BALL_RADIUS + 1) << FIXED_SHIFT;
            ball_vy = ball_vy < 0 ? -ball_vy : ball_vy;
        } else if (previous_x + BALL_RADIUS < brick_x) {
            ball_x = (brick_x - BALL_RADIUS - 1) << FIXED_SHIFT;
            ball_vx = ball_vx > 0 ? -ball_vx : ball_vx;
        } else if (previous_x - BALL_RADIUS > brick_right) {
            ball_x = (brick_right + BALL_RADIUS + 1) << FIXED_SHIFT;
            ball_vx = ball_vx < 0 ? -ball_vx : ball_vx;
        } else {
            ball_vy = -ball_vy;
        }
        ball_vx = speed_up_component(ball_vx, BALL_SPEEDUP_X,
                                     BALL_MAX_X_SPEED);
        ball_vy = speed_up_component(ball_vy, BALL_SPEEDUP_Y,
                                     BALL_MAX_Y_SPEED);
        return index;
    }
    return NO_BRICK;
}

static void start_round(void) {
    waiting_for_launch = 1;
    launch_armed = 0;
    ball_vx = 0;
    ball_vy = 0;
    attach_ball();
}

static void reset_game(void) {
    bricks = 0xffffffffu;
    score = 0u;
    lives = 3u;
    paddle_x = (BADGER_DISPLAY_WIDTH - PADDLE_WIDTH) / 2;
    paddle_position = paddle_x << FIXED_SHIFT;
    paddle_velocity = 0;
    update_labels();
    start_round();
}

static void show_error(const char *message) {
    badger_display_begin();
    for (uint32_t x = 0; x < BADGER_DISPLAY_WIDTH; ++x) {
        for (uint32_t y = 0; y < BADGER_DISPLAY_HEIGHT; ++y) {
            uint16_t color = rgb565(9u, 22u, 42u);
            if (x < 5u || x >= BADGER_DISPLAY_WIDTH - 5u ||
                y < 5u || y >= BADGER_DISPLAY_HEIGHT - 5u) {
                color = rgb565(207u, 34u, 46u);
            } else if (text_pixel("TILT BREAKOUT", x, y, 52u, 62u, 3u) ||
                       text_pixel(message, x, y, 46u, 126u, 2u)) {
                color = rgb565(248u, 250u, 252u);
            }
            badger_display_write(color);
        }
    }
    badger_display_end();
}

void bare_main(void) {
    uint8_t identity;
    badger_display_init();

    if (badger_i2c_init_pins(IMU_SDA, IMU_SCL) != BADGER_I2C_OK) {
        show_error("I2C BUS ERROR");
        while (1) __asm__ volatile ("wfi");
    }
    if (lsm6ds3_init(&identity) != LSM6DS3_OK) {
        show_error("IMU ID ERROR");
        while (1) __asm__ volatile ("wfi");
    }
    if (!lsm6ds3_read(&sample)) {
        show_error("IMU READ ERROR");
        while (1) __asm__ volatile ("wfi");
    }

    filter_input();
    reset_game();
    draw_full_screen();

    while (1) {
        uint32_t frame_started = time_us();
        int32_t old_paddle_x = paddle_x;
        int32_t old_ball_x = ball_x >> FIXED_SHIFT;
        int32_t old_ball_y = ball_y >> FIXED_SHIFT;
        int was_waiting = waiting_for_launch;
        uint32_t old_score = score;
        uint32_t old_lives = lives;
        uint32_t removed_brick = NO_BRICK;
        int redraw_all = 0;

        if (!lsm6ds3_read(&sample)) {
            show_error("IMU READ ERROR");
            while (1) __asm__ volatile ("wfi");
        }
        filter_input();
        update_paddle();

        if (waiting_for_launch) {
            attach_ball();
            if (filtered_y > LAUNCH_ARM_Y_RAW) launch_armed = 1;
            if (launch_armed && filtered_y < LAUNCH_TRIGGER_Y_RAW) {
                waiting_for_launch = 0;
                ball_vx = paddle_velocity < 0 ?
                          -BALL_START_X_SPEED : BALL_START_X_SPEED;
                ball_vy = -BALL_START_Y_SPEED;
            }
        } else {
            removed_brick = update_ball();
            int32_t y = ball_y >> FIXED_SHIFT;
            if (y - BALL_RADIUS > PLAY_BOTTOM) {
                if (lives > 0u) --lives;
                if (lives == 0u) {
                    reset_game();
                    redraw_all = 1;
                } else {
                    update_labels();
                    start_round();
                }
            } else if (bricks == 0u) {
                bricks = 0xffffffffu;
                start_round();
                redraw_all = 1;
            }
        }

        if (score != old_score || lives != old_lives) update_labels();
        if (redraw_all) {
            draw_full_screen();
        } else {
            draw_region(minimum(old_paddle_x, paddle_x) - 1, PADDLE_Y - 1,
                        maximum(old_paddle_x, paddle_x) -
                        minimum(old_paddle_x, paddle_x) +
                        PADDLE_WIDTH + 2, PADDLE_HEIGHT + 2);
            draw_moving_object(old_ball_x, old_ball_y,
                               ball_x >> FIXED_SHIFT,
                               ball_y >> FIXED_SHIFT, BALL_RADIUS);
            if (removed_brick != NO_BRICK) {
                uint32_t column = removed_brick % BRICK_COLUMNS;
                uint32_t row = removed_brick / BRICK_COLUMNS;
                draw_region(BRICK_X + (int32_t)column * BRICK_X_STEP,
                            BRICK_Y + (int32_t)row * BRICK_Y_STEP,
                            BRICK_WIDTH, BRICK_HEIGHT);
            }
            if (score != old_score || lives != old_lives) {
                draw_region(6, 6, 308, 17);
            }
            if (waiting_for_launch != was_waiting) {
                draw_region(26, 186, 268, 18);
            }
        }
        wait_frame(frame_started);
    }
}
