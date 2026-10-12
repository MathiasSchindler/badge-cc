#include "nametag.h"
#include "bytes.h"

const badger_nametag_profile_t badger_nametag_default_profile = {
    .name = "Mathias Schindler", .role = "Senior Manager Developer Policy",
    .employer = "GitHub", .username = "@mathiasschindler", .location = "Potsdam, Germany",
    .interests = {"CRA", "copyright", "AI", "open data", "FOIA"}, .interest_count = 5u
};

static uint32_t text_length(const char *text) {
    uint32_t length = 0u;
    while (text[length] != '\0') ++length;
    return length;
}

static badger_nametag_result_t fail(badger_nametag_error_t *error,
    badger_nametag_result_t result, const char *field, uint32_t offset, uint32_t codepoint) {
    if (error != 0) {
        error->result = result;
        error->field = field;
        error->byte_offset = offset;
        error->codepoint = codepoint;
        error->font_result = BADGER_FONT_OK;
    }
    return result;
}

static badger_nametag_result_t validate_field(const char *text, uint32_t capacity,
    const char *field, int required, badger_nametag_error_t *error) {
    uint32_t length = 0u;
    while (length < capacity && text[length] != '\0') ++length;
    if (length == capacity) return fail(error, BADGER_NAMETAG_LIMIT, field, length, 0u);
    for (uint32_t i = length + 1u; i < capacity; ++i) {
        if (text[i] != '\0') return fail(error, BADGER_NAMETAG_ENCODING, field, i, 0u);
    }
    uint32_t offset = 0u;
    int visible = 0;
    while (offset < length) {
        uint32_t used, codepoint;
        if (badger_utf8_next(text + offset, length - offset, &used, &codepoint) != BADGER_FONT_OK) {
            return fail(error, BADGER_NAMETAG_ENCODING, field, offset, 0u);
        }
        if (!badger_font_codepoint_supported(codepoint)) {
            return fail(error, BADGER_NAMETAG_REPERTOIRE, field, offset, codepoint);
        }
        if (codepoint != ' ' && codepoint != 0xa0u && codepoint != 0xadu) visible = 1;
        offset += used;
    }
    if (required && !visible) return fail(error, BADGER_NAMETAG_EMPTY, field, 0u, 0u);
    return BADGER_NAMETAG_OK;
}

badger_nametag_result_t badger_nametag_validate(const badger_nametag_profile_t *profile,
                                              badger_nametag_error_t *error) {
    if (profile == 0) return fail(error, BADGER_NAMETAG_ARGUMENT, "profile", 0u, 0u);
    const char *fields[] = {profile->name, profile->role, profile->employer, profile->username, profile->location};
    const uint32_t capacities[] = {129u, 193u, 97u, 41u, 129u};
    const char *names[] = {"name", "role", "employer", "username", "location"};
    for (uint32_t i = 0u; i < 5u; ++i) {
        badger_nametag_result_t result = validate_field(fields[i], capacities[i], names[i], i == 0u, error);
        if (result != BADGER_NAMETAG_OK) return result;
    }
    if (profile->interest_count > BADGER_NAMETAG_INTERESTS) {
        return fail(error, BADGER_NAMETAG_LIMIT, "interest_count", 0u, 0u);
    }
    for (uint32_t i = 0u; i < profile->interest_count; ++i) {
        badger_nametag_result_t result = validate_field(profile->interests[i], 65u, "interest", 1, error);
        if (result != BADGER_NAMETAG_OK) return result;
    }
    return fail(error, BADGER_NAMETAG_OK, "", 0u, 0u);
}

