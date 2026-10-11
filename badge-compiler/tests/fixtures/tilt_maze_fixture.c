#define TILT_MAZE_HOST_TEST
#define TILT_MAZE_COMPARE
#include "../../../src/tools/tilt_maze.c"

static uint32_t display_pixels, expected_pixels;
static int display_enabled;

uint32_t diag_time_us(void) { return 0u; }

void badger_display_begin_region(uint32_t x, uint32_t y,
                                 uint32_t width, uint32_t height) {
    (void)x;
    (void)y;
    display_pixels = 0u;
    expected_pixels = width * height;
}

void badger_display_write(uint16_t pixel) {
    (void)pixel;
    ++display_pixels;
}

void badger_display_end_update(void) {}
void badger_display_end(void) { display_enabled = 1; }

int main(void) {
    if (run_comparison() != COMPARISON_FINGERPRINT) return 1;
    reset_game();
    if (columns != 8 || rows != 6 || goal_cell == 0u) return 2;
    for (int32_t cell = 0; cell < columns * rows; ++cell) {
        if (hazards[cell] != HAZARD_NONE || parents[cell] == NO_CELL) return 3;
    }
    lsm6ds3_sample_t sample;
    sample.accel_x = -200;
    sample.accel_y = 500;
    sample.accel_z = -16384;
    read_input(&sample, DIAG_ENTER);
    for (uint32_t i = 1u; i < CALIBRATION_SAMPLES; ++i) read_input(&sample, 0u);
    if (phase != MAZE_RUNNING || neutral_x != -200 || neutral_y != 500) return 4;
    draw_board();
    draw_region(0, 0, BADGER_GFX_WIDTH, BADGER_GFX_HEIGHT);
    if (!display_enabled || display_pixels != 76800u ||
        display_pixels != expected_pixels) return 5;
    int32_t old_x = marble_x / FIXED_ONE;
    int32_t old_y = marble_y / FIXED_ONE;
    marble_x += FIXED_ONE;
    draw_marble(old_x, old_y);
    if (display_pixels != expected_pixels || display_pixels >= 400u) return 6;
    level = 3u;
    generate_maze();
    elapsed_seconds = 42u;
    uint32_t hits = 0u;
    for (int32_t cell = 0; cell < columns * rows; ++cell) {
        if (safe_route[cell] && hazards[cell] != HAZARD_NONE) return 7;
        if (hazards[cell] == HAZARD_NONE) continue;
        marble_x = center_x((uint16_t)cell);
        marble_y = center_y((uint16_t)cell);
        velocity_x = velocity_y = 0;
        physics_step();
        ++hits;
        if (retries != hits || marble_x != center_x(0u) ||
            marble_y != center_y(0u) || elapsed_seconds != 42u) return 8;
    }
    if (hits == 0u) return 9;
    phase = MAZE_WON;
    handle_buttons(DIAG_ENTER);
    if (phase != MAZE_RUNNING || level != 4u ||
        elapsed_seconds != 0u || retries != 0u || neutral_x != -200) return 10;
    show_error("TEST ERROR");
    if (display_pixels != 76800u || display_pixels != expected_pixels) return 11;
    return 0;
}
