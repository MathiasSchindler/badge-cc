#include "boat_physics.h"

static int32_t clamp(int32_t value, int32_t low, int32_t high) {
    return value < low ? low : value > high ? high : value;
}

void boat_physics_init(boat_physics_t *state) {
    for (uint32_t i = 0u; i < BOAT_COLUMNS; ++i) state->depth[i] = BOAT_REST_DEPTH;
    for (uint32_t i = 0u; i <= BOAT_COLUMNS; ++i) state->flow[i] = 0;
    state->x = 160 * BOAT_ONE;
    state->y = (BOAT_BOTTOM - 80 + 4) * BOAT_ONE;
    state->vx = state->vy = state->slope = state->spin = 0;
}

int32_t boat_surface(const boat_physics_t *state, int32_t x_q8) {
    int32_t position = clamp(x_q8 - 2 * BOAT_ONE, 0, 79 * 4 * BOAT_ONE);
    uint32_t column = (uint32_t)position / (4u * BOAT_ONE);
    int32_t depth = state->depth[column];
    if (column + 1u < BOAT_COLUMNS) {
        int32_t fraction = position % (4 * BOAT_ONE);
        depth += (state->depth[column + 1u] - depth) * fraction / (4 * BOAT_ONE);
    }
    return BOAT_BOTTOM * BOAT_ONE - depth;
}

int boat_physics_step(boat_physics_t *state, int32_t gx, int32_t gy) {
    if (state == 0 || gx < -96 || gx > 96 || gy < 192 || gy > 256) return 0;
    if (state->x < 20 * BOAT_ONE || state->x > 300 * BOAT_ONE ||
        state->y < 16 * BOAT_ONE || state->y > BOAT_BOTTOM * BOAT_ONE ||
        state->vx < -256 || state->vx > 256 || state->vy < -512 || state->vy > 512 ||
        state->slope < -128 || state->slope > 128 || state->spin < -8 || state->spin > 8 ||
        state->flow[0] != 0 || state->flow[BOAT_COLUMNS] != 0) return 0;
    for (uint32_t i = 0u; i < BOAT_COLUMNS; ++i)
        if (state->depth[i] < 0 || state->depth[i] > BOAT_MAX_DEPTH) return 0;
    for (uint32_t i = 1u; i < BOAT_COLUMNS; ++i)
        if (state->flow[i] < -32768 || state->flow[i] > 32768) return 0;
    for (uint32_t i = 1u; i < BOAT_COLUMNS; ++i) {
        int32_t gradient = state->depth[i] - state->depth[i - 1u];
        int32_t acceleration = (gx * 4 * BOAT_ONE - gy * gradient) / (4 * 64);
        int32_t velocity = state->flow[i] + acceleration;
        state->flow[i] = clamp(velocity - velocity / 64, -32768, 32768);
    }
    /* Shared interface transfers remove exactly what their neighbour receives. */
    for (uint32_t i = 1u; i < BOAT_COLUMNS; ++i) {
        int32_t mean = (state->depth[i - 1u] + state->depth[i]) / 2;
        int32_t transfer = mean * state->flow[i] / (4 * 65536);
        int32_t low = -state->depth[i];
        if (state->depth[i - 1u] - BOAT_MAX_DEPTH > low)
            low = state->depth[i - 1u] - BOAT_MAX_DEPTH;
        int32_t high = state->depth[i - 1u];
        if (BOAT_MAX_DEPTH - state->depth[i] < high) high = BOAT_MAX_DEPTH - state->depth[i];
        transfer = clamp(transfer, low, high);
        state->depth[i - 1u] -= transfer;
        state->depth[i] += transfer;
    }
    int32_t left = state->y - 12 * state->slope - boat_surface(state, state->x - 12 * BOAT_ONE);
    int32_t right = state->y + 12 * state->slope - boat_surface(state, state->x + 12 * BOAT_ONE);
    left = clamp(left, 0, 12 * BOAT_ONE);
    right = clamp(right, 0, 12 * BOAT_ONE);
    int32_t buoyancy = (left + right) / 8;
    state->vy = clamp((state->vy + (BOAT_ONE - buoyancy) * gy / (BOAT_ONE * 64)) * 985 / 1000, -512, 512);
    state->y = clamp(state->y + state->vy, 16 * BOAT_ONE, BOAT_BOTTOM * BOAT_ONE);
    state->spin = clamp(state->spin + (left - right) / 512, -8, 8);
    state->spin -= state->spin / 4;
    state->slope = clamp(state->slope + state->spin, -128, 128);
    uint32_t column = (uint32_t)state->x / (4u * BOAT_ONE);
    int32_t water_velocity = (state->flow[column] + state->flow[column + 1u]) / (2 * 256);
    state->vx = clamp(state->vx + gx / 128 + (water_velocity - state->vx) / 16, -256, 256);
    state->x += state->vx;
    if (state->x < 20 * BOAT_ONE) { state->x = 20 * BOAT_ONE; state->vx = -state->vx / 2; }
    if (state->x > 300 * BOAT_ONE) { state->x = 300 * BOAT_ONE; state->vx = -state->vx / 2; }
    return 1;
}