static int wrap(badger_nametag_block_t *block) {
    uint32_t total = text_length(block->text), start = 0u;
    block->line_count = 0u;
    while (start < total) {
        while (start < total && block->text[start] == ' ') ++start;
        if (start == total) break;
        if (block->line_count == BADGER_NAMETAG_LINES) return 0;
        uint32_t position = start, end = total, next = total;
        uint32_t break_end = start, break_next = start;
        uint8_t hyphen = 0u, break_hyphen = 0u;
        while (position < total) {
            uint32_t used, codepoint;
            if (badger_utf8_next(block->text + position, total - position, &used, &codepoint) != BADGER_FONT_OK) return 0;
            badger_font_metrics_t metrics;
            if (badger_font_measure(block->font, block->text + start, position + used - start, &metrics) != BADGER_FONT_OK) return 0;
            if (metrics.right - metrics.left > block->clip.width) {
                if (codepoint == ' ' && position > start) {
                    break_end = position;
                    break_next = position + used;
                    break_hyphen = 0u;
                }
                if (break_next == start) return 0;
                end = break_end;
                next = break_next;
                hyphen = break_hyphen;
                break;
            }
            if (codepoint == ' ' && position > start) {
                break_end = position;
                break_next = position + used;
                break_hyphen = 0u;
            } else if (codepoint == 0xadu && position > start) {
                const badger_font_glyph_t *dash = badger_font_glyph(block->font, '-');
                if (dash != 0 && metrics.right - metrics.left + dash->advance <= block->clip.width) {
                    break_end = position;
                    break_next = position + used;
                    break_hyphen = 1u;
                }
            }
            position += used;
        }
        while (end > start && block->text[end - 1u] == ' ') --end;
        uint32_t line = block->line_count++;
        block->starts[line] = (uint16_t)start;
        block->lengths[line] = (uint16_t)(end - start);
        block->hyphens[line] = hyphen;
        start = next;
    }
    return (uint32_t)block->line_count * block->font->line_height <= (uint32_t)block->clip.height;
}

static int fit(badger_nametag_block_t *block, const char *text,
    badger_font_clip_t clip, const badger_font_t *const *fonts, uint32_t count) {
    uint32_t length = text_length(text);
    if (length >= BADGER_NAMETAG_TEXT_BYTES) return 0;
    badger_bytes_zero((uint8_t *)block, (uint32_t)sizeof(*block));
    badger_bytes_copy((uint8_t *)block->text, (const uint8_t *)text, length + 1u);
    block->clip = clip;
    while (count != 0u) {
        block->font = fonts[--count];
        if (block->font != 0 && wrap(block)) return 1;
    }
    return 0;
}

static void append(char *text, const char *suffix) {
    uint32_t length = text_length(text);
    uint32_t extra = text_length(suffix);
    badger_bytes_copy((uint8_t *)text + length, (const uint8_t *)suffix, extra + 1u);
}

static int page(badger_nametag_scene_t *scene, const char *text,
                 const badger_nametag_fonts_t *fonts) {
    if (text[0] == '\0') return 1;
    if (scene->page_count >= BADGER_NAMETAG_PAGES) return 0;
    badger_nametag_block_t *block = &scene->pages[scene->page_count];
    if (!fit(block, text, scene->band, fonts->regular, 4u)) return 0;
    if (block->line_count != 0u) ++scene->page_count;
    return 1;
}

