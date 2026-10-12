#ifndef UNIVERSE26_NAMETAG_H
#define UNIVERSE26_NAMETAG_H

#include "bitmap_font.h"

#define BADGER_NAMETAG_INTERESTS 8u
#define BADGER_NAMETAG_PAGES 10u
#define BADGER_NAMETAG_LINES 8u
#define BADGER_NAMETAG_TEXT_BYTES 384u
#define BADGER_NAMETAG_BAND_ROWS 64u
#define BADGER_NAMETAG_NAME_ROWS 108u

typedef struct {
    char name[129], role[193], employer[97], username[41], location[129];
    char interests[BADGER_NAMETAG_INTERESTS][65];
    uint32_t interest_count;
} badger_nametag_profile_t;

typedef struct {
    const badger_font_t *regular[4];
    const badger_font_t *bold[6];
    const char *license;
} badger_nametag_fonts_t;

typedef enum {
    BADGER_NAMETAG_A, BADGER_NAMETAG_B, BADGER_NAMETAG_C
} badger_nametag_variant_t;

typedef enum {
    BADGER_NAMETAG_OK, BADGER_NAMETAG_ARGUMENT, BADGER_NAMETAG_LIMIT,
    BADGER_NAMETAG_ENCODING, BADGER_NAMETAG_REPERTOIRE, BADGER_NAMETAG_EMPTY,
    BADGER_NAMETAG_FIT, BADGER_NAMETAG_FONT
} badger_nametag_result_t;

typedef struct {
    badger_nametag_result_t result;
    const char *field;
    uint32_t byte_offset, codepoint;
    badger_font_result_t font_result;
} badger_nametag_error_t;

typedef struct {
    char text[BADGER_NAMETAG_TEXT_BYTES];
    const badger_font_t *font;
    badger_font_clip_t clip;
    uint16_t starts[BADGER_NAMETAG_LINES], lengths[BADGER_NAMETAG_LINES];
    uint8_t hyphens[BADGER_NAMETAG_LINES];
    uint8_t line_count;
} badger_nametag_block_t;

typedef struct {
    badger_nametag_profile_t profile;
    badger_nametag_block_t name, role, username;
    badger_nametag_block_t pages[BADGER_NAMETAG_PAGES];
    badger_font_clip_t band;
    badger_nametag_variant_t variant;
    uint8_t page_count, ready;
} badger_nametag_scene_t;

typedef struct {
    uint8_t outgoing[BADGER_GFX_WIDTH * BADGER_NAMETAG_BAND_ROWS];
    uint8_t incoming[BADGER_GFX_WIDTH * BADGER_NAMETAG_BAND_ROWS];
} badger_nametag_workspace_t;

typedef struct {
    uint8_t current, next, mix;
    int32_t outgoing_y_q8, incoming_y_q8;
} badger_nametag_phase_t;

typedef struct {
    uint32_t last_tick, elapsed;
    uint8_t started;
} badger_nametag_clock_t;

typedef struct {
    uint32_t last_tick, elapsed, fraction_us;
    uint8_t started;
} badger_nametag_clock_us_t;

typedef struct {
    uint16_t begin, end;
} badger_nametag_span_t;

typedef struct {
    badger_nametag_scene_t scene;
    badger_nametag_workspace_t masks;
    uint16_t name_pixels[BADGER_GFX_WIDTH * BADGER_NAMETAG_NAME_ROWS];
    badger_nametag_span_t name_spans[BADGER_GFX_WIDTH];
    badger_nametag_span_t page_spans[2][BADGER_NAMETAG_BAND_ROWS];
    uint8_t page_ids[2], ready, crisp;
} badger_nametag_cache_t;

extern const badger_nametag_profile_t badger_nametag_default_profile;

badger_nametag_result_t badger_nametag_validate(const badger_nametag_profile_t *profile,
                                              badger_nametag_error_t *error);
/* Prepare into separate pending storage; only apply successful complete scenes. */
badger_nametag_result_t badger_nametag_prepare(badger_nametag_scene_t *pending,
    const badger_nametag_profile_t *profile, const badger_nametag_fonts_t *fonts,
    badger_nametag_variant_t variant, badger_nametag_error_t *error);
badger_nametag_result_t badger_nametag_replace(badger_nametag_scene_t *active,
    badger_nametag_scene_t *pending, const badger_nametag_profile_t *profile,
    const badger_nametag_fonts_t *fonts, badger_nametag_variant_t variant,
    badger_nametag_error_t *error);
uint32_t badger_nametag_loop_ms(const badger_nametag_scene_t *scene);
badger_nametag_phase_t badger_nametag_phase(const badger_nametag_scene_t *scene, uint32_t elapsed_ms);
uint32_t badger_nametag_wave(uint32_t phase);
badger_nametag_result_t badger_nametag_clock_update(badger_nametag_clock_t *clock,
    uint32_t tick_ms, uint32_t loop_ms, uint32_t *elapsed_ms);
/* Update at least once per 32-bit microsecond timer wrap; retains fractions. */
badger_nametag_result_t badger_nametag_clock_update_us(badger_nametag_clock_us_t *clock,
    uint32_t tick_us, uint32_t loop_ms, uint32_t *elapsed_ms);
badger_font_result_t badger_nametag_render(badger_canvas_t *canvas,
    badger_nametag_workspace_t *workspace, const badger_nametag_scene_t *scene,
    uint32_t elapsed_ms, int crisp);
badger_font_result_t badger_nametag_background(badger_canvas_t *canvas,
    const badger_nametag_scene_t *scene, uint32_t elapsed_ms);
/* Composite text onto an already prepared background. */
badger_font_result_t badger_nametag_text(badger_canvas_t *canvas,
    badger_nametag_workspace_t *workspace, const badger_nametag_scene_t *scene,
    uint32_t elapsed_ms, int crisp);
/* Captures a scene snapshot; fonts must remain immutable. Canvas is scratch.
   Failure invalidates this cache; use pending storage to retain a live cache. */
badger_font_result_t badger_nametag_cache_prepare(badger_nametag_cache_t *cache,
    badger_canvas_t *canvas, const badger_nametag_scene_t *scene, int crisp);
/* Requires the cache's own background, not an arbitrary canvas background. */
badger_font_result_t badger_nametag_cached_text(badger_canvas_t *canvas,
    badger_nametag_cache_t *cache, uint32_t elapsed_ms);
badger_font_result_t badger_nametag_cached_render(badger_canvas_t *canvas,
    badger_nametag_cache_t *cache, uint32_t elapsed_ms);
const char *badger_nametag_result_name(badger_nametag_result_t result);

#endif
