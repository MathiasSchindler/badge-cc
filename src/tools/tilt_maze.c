#include "display.h"
#include "graphics.h"
#include "diagnostic_hw.h"
#include "diagnostics_logic.h"
#include "i2c_bitbang.h"
#include "lsm6ds3.h"

#define MAX_COLUMNS 19
#define MAX_ROWS 13
#define MAX_CELLS (MAX_COLUMNS * MAX_ROWS)
#define MAX_WALLS (MAX_CELLS * 2 + MAX_COLUMNS + MAX_ROWS)
#define WALL_WIDTH 3
#define HUD_HEIGHT 14
#define NORTH 1u
#define EAST 2u
#define SOUTH 4u
#define WEST 8u
#define NO_CELL 0xffffu
#define FIXED_ONE 256
#define MARBLE_RADIUS 4
#define RADIUS_Q (MARBLE_RADIUS * FIXED_ONE)
#define MAX_SPEED 384
#define DEAD_ZONE 200
#define TICK_US 10000u
#define RENDER_US 20000u
#define MAX_CATCHUP 5u
#define CALIBRATION_SAMPLES 32u

#define FLOOR_COLOR 0x0843u
#define WALL_COLOR 0x4350u
#define WHITE_COLOR 0xffffu
#define START_COLOR 0x33bfu
#define GOAL_COLOR 0x4eacu
#define GOLD_COLOR 0xfda4u
#define PIT_COLOR 0x998fu
#define FIRE_COLOR 0xfaa0u

typedef struct {
    int16_t x, y, width, height;
} maze_rect_t;

typedef enum { HAZARD_NONE, HAZARD_PIT, HAZARD_FIRE } hazard_t;

static uint8_t passages[MAX_CELLS];
static uint8_t safe_route[MAX_CELLS];
static hazard_t hazards[MAX_CELLS];
static uint16_t parents[MAX_CELLS], depths[MAX_CELLS];
static maze_rect_t walls[MAX_WALLS];
static uint32_t wall_count, random_state = 1u;
static uint32_t level = 1u, retries, board_revision;
static int32_t columns, rows, cell_width, cell_height, board_x, board_y;
static uint16_t goal_cell;

typedef enum {
    MAZE_READY,
    MAZE_CALIBRATING,
    MAZE_RUNNING,
    MAZE_WON
} maze_phase_t;

#ifdef TILT_MAZE_APPLICATION_TEST
static struct {
    uint32_t before[4];
    badger_canvas_t image;
    uint32_t after[4];
} maze_canvas_storage;
#define canvas maze_canvas_storage.image
#else
static badger_canvas_t canvas;
#endif
static maze_phase_t phase;
static int32_t marble_x, marble_y, velocity_x, velocity_y;
static int32_t filtered_x, filtered_y, neutral_x, neutral_y;
static int32_t calibration_x, calibration_y;
static int32_t calibration_min_x, calibration_max_x;
static int32_t calibration_min_y, calibration_max_y;
static uint32_t calibration_count;
static uint32_t elapsed_seconds, elapsed_remainder;
static const char *prompt;

#ifdef TILT_MAZE_COMPARE
#define COMPARISON_FINGERPRINT 0x7789aa8cu
static uint32_t comparison_physics_ticks, comparison_draw_ticks;
#endif

static int32_t clamp(int32_t value, int32_t low, int32_t high) {
    return value < low ? low : value > high ? high : value;
}

static int32_t absolute(int32_t value) {
    return value < 0 ? -value : value;
}

static uint32_t random_next(void) {
    random_state ^= random_state << 13u;
    random_state ^= random_state >> 17u;
    random_state ^= random_state << 5u;
    return random_state;
}

static int32_t center_x(uint16_t cell) {
    return (board_x + (cell % columns) * cell_width + cell_width / 2) * FIXED_ONE;
}

static int32_t center_y(uint16_t cell) {
    return (board_y + (cell / columns) * cell_height + cell_height / 2) * FIXED_ONE;
}

static void add_wall(int32_t x, int32_t y, int32_t width, int32_t height) {
    walls[wall_count++] = (maze_rect_t){
        (int16_t)x, (int16_t)y, (int16_t)width, (int16_t)height
    };
}