badger_nametag_result_t badger_nametag_prepare(badger_nametag_scene_t *pending,
    const badger_nametag_profile_t *profile, const badger_nametag_fonts_t *fonts,
    badger_nametag_variant_t variant, badger_nametag_error_t *error) {
    if (pending == 0 || profile == &pending->profile || fonts == 0 ||
        variant < BADGER_NAMETAG_A || variant > BADGER_NAMETAG_C) {
        return fail(error, BADGER_NAMETAG_ARGUMENT, "layout", 0u, 0u);
    }
    pending->ready = 0u;
    badger_nametag_result_t result = badger_nametag_validate(profile, error);
    if (result != BADGER_NAMETAG_OK) return result;
    for (uint32_t i = 0u; i < 4u; ++i) {
        if (fonts->regular[i] == 0 || fonts->regular[i]->pixel_size < 12u) {
            return fail(error, BADGER_NAMETAG_FONT, "supporting font", 0u, 0u);
        }
    }
    for (uint32_t i = 0u; i < 6u; ++i) {
        if (fonts->bold[i] == 0 || fonts->bold[i]->pixel_size < 24u) {
            return fail(error, BADGER_NAMETAG_FONT, "name font", 0u, 0u);
        }
    }
    badger_bytes_zero((uint8_t *)pending, (uint32_t)sizeof(*pending));
    badger_bytes_copy((uint8_t *)&pending->profile, (const uint8_t *)profile, (uint32_t)sizeof(*profile));
    pending->variant = variant;
    badger_font_clip_t name_clip = {18, variant == BADGER_NAMETAG_B ? 22 : 36, 284,
                                    variant == BADGER_NAMETAG_B ? 104 : 108};
    if (!fit(&pending->name, profile->name, name_clip, fonts->bold, 6u)) {
        return fail(error, BADGER_NAMETAG_FIT, "name", 0u, 0u);
    }
    char title[BADGER_NAMETAG_TEXT_BYTES];
    badger_bytes_zero((uint8_t *)title, sizeof(title));
    append(title, profile->role);
    if (profile->role[0] != '\0' && profile->employer[0] != '\0') append(title, " at ");
    append(title, profile->employer);
    if (variant == BADGER_NAMETAG_B) {
        pending->band = (badger_font_clip_t){18, 176, 284, 56};
        badger_font_clip_t username_clip = {18, 133, 284, 28};
        if (!fit(&pending->username, profile->username, username_clip, fonts->regular, 4u) ||
            !page(pending, title, fonts)) return fail(error, BADGER_NAMETAG_FIT, "identity card", 0u, 0u);
    } else {
        badger_font_clip_t role_clip = {18, 148, 284, 44};
        if (!fit(&pending->role, title, role_clip, fonts->regular, 4u)) {
            return fail(error, BADGER_NAMETAG_FIT, "role/employer", 0u, 0u);
        }
        pending->band = (badger_font_clip_t){18, variant == BADGER_NAMETAG_C ? 220 : 204, 284,
                                            variant == BADGER_NAMETAG_C ? 20 : 32};
        if (variant == BADGER_NAMETAG_C) {
            badger_font_clip_t username_clip = {18, 195, 284, 23};
            if (!fit(&pending->username, profile->username, username_clip, fonts->regular, 4u)) {
                return fail(error, BADGER_NAMETAG_FIT, "username", 0u, 0u);
            }
        } else if (!page(pending, profile->username, fonts)) {
            return fail(error, BADGER_NAMETAG_FIT, "username", 0u, 0u);
        }
    }
    if (!page(pending, profile->location, fonts)) return fail(error, BADGER_NAMETAG_FIT, "location", 0u, 0u);
    char group[BADGER_NAMETAG_TEXT_BYTES];
    badger_bytes_zero((uint8_t *)group, sizeof(group));
    uint16_t group_size = 0u;
    for (uint32_t i = 0u; i < profile->interest_count; ++i) {
        char candidate[BADGER_NAMETAG_TEXT_BYTES];
        badger_bytes_zero((uint8_t *)candidate, sizeof(candidate));
        uint32_t needed = text_length(group) + (group[0] != '\0' ? 4u : 0u) + text_length(profile->interests[i]);
        badger_nametag_block_t trial;
        int fits = 0;
        if (needed < BADGER_NAMETAG_TEXT_BYTES) {
            append(candidate, group);
            if (group[0] != '\0') append(candidate, " \xc2\xb7 ");
            append(candidate, profile->interests[i]);
            fits = fit(&trial, candidate, pending->band, fonts->regular, 4u);
            if (fits && group_size != 0u && trial.font->pixel_size < group_size) fits = 0;
        }
        if (fits) {
            badger_bytes_copy((uint8_t *)group, (const uint8_t *)candidate, text_length(candidate) + 1u);
            group_size = trial.font->pixel_size;
        } else {
            if (!page(pending, group, fonts)) return fail(error, BADGER_NAMETAG_FIT, "interests", i, 0u);
            badger_bytes_zero((uint8_t *)group, sizeof(group));
            append(group, profile->interests[i]);
            if (!fit(&trial, group, pending->band, fonts->regular, 4u)) {
                return fail(error, BADGER_NAMETAG_FIT, "interest", i, 0u);
            }
            group_size = trial.font->pixel_size;
        }
    }
    if (!page(pending, group, fonts)) return fail(error, BADGER_NAMETAG_FIT, "interests", 0u, 0u);
    pending->ready = 1u;
    return fail(error, BADGER_NAMETAG_OK, "", 0u, 0u);
}

badger_nametag_result_t badger_nametag_replace(badger_nametag_scene_t *active,
    badger_nametag_scene_t *pending, const badger_nametag_profile_t *profile,
    const badger_nametag_fonts_t *fonts, badger_nametag_variant_t variant,
    badger_nametag_error_t *error) {
    if (active == 0 || active == pending) return fail(error, BADGER_NAMETAG_ARGUMENT, "pending scene", 0u, 0u);
    badger_nametag_result_t result = badger_nametag_prepare(pending, profile, fonts, variant, error);
    if (result == BADGER_NAMETAG_OK) {
        badger_bytes_copy((uint8_t *)active, (const uint8_t *)pending, (uint32_t)sizeof(*active));
    }
    return result;
}

uint32_t badger_nametag_loop_ms(const badger_nametag_scene_t *scene) {
    if (scene == 0 || scene->page_count == 0u) return 8000u;
    uint32_t carousel = scene->page_count * 6000u, a = 8000u, b = carousel;
    while (b != 0u) { uint32_t remainder = a % b; a = b; b = remainder; }
    return (8000u / a) * carousel;
}

