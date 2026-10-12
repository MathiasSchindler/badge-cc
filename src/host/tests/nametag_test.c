#include "../../shared/nametag.h"
#include "../../shared/frame_diff.h"
#include "nametag_fonts.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static badger_canvas_t canvas, reference, background;
static badger_nametag_workspace_t workspace;
static badger_nametag_scene_t active, pending, saved;
static badger_nametag_cache_t cache;
static badger_frame_t diff_frame;
static badger_frame_presenter_t presenter;
static badger_canvas_t panel;
static uint8_t coverage[BADGER_GFX_WIDTH * BADGER_GFX_HEIGHT];
static uint32_t writes, fail_at;

static int panel_write(void *context, const badger_canvas_t *source,
                         const badger_frame_region_t *region) {
    (void)context;
    assert(region->width > 0u && region->height > 0u);
    assert(region->x + region->width <= BADGER_GFX_WIDTH);
    assert(region->y + region->height <= BADGER_GFX_HEIGHT);
    ++writes;
    int fail = writes == fail_at;
    uint32_t rows = fail ? (region->height + 1u) / 2u : region->height;
    for (uint32_t x = region->x; x < region->x + region->width; ++x) {
        for (uint32_t y = region->y; y < region->y + rows; ++y) {
            uint32_t index = x * BADGER_GFX_HEIGHT + y;
            assert(coverage[index]++ == 0u);
            panel.pixels[index] = source->pixels[index];
        }
    }
    return !fail;
}

static badger_frame_result_t update_panel(badger_frame_stats_t *stats) {
    memset(coverage, 0, sizeof(coverage));
    writes = 0u;
    badger_frame_result_t result = badger_frame_present(&presenter, &diff_frame, panel_write, 0, stats);
    if (result == BADGER_FRAME_OK) {
        assert(stats->regions == writes);
        assert(stats->pixels <= BADGER_GFX_WIDTH * BADGER_GFX_HEIGHT);
        assert(memcmp(&panel, &diff_frame.canvas, sizeof(panel)) == 0);
        assert(memcmp(&presenter.previous.canvas, &diff_frame.canvas, sizeof(panel)) == 0);
    }
    return result;
}

static uint32_t encode(uint32_t codepoint, char text[4]) {
    if (codepoint < 0x80u) { text[0] = (char)codepoint; return 1u; }
    if (codepoint < 0x800u) {
        text[0] = (char)(0xc0u | (codepoint >> 6u));
        text[1] = (char)(0x80u | (codepoint & 63u));
        return 2u;
    }
    text[0] = (char)(0xe0u | (codepoint >> 12u));
    text[1] = (char)(0x80u | ((codepoint >> 6u) & 63u));
    text[2] = (char)(0x80u | (codepoint & 63u));
    return 3u;
}

static void test_utf8(void) {
    const uint8_t invalid[][5] = {
        {0}, {0xc0,0x80}, {0xc1,0x80}, {0xe0,0x80,0x80}, {0xf0,0x80,0x80,0x80},
        {0xed,0xa0,0x80}, {0xf4,0x90,0x80,0x80}, {0xf5,0x80,0x80,0x80},
        {0x80}, {0xff}, {0xc2,'A'}, {0xe2,0x82}, {0xf0,0x9f,0x92}
    };
    const uint32_t lengths[] = {1,2,2,3,4,3,4,4,1,1,2,2,3};
    for (uint32_t i = 0u; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
        uint32_t used = 99u, codepoint = 99u;
        assert(badger_utf8_next((const char *)invalid[i], lengths[i], &used, &codepoint) == BADGER_FONT_UTF8);
        assert(used == 99u && codepoint == 99u);
    }
    uint32_t used, codepoint;
    assert(badger_utf8_next("\xf0\x9f\x98\x80", 4u, &used, &codepoint) == BADGER_FONT_OK);
    assert(codepoint == 0x1f600u && used == 4u);
    assert(!badger_font_codepoint_supported(codepoint));
    assert(badger_utf8_next("", 0u, &used, &codepoint) == BADGER_FONT_ARGUMENT);
}