static void generate_maze(void) {
    uint16_t stack[MAX_CELLS];
    uint16_t candidates[MAX_CELLS];
    uint32_t stack_size = 1u;
    if (random_state == 0u) random_state = 1u;
    columns = level == 1u ? 8 : level == 2u ? 12 : level == 3u ? 15 : 19;
    rows = level == 1u ? 6 : level == 2u ? 8 : level == 3u ? 10 : 13;
    cell_width = 304 / columns;
    cell_height = 216 / rows;
    board_x = (BADGER_GFX_WIDTH - columns * cell_width) / 2;
    board_y = HUD_HEIGHT + (BADGER_GFX_HEIGHT - HUD_HEIGHT -
                            rows * cell_height) / 2;
    int32_t count = columns * rows;
    for (int32_t cell = 0; cell < count; ++cell) {
        passages[cell] = 0u;
        safe_route[cell] = 0u;
        hazards[cell] = HAZARD_NONE;
        parents[cell] = NO_CELL;
        depths[cell] = 0u;
    }
    stack[0] = 0u;
    parents[0] = 0u;
    while (stack_size != 0u) {
        uint16_t cell = stack[stack_size - 1u];
        uint16_t neighbors[4];
        uint8_t directions[4];
        uint32_t available = 0u;
        int32_t x = cell % columns;
        int32_t y = cell / columns;
        const int32_t offsets[] = {-columns, 1, columns, -1};
        const uint8_t bits[] = {NORTH, EAST, SOUTH, WEST};
        for (uint32_t direction = 0u; direction < 4u; ++direction) {
            if ((direction == 0u && y == 0) ||
                (direction == 1u && x == columns - 1) ||
                (direction == 2u && y == rows - 1) ||
                (direction == 3u && x == 0)) continue;
            uint16_t next = (uint16_t)(cell + offsets[direction]);
            if (parents[next] != NO_CELL) continue;
            neighbors[available] = next;
            directions[available++] = bits[direction];
        }
        if (available == 0u) {
            --stack_size;
            continue;
        }
        uint32_t choice = random_next() % available;
        uint16_t next = neighbors[choice];
        uint8_t direction = directions[choice];
        uint8_t opposite = direction == NORTH ? SOUTH :
                           direction == SOUTH ? NORTH :
                           direction == EAST ? WEST : EAST;
        passages[cell] |= direction;
        passages[next] |= opposite;
        parents[next] = cell;
        depths[next] = (uint16_t)(depths[cell] + 1u);
        stack[stack_size++] = next;
    }
    /* Limit the first route; reserve branches for hazards on later levels. */
    uint32_t target = level == 1u ? 10u : level == 2u ? 32u :
                      level == 3u ? 70u : 100u + (level > 12u ? 12u : level) * 6u;
    if (target > (uint32_t)count * 3u / 4u) target = (uint32_t)count * 3u / 4u;
    goal_cell = 0u;
    for (int32_t cell = 1; cell < count; ++cell) {
        if (depths[cell] <= target && depths[cell] > depths[goal_cell]) {
            goal_cell = (uint16_t)cell;
        }
    }
    uint16_t cell = goal_cell;
    while (cell != 0u) {
        safe_route[cell] = 1u;
        cell = parents[cell];
    }
    safe_route[0] = 1u;
    uint32_t available = 0u;
    for (int32_t index = 0; index < count; ++index) {
        if (!safe_route[index]) candidates[available++] = (uint16_t)index;
    }
    uint32_t budget = level == 1u ? 0u : (uint32_t)count /
                      (level == 2u ? 12u : level == 3u ? 10u :
                       level < 6u ? 8u : level < 10u ? 6u : 4u);
    for (uint32_t index = 0u; index < budget && available != 0u; ++index) {
        uint32_t choice = random_next() % available;
        cell = candidates[choice];
        candidates[choice] = candidates[--available];
        hazards[cell] = level >= 3u && (index & 1u) != 0u ?
                        HAZARD_FIRE : HAZARD_PIT;
    }
    wall_count = 0u;
    for (int32_t row = 0; row < rows; ++row) {
        for (int32_t column = 0; column < columns; ++column) {
            int32_t x = board_x + column * cell_width - 1;
            int32_t y = board_y + row * cell_height - 1;
            uint8_t openings = passages[row * columns + column];
            if ((openings & NORTH) == 0u) {
                add_wall(x, y, cell_width + WALL_WIDTH, WALL_WIDTH);
            }
            if ((openings & WEST) == 0u) {
                add_wall(x, y, WALL_WIDTH, cell_height + WALL_WIDTH);
            }
            if (row == rows - 1) {
                add_wall(x, y + cell_height, cell_width + WALL_WIDTH, WALL_WIDTH);
            }
            if (column == columns - 1) {
                add_wall(x + cell_width, y, WALL_WIDTH, cell_height + WALL_WIDTH);
            }
        }
    }
    ++board_revision;
}