uint32_t badger_nametag_wave(uint32_t phase) {
    uint32_t triangle = (phase & 65535u) >> 7u;
    if (triangle > 256u) triangle = 512u - triangle;
    return (triangle * triangle * (768u - 2u * triangle)) >> 16u;
}

badger_nametag_phase_t badger_nametag_phase(const badger_nametag_scene_t *scene, uint32_t elapsed_ms) {
    badger_nametag_phase_t phase = {0, 0, 0, 0, 0};
    if (scene == 0 || scene->page_count < 2u) return phase;
    phase.current = (uint8_t)((elapsed_ms / 6000u) % scene->page_count);
    phase.next = (uint8_t)((phase.current + 1u) % scene->page_count);
    uint32_t position = elapsed_ms % 6000u;
    if (position >= 5000u) {
        uint32_t t = (position - 5000u) * 256u / 1000u;
        uint32_t eased = (t * t * (768u - 2u * t)) >> 16u;
        phase.mix = (uint8_t)((eased * 255u + 128u) / 256u);
        if (scene->variant == BADGER_NAMETAG_B) {
            phase.outgoing_y_q8 = -(int32_t)((uint32_t)phase.mix * 2048u / 255u);
            phase.incoming_y_q8 = 2048 + phase.outgoing_y_q8;
        }
    }
    return phase;
}

badger_nametag_result_t badger_nametag_clock_update(badger_nametag_clock_t *clock,
    uint32_t tick_ms, uint32_t loop_ms, uint32_t *elapsed_ms) {
    if (clock == 0 || elapsed_ms == 0 || loop_ms == 0u || loop_ms > 0x7fffffffu) return BADGER_NAMETAG_ARGUMENT;
    if (!clock->started) {
        clock->started = 1u;
        clock->elapsed = 0u;
    } else {
        clock->elapsed = (clock->elapsed % loop_ms + (tick_ms - clock->last_tick) % loop_ms) % loop_ms;
    }
    clock->last_tick = tick_ms;
    *elapsed_ms = clock->elapsed;
    return BADGER_NAMETAG_OK;
}

badger_nametag_result_t badger_nametag_clock_update_us(badger_nametag_clock_us_t *clock,
    uint32_t tick_us, uint32_t loop_ms, uint32_t *elapsed_ms) {
    if (clock == 0 || elapsed_ms == 0 || loop_ms == 0u || loop_ms > 0x7fffffffu)
        return BADGER_NAMETAG_ARGUMENT;
    if (!clock->started) {
        clock->started = 1u;
        clock->elapsed = clock->fraction_us = 0u;
    } else {
        uint32_t delta = tick_us - clock->last_tick;
        uint32_t fraction = clock->fraction_us + delta % 1000u;
        clock->elapsed = (clock->elapsed % loop_ms + delta / 1000u + fraction / 1000u) % loop_ms;
        clock->fraction_us = fraction % 1000u;
    }
    clock->last_tick = tick_us;
    *elapsed_ms = clock->elapsed;
    return BADGER_NAMETAG_OK;
}

static void square(badger_canvas_t *canvas, int32_t cx, int32_t cy, uint32_t size,
                    const uint16_t *colors) {
    int32_t left = cx * 256 - (int32_t)size / 2, top = cy * 256 - (int32_t)size / 2;
    int32_t right = left + (int32_t)size, bottom = top + (int32_t)size;
    int32_t first_x = left / 256, first_y = top / 256;
    int32_t end_x = right / 256, end_y = bottom / 256;
    if (left % 256 < 0) --first_x;
    if (top % 256 < 0) --first_y;
    if (right % 256 > 0) ++end_x;
    if (bottom % 256 > 0) ++end_y;
    if (first_x < 0) first_x = 0;
    if (first_y < 0) first_y = 0;
    if (end_x > BADGER_GFX_WIDTH) end_x = BADGER_GFX_WIDTH;
    if (end_y > BADGER_GFX_HEIGHT) end_y = BADGER_GFX_HEIGHT;
    for (int32_t x = first_x; x < end_x; ++x) {
        int32_t x0 = x * 256 > left ? x * 256 : left;
        int32_t x1 = (x + 1) * 256 < right ? (x + 1) * 256 : right;
        if (x1 <= x0) continue;
        for (int32_t y = first_y; y < end_y; ++y) {
            int32_t y0 = y * 256 > top ? y * 256 : top;
            int32_t y1 = (y + 1) * 256 < bottom ? (y + 1) * 256 : bottom;
            if (y1 <= y0) continue;
            uint8_t alpha = (uint8_t)(((uint32_t)(x1 - x0) * (uint32_t)(y1 - y0) * 255u + 32768u) / 65536u);
            uint32_t index = (uint32_t)x * BADGER_GFX_HEIGHT + (uint32_t)y;
            canvas->pixels[index] = colors[alpha];
        }
    }
}