static void test_assets(void) {
    const badger_nametag_fonts_t *families[] = {&nametag_mono_fonts, &nametag_code_fonts};
    for (uint32_t family = 0u; family < 2u; ++family) {
        for (uint32_t number = 0u; number < 10u; ++number) {
            const badger_font_t *font = number < 4u ? families[family]->regular[number] : families[family]->bold[number - 4u];
            assert(font->glyph_count == 206u);
            assert(font->coverage_bits == 8u || font->coverage_bits == 4u);
            uint32_t count = 0u;
            for (uint32_t cp = 0x20u; cp <= 0x20acu; ++cp) {
                if (!badger_font_codepoint_supported(cp)) continue;
                const badger_font_glyph_t *glyph = badger_font_glyph(font, cp);
                assert(glyph != NULL && glyph->codepoint == cp);
                if (count != 0u) assert(font->glyphs[count - 1u].codepoint < glyph->codepoint);
                assert(glyph == &font->glyphs[count++]);
                assert(glyph->width <= 128u && glyph->height <= 128u);
                if (glyph->height) {
                    assert(glyph->top <= (int)font->ascent);
                    assert((int)font->ascent - glyph->top + glyph->height <= font->line_height);
                }
                char text[4];
                uint32_t length = encode(cp, text);
                badger_font_metrics_t metrics;
                assert(badger_font_measure(font, text, length, &metrics) == BADGER_FONT_OK);
                assert(metrics.advance == (cp == 0xadu ? 0 : glyph->advance));
            }
            assert(count == 206u);
            badger_font_metrics_t metrics;
            assert(badger_font_measure(font, "\xe2\x98\x83", 3u, &metrics) == BADGER_FONT_MISSING);
            assert(badger_font_measure(font, "ab\xc2\xad" "cd", 6u, &metrics) == BADGER_FONT_OK);
            assert(metrics.advance == badger_font_glyph(font, 'a')->advance * 4);
        }
    }
}

static void test_blending_and_clipping(void) {
    for (uint32_t value = 0u; value <= 65025u; ++value)
        assert(badger_font_alpha_round(value) == (value + 127u) / 255u);
    for (uint32_t alpha = 0u; alpha <= 255u; ++alpha) {
        for (uint32_t background = 0u; background < 64u; ++background) {
            for (uint32_t foreground = 0u; foreground < 64u; ++foreground) {
                uint32_t b = background & 31u, f = foreground & 31u;
                uint16_t expected = (uint16_t)((b * (255u - alpha) + f * alpha + 127u) / 255u);
                expected |= (uint16_t)(((background * (255u - alpha) + foreground * alpha + 127u) / 255u) << 5u);
                expected |= (uint16_t)(((b * (255u - alpha) + f * alpha + 127u) / 255u) << 11u);
                uint16_t bg = (uint16_t)((b << 11u) | (background << 5u) | b);
                uint16_t fg = (uint16_t)((f << 11u) | (foreground << 5u) | f);
                assert(badger_rgb565_blend(bg, fg, (uint8_t)alpha) == expected);
            }
        }
    }
    for (uint32_t background = 0u; background < 65536u; background += 137u) {
        assert(badger_rgb565_blend((uint16_t)background, 0xffffu, 0u) == background);
        assert(badger_rgb565_blend((uint16_t)background, 0xffffu, 255u) == 0xffffu);
        assert(badger_rgb565_blend((uint16_t)background, (uint16_t)background, 128u) == background);
    }
    const badger_font_glyph_t glyph = {65,0,2,-1,2,2,2};
    const uint8_t pixels[] = {255,128,64,0};
    const badger_font_t font = {&glyph,pixels,4,1,12,3,2,8,0};
    badger_font_metrics_t metrics;
    assert(badger_font_measure(&font, "A", 1u, &metrics) == BADGER_FONT_OK);
    assert(metrics.left == -1 && metrics.right == 2 && metrics.top == -2 && metrics.bottom == 0);
    const badger_font_clip_t clips[] = {
        {0,0,320,240}, {1,1,318,238}, {-20,-20,21,21}, {319,239,10,10},
        {INT_MAX,INT_MAX,INT_MAX,INT_MAX}
    };
    const int32_t positions[][2] = {{1,2},{0,1},{320,240},{319,239},{-100,-100},{INT_MAX,INT_MIN}};
    for (uint32_t c = 0u; c < sizeof(clips) / sizeof(clips[0]); ++c) {
        for (uint32_t p = 0u; p < sizeof(positions) / sizeof(positions[0]); ++p) {
            memset(&canvas, 0, sizeof(canvas));
            assert(badger_font_draw(&canvas, &font, "A", 1u, positions[p][0], positions[p][1],
                                   0xffffu, 255u, clips[c], 0) == BADGER_FONT_OK);
            for (int32_t x = 0; x < 320; ++x) for (int32_t y = 0; y < 240; ++y) {
                uint16_t expected = 0u;
                int64_t gx = (int64_t)x - positions[p][0] + 1;
                int64_t gy = (int64_t)y - positions[p][1] + 2;
                if (gx >= 0 && gx < 2 && gy >= 0 && gy < 2 &&
                    x >= clips[c].x && (int64_t)x < (int64_t)clips[c].x + clips[c].width &&
                    y >= clips[c].y && (int64_t)y < (int64_t)clips[c].y + clips[c].height) {
                    expected = badger_rgb565_blend(0u, 0xffffu, pixels[(uint32_t)gy * 2u + (uint32_t)gx]);
                }
                assert(canvas.pixels[x * 240 + y] == expected);
            }
        }
    }
    memset(&canvas, 0, sizeof(canvas));
    assert(badger_font_draw(&canvas, &font, "A", 1u, 1, 2, 0xffffu, 0u, clips[0], 1) == BADGER_FONT_OK);
    assert(canvas.pixels[0] == 0u);
    assert(badger_font_draw(&canvas, &font, "A", 1u, 1, 2, 0xffffu, 255u, clips[0], 1) == BADGER_FONT_OK);
    assert(canvas.pixels[0] == 0xffffu);
    assert(canvas.pixels[240] > badger_rgb565_blend(0u, 0xffffu, 128u));
    badger_font_glyph_t broken = glyph;
    broken.offset = 100u;
    badger_font_t broken_font = font;
    broken_font.glyphs = &broken;
    assert(badger_font_measure(&broken_font, "A", 1u, &metrics) == BADGER_FONT_ASSET);
    assert(badger_font_draw(&canvas, &font, "A", 1u, 0, 0, 0u, 255u,
                           (badger_font_clip_t){0,0,0,0}, 0) == BADGER_FONT_ARGUMENT);
}