static void respawn(void) {
    marble_x = center_x(0u);
    marble_y = center_y(0u);
    velocity_x = velocity_y = 0;
    filtered_x = filtered_y = 0;
}

static void reset_game(void) {
    level = 1u;
    retries = 0u;
    generate_maze();
    phase = MAZE_READY;
    respawn();
    neutral_x = neutral_y = 0;
    calibration_count = 0u;
    elapsed_seconds = elapsed_remainder = 0u;
    prompt = "ENTER START   HOME RESET";
}

static void start_calibration(void) {
    respawn();
    retries = elapsed_seconds = elapsed_remainder = 0u;
    calibration_count = 0u;
    phase = MAZE_CALIBRATING;
    prompt = "HOLD STILL WITH SCREEN UP";
}

static void handle_buttons(uint8_t pressed) {
    if ((pressed & DIAG_HOME) != 0u) {
        reset_game();
        return;
    }
    if ((pressed & DIAG_ENTER) != 0u && phase != MAZE_CALIBRATING) {
        if (phase == MAZE_WON) {
            if (level < 9999u) ++level;
            generate_maze();
            retries = elapsed_seconds = elapsed_remainder = 0u;
            respawn();
            phase = MAZE_RUNNING;
        } else {
            start_calibration();
        }
    }
}

static void read_input(const lsm6ds3_sample_t *sample, uint8_t pressed) {
    handle_buttons(pressed);
    if (phase == MAZE_CALIBRATING) {
        int32_t x = sample->accel_x;
        int32_t y = sample->accel_y;
        /* Restrict calibration to a face-up, moderately inclined board. */
        if (sample->accel_z > -10000 || absolute(x) > 9000 ||
            absolute(y) > 9000) {
            phase = MAZE_READY;
            prompt = "SCREEN UP THEN PRESS ENTER";
            return;
        }
        if (calibration_count == 0u) {
            calibration_x = calibration_y = 0;
            calibration_min_x = calibration_max_x = x;
            calibration_min_y = calibration_max_y = y;
        }
        calibration_min_x = clamp(x, -32768, calibration_min_x);
        calibration_max_x = clamp(x, calibration_max_x, 32767);
        calibration_min_y = clamp(y, -32768, calibration_min_y);
        calibration_max_y = clamp(y, calibration_max_y, 32767);
        if (calibration_max_x - calibration_min_x > 1000 ||
            calibration_max_y - calibration_min_y > 1000) {
            phase = MAZE_READY;
            prompt = "HOLD STILL THEN PRESS ENTER";
            return;
        }
        calibration_x += x;
        calibration_y += y;
        if (++calibration_count == CALIBRATION_SAMPLES) {
            neutral_x = calibration_x / (int32_t)CALIBRATION_SAMPLES;
            neutral_y = calibration_y / (int32_t)CALIBRATION_SAMPLES;
            phase = MAZE_RUNNING;
            prompt = "TILT TO ROLL   ENTER RESTART";
        }
    } else if (phase == MAZE_RUNNING) {
        filtered_x += ((int32_t)sample->accel_x - neutral_x - filtered_x) / 3;
        /* Positive sensor Y points toward the holder: screen down. */
        filtered_y += ((int32_t)sample->accel_y - neutral_y - filtered_y) / 3;
    }
}