static badger_font_result_t block_draw(badger_canvas_t *canvas, uint8_t *mask,
    uint32_t rows, const badger_nametag_block_t *block, int32_t shift, uint16_t color, int crisp) {
    for (uint32_t line = 0u; line < block->line_count; ++line) {
        const char *text = block->text + block->starts[line];
        uint32_t length = block->lengths[line];
        badger_font_metrics_t metrics;
        badger_font_result_t result = badger_font_measure(block->font, text, length, &metrics);
        if (result != BADGER_FONT_OK) return result;
        int32_t x = block->clip.x - metrics.left;
        int32_t baseline = (int32_t)block->font->ascent + (int32_t)(line * block->font->line_height) + shift;
        if (mask == 0) {
            result = badger_font_draw(canvas, block->font, text, length, x, block->clip.y + baseline,
                                      color, 255u, block->clip, crisp);
        } else result = badger_font_mask(mask, BADGER_GFX_WIDTH, rows, block->font, text, length, x, baseline, crisp);
        if (result != BADGER_FONT_OK) return result;
        if (block->hyphens[line]) {
            if (mask == 0) {
                result = badger_font_draw(canvas, block->font, "-", 1u, x + metrics.advance, block->clip.y + baseline,
                                          color, 255u, block->clip, crisp);
            } else result = badger_font_mask(mask, BADGER_GFX_WIDTH, rows, block->font, "-", 1u, x + metrics.advance, baseline, crisp);
            if (result != BADGER_FONT_OK) return result;
        }
    }
    return BADGER_FONT_OK;
}

typedef struct {
    const uint8_t *first, *second;
    uint32_t fraction, begin, end;
} mask_row_t;

static mask_row_t mask_row(const uint8_t *mask, uint32_t y,
    uint32_t rows, int32_t shift_q8, const badger_nametag_span_t *spans) {
    int32_t source = (int32_t)(y * 256u) - shift_q8;
    int32_t row = source / 256, fraction = source % 256;
    if (fraction < 0) { fraction += 256; --row; }
    mask_row_t sample;
    sample.first = row >= 0 && row < (int32_t)rows ? mask + (uint32_t)row * BADGER_GFX_WIDTH : 0;
    sample.second = fraction != 0 && row + 1 >= 0 && row + 1 < (int32_t)rows ?
        mask + (uint32_t)(row + 1) * BADGER_GFX_WIDTH : 0;
    sample.fraction = (uint32_t)fraction;
    sample.begin = 0u;
    sample.end = BADGER_GFX_WIDTH;
    if (spans != 0) {
        sample.begin = BADGER_GFX_WIDTH;
        sample.end = 0u;
        if (sample.first != 0) {
            sample.begin = spans[row].begin;
            sample.end = spans[row].end;
        }
        if (sample.second != 0) {
            if (spans[row + 1].begin < sample.begin) sample.begin = spans[row + 1].begin;
            if (spans[row + 1].end > sample.end) sample.end = spans[row + 1].end;
        }
    }
    return sample;
}

static uint32_t mask_row_sample(mask_row_t sample, uint32_t x) {
    uint32_t first = sample.first != 0 ? sample.first[x] : 0u;
    if (sample.fraction == 0u) return first;
    uint32_t second = sample.second != 0 ? sample.second[x] : 0u;
    return (first * (256u - sample.fraction) + second * sample.fraction + 128u) >> 8u;
}

