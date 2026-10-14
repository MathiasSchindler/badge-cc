#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../shared/boat_fluid.h"
#include "../tools/boat_fluid_topology.h"

static boat_fluid_t water, copy;
static badger_canvas_t canvas;

static void bounds(void) {
    for (uint32_t i = 0u; i < BOAT_FLUID_PARTICLES; ++i) {
        const boat_fluid_particle_t *p = water.particle + i;
        assert(p->x >= 3 * BOAT_ONE && p->x <= 316 * BOAT_ONE);
        assert(p->y >= 32 * BOAT_ONE && p->y <= 225 * BOAT_ONE);
        assert(p->vx >= -512 && p->vx <= 512 && p->vy >= -512 && p->vy <= 512);
    }
    assert(water.x >= 20 * BOAT_ONE && water.x <= 300 * BOAT_ONE);
    assert(water.y >= 16 * BOAT_ONE && water.y <= BOAT_BOTTOM * BOAT_ONE);
}

int main(void) {
    boat_fluid_init(&water);
    assert(boat_fluid_step(&water, 24, 256));
    assert(water.gravity_fraction_x == 24);
    boat_fluid_init(&water);
    copy = water;
    assert(!boat_fluid_step(&water, 97, 256));
    assert(memcmp(&copy, &water, sizeof(water)) == 0);
    assert(!boat_fluid_step(&water, 0, 191));
    assert(!boat_fluid_step(0, 0, 256));
    water.particle[17].x = 0;
    copy = water;
    assert(!boat_fluid_step(&water, 0, 256));
    assert(memcmp(&copy, &water, sizeof(water)) == 0);
    boat_fluid_init(&water);
    for (uint32_t step = 0u; step < 3000u; ++step) {
        int32_t gx = step < 500u ? 0 : step < 1200u ? 96 : step < 2000u ? -96 : 0;
        assert(boat_fluid_step(&water, gx, gx == 0 ? 256 : 240));
        bounds();
    }
    assert(water.steps == 3000u);
    boat_fluid_render(&water, &canvas);
    uint32_t liquid = 0u, surface = 0u, foam = 0u;
    for (uint32_t i = 0u; i < BOAT_FLUID_FIELD; ++i) {
        liquid += water.field[i] >= 10u;
        surface += canvas.pixels[(i / 120u) * 480u + (i % 120u) * 2u] == 0x6e9cu;
        foam += canvas.pixels[(i / 120u) * 480u + (i % 120u) * 2u] == 0xe7ffu;
    }
    assert(liquid > 1000u && liquid < 10000u && surface + foam > 0u);
    printf("boat fluid: %u equal-mass particles, 3000 bounded steps, %u occupied cells, %u impacts, explicit unchanged-state rejection PASS\n",
        BOAT_FLUID_PARTICLES, liquid, water.impacts);
    boat_fluid_init(&water);
    for (uint32_t step = 0u; step < 1000u; ++step) assert(boat_fluid_step(&water, 0, 256));
    boat_fluid_field(&water);
    assert(boat_fluid_overhangs(&water) == 0u);
    assert(water.y >= 120 * BOAT_ONE && water.y <= 180 * BOAT_ONE);
    boat_fluid_init(&water);
    for (uint32_t i = 0u; i < BOAT_FLUID_PARTICLES; ++i) {
        water.particle[i].x = 1856 + (int32_t)(i % 24u) * 1664;
        water.particle[i].y = 225 * BOAT_ONE - (int32_t)(i / 24u) * 1664;
    }
    uint32_t peak_overhang = 0u, overhang_frames = 0u;
    for (uint32_t step = 0u; step < 800u; ++step) {
        assert(boat_fluid_step(&water, 0, 256));
        bounds();
        if (step % 10u == 0u) {
            boat_fluid_field(&water);
            uint32_t overhang = boat_fluid_overhangs(&water);
            if (overhang > peak_overhang) peak_overhang = overhang;
            overhang_frames += overhang != 0u;
        }
    }
    printf("boat fluid: dam-collapse attached overhang peak %u columns, %u sampled frames\n",
        peak_overhang, overhang_frames);
    assert(peak_overhang >= 5u && overhang_frames >= 10u);
    assert(water.impacts > 0u);
    boat_fluid_render(&water, &canvas);
    memset(water.field, 0, sizeof(water.field));
    /* Isolated spray above bulk water is not an attached overhang. */
    for (uint32_t x = 30u; x < 36u; ++x) {
        for (uint32_t y = 95u; y <= 112u; ++y) water.field[x * 120u + y] = 20u;
        for (uint32_t y = 80u; y < 83u; ++y) water.field[x * 120u + y] = 20u;
    }
    assert(boat_fluid_overhangs(&water) == 0u);
    for (uint32_t y = 80u; y < 100u; ++y) water.field[30u * 120u + y] = 20u;
    assert(boat_fluid_overhangs(&water) >= 4u);
    boat_fluid_init(&water);
    for (uint32_t step = 0u; step < 60000u; ++step) {
        int32_t gx = step % 1200u < 400u ? 80 : step % 1200u < 800u ? -80 : 0;
        assert(boat_fluid_step(&water, gx, gx == 0 ? 256 : 240));
        bounds();
    }
    assert(water.steps == 60000u);
    puts("boat fluid: ten simulated minutes, fixed particle mass, bounded hull and explicit topology discrimination PASS");
}