static void collide_wall(int32_t left, int32_t top,
                         int32_t width, int32_t height) {
    int32_t right = left + width;
    int32_t bottom = top + height;
    if (marble_y <= top - RADIUS_Q || marble_y >= bottom + RADIUS_Q ||
        marble_x <= left - RADIUS_Q || marble_x >= right + RADIUS_Q) return;
    int32_t dx = marble_x - clamp(marble_x, left, right);
    int32_t dy = marble_y - clamp(marble_y, top, bottom);
    if (absolute(dx) >= RADIUS_Q || absolute(dy) >= RADIUS_Q) return;
    int32_t squared = dx * dx + dy * dy;
    if (squared >= RADIUS_Q * RADIUS_Q) return;

    int32_t nx, ny;
    if (squared != 0) {
        int32_t distance = (int32_t)diag_sqrt((uint32_t)squared);
        int32_t penetration = RADIUS_Q - distance + 2;
        marble_x += dx * penetration / distance;
        marble_y += dy * penetration / distance;
        nx = dx;
        ny = dy;
    } else {
        int32_t closest = marble_x - left;
        nx = -FIXED_ONE;
        ny = 0;
        if (right - marble_x < closest) {
            closest = right - marble_x;
            nx = FIXED_ONE;
        }
        if (marble_y - top < closest) {
            closest = marble_y - top;
            nx = 0;
            ny = -FIXED_ONE;
        }
        if (bottom - marble_y < closest) {
            closest = bottom - marble_y;
            nx = 0;
            ny = FIXED_ONE;
        }
        marble_x += nx * (closest + RADIUS_Q) / FIXED_ONE;
        marble_y += ny * (closest + RADIUS_Q) / FIXED_ONE;
    }
    int32_t inward = velocity_x * nx + velocity_y * ny;
    if (inward < 0) {
        /* Remove the normal component, retaining motion along the wall. */
        int32_t normal_squared = nx * nx + ny * ny;
        velocity_x -= inward * nx / normal_squared;
        velocity_y -= inward * ny / normal_squared;
    }
}

static int32_t acceleration(int32_t tilt) {
    if (absolute(tilt) <= DEAD_ZONE) return 0;
    tilt += tilt < 0 ? DEAD_ZONE : -DEAD_ZONE;
    return clamp(tilt, -12000, 12000) / 512;
}

static int circle_touches(int32_t x, int32_t y, int32_t radius) {
    int32_t dx = marble_x - x;
    int32_t dy = marble_y - y;
    return absolute(dx) <= radius && absolute(dy) <= radius &&
           dx * dx + dy * dy <= radius * radius;
}

static int32_t pit_radius(void) {
    int32_t size = cell_width < cell_height ? cell_width : cell_height;
    return clamp(size / 6, 3, 5);
}

static maze_rect_t fire_rect(uint16_t cell) {
    return (maze_rect_t){
        (int16_t)(board_x + (cell % columns) * cell_width + 4),
        (int16_t)(center_y(cell) / FIXED_ONE - 1),
        (int16_t)(cell_width - 8), 3
    };
}

static int touches_hazard(void) {
    for (int32_t cell = 0; cell < columns * rows; ++cell) {
        if (hazards[cell] == HAZARD_PIT) {
            if (circle_touches(center_x((uint16_t)cell), center_y((uint16_t)cell),
                               (MARBLE_RADIUS + pit_radius()) * FIXED_ONE)) return 1;
        } else if (hazards[cell] == HAZARD_FIRE) {
            maze_rect_t fire = fire_rect((uint16_t)cell);
            int32_t dx = marble_x - clamp(marble_x, fire.x * FIXED_ONE,
                                         (fire.x + fire.width) * FIXED_ONE);
            int32_t dy = marble_y - clamp(marble_y, fire.y * FIXED_ONE,
                                         (fire.y + fire.height) * FIXED_ONE);
            if (absolute(dx) <= RADIUS_Q && absolute(dy) <= RADIUS_Q &&
                dx * dx + dy * dy <= RADIUS_Q * RADIUS_Q) return 1;
        }
    }
    return 0;
}

