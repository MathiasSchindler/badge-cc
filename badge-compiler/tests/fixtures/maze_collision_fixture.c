#define TILT_MAZE_HOST_TEST
#include "../../../src/tools/tilt_maze.c"

static void (*volatile collision)(int32_t, int32_t, int32_t, int32_t) = collide_wall;

int main(void) {
    static const int32_t cases[][8] = {
        {3072, 5120, 73, 11, 3072, 5120, 73, 11},
        {3584, 5120, 73, 11, 3070, 5120, 0, 11},
        {3584, 5120, -73, 11, 3070, 5120, -73, 11},
        {3496, 3496, 42, 10, 3371, 3371, 16, -16},
        {3296, 3296, 42, 10, 3296, 3296, 42, 10},
        {5120, 5120, 73, 11, 3072, 5120, 0, 11},
        {5120, 6044, -29, -15, 5120, 7168, -29, 0},
        {5120, 7168, -29, -15, 5120, 7168, -29, -15},
    };
    for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const int32_t *c = cases[i];
        marble_x = c[0];
        marble_y = c[1];
        velocity_x = c[2];
        velocity_y = c[3];
        collision(4096, 4096, 2048, 2048);
        if (marble_x != c[4] || marble_y != c[5] || velocity_x != c[6] || velocity_y != c[7])
            return (int)(i + 1);
    }
    return 0;
}
