#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../../shared/boat_fluid.h"
#include "boat_fluid_topology.h"

static boat_fluid_t water;
static badger_canvas_t canvas;

static int image(const char *path) {
    FILE *out = fopen(path, "wb");
    if (out == NULL) { perror(path); return 0; }
    int ok = fprintf(out, "P6\n320 240\n255\n") > 0;
    for (uint32_t y = 0u; y < 240u && ok; ++y) {
        for (uint32_t x = 0u; x < 320u; ++x) {
            uint16_t p = canvas.pixels[x * 240u + y];
            uint8_t rgb[3] = {(uint8_t)(((p >> 11u) & 31u) * 255u / 31u),
                (uint8_t)(((p >> 5u) & 63u) * 255u / 63u), (uint8_t)((p & 31u) * 255u / 31u)};
            if (fwrite(rgb, 1u, 3u, out) != 3u) { ok = 0; break; }
        }
    }
    if (fclose(out) != 0) ok = 0;
    if (!ok) fprintf(stderr, "Preview write failed: %s\n", path);
    return ok;
}

int main(int argc, char **argv) {
    if (argc != 4 || (strcmp(argv[1], "rest") && strcmp(argv[1], "dam") && strcmp(argv[1], "tilt"))) {
        fputs("Usage: boat_fluid_preview rest|dam|tilt STEPS output.ppm\n", stderr); return 2;
    }
    char *end;
    unsigned long steps = strtoul(argv[2], &end, 10);
    if (*argv[2] == 0 || *end != 0 || steps > 60000u) { fputs("Invalid steps (0..60000)\n", stderr); return 2; }
    boat_fluid_init(&water);
    if (!strcmp(argv[1], "dam")) {
        for (uint32_t i = 0u; i < BOAT_FLUID_PARTICLES; ++i) {
            water.particle[i].x = 1856 + (int32_t)(i % 24u) * 1664;
            water.particle[i].y = 225 * BOAT_ONE - (int32_t)(i / 24u) * 1664;
        }
    }
    clock_t started = clock();
    for (uint32_t step = 0u; step < steps; ++step) {
        int32_t gx = strcmp(argv[1], "tilt") ? 0 : step % 1000u < 500u ? 96 : -96;
        if (!boat_fluid_step(&water, gx, gx == 0 ? 256 : 240)) {
            fprintf(stderr, "Fluid rejected at step %u\n", step); return 1;
        }
    }
    boat_fluid_render(&water, &canvas);
    printf("scene=%s steps=%lu host_cpu_s=%.3f state_bytes=%zu particles=%u impacts=%u overhang_columns=%u\n",
        argv[1], steps, (double)(clock() - started) / CLOCKS_PER_SEC,
        sizeof(water), BOAT_FLUID_PARTICLES, water.impacts, boat_fluid_overhangs(&water));
    return image(argv[3]) ? 0 : 1;
}
