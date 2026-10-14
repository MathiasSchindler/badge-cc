#include "boat_fluid.h"
#include "bytes.h"
#include "../tools/diagnostics_logic.h"

#define NONE 0xffffu
#define REST_DENSITY 160
#define RELAXATIONS 3u
#define MIN_X (3 * BOAT_ONE)
#define MAX_X (316 * BOAT_ONE)
#define MIN_Y (32 * BOAT_ONE)
#define MAX_Y (225 * BOAT_ONE)

static int32_t clamp(int32_t v, int32_t low, int32_t high) {
    return v < low ? low : v > high ? high : v;
}

static void grid(boat_fluid_t *s) {
    for (uint32_t i = 0u; i < BOAT_FLUID_CELLS; ++i) s->head[i] = NONE;
    for (uint32_t i = 0u; i < BOAT_FLUID_PARTICLES; ++i) {
        boat_fluid_particle_t *p = s->particle + i;
        uint32_t cell = (uint32_t)p->x / (BOAT_FLUID_RADIUS * BOAT_ONE) +
            32u * ((uint32_t)p->y / (BOAT_FLUID_RADIUS * BOAT_ONE));
        p->next = s->head[cell];
        s->head[cell] = (uint16_t)i;
    }
}

static int32_t weight(const boat_fluid_t *s, int32_t dx, int32_t dy, int32_t *distance) {
    if (dx <= -BOAT_FLUID_RADIUS * BOAT_ONE || dx >= BOAT_FLUID_RADIUS * BOAT_ONE ||
        dy <= -BOAT_FLUID_RADIUS * BOAT_ONE || dy >= BOAT_FLUID_RADIUS * BOAT_ONE) return 0;
    uint32_t square = (uint32_t)(dx * dx + dy * dy);
    if (square >= BOAT_FLUID_RADIUS * BOAT_FLUID_RADIUS * BOAT_ONE * BOAT_ONE) return 0;
    int32_t d = s->distance[square / 4096u];
    if (d < 64) d = 64;
    *distance = d;
    return BOAT_ONE - d / BOAT_FLUID_RADIUS;
}

static void neighbors(const boat_fluid_t *s, uint32_t i, int viscosity,
    int density_pass, boat_fluid_t *out) {
    const boat_fluid_particle_t *p = s->particle + i;
    int32_t cx = p->x / (BOAT_FLUID_RADIUS * BOAT_ONE);
    int32_t cy = p->y / (BOAT_FLUID_RADIUS * BOAT_ONE);
    uint32_t rho = 0u, near = 0u;
    for (int32_t yy = cy - 1; yy <= cy + 1; ++yy) {
        if (yy < 0 || yy >= 24) continue;
        for (int32_t xx = cx - 1; xx <= cx + 1; ++xx) {
            if (xx < 0 || xx >= 32) continue;
            uint32_t j = s->head[yy * 32 + xx];
            while (j != NONE) {
                const boat_fluid_particle_t *other = s->particle + j;
                if (j != i && (density_pass || j > i)) {
                    int32_t dx = other->x - p->x, dy = other->y - p->y, distance;
                    int32_t q = weight(s, dx, dy, &distance);
                    if (q > 0) {
                        if (density_pass) {
                            rho += (uint32_t)(q * q / BOAT_ONE);
                            near += (uint32_t)(q * q * q / (BOAT_ONE * BOAT_ONE));
                        } else {
                            int32_t amount_x, amount_y;
                            if (viscosity) {
                                amount_x = q * (other->vx - p->vx) / 8192;
                                amount_y = q * (other->vy - p->vy) / 8192;
                            } else {
                                int32_t force = q * ((s->pressure[i] + s->pressure[j]) / 2 +
                                    q * (s->near_pressure[i] + s->near_pressure[j]) / 512) / BOAT_ONE;
                                force = clamp(force, -128, 128);
                                if (dx == 0 && dy == 0) {
                                    dx = (i + j) % 2u ? 64 : -64;
                                    distance = 64;
                                }
                                amount_x = -dx * force / (2 * distance);
                                amount_y = -dy * force / (2 * distance);
                            }
                            out->dx[i] += amount_x; out->dy[i] += amount_y;
                            out->dx[j] -= amount_x; out->dy[j] -= amount_y;
                        }
                    }
                }
                j = other->next;
            }
        }
    }
    if (density_pass) {
        out->density[i] = rho;
        out->pressure[i] = clamp(((int32_t)rho - REST_DENSITY) * 768 / 256, -16, 512);
        out->near_pressure[i] = clamp((int32_t)near, 0, 256);
    }
}

