#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../shared/boat_physics.h"

static boat_physics_t sea;

static int32_t volume(void) {
    int32_t total = 0;
    for (uint32_t i = 0u; i < BOAT_COLUMNS; ++i) {
        assert(sea.depth[i] >= 0 && sea.depth[i] <= BOAT_MAX_DEPTH);
        total += sea.depth[i];
    }
    return total;
}

static void steps(uint32_t count, int32_t gx, int32_t gy) {
    int32_t initial = volume();
    for (uint32_t i = 0u; i < count; ++i) {
        assert(boat_physics_step(&sea, gx, gy));
        assert(volume() == initial);
        assert(sea.flow[0] == 0 && sea.flow[BOAT_COLUMNS] == 0);
    }
}

static void mirrored_tilts(void) {
    boat_physics_t other;
    boat_physics_init(&sea);
    boat_physics_init(&other);
    int32_t peak_asymmetry = 0;
    for (uint32_t step = 0u; step < 6000u; ++step) {
        uint32_t phase = step % 1200u;
        int32_t gx = phase < 200u ? (int32_t)phase * 80 / 200 :
            phase < 600u ? 80 - (int32_t)(phase - 200u) * 160 / 400 :
            phase < 800u ? -80 + (int32_t)(phase - 600u) * 80 / 200 : 0;
        int32_t gy = gx == 0 ? 256 : 240;
        assert(boat_physics_step(&sea, gx, gy));
        assert(boat_physics_step(&other, -gx, gy));
        int32_t other_volume = 0;
        for (uint32_t i = 0u; i < BOAT_COLUMNS; ++i) {
            assert(other.depth[i] >= 0 && other.depth[i] <= BOAT_MAX_DEPTH);
            other_volume += other.depth[i];
            int32_t difference = sea.depth[i] - other.depth[BOAT_COLUMNS - 1u - i];
            if (difference < 0) difference = -difference;
            if (difference > peak_asymmetry) peak_asymmetry = difference;
        }
        assert(volume() == BOAT_COLUMNS * BOAT_REST_DEPTH);
        assert(other_volume == BOAT_COLUMNS * BOAT_REST_DEPTH);
        assert(other.flow[0] == 0 && other.flow[BOAT_COLUMNS] == 0);
    }
    /* Characterization, not a grid/time-step convergence claim. */
    printf("boat: mirrored 60s tilt replay maximum depth asymmetry %d/256 px\n", peak_asymmetry);
}

int main(void) {
    mirrored_tilts();
    boat_physics_init(&sea);
    boat_physics_t initial = sea;
    steps(1000u, 0, 256);
    assert(memcmp(&sea, &initial, sizeof(sea)) == 0);
    assert(boat_surface(&sea, -1000) == 148 * BOAT_ONE);
    assert(boat_surface(&sea, 1000000) == 148 * BOAT_ONE);
    for (uint32_t i = 0u; i < 32u; ++i) {
        steps(300u, 80, 240);
        steps(300u, -80, 240);
        steps(300u, 0, 256);
    }
    boat_physics_init(&sea);
    steps(3000u, 80, 240);
    assert(sea.depth[79] > sea.depth[0] + 10 * BOAT_ONE);
    int32_t tilted = sea.depth[79] - sea.depth[0];
    steps(5000u, 0, 256);
    int32_t settled = sea.depth[79] - sea.depth[0];
    assert(settled < tilted / 2 && settled > -tilted / 2);
    boat_physics_init(&sea);
    steps(1000u, 24, 254);
    assert(sea.depth[79] > sea.depth[0] + 2 * BOAT_ONE);
    boat_physics_init(&sea);
    steps(1000u, -24, 254);
    assert(sea.depth[0] > sea.depth[79] + 2 * BOAT_ONE);
    int moving = 0;
    for (uint32_t i = 1u; i < BOAT_COLUMNS; ++i) moving |= sea.flow[i] != 0;
    assert(moving);
    steps(1u, 0, 256);
    moving = 0;
    for (uint32_t i = 1u; i < BOAT_COLUMNS; ++i) moving |= sea.flow[i] != 0;
    assert(moving);
    boat_physics_init(&sea);
    sea.y += 3 * BOAT_ONE;
    steps(1000u, 0, 256);
    assert(sea.y > initial.y - 2 * BOAT_ONE && sea.y < initial.y + 2 * BOAT_ONE);
    boat_physics_init(&sea);
    sea.depth[36] += 4 * BOAT_ONE;
    sea.depth[37] += 4 * BOAT_ONE;
    sea.depth[42] -= 4 * BOAT_ONE;
    sea.depth[43] -= 4 * BOAT_ONE;
    steps(1u, 0, 256);
    assert(sea.slope != 0 || sea.spin != 0);
    steps(60000u, 0, 256);
    boat_physics_init(&sea);
    for (uint32_t i = 0u; i < BOAT_COLUMNS; ++i)
        sea.depth[i] = i % 2u ? BOAT_MAX_DEPTH : 0;
    steps(10000u, 96, 192);
    initial = sea;
    assert(!boat_physics_step(&sea, 97, 256));
    assert(memcmp(&sea, &initial, sizeof(sea)) == 0);
    assert(!boat_physics_step(&sea, 0, 191));
    assert(!boat_physics_step(&sea, -97, 256));
    assert(!boat_physics_step(&sea, 0, 257));
    assert(!boat_physics_step(0, 0, 256));
    sea.depth[0] = -1;
    initial = sea;
    assert(!boat_physics_step(&sea, 0, 256));
    assert(memcmp(&sea, &initial, sizeof(sea)) == 0);
    for (uint32_t invalid = 0u; invalid < 4u; ++invalid) {
        boat_physics_init(&sea);
        if (invalid == 0u) sea.depth[0] = BOAT_MAX_DEPTH + 1;
        if (invalid == 1u) sea.flow[0] = 1;
        if (invalid == 2u) sea.flow[1] = 32769;
        if (invalid == 3u) sea.x = 0;
        initial = sea;
        assert(!boat_physics_step(&sea, 0, 256));
        assert(memcmp(&sea, &initial, sizeof(sea)) == 0);
    }
    puts("boat: rest, exact volume, tilt/reversal/settling, buoyancy/pitch, dry/full bounds and explicit rejection PASS");
}