static void footer_composite(badger_canvas_t *canvas, const uint8_t *outgoing,
    const uint8_t *incoming, const badger_nametag_scene_t *scene,
    badger_nametag_phase_t phase, const badger_nametag_span_t *outgoing_spans,
    const badger_nametag_span_t *incoming_spans) {
    uint32_t rows = (uint32_t)scene->band.height;
    for (uint32_t y = 0u; y < rows; ++y) {
        uint32_t row_index = (uint32_t)scene->band.y + y;
        if (phase.mix == 0u) {
            const uint8_t *mask = outgoing + y * BADGER_GFX_WIDTH;
            uint32_t begin = outgoing_spans != 0 ? outgoing_spans[y].begin : 0u;
            uint32_t end = outgoing_spans != 0 ? outgoing_spans[y].end : BADGER_GFX_WIDTH;
            for (uint32_t x = begin; x < end; ++x) {
                if (mask[x] == 0u) continue;
                uint32_t index = x * BADGER_GFX_HEIGHT + row_index;
                canvas->pixels[index] = badger_rgb565_blend(canvas->pixels[index], 0xd567u, mask[x]);
            }
            continue;
        }
        mask_row_t a = mask_row(outgoing, y, rows, phase.outgoing_y_q8, outgoing_spans);
        mask_row_t b = mask_row(incoming, y, rows, phase.incoming_y_q8, incoming_spans);
        uint32_t begin = a.begin < b.begin ? a.begin : b.begin;
        uint32_t end = a.end > b.end ? a.end : b.end;
        for (uint32_t x = begin; x < end; ++x) {
            uint32_t first = mask_row_sample(a, x), second = mask_row_sample(b, x);
            if ((first | second) == 0u) continue;
            uint8_t alpha = (uint8_t)badger_font_alpha_round(first * (255u - phase.mix) + second * phase.mix);
            if (alpha == 0u) continue;
            uint32_t index = x * BADGER_GFX_HEIGHT + row_index;
            canvas->pixels[index] = badger_rgb565_blend(canvas->pixels[index], 0xd567u, alpha);
        }
    }
}

badger_font_result_t badger_nametag_background(badger_canvas_t *canvas,
    const badger_nametag_scene_t *scene, uint32_t elapsed_ms) {
    if (canvas == 0 || scene == 0 || !scene->ready) return BADGER_FONT_ARGUMENT;
    badger_pixels_fill(canvas->pixels, 0x0801u, BADGER_GFX_WIDTH * BADGER_GFX_HEIGHT);
    /* Squares stay below the 8px pitch and blend only with the uniform fill. */
    uint16_t colors[256];
    for (uint32_t alpha = 0u; alpha < 256u; ++alpha)
        colors[alpha] = badger_rgb565_blend(0x0801u, 0x8b65u, (uint8_t)alpha);
    uint32_t time_phase = (elapsed_ms % 8000u) * 65536u / 8000u;
    for (int32_t x = 4; x < BADGER_GFX_WIDTH; x += 8) {
        for (int32_t y = 4; y < BADGER_GFX_HEIGHT; y += 8) {
            badger_font_clip_t clip = scene->name.clip;
            if (x >= clip.x - 4 && x < clip.x + clip.width + 4 &&
                y >= clip.y - 4 && y < clip.y + clip.height + 4) continue;
            uint32_t density = (uint32_t)(x + y) * 256u / 560u;
            density = density * density / 256u;
            uint32_t wave = badger_nametag_wave(time_phase + (uint32_t)(x + y) * 192u);
            uint32_t size = density * 1280u / 256u;
            size = size * (192u + wave / 2u) / 256u;
            if (scene->variant == BADGER_NAMETAG_C && x < 224) size /= 2u;
            square(canvas, x, y, size, colors);
        }
    }
    if (scene->variant == BADGER_NAMETAG_B) badger_gfx_rect(canvas, 18, 169, 284, 1, 0x7283u);
    return BADGER_FONT_OK;
}

badger_font_result_t badger_nametag_text(badger_canvas_t *canvas,
    badger_nametag_workspace_t *workspace, const badger_nametag_scene_t *scene,
    uint32_t elapsed_ms, int crisp) {
    if (canvas == 0 || workspace == 0 || scene == 0 || !scene->ready) return BADGER_FONT_ARGUMENT;
    const badger_nametag_block_t *static_blocks[] = {&scene->name, &scene->role, &scene->username};
    for (uint32_t i = 0u; i < 3u; ++i) {
        badger_font_result_t result = block_draw(canvas, 0, 0u, static_blocks[i], 0, i == 0u ? 0xf778u : 0xd567u, crisp);
        if (result != BADGER_FONT_OK) return result;
    }
    if (scene->page_count != 0u) {
        uint32_t rows = (uint32_t)scene->band.height;
        if (rows > BADGER_NAMETAG_BAND_ROWS) return BADGER_FONT_ARGUMENT;
        badger_bytes_zero(workspace->outgoing, rows * BADGER_GFX_WIDTH);
        badger_nametag_phase_t phase = badger_nametag_phase(scene, elapsed_ms);
        badger_font_result_t result = block_draw(canvas, workspace->outgoing, rows,
            &scene->pages[phase.current], 0, 0u, crisp);
        if (result != BADGER_FONT_OK) return result;
        if (phase.mix != 0u) {
            badger_bytes_zero(workspace->incoming, rows * BADGER_GFX_WIDTH);
            result = block_draw(canvas, workspace->incoming, rows,
                &scene->pages[phase.next], 0, 0u, crisp);
            if (result != BADGER_FONT_OK) return result;
        }
        footer_composite(canvas, workspace->outgoing, workspace->incoming, scene, phase, 0, 0);
    }
    return BADGER_FONT_OK;
}

