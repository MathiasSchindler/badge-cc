#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TILT_MAZE_HOST_TEST
#define TILT_MAZE_COMPARE
#include "../../tools/tilt_maze.c"

static uint32_t sent_pixels, expected_pixels, region_count;
static uint32_t full_screen_count;
static int display_enabled;

uint32_t diag_time_us(void) {
    static uint32_t ticks;
    ticks += 1000u;
    return ticks;
}

void badger_display_begin_region(uint32_t x, uint32_t y,
                                 uint32_t width, uint32_t height) {
    assert(width > 0u && height > 0u);
    assert(x + width <= BADGER_DISPLAY_WIDTH);
    assert(y + height <= BADGER_DISPLAY_HEIGHT);
    sent_pixels = 0u;
    expected_pixels = width * height;
    ++region_count;
}

void badger_display_write(uint16_t pixel) {
    (void)pixel;
    ++sent_pixels;
}

void badger_display_end_update(void) {
    assert(sent_pixels == expected_pixels);
}

void badger_display_end(void) {
    badger_display_end_update();
    display_enabled = 1;
    ++full_screen_count;
}

static void assert_clear(void) {
    assert(marble_x >= board_x * FIXED_ONE + RADIUS_Q);
    assert(marble_y >= board_y * FIXED_ONE + RADIUS_Q);
    assert(marble_x <= (board_x + columns * cell_width) * FIXED_ONE - RADIUS_Q);
    assert(marble_y <= (board_y + rows * cell_height) * FIXED_ONE - RADIUS_Q);
    for (uint32_t wall = 0u; wall < wall_count; ++wall) {
        const maze_rect_t *rect = &walls[wall];
        int32_t left = rect->x * FIXED_ONE;
        int32_t top = rect->y * FIXED_ONE;
        int32_t dx = marble_x - clamp(marble_x, left,
                                     left + rect->width * FIXED_ONE);
        int32_t dy = marble_y - clamp(marble_y, top,
                                     top + rect->height * FIXED_ONE);
        if (absolute(dx) < RADIUS_Q && absolute(dy) < RADIUS_Q) {
            assert(dx * dx + dy * dy >= RADIUS_Q * RADIUS_Q);
        }
    }
}

static void assert_generated(void) {
    uint16_t queue[MAX_CELLS];
    uint8_t visited[MAX_CELLS] = {0};
    uint32_t head = 0u, tail = 1u, edges = 0u, pit_count = 0u, fire_count = 0u;
    assert(columns * rows <= MAX_CELLS);
    assert(wall_count <= MAX_WALLS);
    assert(board_y > HUD_HEIGHT);
    assert(columns * cell_width >= 300 && rows * cell_height >= 208);
    assert(board_x - 1 >= 0 && board_y - 1 >= HUD_HEIGHT);
    assert(board_x + columns * cell_width + 2 <= BADGER_GFX_WIDTH);
    assert(board_y + rows * cell_height + 2 <= BADGER_GFX_HEIGHT);
    queue[0] = 0u;
    visited[0] = 1u;
    while (head < tail) {
        uint16_t cell = queue[head++];
        int32_t x = cell % columns, y = cell / columns;
        const uint8_t bits[] = {NORTH, EAST, SOUTH, WEST};
        const uint8_t opposite[] = {SOUTH, WEST, NORTH, EAST};
        const int32_t offsets[] = {-columns, 1, columns, -1};
        for (uint32_t direction = 0u; direction < 4u; ++direction) {
            if ((passages[cell] & bits[direction]) == 0u) continue;
            assert(!(direction == 0u && y == 0));
            assert(!(direction == 1u && x == columns - 1));
            assert(!(direction == 2u && y == rows - 1));
            assert(!(direction == 3u && x == 0));
            uint16_t next = (uint16_t)(cell + offsets[direction]);
            assert((passages[next] & opposite[direction]) != 0u);
            ++edges;
            if (!visited[next]) {
                visited[next] = 1u;
                queue[tail++] = next;
            }
        }
    }
    assert(tail == (uint32_t)(columns * rows));
    assert(edges == 2u * (tail - 1u));
    assert(goal_cell != 0u && safe_route[0] && safe_route[goal_cell]);
    uint32_t route_size = 0u;
    for (int32_t cell = 0; cell < columns * rows; ++cell) {
        assert(parents[cell] != NO_CELL);
        if (safe_route[cell]) {
            ++route_size;
            assert(hazards[cell] == HAZARD_NONE);
        }
        if (hazards[cell] == HAZARD_PIT) ++pit_count;
        if (hazards[cell] == HAZARD_FIRE) ++fire_count;
    }
    assert(route_size == (uint32_t)depths[goal_cell] + 1u);
    if (level == 1u) {
        assert(pit_count == 0u && fire_count == 0u);
        assert(depths[goal_cell] <= 10u);
    } else {
        assert(pit_count > 0u);
        assert(level < 3u ? fire_count == 0u : fire_count > 0u);
    }
}