static void physics_step(void) {
    if (phase != MAZE_RUNNING) return;
    velocity_x = clamp((velocity_x + acceleration(filtered_x)) * 250 / 256,
                       -MAX_SPEED, MAX_SPEED);
    velocity_y = clamp((velocity_y + acceleration(filtered_y)) * 250 / 256,
                       -MAX_SPEED, MAX_SPEED);
    /* Four substeps bound each axis to less than half a pixel per move. */
    for (uint32_t substep = 0u; substep < 4u; ++substep) {
        marble_x += velocity_x / 4;
        marble_y += velocity_y / 4;
        for (uint32_t wall = 0u; wall < wall_count; ++wall) {
            const maze_rect_t *rect = &walls[wall];
            collide_wall(rect->x * FIXED_ONE, rect->y * FIXED_ONE,
                         rect->width * FIXED_ONE, rect->height * FIXED_ONE);
        }
        if (touches_hazard()) {
            if (retries < 9999u) ++retries;
            respawn();
            return;
        }
    }
    if (circle_touches(center_x(goal_cell), center_y(goal_cell), 5 * FIXED_ONE)) {
        phase = MAZE_WON;
        velocity_x = velocity_y = 0;
        prompt = "CLEAR ENTER NEXT";
    }
}

static void update_time(uint32_t elapsed) {
    if (phase != MAZE_RUNNING || elapsed_seconds == 9999u) return;
    elapsed_remainder += elapsed;
    elapsed_seconds += elapsed_remainder / 1000000u;
    elapsed_remainder %= 1000000u;
    if (elapsed_seconds > 9999u) elapsed_seconds = 9999u;
}

static void draw_header(void) {
    char seconds[12], number[12], failures[12];
    diag_decimal(seconds, (int32_t)elapsed_seconds);
    diag_decimal(number, (int32_t)level);
    diag_decimal(failures, (int32_t)retries);
    badger_gfx_rect(&canvas, 0, 0, 320, HUD_HEIGHT, FLOOR_COLOR);
    badger_gfx_text(&canvas, 4, 3, "L", 1u, WHITE_COLOR);
    badger_gfx_text(&canvas, 14, 3, number, 1u, WHITE_COLOR);
    badger_gfx_text(&canvas, 52, 3, "TIME", 1u, WHITE_COLOR);
    badger_gfx_text(&canvas, 82, 3, seconds, 1u, GOLD_COLOR);
    badger_gfx_text(&canvas, 112, 3, "S", 1u, WHITE_COLOR);
    if (phase == MAZE_WON) {
        badger_gfx_text(&canvas, 146, 3, prompt, 1u, GOAL_COLOR);
    } else {
        badger_gfx_text(&canvas, 158, 3, "RETRIES", 1u, WHITE_COLOR);
        badger_gfx_text(&canvas, 208, 3, failures, 1u, GOLD_COLOR);
    }
#ifdef TILT_MAZE_COMPARE
    char ticks[12];
    diag_decimal(ticks, (int32_t)comparison_physics_ticks);
#ifdef TILT_MAZE_COMPILER_OWNED
    badger_gfx_text(&canvas, 244, 3, "O P", 1u, GOLD_COLOR);
#else
    badger_gfx_text(&canvas, 244, 3, "L P", 1u, GOLD_COLOR);
#endif
    badger_gfx_text(&canvas, 266, 3, ticks, 1u, WHITE_COLOR);
#endif
}

static void draw_disc(int32_t x, int32_t y, int32_t radius,
                      uint16_t color) {
    for (int32_t dx = -radius; dx <= radius; ++dx) {
        for (int32_t dy = -radius; dy <= radius; ++dy) {
            if (dx * dx + dy * dy <= radius * radius) {
                canvas.pixels[(x + dx) * BADGER_GFX_HEIGHT + y + dy] = color;
            }
        }
    }
}

