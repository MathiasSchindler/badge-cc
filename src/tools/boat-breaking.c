#define BOAT_BREAKING
#include "../shared/boat_fluid.h"
#define boat_physics_init boat_fluid_init
#define boat_physics_step boat_fluid_step
#include "boat.c"