badger_font_result_t badger_nametag_render(badger_canvas_t *canvas,
    badger_nametag_workspace_t *workspace, const badger_nametag_scene_t *scene,
    uint32_t elapsed_ms, int crisp) {
    if (workspace == 0) return BADGER_FONT_ARGUMENT;
    badger_font_result_t result = badger_nametag_background(canvas, scene, elapsed_ms);
    if (result != BADGER_FONT_OK) return result;
    return badger_nametag_text(canvas, workspace, scene, elapsed_ms, crisp);
}

static badger_font_result_t cache_page(badger_nametag_cache_t *cache,
                                        uint32_t slot, uint8_t page_id) {
    uint8_t *mask = slot == 0u ? cache->masks.outgoing : cache->masks.incoming;
    uint32_t rows = (uint32_t)cache->scene.band.height;
    cache->page_ids[slot] = 255u;
    badger_bytes_zero(mask, rows * BADGER_GFX_WIDTH);
    badger_font_result_t result = block_draw(0, mask, rows, &cache->scene.pages[page_id], 0, 0u, cache->crisp);
    if (result != BADGER_FONT_OK) return result;
    for (uint32_t y = 0u; y < rows; ++y) {
        badger_nametag_span_t *span = &cache->page_spans[slot][y];
        span->begin = BADGER_GFX_WIDTH;
        span->end = 0u;
        for (uint32_t x = 0u; x < BADGER_GFX_WIDTH; ++x) {
            if (mask[y * BADGER_GFX_WIDTH + x] == 0u) continue;
            if (span->end == 0u) span->begin = (uint16_t)x;
            span->end = (uint16_t)(x + 1u);
        }
    }
    cache->page_ids[slot] = page_id;
    return BADGER_FONT_OK;
}

static badger_font_result_t cache_pages(badger_nametag_cache_t *cache,
    badger_nametag_phase_t phase, uint32_t *outgoing_slot, uint32_t *incoming_slot) {
    uint32_t slot = cache->page_ids[1] == phase.current ? 1u : 0u;
    if (cache->page_ids[slot] != phase.current) {
        if (cache->page_ids[0] == phase.next) slot = 1u;
        badger_font_result_t result = cache_page(cache, slot, phase.current);
        if (result != BADGER_FONT_OK) return result;
    }
    *outgoing_slot = slot;
    *incoming_slot = phase.next == phase.current ? slot : 1u - slot;
    if (cache->page_ids[*incoming_slot] != phase.next)
        return cache_page(cache, *incoming_slot, phase.next);
    return BADGER_FONT_OK;
}

badger_font_result_t badger_nametag_cache_prepare(badger_nametag_cache_t *cache,
    badger_canvas_t *canvas, const badger_nametag_scene_t *scene, int crisp) {
    if (cache == 0) return BADGER_FONT_ARGUMENT;
    cache->ready = 0u;
    if (canvas == 0 || scene == 0 || scene == &cache->scene || !scene->ready ||
        scene->page_count > BADGER_NAMETAG_PAGES) return BADGER_FONT_ARGUMENT;
    badger_font_clip_t clip = scene->name.clip;
    if (clip.x < 0 || clip.y < 0 || clip.width <= 0 || clip.height <= 0 ||
        clip.width > BADGER_GFX_WIDTH || clip.height > (int32_t)BADGER_NAMETAG_NAME_ROWS ||
        clip.x > BADGER_GFX_WIDTH - clip.width || clip.y > BADGER_GFX_HEIGHT - clip.height ||
        scene->band.y < 0 || scene->band.height <= 0 ||
        scene->band.height > (int32_t)BADGER_NAMETAG_BAND_ROWS ||
        scene->band.y > BADGER_GFX_HEIGHT - scene->band.height) return BADGER_FONT_ARGUMENT;
    badger_bytes_copy((uint8_t *)&cache->scene, (const uint8_t *)scene, (uint32_t)sizeof(*scene));
    cache->crisp = crisp != 0;
    cache->page_ids[0] = cache->page_ids[1] = 255u;
    badger_font_result_t result = badger_nametag_background(canvas, &cache->scene, 0u);
    if (result != BADGER_FONT_OK) return result;
    result = block_draw(canvas, 0, 0u, &cache->scene.name, 0, 0xf778u, cache->crisp);
    if (result != BADGER_FONT_OK) return result;
    for (uint32_t x = 0u; x < (uint32_t)clip.width; ++x) {
        badger_nametag_span_t *span = &cache->name_spans[x];
        span->begin = (uint16_t)clip.height;
        span->end = 0u;
        for (uint32_t y = 0u; y < (uint32_t)clip.height; ++y) {
            uint16_t pixel = canvas->pixels[((uint32_t)clip.x + x) * BADGER_GFX_HEIGHT + (uint32_t)clip.y + y];
            cache->name_pixels[x * BADGER_NAMETAG_NAME_ROWS + y] = pixel;
            if (pixel == 0x0801u) continue;
            if (span->end == 0u) span->begin = (uint16_t)y;
            span->end = (uint16_t)(y + 1u);
        }
    }
    if (scene->page_count != 0u) {
        uint32_t outgoing, incoming;
        result = cache_pages(cache, badger_nametag_phase(&cache->scene, 0u), &outgoing, &incoming);
        if (result != BADGER_FONT_OK) return result;
    }
    cache->ready = 1u;
    return BADGER_FONT_OK;
}