static void test_profile_and_layout(void) {
    badger_nametag_error_t error;
    badger_nametag_profile_t profile = badger_nametag_default_profile;
    assert(badger_nametag_validate(&profile, &error) == BADGER_NAMETAG_OK);
    assert(badger_nametag_replace(&active, &pending, &profile, &nametag_mono_fonts, BADGER_NAMETAG_A, &error) == BADGER_NAMETAG_OK);
    saved = active;
    memset(profile.name, 'X', sizeof(profile.name));
    assert(badger_nametag_replace(&active, &pending, &profile, &nametag_mono_fonts, BADGER_NAMETAG_A, &error) == BADGER_NAMETAG_LIMIT);
    assert(memcmp(&active, &saved, sizeof(active)) == 0);
    profile = badger_nametag_default_profile;
    memset(profile.name, 0, sizeof(profile.name));
    strcpy(profile.name, "\xc0\x80");
    assert(badger_nametag_validate(&profile, &error) == BADGER_NAMETAG_ENCODING);
    strcpy(profile.name, "\xf0\x9f\x98\x80");
    assert(badger_nametag_validate(&profile, &error) == BADGER_NAMETAG_REPERTOIRE);
    memset(profile.name, 0, sizeof(profile.name));
    strcpy(profile.name, " \xc2\xa0\xc2\xad ");
    assert(badger_nametag_validate(&profile, &error) == BADGER_NAMETAG_EMPTY);
    profile = badger_nametag_default_profile;
    profile.name[30] = 'X';
    assert(badger_nametag_validate(&profile, &error) == BADGER_NAMETAG_ENCODING);
    profile = badger_nametag_default_profile;
    profile.interest_count = 9u;
    assert(badger_nametag_validate(&profile, &error) == BADGER_NAMETAG_LIMIT);
    profile = badger_nametag_default_profile;
    memset(profile.role, 'X', sizeof(profile.role) - 1u);
    assert(badger_nametag_replace(&active, &pending, &profile, &nametag_mono_fonts, BADGER_NAMETAG_A, &error) == BADGER_NAMETAG_FIT);
    assert(memcmp(&active, &saved, sizeof(active)) == 0);
    profile = badger_nametag_default_profile;
    for (uint32_t family = 0u; family < 2u; ++family) for (int variant = 0; variant < 3; ++variant) {
        const badger_nametag_fonts_t *fonts = family ? &nametag_code_fonts : &nametag_mono_fonts;
        assert(badger_nametag_prepare(&pending, &profile, fonts, (badger_nametag_variant_t)variant, &error) == BADGER_NAMETAG_OK);
        assert(memcmp(&pending.profile, &profile, sizeof(profile)) == 0);
        assert(pending.name.font->pixel_size >= 24u && pending.name.line_count == 2u);
        assert(pending.page_count > 0u && pending.page_count <= BADGER_NAMETAG_PAGES);
        for (uint32_t page = 0u; page < pending.page_count; ++page) {
            assert(pending.pages[page].font->pixel_size >= 12u);
            assert(pending.pages[page].font->line_height * pending.pages[page].line_count <= pending.band.height);
        }
    }
    memset(profile.name, 0, sizeof(profile.name));
    strcpy(profile.name, "Ada Lovelace");
    assert(badger_nametag_prepare(&pending, &profile, &nametag_mono_fonts, BADGER_NAMETAG_A, &error) == BADGER_NAMETAG_OK);
    assert(pending.name.line_count == 2u);
    memset(profile.name, 0, sizeof(profile.name));
    strcpy(profile.name, "Ada\xc2\xa0Lovelace");
    assert(badger_nametag_prepare(&pending, &profile, &nametag_mono_fonts, BADGER_NAMETAG_A, &error) == BADGER_NAMETAG_OK);
    assert(pending.name.line_count == 1u);
    memset(profile.name, 0, sizeof(profile.name));
    strcpy(profile.name, "ABCDEFGHIJKLMNOPQR\xc2\xadSTUVWXYZABCDEFGHIJ");
    assert(badger_nametag_prepare(&pending, &profile, &nametag_mono_fonts, BADGER_NAMETAG_A, &error) == BADGER_NAMETAG_OK);
    assert(pending.name.hyphens[0] == 1u);
    assert(strcmp(pending.profile.name, profile.name) == 0);
    memset(&profile, 0, sizeof(profile));
    strcpy(profile.name, "Ada");
    assert(badger_nametag_prepare(&pending, &profile, &nametag_mono_fonts, BADGER_NAMETAG_A, &error) == BADGER_NAMETAG_OK);
    assert(pending.page_count == 0u && badger_nametag_loop_ms(&pending) == 8000u);
}

