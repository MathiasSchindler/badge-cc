#include "application.h"
#define TILT_MAZE_HOST_TEST
#define TILT_MAZE_APPLICATION_TEST
#include "../tilt_maze.c"

static uint32_t input_seed, digest, encounters, goals;
static maze_drawn_t drawn;
static diag_buttons_t buttons;
static const uint32_t counts[6] = {16u, 64u, 4u, 4u, 16u, 256u};

static uint32_t mix(uint32_t hash, uint32_t value) {
    return (hash ^ value) * 16777619u;
}

void compiler_application_prepare(uint32_t module, uint32_t seed) {
    (void)show_error;
    input_seed = seed;
    random_state = seed == 0u ? 1u : seed;
    board_revision = retries = elapsed_seconds = elapsed_remainder = 0u;
    neutral_x = neutral_y = 0;
    calibration_count = 0u;
    level = 1u + seed % 4u;
    for (uint32_t i = 0u; i < MAX_CELLS; ++i) {
        passages[i] = safe_route[i] = 0u;
        hazards[i] = HAZARD_NONE;
        parents[i] = depths[i] = 0u;
    }
    generate_maze();
    respawn();
    phase = MAZE_RUNNING;
    prompt = "TILT TO ROLL   ENTER RESTART";
    digest = 2166136261u;
    encounters = goals = 0u;
    for (uint32_t i = 0u; i < DIAG_BUTTON_COUNT; ++i) {
        buttons.changed_at[i] = 0u;
        buttons.presses[i] = 0u;
    }
    buttons.candidate = buttons.stable = buttons.seen = 0u;
    for (uint32_t i = 0u; i < 4u; ++i) {
        maze_canvas_storage.before[i] = 0x1234abcdu + i;
        maze_canvas_storage.after[i] = 0xedcb5432u - i;
    }
    draw_board();
    capture_drawn_state(&drawn);
    (void)module;
}

static void scripted_step(uint32_t step) {
    lsm6ds3_sample_t sample = {0};
    sample.accel_x = (int16_t)((int32_t)((input_seed + step * 251u) % 16001u) - 8000);
    sample.accel_y = (int16_t)((int32_t)((input_seed + step * 509u) % 16001u) - 8000);
    sample.accel_z = -16384;
    if ((step & 31u) == 0u) {
        uint16_t cell = (uint16_t)((input_seed + step) % (uint32_t)(columns * rows));
        marble_x = center_x(cell);
        marble_y = center_y(cell);
        velocity_x = velocity_y = 0;
        phase = MAZE_RUNNING;
    }
    if (step == 1u) {
        marble_x = center_x(goal_cell);
        marble_y = center_y(goal_cell);
        velocity_x = velocity_y = 0;
    }
    if (step == 2u) {
        phase = MAZE_RUNNING;
        respawn();
    }
    if (step == 3u) {
        for (uint16_t cell = 0u; cell < (uint16_t)(columns * rows); ++cell) {
            if (hazards[cell] != HAZARD_NONE) {
                marble_x = center_x(cell);
                marble_y = center_y(cell);
                break;
            }
        }
    }
    if (step == 4u) {
        marble_x = walls[0].x * FIXED_ONE;
        marble_y = walls[0].y * FIXED_ONE;
        phase = MAZE_RUNNING;
    }
    read_input(&sample, 0u);
    uint32_t old_retries = retries;
    physics_step();
    encounters += retries - old_retries;
    if (phase == MAZE_WON) ++goals;
    update_time(TICK_US);
}