void boat_fluid_init(boat_fluid_t *s) {
    badger_bytes_zero((uint8_t *)s, (uint32_t)sizeof(*s));
    s->x = 160 * BOAT_ONE;
    s->y = 148 * BOAT_ONE;
    for (uint32_t i = 0u; i < BOAT_FLUID_DISTANCE; ++i)
        s->distance[i] = (uint16_t)diag_sqrt(i * 4096u);
    for (uint32_t i = 0u; i < BOAT_FLUID_PARTICLES; ++i) {
        s->particle[i].x = 1856 + (int32_t)(i % BOAT_FLUID_COLUMNS) * 1664;
        s->particle[i].y = MAX_Y - (int32_t)(i / BOAT_FLUID_COLUMNS) * 1664;
    }
    grid(s);
}

static int32_t immersion(const boat_fluid_t *s, int32_t x, int32_t y) {
    int32_t total = 0;
    for (int32_t level = -6; level <= 6; level += 4) {
        int32_t py = y + level * BOAT_ONE;
        int32_t cx = x / (BOAT_FLUID_RADIUS * BOAT_ONE);
        int32_t cy = py / (BOAT_FLUID_RADIUS * BOAT_ONE);
        int32_t density = 0;
        for (int32_t yy = cy - 1; yy <= cy + 1; ++yy) {
            if (yy < 0 || yy >= 24) continue;
            for (int32_t xx = cx - 1; xx <= cx + 1; ++xx) {
                if (xx < 0 || xx >= 32) continue;
                uint32_t i = s->head[yy * 32 + xx];
                while (i != NONE) {
                    const boat_fluid_particle_t *p = s->particle + i;
                    int32_t distance, q = weight(s, p->x - x, p->y - py, &distance);
                    density += q * q / BOAT_ONE;
                    i = p->next;
                }
            }
        }
        total += clamp(density * BOAT_ONE / 256, 0, BOAT_ONE);
    }
    return total;
}

static int32_t current(const boat_fluid_t *s, int32_t x, int32_t y) {
    int32_t cx = x / (BOAT_FLUID_RADIUS * BOAT_ONE);
    int32_t cy = y / (BOAT_FLUID_RADIUS * BOAT_ONE);
    int32_t velocity = 0, weights = 0;
    for (int32_t yy = cy - 1; yy <= cy + 1; ++yy) {
        if (yy < 0 || yy >= 24) continue;
        for (int32_t xx = cx - 1; xx <= cx + 1; ++xx) {
            if (xx < 0 || xx >= 32) continue;
            uint32_t i = s->head[yy * 32 + xx];
            while (i != NONE) {
                const boat_fluid_particle_t *p = s->particle + i;
                int32_t distance, q = weight(s, p->x - x, p->y - y, &distance);
                velocity += p->vx * q;
                weights += q;
                i = p->next;
            }
        }
    }
    return weights != 0 ? velocity / weights : 0;
}