static void test_font_spans(void) {
    const badger_font_glyph_t glyph = {65,0,1,-1,2,2,2};
    const uint8_t pixels[] = {255,136,68,0}, packed[] = {0xf8,0x40};
    for (uint32_t bits = 4u; bits <= 8u; bits += 4u) {
        badger_font_t font = {&glyph,bits == 8u ? pixels : packed,bits == 8u ? 4u : 2u,
                              1,12,3,2,(uint8_t)bits,0};
        for (int crisp = 0; crisp < 2; ++crisp) {
            uint8_t mask[12], expected_mask[12];
            memset(mask, 16, sizeof(mask));
            memset(expected_mask, 16, sizeof(expected_mask));
            assert(badger_font_mask(mask + 1, 3u, 3u, &font, "AA", 2u, 1, 2, crisp) == BADGER_FONT_OK);
            for (uint32_t opacity = 0u; opacity <= 255u; ++opacity) {
                memset(&canvas, 0x5a, sizeof(canvas));
                memcpy(&reference, &canvas, sizeof(canvas));
                assert(badger_font_draw(&canvas, &font, "AA", 2u, 1, 2, 0xd567u,
                    (uint8_t)opacity, (badger_font_clip_t){0,0,320,240}, crisp) == BADGER_FONT_OK);
                for (uint32_t letter = 0u; letter < 2u; ++letter) {
                    for (uint32_t y = 0u; y < 2u; ++y) {
                        for (uint32_t x = 0u; x < 2u; ++x) {
                            uint32_t alpha = pixels[y * 2u + x];
                            if (crisp) alpha = 255u - ((255u - alpha) * (255u - alpha) + 127u) / 255u;
                            uint32_t mask_index = 1u + y * 3u + letter + x;
                            if (alpha > expected_mask[mask_index]) expected_mask[mask_index] = (uint8_t)alpha;
                            alpha = (alpha * opacity + 127u) / 255u;
                            uint32_t index = (letter + x) * 240u + y;
                            reference.pixels[index] = badger_rgb565_blend(reference.pixels[index], 0xd567u, (uint8_t)alpha);
                        }
                    }
                }
                assert(memcmp(&canvas, &reference, sizeof(canvas)) == 0);
            }
            assert(memcmp(mask, expected_mask, sizeof(mask)) == 0);
        }
    }
}