static void test_generator(void) {
    uint8_t previous[MAX_CELLS];
    random_state = 100u;
    reset_game();
    memcpy(previous, passages, sizeof(previous));
    random_state = 101u;
    reset_game();
    assert(memcmp(previous, passages, (size_t)(columns * rows)) != 0);
    for (uint32_t difficulty = 1u; difficulty <= 12u; ++difficulty) {
        for (uint32_t seed = 0u; seed < 100u; ++seed) {
            level = difficulty;
            random_state = seed;
            generate_maze();
            assert_generated();
            respawn();
            assert_clear();
        }
    }
    level = 9999u;
    generate_maze();
    assert_generated();
}

static void test_input(void) {
    lsm6ds3_sample_t sample = {.accel_x = -200, .accel_y = 500,
                              .accel_z = -16384};
    reset_game();
    physics_step();
    assert(phase == MAZE_READY && velocity_x == 0);
    read_input(&sample, DIAG_ENTER);
    assert(phase == MAZE_CALIBRATING);
    for (uint32_t i = 1; i < CALIBRATION_SAMPLES; ++i) read_input(&sample, 0u);
    assert(phase == MAZE_RUNNING && neutral_x == -200 && neutral_y == 500);
    read_input(&sample, 0u);
    physics_step();
    assert(velocity_x == 0 && velocity_y == 0);
    sample.accel_x = 10000;
    sample.accel_y = 10000;
    read_input(&sample, 0u);
    physics_step();
    assert(velocity_x > 0 && velocity_y > 0);
    read_input(&sample, DIAG_HOME);
    assert(phase == MAZE_READY && elapsed_seconds == 0u);
    sample.accel_z = 16384;
    read_input(&sample, DIAG_ENTER);
    assert(phase == MAZE_READY);
    assert(strcmp(prompt, "SCREEN UP THEN PRESS ENTER") == 0);
    sample.accel_z = -16384;
    sample.accel_x = sample.accel_y = 0;
    read_input(&sample, DIAG_ENTER);
    sample.accel_x = 1001;
    read_input(&sample, 0u);
    assert(phase == MAZE_READY);
    assert(strcmp(prompt, "HOLD STILL THEN PRESS ENTER") == 0);
}

static void test_collisions(void) {
    int32_t left = 100 * FIXED_ONE;
    int32_t top = 100 * FIXED_ONE;
    marble_x = left - RADIUS_Q + 100;
    marble_y = top + 10 * FIXED_ONE;
    velocity_x = 300;
    velocity_y = 80;
    collide_wall(left, top, 20 * FIXED_ONE, 20 * FIXED_ONE);
    assert(marble_x <= left - RADIUS_Q);
    assert(velocity_x == 0 && velocity_y == 80);
    marble_x = left - 700;
    marble_y = top - 700;
    velocity_x = velocity_y = 300;
    collide_wall(left, top, 20 * FIXED_ONE, 20 * FIXED_ONE);
    int32_t dx = marble_x - left;
    int32_t dy = marble_y - top;
    assert(dx * dx + dy * dy >= RADIUS_Q * RADIUS_Q);
    assert(absolute(velocity_x) <= 1 && absolute(velocity_y) <= 1);
    marble_x = left - 1;
    marble_y = top - 1;
    velocity_x = velocity_y = 300;
    collide_wall(left, top, 20 * FIXED_ONE, 20 * FIXED_ONE);
    assert(velocity_x == 0 && velocity_y == 0);
    marble_x = left + FIXED_ONE;
    marble_y = top + 10 * FIXED_ONE;
    velocity_x = 100;
    velocity_y = 40;
    collide_wall(left, top, 20 * FIXED_ONE, 20 * FIXED_ONE);
    assert(marble_x == left - RADIUS_Q && velocity_x == 0 && velocity_y == 40);

    reset_game();
    phase = MAZE_RUNNING;
    filtered_x = 32767;
    velocity_x = MAX_SPEED;
    for (uint32_t i = 0; i < 1000u; ++i) {
        physics_step();
        assert_clear();
        assert(absolute(velocity_x) <= MAX_SPEED);
    }
    assert_clear();
}