int boat_fluid_step(boat_fluid_t *s, int32_t gx, int32_t gy) {
    if (s == 0 || gx < -96 || gx > 96 || gy < 192 || gy > 256) return 0;
    if (s->x < 20 * BOAT_ONE || s->x > 300 * BOAT_ONE ||
        s->y < 16 * BOAT_ONE || s->y > BOAT_BOTTOM * BOAT_ONE ||
        s->vx < -256 || s->vx > 256 || s->vy < -512 || s->vy > 512 ||
        s->slope < -128 || s->slope > 128 || s->spin < -8 || s->spin > 8) return 0;
    if (s->gravity_fraction_x <= -64 || s->gravity_fraction_x >= 64 ||
        s->gravity_fraction_y <= -64 || s->gravity_fraction_y >= 64) return 0;
    for (uint32_t i = 0u; i < BOAT_FLUID_PARTICLES; ++i) {
        const boat_fluid_particle_t *p = s->particle + i;
        if (p->x < MIN_X || p->x > MAX_X || p->y < MIN_Y || p->y > MAX_Y ||
            p->vx < -512 || p->vx > 512 || p->vy < -512 || p->vy > 512) return 0;
    }
    int32_t ax = (s->gravity_fraction_x + gx) / 64;
    int32_t ay = (s->gravity_fraction_y + gy) / 64;
    s->gravity_fraction_x += gx - ax * 64;
    s->gravity_fraction_y += gy - ay * 64;
    for (uint32_t i = 0u; i < BOAT_FLUID_PARTICLES; ++i) {
        boat_fluid_particle_t *p = s->particle + i;
        s->old_x[i] = p->x; s->old_y[i] = p->y;
        p->vx = clamp(p->vx - p->vx / 500 + ax, -512, 512);
        p->vy = clamp(p->vy - p->vy / 500 + ay, -512, 512);
        int32_t x = p->x + p->vx, y = p->y + p->vy;
        if ((x < MIN_X || x > MAX_X || y < MIN_Y || y > MAX_Y) &&
            (p->vx < -96 || p->vx > 96 || p->vy < -96 || p->vy > 96)) {
            p->foam = 25u; ++s->impacts;
        } else if (p->foam != 0u) --p->foam;
        p->x = clamp(x, MIN_X, MAX_X);
        p->y = clamp(y, MIN_Y, MAX_Y);
    }
    for (uint32_t pass = 0u; pass < RELAXATIONS; ++pass) {
        grid(s);
        for (uint32_t i = 0u; i < BOAT_FLUID_PARTICLES; ++i) {
            s->dx[i] = s->dy[i] = 0;
            neighbors(s, i, 0, 1, s);
        }
        for (uint32_t i = 0u; i < BOAT_FLUID_PARTICLES; ++i) neighbors(s, i, 0, 0, s);
        for (uint32_t i = 0u; i < BOAT_FLUID_PARTICLES; ++i) {
            boat_fluid_particle_t *p = s->particle + i;
            p->x = clamp(p->x + clamp(s->dx[i], -64, 64), MIN_X, MAX_X);
            p->y = clamp(p->y + clamp(s->dy[i], -64, 64), MIN_Y, MAX_Y);
        }
    }
    grid(s);
    for (uint32_t i = 0u; i < BOAT_FLUID_PARTICLES; ++i) {
        boat_fluid_particle_t *p = s->particle + i;
        p->vx = clamp(p->x - s->old_x[i], -512, 512);
        p->vy = clamp(p->y - s->old_y[i], -512, 512);
        s->dx[i] = s->dy[i] = 0;
        if (s->density[i] < 120u && (p->vx > 96 || p->vx < -96 || p->vy > 96 || p->vy < -96))
            p->foam = 25u;
    }
    for (uint32_t i = 0u; i < BOAT_FLUID_PARTICLES; ++i) neighbors(s, i, 1, 0, s);
    for (uint32_t i = 0u; i < BOAT_FLUID_PARTICLES; ++i) {
        s->particle[i].vx = clamp(s->particle[i].vx + s->dx[i], -512, 512);
        s->particle[i].vy = clamp(s->particle[i].vy + s->dy[i], -512, 512);
    }
    int32_t left = immersion(s, s->x - 12 * BOAT_ONE, s->y - 12 * s->slope);
    int32_t right = immersion(s, s->x + 12 * BOAT_ONE, s->y + 12 * s->slope);
    int32_t buoyancy = (left + right) / 4;
    s->vy = clamp((s->vy + (BOAT_ONE - buoyancy) * gy / (BOAT_ONE * 64)) * 985 / 1000, -512, 512);
    s->y = clamp(s->y + s->vy, 16 * BOAT_ONE, BOAT_BOTTOM * BOAT_ONE);
    s->spin = clamp(s->spin + (left - right) / 128, -8, 8);
    s->spin -= s->spin / 4;
    s->slope = clamp(s->slope + s->spin, -128, 128);
    int32_t water_velocity = current(s, s->x, s->y + 4 * BOAT_ONE);
    s->vx = clamp(s->vx + (water_velocity - s->vx) / 16, -256, 256);
    s->x += s->vx;
    if (s->x < 20 * BOAT_ONE) { s->x = 20 * BOAT_ONE; s->vx = -s->vx / 2; }
    if (s->x > 300 * BOAT_ONE) { s->x = 300 * BOAT_ONE; s->vx = -s->vx / 2; }
    ++s->steps;
    return 1;
}