static uint32_t reference_sample(const uint8_t *mask, uint32_t x, uint32_t y,
                                  uint32_t rows, int32_t shift) {
    int32_t source = (int32_t)(y * 256u) - shift;
    int32_t row = source / 256, fraction = source % 256;
    if (fraction < 0) { fraction += 256; --row; }
    uint32_t a = row >= 0 && row < (int32_t)rows ? mask[(uint32_t)row * 320u + x] : 0u;
    uint32_t b = row + 1 >= 0 && row + 1 < (int32_t)rows ? mask[(uint32_t)(row + 1) * 320u + x] : 0u;
    return (a * (256u - (uint32_t)fraction) + b * (uint32_t)fraction + 128u) / 256u;
}

static void test_animation(void) {
    badger_nametag_error_t error;
    badger_nametag_clock_t clock = {0};
    uint32_t elapsed;
    assert(badger_nametag_clock_update(&clock, 0xfffffff0u, 24000u, &elapsed) == BADGER_NAMETAG_OK && elapsed == 0u);
    assert(badger_nametag_clock_update(&clock, 20u, 24000u, &elapsed) == BADGER_NAMETAG_OK && elapsed == 36u);
    assert(badger_nametag_clock_update(&clock, 123476u, 24000u, &elapsed) == BADGER_NAMETAG_OK && elapsed == (36u + 123456u) % 24000u);
    assert(badger_nametag_clock_update(&clock, 0u, 0u, &elapsed) == BADGER_NAMETAG_ARGUMENT);
    badger_nametag_clock_us_t microclock = {0};
    uint32_t tick_us = 0xffffffc0u;
    uint64_t total_us = 0u;
    const uint32_t steps[] = {750u,250u,33333u,33333u,1u,999u,0xfffffff0u,8000000u};
    assert(badger_nametag_clock_update_us(&microclock, tick_us, 72000u, &elapsed) == BADGER_NAMETAG_OK);
    assert(elapsed == 0u);
    for (uint32_t repeat = 0u; repeat < 32u; ++repeat) {
        for (uint32_t i = 0u; i < sizeof(steps) / sizeof(steps[0]); ++i) {
            tick_us += steps[i];
            total_us += steps[i];
            assert(badger_nametag_clock_update_us(&microclock, tick_us, 72000u, &elapsed) == BADGER_NAMETAG_OK);
            assert(elapsed == (total_us / 1000u) % 72000u);
            assert(microclock.fraction_us == total_us % 1000u);
        }
    }
    const badger_nametag_clock_us_t preserved = microclock;
    assert(badger_nametag_clock_update_us(&microclock, tick_us, 0u, &elapsed) == BADGER_NAMETAG_ARGUMENT);
    assert(memcmp(&microclock, &preserved, sizeof(microclock)) == 0);
    assert(badger_nametag_clock_update_us(0, tick_us, 72000u, &elapsed) == BADGER_NAMETAG_ARGUMENT);
    assert(badger_nametag_clock_update_us(&microclock, tick_us, 72000u, 0) == BADGER_NAMETAG_ARGUMENT);
    assert(badger_nametag_clock_update_us(&microclock, tick_us, 0x80000000u, &elapsed) == BADGER_NAMETAG_ARGUMENT);
    assert(badger_nametag_wave(0u) == 0u && badger_nametag_wave(32768u) == 256u);
    for (uint32_t phase = 0u; phase < 65536u; phase += 137u) {
        assert(badger_nametag_wave(phase) == badger_nametag_wave(phase + 65536u));
        /* Equal phase moves toward smaller x+y as time advances. */
        assert(badger_nametag_wave(phase + 1920u + 300u * 192u) ==
               badger_nametag_wave(phase + 310u * 192u));
    }
    for (int variant = 0; variant < 3; ++variant) {
        assert(badger_nametag_prepare(&active, &badger_nametag_default_profile, &nametag_mono_fonts,
            (badger_nametag_variant_t)variant, &error) == BADGER_NAMETAG_OK);
        badger_nametag_phase_t phase = badger_nametag_phase(&active, 4999u);
        assert(phase.mix == 0u && phase.current == 0u);
        phase = badger_nametag_phase(&active, 5500u);
        assert(phase.mix == 128u && phase.current == 0u && phase.next == 1u);
        if (variant == BADGER_NAMETAG_B) {
            assert(phase.outgoing_y_q8 < -1024 && phase.incoming_y_q8 < 1024);
        }
        phase = badger_nametag_phase(&active, 6000u);
        assert(phase.mix == 0u && phase.current == 1u);
        assert(badger_nametag_render(&reference, &workspace, &active, 0u, 0) == BADGER_FONT_OK);
        uint32_t loop = badger_nametag_loop_ms(&active);
        assert(loop % 8000u == 0u && loop % (active.page_count * 6000u) == 0u);
        assert(badger_nametag_render(&canvas, &workspace, &active, loop, 0) == BADGER_FONT_OK);
        assert(memcmp(&canvas, &reference, sizeof(canvas)) == 0);
        for (uint32_t time = 5000u; time <= 6000u; time += 125u) {
            memset(&workspace, 255, sizeof(workspace));
            assert(badger_nametag_render(&canvas, &workspace, &active, time, 0) == BADGER_FONT_OK);
            assert(badger_nametag_background(&background, &active, time) == BADGER_FONT_OK);
            phase = badger_nametag_phase(&active, time);
            uint32_t rows = (uint32_t)active.band.height;
            for (uint32_t y = 0u; y < rows; ++y) {
                for (uint32_t x = 0u; x < 320u; ++x) {
                    uint32_t a = reference_sample(workspace.outgoing, x, y, rows, phase.outgoing_y_q8);
                    uint32_t b = phase.mix == 0u ? 0u :
                        reference_sample(workspace.incoming, x, y, rows, phase.incoming_y_q8);
                    uint8_t alpha = (uint8_t)((a * (255u - phase.mix) + b * phase.mix + 127u) / 255u);
                    uint32_t index = x * 240u + (uint32_t)active.band.y + y;
                    assert(canvas.pixels[index] == badger_rgb565_blend(background.pixels[index], 0xd567u, alpha));
                }
            }
            badger_font_clip_t clip = active.name.clip;
            for (int32_t x = clip.x; x < clip.x + clip.width; ++x) {
                for (int32_t y = clip.y; y < clip.y + clip.height; ++y) {
                    assert(canvas.pixels[x * 240 + y] == reference.pixels[x * 240 + y]);
                }
            }
        }
        assert(badger_nametag_render(&canvas, &workspace, &active, 12345u, 1) == BADGER_FONT_OK);
        memset(&canvas, 0xa5, sizeof(canvas));
        memset(&workspace, 0xa5, sizeof(workspace));
        assert(badger_nametag_render(&canvas, &workspace, &active, 0u, 0) == BADGER_FONT_OK);
        assert(memcmp(&canvas, &reference, sizeof(canvas)) == 0);
    }
    /* Identical pages must not darken at the midpoint of a true dissolve. */
    assert(badger_nametag_prepare(&active, &badger_nametag_default_profile, &nametag_mono_fonts, BADGER_NAMETAG_A, &error) == BADGER_NAMETAG_OK);
    active.pages[1] = active.pages[0];
    assert(badger_nametag_render(&canvas, &workspace, &active, 5500u, 0) == BADGER_FONT_OK);
    assert(memcmp(workspace.outgoing, workspace.incoming, 320u * (uint32_t)active.band.height) == 0);
}

