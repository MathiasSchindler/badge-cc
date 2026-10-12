#include "../../shared/nametag.h"
#include "../../shared/frame_diff.h"
#include "nametag_fonts.h"

static badger_frame_t frame;
static badger_frame_presenter_t presenter;
static badger_nametag_workspace_t workspace;
static badger_nametag_cache_t cache;
static badger_nametag_scene_t active, pending;
volatile uint32_t nametag_target_result;
const uint32_t nametag_target_sizes[] = {
    sizeof(frame), sizeof(workspace), sizeof(active), sizeof(badger_nametag_profile_t),
    sizeof(frame) + sizeof(workspace) + 2u * sizeof(active),
    sizeof(cache), sizeof(frame) + sizeof(cache) + 2u * sizeof(active),
    sizeof(presenter)
};

static int writer(void *context, const badger_canvas_t *canvas,
                    const badger_frame_region_t *region) {
    (void)context; (void)canvas; (void)region;
    return 1;
}

/* Link-only proof: no vectors, PICOBIN, hardware initialization or UF2 output. */
void nametag_target_main(void) {
    badger_nametag_error_t error;
    nametag_target_result = badger_nametag_replace(&active, &pending, &badger_nametag_default_profile,
        &nametag_mono_fonts, BADGER_NAMETAG_B, &error);
    if (nametag_target_result == BADGER_NAMETAG_OK) {
        nametag_target_result = badger_nametag_render(&frame.canvas, &workspace, &active, 5500u, 0);
        if (nametag_target_result == BADGER_FONT_OK)
            nametag_target_result = badger_nametag_cache_prepare(&cache, &frame.canvas, &active, 0);
        if (nametag_target_result == BADGER_FONT_OK)
            nametag_target_result = badger_nametag_cached_render(&frame.canvas, &cache, 5500u);
        if (nametag_target_result == BADGER_FONT_OK) {
            badger_frame_stats_t stats;
            nametag_target_result = badger_frame_present(&presenter, &frame, writer, 0, &stats);
        }
    }
}
