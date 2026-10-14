#ifndef UNIVERSE26_BOAT_PHYSICS_H
#define UNIVERSE26_BOAT_PHYSICS_H

#include <stdint.h>

#define BOAT_COLUMNS 80u
#define BOAT_ONE 256
#define BOAT_STEP_US 10000u
#define BOAT_BOTTOM 228
#define BOAT_MAX_DEPTH (196 * BOAT_ONE)
#define BOAT_REST_DEPTH (80 * BOAT_ONE)

typedef struct {
    int32_t depth[BOAT_COLUMNS];
    /* Q16 interface displacement per 10 ms step; depth/boat coordinates Q8. */
    int32_t flow[BOAT_COLUMNS + 1u];
    int32_t x, y, vx, vy, slope, spin;
} boat_physics_t;

void boat_physics_init(boat_physics_t *state);
/* Q8 gravity components, screen right/down; moderate tilt only. */
int boat_physics_step(boat_physics_t *state, int32_t gravity_x, int32_t gravity_y);
int32_t boat_surface(const boat_physics_t *state, int32_t x_q8);

#endif