static void test_cached_renderer(void) {
    const badger_nametag_fonts_t *families[] = {&nametag_mono_fonts, &nametag_code_fonts};
    const uint32_t times[] = {0,4999,5000,5001,5073,5500,5873,5999,6000,6001,
        11000,11500,11999,12000,17500,18000,47999,48000,53999,54000,59999,60000,
        235000,239999,240000,71999,72000,UINT_MAX,0,6000,5500,0,12000};
    badger_nametag_error_t error;
    assert(badger_nametag_cached_render(&canvas, &cache, 0u) == BADGER_FONT_ARGUMENT);
    for (uint32_t profile_id = 0u; profile_id < 5u; ++profile_id) {
        badger_nametag_profile_t profile = badger_nametag_default_profile;
        if (profile_id == 1u) {
            memset(&profile, 0, sizeof(profile));
            strcpy(profile.name, "Zo\xc3\xab Garc\xc3\xad" "a");
            strcpy(profile.role, "Policy");
            strcpy(profile.employer, "GitHub");
            strcpy(profile.username, "@zoe");
            strcpy(profile.location, "Z\xc3\xbcrich");
            strcpy(profile.interests[0], "open\xc2\xa0" "data");
            strcpy(profile.interests[1], "copy\xc2\xadright");
            profile.interest_count = 2u;
        } else if (profile_id == 4u) {
            for (uint32_t i = 0u; i < BADGER_NAMETAG_INTERESTS; ++i)
                strcpy(profile.interests[i], "Copyright and open data rights");
            profile.interest_count = BADGER_NAMETAG_INTERESTS;
        } else if (profile_id >= 2u) {
            memset(&profile, 0, sizeof(profile));
            strcpy(profile.name, "M");
            if (profile_id == 3u) {
                strcpy(profile.interests[0], "AI");
                profile.interest_count = 1u;
            }
        }
        for (uint32_t family = 0u; family < 2u; ++family) {
            for (int variant = 0; variant < 3; ++variant) {
                assert(badger_nametag_prepare(&active, &profile, families[family],
                    (badger_nametag_variant_t)variant, &error) == BADGER_NAMETAG_OK);
                if (profile_id == 4u && variant != BADGER_NAMETAG_C)
                    assert(active.page_count == BADGER_NAMETAG_PAGES);
                for (int crisp = 0; crisp < 2; ++crisp) {
                    memset(&cache, 0xa5, sizeof(cache));
                    assert(badger_nametag_cache_prepare(&cache, &canvas, &active, crisp) == BADGER_FONT_OK);
                    for (uint32_t i = 0u; i < sizeof(times) / sizeof(times[0]); ++i) {
                        memset(&canvas, 0xa5, sizeof(canvas));
                        assert(badger_nametag_cached_render(&canvas, &cache, times[i]) == BADGER_FONT_OK);
                        assert(badger_nametag_render(&reference, &workspace, &active, times[i], crisp) == BADGER_FONT_OK);
                        assert(memcmp(&canvas, &reference, sizeof(canvas)) == 0);
                        memcpy(&diff_frame.canvas, &canvas, sizeof(canvas));
                        badger_frame_stats_t stats;
                        assert(update_panel(&stats) == BADGER_FRAME_OK);
                    }
                    saved = active;
                    memset(&active, 0, sizeof(active));
                    assert(badger_nametag_cached_render(&canvas, &cache, 5500u) == BADGER_FONT_OK);
                    assert(badger_nametag_render(&reference, &workspace, &saved, 5500u, crisp) == BADGER_FONT_OK);
                    assert(memcmp(&canvas, &reference, sizeof(canvas)) == 0);
                    active = saved;
                }
            }
        }
    }
    active.name.clip.height = BADGER_NAMETAG_NAME_ROWS + 1u;
    assert(badger_nametag_cache_prepare(&cache, &canvas, &active, 0) == BADGER_FONT_ARGUMENT);
    assert(badger_nametag_cached_render(&canvas, &cache, 0u) == BADGER_FONT_ARGUMENT);
    assert(badger_nametag_prepare(&active, &badger_nametag_default_profile, &nametag_mono_fonts,
        BADGER_NAMETAG_B, &error) == BADGER_NAMETAG_OK);
    const badger_font_t missing = {0};
    active.pages[0].font = &missing;
    assert(badger_nametag_cache_prepare(&cache, &canvas, &active, 0) == BADGER_FONT_MISSING);
    assert(!cache.ready);
    assert(badger_nametag_cached_render(&canvas, &cache, 0u) == BADGER_FONT_ARGUMENT);
}

