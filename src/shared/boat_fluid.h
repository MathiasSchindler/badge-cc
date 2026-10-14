#ifndef UNIVERSE26_BOAT_FLUID_H
#define UNIVERSE26_BOAT_FLUID_H

#include "boat_physics.h"
#include "../platform/badger/graphics.h"

#define BOAT_FLUID_COLUMNS 48u
#define BOAT_FLUID_ROWS 12u
#define BOAT_FLUID_PARTICLES (BOAT_FLUID_COLUMNS * BOAT_FLUID_ROWS)
#define BOAT_FLUID_RADIUS 10
#define BOAT_FLUID_CELLS (32u * 24u)
#define BOAT_FLUID_FIELD (160u * 120u)
#define BOAT_FLUID_DISTANCE (16u * BOAT_FLUID_RADIUS * BOAT_FLUID_RADIUS + 1u)

typedef struct {
    int32_t x, y, vx, vy;
    uint16_t next;
    uint8_t foam;
} boat_fluid_particle_t;

typedef struct {
    int32_t x, y, vx, vy, slope, spin;
    boat_fluid_particle_t particle[BOAT_FLUID_PARTICLES];
    int32_t old_x[BOAT_FLUID_PARTICLES], old_y[BOAT_FLUID_PARTICLES];
    int32_t dx[BOAT_FLUID_PARTICLES], dy[BOAT_FLUID_PARTICLES];
    int32_t pressure[BOAT_FLUID_PARTICLES], near_pressure[BOAT_FLUID_PARTICLES];
    uint32_t density[BOAT_FLUID_PARTICLES];
    uint16_t head[BOAT_FLUID_CELLS], distance[BOAT_FLUID_DISTANCE];
    uint16_t field[BOAT_FLUID_FIELD];
    uint8_t foam_field[BOAT_FLUID_FIELD];
    uint32_t steps, impacts;
    int32_t gravity_fraction_x, gravity_fraction_y;
} boat_fluid_t;

/* Equal-mass particles; no creation/deletion and no per-column surface limit.
   Scratch/field arrays are owned by the state. Initialize before stepping. */
void boat_fluid_init(boat_fluid_t *state);
int boat_fluid_step(boat_fluid_t *state, int32_t gx, int32_t gy);
void boat_fluid_render(boat_fluid_t *state, badger_canvas_t *canvas);
/* Build the exact same 2D occupancy field used by the target renderer. */
void boat_fluid_field(boat_fluid_t *state);

#endif