void compiler_application_run(uint32_t module) {
    for (uint32_t i = 0u; i < counts[module]; ++i) {
        if (module == 0u) {
            level = 1u + i % 4u;
            generate_maze();
        } else if (module == 1u) {
            scripted_step(i);
        } else if (module == 2u) {
            phase = (maze_phase_t)(i % 4u);
            draw_board();
        } else if (module == 3u) {
            if (i == 0u) draw_region(0, 0, 320, 240);
            if (i == 1u) draw_region(0, 0, 320, HUD_HEIGHT);
            if (i == 2u) draw_region(-4, -4, 16, 16);
            if (i == 3u) draw_marble(4, 4);
        } else if (module == 4u) {
            scripted_step(i);
            if (i == 0u) drawn.revision = board_revision - 1u;
            if (i == 8u) ++elapsed_seconds;
            render_frame(&drawn);
        } else {
            char decimal[12], hex[9];
            uint8_t pressed = diag_buttons_update(&buttons, (uint8_t)(i % 64u), i * 40000u);
            uint32_t value = diag_sqrt(input_seed ^ (i * 2654435761u));
            diag_decimal(decimal, (int32_t)value);
            diag_hex(hex, input_seed + i, 8u);
            badger_gfx_rect(&canvas, (int32_t)(i % 330u) - 5, (int32_t)(i % 250u) - 5,
                            17, 11, (uint16_t)value);
            badger_gfx_text(&canvas, 2, 2, decimal, 1u, 0xffffu);
            badger_gfx_text(&canvas, 2, 12, hex, 1u, 0xffffu);
            digest = mix(digest, value + pressed);
        }
    }
}

int compiler_application_result(uint32_t module, compiler_result_t *out) {
    uint32_t hash = digest, guards = 1u;
    for (uint32_t i = 0u; i < 4u; ++i) {
        if (maze_canvas_storage.before[i] != 0x1234abcdu + i ||
            maze_canvas_storage.after[i] != 0xedcb5432u - i) guards = 0u;
    }
    for (uint32_t i = 0u; i < MAX_CELLS; ++i) {
        hash = mix(hash, passages[i]);
        hash = mix(hash, safe_route[i]);
        hash = mix(hash, (uint32_t)hazards[i]);
        hash = mix(hash, parents[i]);
        hash = mix(hash, depths[i]);
    }
    for (uint32_t i = 0u; i < wall_count && i < MAX_WALLS; ++i) {
        hash = mix(hash, (uint32_t)walls[i].x);
        hash = mix(hash, (uint32_t)walls[i].y);
        hash = mix(hash, (uint32_t)walls[i].width);
        hash = mix(hash, (uint32_t)walls[i].height);
    }
    for (uint32_t i = 0u; i < BADGER_GFX_WIDTH * BADGER_GFX_HEIGHT; ++i)
        hash = mix(hash, canvas.pixels[i]);
    hash = mix(hash, (uint32_t)marble_x);
    hash = mix(hash, (uint32_t)marble_y);
    hash = mix(hash, (uint32_t)velocity_x);
    hash = mix(hash, (uint32_t)velocity_y);
    hash = mix(hash, (uint32_t)filtered_x);
    hash = mix(hash, (uint32_t)filtered_y);
    out->cases = counts[module];
    out->values[0] = hash;
    out->values[1] = wall_count;
    out->values[2] = encounters;
    out->values[3] = goals;
    out->values[4] = guards;
    out->values[5] = board_revision;
    return guards && wall_count <= MAX_WALLS && columns * rows <= MAX_CELLS;
}

int compiler_application_measure(uint32_t module, uint32_t seed,
    compiler_result_t *out, uint32_t (*clock)(void *),
    void (*service)(void *), void *context, uint32_t samples[7]) {
    for (uint32_t sample = 0u; sample < 7u; ++sample) samples[sample] = 0u;
    compiler_application_prepare(module, seed);
    compiler_application_run(module);
    if (!compiler_application_result(module, out)) return 0;
    service(context);
    for (uint32_t sample = 0u; sample < 7u; ++sample) {
        compiler_result_t measured;
        compiler_application_prepare(module, seed);
        uint32_t before = clock(context);
        compiler_application_run(module);
        samples[sample] = clock(context) - before;
        if (!compiler_application_result(module, &measured) ||
            measured.cases != out->cases || samples[sample] < 100u ||
            samples[sample] > 1000000u) return 0;
        for (uint32_t lane = 0u; lane < 6u; ++lane)
            if (measured.values[lane] != out->values[lane]) return 0;
        service(context);
    }
    return 1;
}