badger_font_result_t badger_nametag_cached_text(badger_canvas_t *canvas,
    badger_nametag_cache_t *cache, uint32_t elapsed_ms) {
    if (canvas == 0 || cache == 0 || !cache->ready) return BADGER_FONT_ARGUMENT;
    const badger_nametag_scene_t *scene = &cache->scene;
    badger_font_clip_t clip = scene->name.clip;
    for (uint32_t x = 0u; x < (uint32_t)clip.width; ++x) {
        const uint16_t *source = cache->name_pixels + x * BADGER_NAMETAG_NAME_ROWS;
        uint16_t *destination = canvas->pixels + ((uint32_t)clip.x + x) * BADGER_GFX_HEIGHT + (uint32_t)clip.y;
        for (uint32_t y = cache->name_spans[x].begin; y < cache->name_spans[x].end; ++y)
            destination[y] = source[y];
    }
    badger_font_result_t result = block_draw(canvas, 0, 0u, &scene->role, 0, 0xd567u, cache->crisp);
    if (result == BADGER_FONT_OK) result = block_draw(canvas, 0, 0u, &scene->username, 0, 0xd567u, cache->crisp);
    if (result == BADGER_FONT_OK && scene->page_count != 0u) {
        badger_nametag_phase_t phase = badger_nametag_phase(scene, elapsed_ms);
        uint32_t outgoing, incoming;
        result = cache_pages(cache, phase, &outgoing, &incoming);
        if (result == BADGER_FONT_OK) {
            const uint8_t *masks[] = {cache->masks.outgoing, cache->masks.incoming};
            footer_composite(canvas, masks[outgoing], masks[incoming], scene, phase,
                cache->page_spans[outgoing], cache->page_spans[incoming]);
        }
    }
    if (result != BADGER_FONT_OK) cache->ready = 0u;
    return result;
}

badger_font_result_t badger_nametag_cached_render(badger_canvas_t *canvas,
    badger_nametag_cache_t *cache, uint32_t elapsed_ms) {
    if (canvas == 0 || cache == 0 || !cache->ready) return BADGER_FONT_ARGUMENT;
    badger_font_result_t result = badger_nametag_background(canvas, &cache->scene, elapsed_ms);
    if (result != BADGER_FONT_OK) return result;
    return badger_nametag_cached_text(canvas, cache, elapsed_ms);
}

const char *badger_nametag_result_name(badger_nametag_result_t result) {
    switch (result) {
        case BADGER_NAMETAG_OK: return "ok";
        case BADGER_NAMETAG_ARGUMENT: return "invalid nametag argument";
        case BADGER_NAMETAG_LIMIT: return "profile field/count exceeds its limit";
        case BADGER_NAMETAG_ENCODING: return "invalid UTF-8 or nonzero bytes after terminator";
        case BADGER_NAMETAG_REPERTOIRE: return "unsupported character";
        case BADGER_NAMETAG_EMPTY: return "required field is blank";
        case BADGER_NAMETAG_FIT: return "complete text cannot fit at approved minimum sizes";
        case BADGER_NAMETAG_FONT: return "missing or invalid font bank";
    }
    return "unknown nametag error";
}