void boat_fluid_field(boat_fluid_t *s) {
    badger_bytes_zero((uint8_t *)s->field, (uint32_t)sizeof(s->field));
    badger_bytes_zero(s->foam_field, (uint32_t)sizeof(s->foam_field));
    for (uint32_t i = 0u; i < BOAT_FLUID_PARTICLES; ++i) {
        const boat_fluid_particle_t *p = s->particle + i;
        int32_t cx = p->x / 512, cy = p->y / 512;
        for (int32_t dy = -3; dy <= 3; ++dy) {
            int32_t y = cy + dy;
            if (y < 0 || y >= 114) continue;
            for (int32_t dx = -3; dx <= 3; ++dx) {
                int32_t x = cx + dx, contribution = 16 - dx * dx - dy * dy;
                if (x < 0 || x >= 160 || contribution <= 0) continue;
                uint32_t cell = (uint32_t)x * 120u + (uint32_t)y;
                s->field[cell] += (uint16_t)contribution;
                if (p->foam != 0u) s->foam_field[cell] = p->foam;
            }
        }
    }
}

void boat_fluid_render(boat_fluid_t *s, badger_canvas_t *canvas) {
    boat_fluid_field(s);
    for (uint32_t x = 0u; x < 160u; ++x) {
        uint16_t *a = canvas->pixels + x * 2u * 240u, *b = a + 240u;
        for (uint32_t y = 0u; y < 120u; ++y) {
            uint32_t at = x * 120u + y;
            uint16_t color;
            if (y >= 114u) color = 0x0843u;
            else if (s->field[at] >= 10u) {
                int edge = y == 0u || s->field[at - 1u] < 10u ||
                    s->field[at + 1u] < 10u ||
                    (x > 0u && s->field[at - 120u] < 10u) ||
                    (x < 159u && s->field[at + 120u] < 10u);
                if (edge && s->foam_field[at] != 0u) color = 0xe7ffu;
                else if (edge) color = 0x6e9cu;
                else {
                    uint32_t light = y < 75u ? 0u : (y - 75u) / 6u;
                    uint32_t green = 37u - light, blue = 25u - light / 2u;
                    color = (uint16_t)((2u << 11u) | (green << 5u) | blue);
                }
            } else {
                uint32_t level = y / 8u;
                color = (uint16_t)(((12u + level / 2u) << 11u) |
                    ((44u + level) << 5u) | 29u);
            }
            a[y * 2u] = a[y * 2u + 1u] = color;
            b[y * 2u] = b[y * 2u + 1u] = color;
        }
    }
    /* A sparse, fixed subset of advected particles reveals interior flow. */
    for (uint32_t i = 0u; i < BOAT_FLUID_PARTICLES; i += 17u) {
        const boat_fluid_particle_t *p = s->particle + i;
        uint32_t x = (uint32_t)p->x / BOAT_ONE, y = (uint32_t)p->y / BOAT_ONE;
        if (y < BOAT_BOTTOM && s->field[(x / 2u) * 120u + y / 2u] >= 10u)
            canvas->pixels[x * 240u + y] = 0x3dfau;
    }
}