static void test_momentum(void) {
    reset_game();
    phase = MAZE_RUNNING;
    velocity_x = 200;
    int32_t old_x = marble_x;
    physics_step();
    assert(marble_x > old_x && velocity_x > 0 && velocity_x < 200);
    filtered_x = -12000;
    for (uint32_t i = 0; i < 15u; ++i) physics_step();
    assert(velocity_x < 0);
    filtered_x = filtered_y = 0;
    for (uint32_t i = 0; i < 300u; ++i) physics_step();
    assert(velocity_x == 0 && velocity_y == 0);
}

static void test_playable_route(void) {
    uint16_t route[MAX_CELLS];
    uint32_t count = 0u;
    for (uint16_t cell = goal_cell; cell != 0u; cell = parents[cell]) {
        route[count++] = cell;
    }
    respawn();
    phase = MAZE_RUNNING;
    retries = 0u;
    while (count > 0u && phase != MAZE_WON) {
        uint16_t cell = route[--count];
        int32_t target_x = center_x(cell);
        int32_t target_y = center_y(cell);
        uint32_t steps = 0u;
        while (phase != MAZE_WON &&
               (absolute(target_x - marble_x) > FIXED_ONE ||
                absolute(target_y - marble_y) > FIXED_ONE ||
                absolute(velocity_x) > 32 || absolute(velocity_y) > 32)) {
            filtered_x = clamp((target_x - marble_x) * 8 - velocity_x * 96,
                               -12000, 12000);
            filtered_y = clamp((target_y - marble_y) * 8 - velocity_y * 96,
                               -12000, 12000);
            physics_step();
            assert_clear();
            assert(++steps < 1000u);
        }
    }
    assert(phase == MAZE_WON && velocity_x == 0 && velocity_y == 0);
    assert(retries == 0u);
    int32_t won_x = marble_x;
    physics_step();
    assert(marble_x == won_x);
}

static void test_levels(void) {
    reset_game();
    neutral_x = 123;
    neutral_y = -456;
    for (uint32_t difficulty = 1u; difficulty <= 5u; ++difficulty) {
        assert(level == difficulty);
        assert_generated();
        test_playable_route();
        update_time(1000000u);
        assert(elapsed_seconds == 0u);
        uint32_t previous_revision = board_revision;
        handle_buttons(DIAG_ENTER);
        assert(level == difficulty + 1u && phase == MAZE_RUNNING);
        assert(board_revision == previous_revision + 1u);
        assert(neutral_x == 123 && neutral_y == -456);
        assert(marble_x == center_x(0u) && marble_y == center_y(0u));
    }
    uint8_t previous[MAX_CELLS];
    memcpy(previous, passages, sizeof(previous));
    handle_buttons(DIAG_ENTER);
    assert(phase == MAZE_CALIBRATING && level == 6u);
    assert(memcmp(previous, passages, sizeof(previous)) == 0);
    handle_buttons(DIAG_HOME);
    assert(level == 1u && phase == MAZE_READY);
    for (uint32_t difficulty = 1u; difficulty <= 4u; ++difficulty) {
        for (uint32_t seed = 1u; seed <= 3u; ++seed) {
            level = difficulty;
            random_state = seed;
            generate_maze();
            test_playable_route();
        }
    }
}

static void test_hazards(void) {
    reset_game();
    level = 3u;
    generate_maze();
    phase = MAZE_RUNNING;
    neutral_x = 100;
    elapsed_seconds = 42u;
    elapsed_remainder = 200000u;
    uint32_t pit_count = 0u, fire_count = 0u, hits = 0u;
    for (int32_t cell = 0; cell < columns * rows; ++cell) {
        if (hazards[cell] == HAZARD_NONE) continue;
        marble_x = center_x((uint16_t)cell);
        marble_y = center_y((uint16_t)cell);
        velocity_x = velocity_y = 0;
        if (hazards[cell] == HAZARD_PIT) ++pit_count;
        else ++fire_count;
        assert(touches_hazard());
        physics_step();
        assert(retries == ++hits && phase == MAZE_RUNNING);
        assert(marble_x == center_x(0u) && marble_y == center_y(0u));
        assert(velocity_x == 0 && velocity_y == 0 && neutral_x == 100);
        assert(elapsed_seconds == 42u && elapsed_remainder == 200000u);
        assert_clear();
    }
    assert(pit_count > 0u && fire_count > 0u);
    update_time(1000000u);
    assert(elapsed_seconds == 43u);
    hazard_t saved_hazards[MAX_CELLS];
    memcpy(saved_hazards, hazards, sizeof(hazards));
    memset(hazards, 0, sizeof(hazards));
    for (int32_t cell = 0; cell < columns * rows; ++cell) {
        hazards[cell] = saved_hazards[cell];
        if (hazards[cell] == HAZARD_PIT) {
            marble_x = center_x((uint16_t)cell) +
                       (pit_radius() + MARBLE_RADIUS) * FIXED_ONE;
            marble_y = center_y((uint16_t)cell);
            assert(touches_hazard());
            ++marble_x;
            assert(!touches_hazard());
        } else if (hazards[cell] == HAZARD_FIRE) {
            maze_rect_t rect = fire_rect((uint16_t)cell);
            marble_x = (rect.x - MARBLE_RADIUS) * FIXED_ONE;
            marble_y = (rect.y + 1) * FIXED_ONE;
            assert(touches_hazard());
            --marble_x;
            assert(!touches_hazard());
        }
        hazards[cell] = HAZARD_NONE;
    }
    memcpy(hazards, saved_hazards, sizeof(hazards));
}