static void draw_board(void) {
    badger_gfx_clear(&canvas, FLOOR_COLOR);
    for (uint32_t wall = 0u; wall < wall_count; ++wall) {
        const maze_rect_t *rect = &walls[wall];
        badger_gfx_rect(&canvas, rect->x, rect->y,
                        rect->width, rect->height, WALL_COLOR);
    }
    for (int32_t cell = 0; cell < columns * rows; ++cell) {
        int32_t x = center_x((uint16_t)cell) / FIXED_ONE;
        int32_t y = center_y((uint16_t)cell) / FIXED_ONE;
        if (hazards[cell] == HAZARD_PIT) {
            draw_disc(x, y, pit_radius(), PIT_COLOR);
            draw_disc(x, y, pit_radius() - 1, 0u);
        } else if (hazards[cell] == HAZARD_FIRE) {
            maze_rect_t rect = fire_rect((uint16_t)cell);
            badger_gfx_rect(&canvas, rect.x, rect.y, rect.width, rect.height,
                            FIRE_COLOR);
            for (int32_t flame = rect.x; flame < rect.x + rect.width; flame += 3) {
                badger_gfx_rect(&canvas, flame, rect.y, 1, rect.height, GOLD_COLOR);
            }
        }
    }
    draw_disc(center_x(0u) / FIXED_ONE, center_y(0u) / FIXED_ONE, 6, START_COLOR);
    draw_disc(center_x(goal_cell) / FIXED_ONE,
              center_y(goal_cell) / FIXED_ONE, 6, GOAL_COLOR);
    badger_gfx_text(&canvas, center_x(goal_cell) / FIXED_ONE - 2,
                    center_y(goal_cell) / FIXED_ONE - 3, "G", 1u, FLOOR_COLOR);
    draw_header();
    if (phase == MAZE_READY || phase == MAZE_CALIBRATING) {
        badger_gfx_rect(&canvas, 30, 65, 260, 114, WALL_COLOR);
        badger_gfx_rect(&canvas, 32, 67, 256, 110, FLOOR_COLOR);
        badger_gfx_text(&canvas, 88, 76, "TILT MAZE", 2u, WHITE_COLOR);
        badger_gfx_text(&canvas, 44, 102, prompt, 1u, GOLD_COLOR);
        badger_gfx_text(&canvas, 44, 120, "TILT TO ROLL FROM BLUE TO GREEN", 1u,
                        WHITE_COLOR);
        badger_gfx_text(&canvas, 44, 136, "AVOID BLACK PITS AND ORANGE FIRE", 1u,
                        WHITE_COLOR);
        badger_gfx_text(&canvas, 44, 152, "HOME RESET   ENTER RECALIBRATE", 1u,
                        WHITE_COLOR);
#ifdef TILT_MAZE_COMPARE
        char ticks[12];
        diag_decimal(ticks, (int32_t)comparison_draw_ticks);
        badger_gfx_text(&canvas, 44, 166, "DRAW TICKS", 1u, GOLD_COLOR);
        badger_gfx_text(&canvas, 114, 166, ticks, 1u, WHITE_COLOR);
#endif
    }
}

#ifdef TILT_MAZE_COMPARE
static uint32_t comparison_fingerprint(void) {
    uint32_t hash = 2166136261u;
    for (int32_t cell = 0; cell < columns * rows; ++cell) {
        hash = (hash ^ passages[cell]) * 16777619u;
        hash = (hash ^ (uint32_t)hazards[cell]) * 16777619u;
    }
    hash = (hash ^ (uint32_t)marble_x) * 16777619u;
    hash = (hash ^ (uint32_t)marble_y) * 16777619u;
    hash = (hash ^ (uint32_t)velocity_x) * 16777619u;
    hash = (hash ^ (uint32_t)velocity_y) * 16777619u;
    hash = (hash ^ retries) * 16777619u;
    hash = (hash ^ (uint32_t)phase) * 16777619u;
    for (int32_t x = 0; x < BADGER_GFX_WIDTH; x += 2) {
        for (int32_t y = HUD_HEIGHT; y < BADGER_GFX_HEIGHT; y += 2) {
            hash = (hash ^ canvas.pixels[x * BADGER_GFX_HEIGHT + y]) * 16777619u;
        }
    }
    return hash;
}

static uint32_t run_comparison(void) {
    uint32_t physics_ticks = 0u, draw_ticks = 0u, fingerprint = 0u;
    /* Warm up, then time three identical dense-maze workloads. */
    for (uint32_t run = 0u; run < 4u; ++run) {
        random_state = 0x6d617a65u;
        level = 4u;
        generate_maze();
        phase = MAZE_RUNNING;
        retries = elapsed_seconds = elapsed_remainder = 0u;
        respawn();
        uint32_t started = diag_time_us();
        for (uint32_t step = 0u; step < 64u; ++step) {
            filtered_x = (int32_t)((step * 251u) & 32767u) - 16384;
            filtered_y = (int32_t)((step * 509u) & 32767u) - 16384;
            physics_step();
        }
        uint32_t physics_elapsed = diag_time_us() - started;
        started = diag_time_us();
        draw_board();
        uint32_t draw_elapsed = diag_time_us() - started;
        uint32_t result = comparison_fingerprint();
        if (run != 0u) {
            if (result != fingerprint) return 0u;
            physics_ticks += physics_elapsed;
            draw_ticks += draw_elapsed;
        }
        fingerprint = result;
    }
    comparison_physics_ticks = physics_ticks / (3u * 64u);
    comparison_draw_ticks = draw_ticks / 3u;
    return fingerprint;
}
#endif

