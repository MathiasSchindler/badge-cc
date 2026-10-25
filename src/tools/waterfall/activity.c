#include "activity.h"
#include "../../shared/bytes.h"
#include "../diagnostics_logic.h"

uint16_t activity_color(uint32_t count) {
    static const uint16_t colors[] = {
        0x0841u, 0x0011u, 0x025fu, 0x07ffu, 0x07e0u, 0xffe0u, 0xfd20u, 0xf800u
    };
    uint32_t index = 0u;
    while (count && index < 7u) { ++index; count >>= 1u; }
    return colors[index];
}

uint16_t activity_rate(uint16_t count, uint32_t elapsed_us) {
    uint32_t ms = elapsed_us / 1000u;
    if (!ms) return 0u;
    uint32_t rate = ((uint32_t)count * 1000u + ms / 2u) / ms;
    return (uint16_t)(rate > 0xffffu ? 0xffffu : rate);
}

void activity_button(activity_button_t *b, uint8_t pressed, uint32_t now_us) {
    if (pressed != b->raw) { b->raw = pressed; b->changed_at = now_us; }
    if (b->stable != b->raw && now_us - b->changed_at >= 20000u) {
        b->stable = b->raw;
        if (b->stable) b->fast ^= 1u;
    }
}

int activity_frame(activity_t *a, const uint8_t *packet, uint32_t length, uint32_t channel) {
    if (channel < 1u || channel > ACT_CHANNELS || length < 32u ||
        length > CLEAN_RX_PACKET_BYTES || packet[14] != 0x20u ||
        (packet[16] & 15u) != channel ||
        diag_crc32(packet + 18u, length - 22u) != badger_bytes_load_u32(packet + length - 4u)) {
        ++a->rejected;
        return 0;
    }
    if (a->counts[channel - 1u] != 0xffffu) ++a->counts[channel - 1u];
    ++a->frames;
    if (length <= sizeof(a->sample)) {
        a->sample_length = length;
        badger_bytes_copy(a->sample, packet, length);
    }
    return 1;
}

void activity_commit(activity_t *a) {
    for (uint32_t row = ACT_ROWS - 1u; row; --row)
        for (uint32_t ch = 0u; ch < ACT_CHANNELS; ++ch)
            a->history[row][ch] = a->history[row - 1u][ch];
    for (uint32_t ch = 0u; ch < ACT_CHANNELS; ++ch) {
        a->history[0][ch] = a->rates[ch];
        a->counts[ch] = 0u;
        a->rates[ch] = 0u;
    }
    ++a->sweeps;
}

void activity_progress(badger_canvas_t *canvas, const activity_t *a, uint32_t completed) {
    for (uint32_t ch = 0u; ch < ACT_CHANNELS; ++ch) {
        uint16_t color = ch < completed ? activity_color(a->rates[ch]) : 0x4208u;
        badger_gfx_rect(canvas, (int32_t)(ACT_X + ch * ACT_CELL), ACT_Y,
                        ACT_CELL - 1u, 1, color);
        badger_gfx_rect(canvas, (int32_t)(ACT_X + ch * ACT_CELL), ACT_Y - 3u,
                        ACT_CELL - 1u, 2, ch + 1u == completed ? 0xffffu : 0x0841u);
    }
}

static void number(badger_canvas_t *canvas, uint32_t x, uint32_t y, uint32_t value) {
    char text[12];
    diag_decimal(text, (int32_t)(value % 1000000000u));
    badger_gfx_text(canvas, (int32_t)x, (int32_t)y, text, 1u, 0xffffu);
}

void activity_render(badger_canvas_t *canvas, const activity_t *a, const char *status) {
    badger_gfx_clear(canvas, 0x0841u);
    badger_gfx_text(canvas, 12, 5, "WLAN ACTIVITY", 2u, 0xffffu);
    badger_gfx_text(canvas, 12, 25, a->dwell_us == ACT_FAST_DWELL_US ?
                        "FRAMES/S - FAST 50MS - ENTER: SLOW" :
                        "FRAMES/S - SLOW 150MS - ENTER: FAST", 1u, 0x9cf3u);
    for (uint32_t ch = 0u; ch < ACT_CHANNELS; ++ch) {
        number(canvas, ACT_X + ch * ACT_CELL + 7u, 41u, ch + 1u);
        for (uint32_t row = 0u; row < ACT_ROWS; ++row)
            badger_gfx_rect(canvas, (int32_t)(ACT_X + ch * ACT_CELL),
                            (int32_t)(ACT_Y + row), ACT_CELL - 1u, 1,
                            activity_color(a->history[row][ch]));
    }
    badger_gfx_text(canvas, 12, 209, "2412", 1u, 0x9cf3u);
    badger_gfx_text(canvas, 87, 209, "NEWEST AT TOP", 1u, 0x9cf3u);
    badger_gfx_text(canvas, 257, 209, "2462 MHZ", 1u, 0x9cf3u);
    badger_gfx_text(canvas, 12, 222, status, 1u, 0xffffu);
    number(canvas, 110u, 222u, a->frames);
    badger_gfx_text(canvas, 178, 222, "SWEEP", 1u, 0x9cf3u);
    number(canvas, 215u, 222u, a->sweeps);
    for (uint32_t i = 0u; i < 8u; ++i)
        badger_gfx_rect(canvas, 259 + (int32_t)i * 6, 223, 6, 6,
                        activity_color(i ? 1u << (i - 1u) : 0u));
}
