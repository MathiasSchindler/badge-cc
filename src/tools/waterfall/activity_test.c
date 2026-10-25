#include <stdio.h>
#include "activity.h"
#include "../../shared/bytes.h"
#include "../diagnostics_logic.h"

#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static activity_t state;
static badger_canvas_t canvas;

int main(int argc, char **argv) {
    uint8_t packet[64] = {0};
    packet[14] = 0x20u; packet[16] = 6u;
    badger_bytes_store_u32(packet + 60u, diag_crc32(packet + 18u, 42u));
    CHECK(activity_frame(&state, packet, sizeof(packet), 6u));
    CHECK(state.frames == 1u && state.counts[5] == 1u);
    CHECK(!activity_frame(&state, packet, sizeof(packet), 5u));
    packet[20] ^= 1u;
    CHECK(!activity_frame(&state, packet, sizeof(packet), 6u));
    CHECK(!activity_frame(&state, packet, 10u, 6u));
    CHECK(state.rejected == 3u);
    state.rates[5] = activity_rate(state.counts[5], 150000u);
    activity_commit(&state);
    CHECK(state.sweeps == 1u && state.history[0][5] == 7u && !state.counts[5]);
    activity_commit(&state);
    CHECK(state.history[1][5] == 7u && !state.history[0][5]);
    state.counts[0] = 0xffffu;
    packet[20] ^= 1u; packet[16] = 1u;
    CHECK(activity_frame(&state, packet, sizeof(packet), 1u));
    CHECK(state.counts[0] == 0xffffu);
    CHECK(activity_color(0u) != activity_color(1u));
    CHECK(activity_color(64u) == activity_color(0xffffffffu));
    for (uint32_t n = 0u; n < ACT_ROWS + 10u; ++n) activity_commit(&state);
    activity_render(&canvas, &state, "RX FRAMES");
    CHECK(canvas.pixels[ACT_X * 240u + ACT_Y] == activity_color(0u));
    CHECK(activity_rate(3u, 150000u) == 20u);
    CHECK(activity_rate(1u, 50000u) == 20u);
    CHECK(activity_rate(0xffffu, 50000u) == 0xffffu);
    CHECK(activity_rate(1u, 0u) == 0u);
    CHECK(activity_rate(1u, 50001u) == 20u);
    state.rates[0] = 20u;
    activity_progress(&canvas, &state, 1u);
    CHECK(canvas.pixels[ACT_X * 240u + ACT_Y] == activity_color(20u));
    CHECK(canvas.pixels[(ACT_X + ACT_CELL) * 240u + ACT_Y] == 0x4208u);
    CHECK(canvas.pixels[ACT_X * 240u + ACT_Y - 3u] == 0xffffu);
    activity_button_t button = {0};
    activity_button(&button, 1u, 100u);
    activity_button(&button, 0u, 1000u);
    activity_button(&button, 1u, 2000u);
    activity_button(&button, 1u, 21999u); CHECK(!button.fast);
    activity_button(&button, 1u, 22000u); CHECK(button.fast);
    activity_button(&button, 1u, 500000u); CHECK(button.fast);
    activity_button(&button, 0u, 600000u);
    activity_button(&button, 0u, 620000u);
    activity_button(&button, 1u, 0xfffffff0u);
    activity_button(&button, 1u, 20000u); CHECK(!button.fast);
    if (argc == 2) {
        for (uint32_t r = 0u; r < ACT_ROWS; ++r)
            for (uint32_t ch = 0u; ch < ACT_CHANNELS; ++ch)
                state.history[r][ch] = (uint16_t)((r / 8u + ch) % 90u);
        activity_render(&canvas, &state, "SYNTHETIC TEST");
        FILE *f = fopen(argv[1], "wb");
        if (!f) return 1;
        if (fprintf(f, "P6\n320 240\n255\n") < 0) return 1;
        for (uint32_t y = 0u; y < 240u; ++y)
            for (uint32_t x = 0u; x < 320u; ++x) {
                uint16_t p = canvas.pixels[x * 240u + y];
                uint8_t rgb[] = {(uint8_t)(((p >> 11u) & 31u) * 255u / 31u),
                    (uint8_t)(((p >> 5u) & 63u) * 255u / 63u), (uint8_t)((p & 31u) * 255u / 31u)};
                if (fwrite(rgb, 1u, 3u, f) != 3u) return 1;
            }
        if (fclose(f)) return 1;
    }
    return 0;
}
