#include "application.h"
#include "display.h"
#include "lsm6ds3.h"

extern void render_visual(void);
extern uint16_t screen_pixel(uint32_t x, uint32_t y);
#ifdef COMPILER_TEST_IMU_PREPARE_LABELS
extern void prepare_screen_labels(void);
#endif
extern int32_t filtered_x, filtered_y, filtered_z, retained_sin_roll, retained_cos_roll;
extern uint8_t identity;
extern int read_ok;
extern lsm6ds3_sample_t sample;
extern uint16_t visual[16500];

static struct {
    uint32_t before;
    uint16_t pixels[BADGER_DISPLAY_WIDTH * BADGER_DISPLAY_HEIGHT];
    uint32_t after;
} panel;
static uint32_t input_seed, written, render_digest;
static const uint8_t coordinates[6][4] = {
    {39,82,72,14}, {39,103,72,14}, {39,124,72,14},
    {27,180,36,7}, {27,195,36,7}, {27,210,36,7}
};
static const uint32_t counts[6] = {8u,76800u,76800u,3780u,76800u,3780u};

static uint32_t mix(uint32_t hash, uint32_t value) {
    return (hash ^ value) * 16777619u;
}
static void pixels(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h) {
    for (uint32_t x = x0; x < x0 + w; x++)
        for (uint32_t y = y0; y < y0 + h; y++)
            panel.pixels[written++] = screen_pixel(x, y);
}
static void transfer(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h) {
    badger_display_begin_region(x0, y0, w, h);
    uint32_t end = written + w * h;
    while (written < end) badger_display_write(panel.pixels[written++]);
    badger_display_end_update();
}
void compiler_application_prepare(uint32_t module, uint32_t seed) {
    input_seed = seed;
    written = 0u;
    render_digest = 2166136261u;
    panel.before = 0x1234abcdu;
    panel.after = 0xedcb5432u;
    identity = 0x6au;
    read_ok = module != 2u;
    sample.accel_x = (int16_t)((int32_t)(seed % 256u) - 310);
    sample.accel_y = 1004;
    sample.accel_z = 265;
    sample.gyro_x = -1045;
    sample.gyro_y = 23;
    sample.gyro_z = 389;
    retained_sin_roll = 0;
    retained_cos_roll = 1024;
    if (module >= 1u && module <= 3u) {
        for (uint32_t i = 0u; i < 16500u; i++) visual[i] = (uint16_t)(seed + i * 3u);
        for (uint32_t i = 0u; i < counts[module]; i++) panel.pixels[i] = 0x55aau;
    } else if (module >= 4u && module < 6u) {
        for (uint32_t i = 0u; i < counts[module]; i++) panel.pixels[i] = (uint16_t)(seed + i * 271u);
    }
}
void compiler_application_run(uint32_t module) {
#ifdef COMPILER_TEST_IMU_PREPARE_LABELS
    if (module >= 1u && module <= 3u) prepare_screen_labels();
#endif
    if (module == 0u) {
        for (uint32_t i = 0u; i < counts[0]; i++) {
            filtered_x = (int32_t)((input_seed + i * 251u) % 2049u) - 1024;
            filtered_y = (int32_t)((input_seed + i * 509u) % 2049u) - 1024;
            filtered_z = i == 0u ? 0 : -1024;
            render_visual();
            render_digest = mix(render_digest, visual[(input_seed + i * 199u) % 16500u]);
            written++;
        }
    } else if (module < 3u)
        pixels(0u, 0u, BADGER_DISPLAY_WIDTH, BADGER_DISPLAY_HEIGHT);
    else if (module == 4u)
        transfer(0u, 0u, BADGER_DISPLAY_WIDTH, BADGER_DISPLAY_HEIGHT);
    else if (module < 6u) {
        for (uint32_t i = 0u; i < 6u; i++) {
            const uint8_t *r = coordinates[i];
            if (module == 3u) pixels(r[0], r[1], r[2], r[3]);
            else transfer(r[0], r[1], r[2], r[3]);
        }
    }
}
int compiler_application_result(uint32_t module, compiler_result_t *out) {
    if (module >= 6u || written != counts[module]) return 0;
    uint32_t digest = render_digest;
    if (module == 0u) {
        for (uint32_t i = 0u; i < 16500u; i++) digest = mix(digest, visual[i]);
        digest = mix(mix(digest, (uint32_t)retained_sin_roll), (uint32_t)retained_cos_roll);
    } else {
        for (uint32_t i = 0u; i < written; i++) {
            if (module >= 4u && panel.pixels[i] != (uint16_t)(input_seed + i * 271u)) return 0;
            digest = mix(digest, panel.pixels[i]);
        }
    }
    uint32_t guards = panel.before == 0x1234abcdu && panel.after == 0xedcb5432u;
    *out = (compiler_result_t){written, {digest, module, input_seed, 0u, guards, 0u}};
    return guards != 0u;
}
int compiler_application_measure(uint32_t module, uint32_t seed, compiler_result_t *out,
    uint32_t (*clock)(void *), void (*service)(void *), void *context, uint32_t samples[7]) {
    if (module >= 6u) return 0;
    for (uint32_t i = 0u; i < 7u; i++) samples[i] = 0u;
    compiler_application_prepare(module, seed);
    compiler_application_run(module);
    if (!compiler_application_result(module, out)) return 0;
    for (uint32_t i = 0u; i < 7u; i++) {
        compiler_result_t measured;
        service(context);
        compiler_application_prepare(module, seed);
        uint32_t before = clock(context);
        compiler_application_run(module);
        samples[i] = clock(context) - before;
        if (samples[i] < 100u || samples[i] > 4000000u ||
            !compiler_application_result(module, &measured)) return 0;
        for (uint32_t lane = 0u; lane < 6u; lane++)
            if (measured.values[lane] != out->values[lane]) return 0;
    }
    return 1;
}