static void test_stress(void) {
    uint32_t random = 1u;
    reset_game();
    level = 4u;
    generate_maze();
    respawn();
    phase = MAZE_RUNNING;
    for (uint32_t i = 0u; i < 20000u; ++i) {
        random = random * 1664525u + 1013904223u;
        filtered_x = (int32_t)(random & 65535u) - 32768;
        random = random * 1664525u + 1013904223u;
        filtered_y = (int32_t)(random & 65535u) - 32768;
        physics_step();
        assert_clear();
        assert(absolute(velocity_x) <= MAX_SPEED);
        assert(absolute(velocity_y) <= MAX_SPEED);
    }
}

static void test_render_and_timer(void) {
    reset_game();
    draw_board();
    display_enabled = 0;
    draw_region(0, 0, 320, 240);
    assert(expected_pixels == 76800u);
    assert(display_enabled && full_screen_count == 1u);
    assert(canvas.pixels[88 * BADGER_GFX_HEIGHT + 76] != WALL_COLOR);
    phase = MAZE_RUNNING;
    draw_board();
    for (int32_t x = 0; x < BADGER_GFX_WIDTH; ++x) {
        assert(canvas.pixels[x * BADGER_GFX_HEIGHT + BADGER_GFX_HEIGHT - 1] ==
               FLOOR_COLOR);
    }
    int32_t old_x = marble_x / FIXED_ONE;
    int32_t old_y = marble_y / FIXED_ONE;
    assert(screen_pixel(old_x, old_y) == GOLD_COLOR);
    marble_x += 10 * FIXED_ONE;
    draw_marble(old_x, old_y);
    assert(expected_pixels < 400u);
    assert(full_screen_count == 1u);
    assert(screen_pixel(old_x, old_y) ==
           canvas.pixels[old_x * BADGER_GFX_HEIGHT + old_y]);
    phase = MAZE_READY;
    update_time(1000000u);
    assert(elapsed_seconds == 0u);
    phase = MAZE_RUNNING;
    update_time(900000u);
    update_time(200000u);
    assert(elapsed_seconds == 1u && elapsed_remainder == 100000u);
    phase = MAZE_WON;
    update_time(1000000u);
    assert(elapsed_seconds == 1u);
    draw_header();
    elapsed_seconds = 9998u;
    phase = MAZE_RUNNING;
    update_time(2000000u);
    assert(elapsed_seconds == 9999u);
    draw_header();
    uint32_t regions = region_count;
    draw_region(-10, -10, 20, 20);
    assert(expected_pixels == 100u);
    draw_region(320, 240, 10, 10);
    assert(region_count == regions + 1u);
    display_enabled = 0;
    show_error("IMU READ ERROR");
    assert(expected_pixels == 76800u);
    assert(display_enabled && full_screen_count == 2u);
    assert(screen_pixel(40, 70) == canvas.pixels[40 * BADGER_GFX_HEIGHT + 70]);
}

int main(void) {
    uint32_t fingerprint = run_comparison();
    printf("maze comparison fingerprint: %08x\n", fingerprint);
    assert(fingerprint == COMPARISON_FINGERPRINT);
    assert(comparison_physics_ticks == 15u && comparison_draw_ticks == 1000u);
    test_generator();
    test_input();
    test_collisions();
    test_momentum();
    test_levels();
    test_hazards();
    test_stress();
    test_render_and_timer();
    reset_game();
    draw_board();
    maze_drawn_t drawn;
    capture_drawn_state(&drawn);
    uint32_t regions = region_count;
    render_frame(&drawn);
    assert(region_count == regions);
    drawn.revision = board_revision - 1u;
    render_frame(&drawn);
    assert(region_count == regions + 1u && expected_pixels == 76800u);
    phase = MAZE_RUNNING;
    draw_board();
    capture_drawn_state(&drawn);
    ++elapsed_seconds;
    render_frame(&drawn);
    assert(expected_pixels == 320u * HUD_HEIGHT);
    puts("tilt maze tests passed");
    return 0;
}