static uint16_t screen_pixel(int32_t x, int32_t y) {
    int32_t dx = x - marble_x / FIXED_ONE;
    int32_t dy = y - marble_y / FIXED_ONE;
    if (dx * dx + dy * dy <= MARBLE_RADIUS * MARBLE_RADIUS) {
        if (dx < 0 && dy < 0) return WHITE_COLOR;
        return phase == MAZE_WON ? GOAL_COLOR : GOLD_COLOR;
    }
    return canvas.pixels[x * BADGER_GFX_HEIGHT + y];
}

static void draw_region(int32_t x, int32_t y, int32_t width, int32_t height) {
    int32_t right = clamp(x + width, 0, BADGER_GFX_WIDTH);
    int32_t bottom = clamp(y + height, 0, BADGER_GFX_HEIGHT);
    x = clamp(x, 0, BADGER_GFX_WIDTH);
    y = clamp(y, 0, BADGER_GFX_HEIGHT);
    if (right <= x || bottom <= y) return;
    badger_display_begin_region((uint32_t)x, (uint32_t)y,
                                (uint32_t)(right - x), (uint32_t)(bottom - y));
    for (int32_t column = x; column < right; ++column) {
        for (int32_t row = y; row < bottom; ++row) {
            badger_display_write(screen_pixel(column, row));
        }
    }
    if (x == 0 && y == 0 &&
        right == BADGER_GFX_WIDTH && bottom == BADGER_GFX_HEIGHT) {
        badger_display_end();
    } else {
        badger_display_end_update();
    }
}

static void draw_marble(int32_t old_x, int32_t old_y) {
    int32_t x = marble_x / FIXED_ONE;
    int32_t y = marble_y / FIXED_ONE;
    if (absolute(old_x - x) > 32 || absolute(old_y - y) > 32) {
        draw_region(old_x - MARBLE_RADIUS, old_y - MARBLE_RADIUS,
                    MARBLE_RADIUS * 2 + 1, MARBLE_RADIUS * 2 + 1);
        draw_region(x - MARBLE_RADIUS, y - MARBLE_RADIUS,
                    MARBLE_RADIUS * 2 + 1, MARBLE_RADIUS * 2 + 1);
        return;
    }
    int32_t left = old_x < x ? old_x : x;
    int32_t top = old_y < y ? old_y : y;
    draw_region(left - MARBLE_RADIUS, top - MARBLE_RADIUS,
                absolute(old_x - x) + MARBLE_RADIUS * 2 + 1,
                absolute(old_y - y) + MARBLE_RADIUS * 2 + 1);
}

typedef struct {
    int32_t x, y;
    maze_phase_t phase;
    uint32_t seconds, retries, revision;
    const char *prompt;
} maze_drawn_t;

static void capture_drawn_state(maze_drawn_t *drawn) {
    drawn->x = marble_x / FIXED_ONE;
    drawn->y = marble_y / FIXED_ONE;
    drawn->phase = phase;
    drawn->seconds = elapsed_seconds;
    drawn->retries = retries;
    drawn->revision = board_revision;
    drawn->prompt = prompt;
}