static void test_frame_diff(void) {
    badger_frame_stats_t stats;
    assert(badger_frame_present(0, &diff_frame, panel_write, 0, &stats) == BADGER_FRAME_ARGUMENT);
    assert(badger_frame_present(&presenter, &presenter.previous, panel_write, 0, &stats) == BADGER_FRAME_ARGUMENT);
    memset(&diff_frame, 0, sizeof(diff_frame));
    memset(&panel, 0xa5, sizeof(panel));
    badger_frame_invalidate(&presenter);
    assert(update_panel(&stats) == BADGER_FRAME_OK);
    assert(stats.full == 1u && stats.regions == 1u && stats.pixels == 76800u);
    assert(update_panel(&stats) == BADGER_FRAME_OK);
    assert(stats.regions == 0u && stats.pixels == 0u && stats.dirty_tiles == 0u);
    diff_frame.canvas.pixels[0] = 1u;
    diff_frame.canvas.pixels[76799] = 2u;
    assert(update_panel(&stats) == BADGER_FRAME_OK);
    assert(!stats.full && stats.regions == 2u && stats.pixels == 8u);
    diff_frame.canvas.pixels[0] = 3u;
    diff_frame.canvas.pixels[76799] = 4u;
    fail_at = 2u;
    assert(update_panel(&stats) == BADGER_FRAME_IO);
    assert(!presenter.valid && stats.regions == 1u);
    assert(memcmp(&panel, &diff_frame.canvas, sizeof(panel)) != 0);
    fail_at = 0u;
    assert(update_panel(&stats) == BADGER_FRAME_OK && stats.full == 1u);
    const uint32_t xs[] = {0u,1u,2u,3u,4u,5u,6u,317u,318u,319u};
    const uint32_t ys[] = {0u,1u,2u,3u,4u,5u,6u,237u,238u,239u};
    for (uint32_t xi = 0u; xi < sizeof(xs) / sizeof(xs[0]); ++xi) {
        for (uint32_t yi = 0u; yi < sizeof(ys) / sizeof(ys[0]); ++yi) {
            uint32_t x = xs[xi], y = ys[yi];
            for (uint32_t bit = 0u; bit <= 15u; bit += 15u) {
                diff_frame.canvas.pixels[x * 240u + y] ^= (uint16_t)(1u << bit);
                assert(update_panel(&stats) == BADGER_FRAME_OK);
                uint32_t width = x < 2u || x >= 318u ? 2u : 4u;
                uint32_t height = y < 2u || y >= 238u ? 2u : 4u;
                assert(stats.dirty_tiles == 1u && stats.regions == 1u && !stats.full);
                assert(stats.pixels == width * height);
            }
        }
    }
    for (uint32_t extra = 0u; extra < 2u; ++extra) {
        memset(&diff_frame, 0, sizeof(diff_frame));
        badger_frame_invalidate(&presenter);
        assert(update_panel(&stats) == BADGER_FRAME_OK);
        uint32_t count = BADGER_FRAME_TILES * 3u / 4u + extra;
        for (uint32_t tile = 0u; tile < count; ++tile) {
            uint32_t column = tile % BADGER_FRAME_COLUMNS, row = tile / BADGER_FRAME_COLUMNS;
            uint32_t x = column == 0u ? 0u : column * 4u - 2u;
            uint32_t y = row == 0u ? 0u : row * 4u - 2u;
            diff_frame.canvas.pixels[x * 240u + y] = 1u;
        }
        assert(update_panel(&stats) == BADGER_FRAME_OK);
        assert(stats.dirty_tiles == count);
        assert(stats.full == extra);
    }
    memset(&diff_frame, 0xff, sizeof(diff_frame));
    assert(update_panel(&stats) == BADGER_FRAME_OK && stats.full == 1u && stats.regions == 1u);
    assert(stats.dirty_tiles == BADGER_FRAME_TILES);
    for (uint32_t step = 0u; step < 12u; ++step) {
        for (uint32_t x = 0u; x < 320u; ++x)
            for (uint32_t y = 0u; y < 240u; ++y)
                diff_frame.canvas.pixels[x * 240u + y] = (uint16_t)(((x + step) % 16u < 8u) ? 0xffffu : 0u);
        assert(update_panel(&stats) == BADGER_FRAME_OK);
    }
    puts("frame diff: exact panel reconstruction, no duplicate writes, edge cells, unchanged/full frames, strict 75% fallback, partial failure and forced-full recovery PASS");
}

int main(void) {
    test_utf8();
    test_assets();
    test_blending_and_clipping();
    test_font_spans();
    test_profile_and_layout();
    test_animation();
    test_frame_diff();
    test_cached_renderer();
    puts("cached nametag: exact pixels across profiles/fonts/layouts/coverage/phases/seeks, snapshot isolation, reprepare and explicit preparation failure PASS");
    puts("nametag: repertoire/metrics, strict UTF-8, clipping, alpha endpoints, profile preservation, fit, NBSP/soft hyphen, loop/stall/wraparound and rendering restoration PASS");
    return 0;
}