static void render_frame(maze_drawn_t *drawn) {
    int overlay_changed = drawn->phase != phase &&
        (drawn->phase == MAZE_READY || drawn->phase == MAZE_CALIBRATING ||
         phase == MAZE_READY || phase == MAZE_CALIBRATING);
    int full_frame = drawn->revision != board_revision || overlay_changed ||
                     (phase == MAZE_READY && drawn->prompt != prompt);
    if (full_frame) {
        draw_board();
        draw_region(0, 0, BADGER_GFX_WIDTH, BADGER_GFX_HEIGHT);
    } else if (drawn->phase != phase || drawn->seconds != elapsed_seconds ||
               drawn->retries != retries) {
        draw_header();
        draw_region(0, 0, 320, HUD_HEIGHT);
    }
    if (!full_frame && (drawn->x != marble_x / FIXED_ONE ||
        drawn->y != marble_y / FIXED_ONE || drawn->phase != phase)) {
        draw_marble(drawn->x, drawn->y);
    }
    capture_drawn_state(drawn);
}

static void show_error(const char *message) {
    badger_gfx_clear(&canvas, FLOOR_COLOR);
    badger_gfx_text(&canvas, 16, 30, "TILT MAZE ERROR", 2u, WHITE_COLOR);
    badger_gfx_text(&canvas, 16, 80, message, 1u, GOLD_COLOR);
    /* Error rendering must not overlay a marble on the diagnostic screen. */
    marble_x = marble_y = -100 * FIXED_ONE;
    draw_region(0, 0, BADGER_GFX_WIDTH, BADGER_GFX_HEIGHT);
}

#ifndef TILT_MAZE_HOST_TEST
static void halt_error(const char *message) {
    show_error(message);
    while (1) __asm__ volatile ("wfi");
}

void bare_main(void) {
    uint8_t identity = 0u;
    static diag_buttons_t buttons;
    lsm6ds3_sample_t sample;
    badger_display_init();
    diag_gpio_init();
    if (badger_i2c_init_pins(18u, 19u) != BADGER_I2C_OK) {
        halt_error("I2C BUS ERROR");
    }
    lsm6ds3_result_t result = lsm6ds3_init(&identity);
    if (result != LSM6DS3_OK) {
        halt_error(result == LSM6DS3_ID_MISMATCH ?
                   "IMU ID ERROR" : "IMU INIT I2C ERROR");
    }
#ifdef TILT_MAZE_COMPARE
    badger_gfx_clear(&canvas, FLOOR_COLOR);
    badger_gfx_text(&canvas, 22, 76, "MAZE COMPILER COMPARISON", 2u, WHITE_COLOR);
    badger_gfx_text(&canvas, 22, 110, "CHECKING PHYSICS AND DRAWING", 1u, GOLD_COLOR);
    marble_x = marble_y = -100 * FIXED_ONE;
    draw_region(0, 0, BADGER_GFX_WIDTH, BADGER_GFX_HEIGHT);
    if (run_comparison() != COMPARISON_FINGERPRINT) {
        halt_error("BENCHMARK FINGERPRINT MISMATCH");
    }
    random_state = 0x6d617a65u;
#else
    random_state = diag_time_us();
#endif
    reset_game();
    draw_board();
    draw_region(0, 0, BADGER_GFX_WIDTH, BADGER_GFX_HEIGHT);
    uint32_t last_tick = diag_time_us();
    uint32_t last_render = last_tick;
    uint32_t last_time = last_tick;
    maze_drawn_t drawn;
    capture_drawn_state(&drawn);
    while (1) {
        uint32_t now = diag_time_us();
        update_time(now - last_time);
        last_time = now;
        uint8_t pressed = diag_buttons_update(&buttons, diag_buttons_raw(), now);
        maze_phase_t previous_phase = phase;
        uint32_t previous_revision = board_revision;
        handle_buttons(pressed);
        if (now - last_tick >= TICK_US) {
            if (!lsm6ds3_read(&sample)) halt_error("IMU READ ERROR");
            read_input(&sample, 0u);
            uint32_t ticks = (now - last_tick) / TICK_US;
            /* Drop excessive backlog instead of freezing on catch-up work. */
            if (ticks > MAX_CATCHUP) {
                ticks = MAX_CATCHUP;
                last_tick = now - ticks * TICK_US;
            }
            if (previous_phase != phase || previous_revision != board_revision) {
                ticks = 0u;
                last_tick = now;
            }
            while (ticks-- != 0u) {
                physics_step();
                last_tick += TICK_US;
            }
        }
        if (now - last_render >= RENDER_US) {
            render_frame(&drawn);
            last_render = now;
        }
    }
}
#endif
