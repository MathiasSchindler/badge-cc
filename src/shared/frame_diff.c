#include "frame_diff.h"
#include "bytes.h"

_Static_assert(BADGER_GFX_WIDTH % BADGER_FRAME_TILE == 0 &&
    BADGER_GFX_HEIGHT % BADGER_FRAME_TILE == 0, "frame grid requires whole tile periods");
_Static_assert(sizeof(badger_frame_t) == sizeof(badger_canvas_t), "frame views must match");
_Static_assert(BADGER_FRAME_TILE == 4u && BADGER_FRAME_OFFSET == 2u,
    "paired comparison requires shifted four-pixel cells");

void badger_frame_invalidate(badger_frame_presenter_t *presenter) {
    presenter->valid = 0u;
}

static uint32_t boundary(uint32_t tile, uint32_t limit) {
    uint32_t position = tile == 0u ? 0u : tile * BADGER_FRAME_TILE - BADGER_FRAME_OFFSET;
    return position < limit ? position : limit;
}

static uint32_t mark_changed(badger_frame_presenter_t *presenter,
    uint32_t index, uint32_t difference) {
    if (difference == 0u || presenter->dirty[index]) return 0u;
    presenter->dirty[index] = 1u;
    return 1u;
}

#if defined(__GNUC__)
__attribute__((noinline))
#endif
static uint32_t classify(badger_frame_presenter_t *presenter, const badger_frame_t *frame) {
    uint32_t total = 0u;
    badger_bytes_zero(presenter->dirty, BADGER_FRAME_TILES);
    for (uint32_t x = 0u; x < BADGER_GFX_WIDTH; ++x) {
        uint32_t column = (x + BADGER_FRAME_OFFSET) / BADGER_FRAME_TILE;
        const uint32_t *a = frame->words + x * (BADGER_GFX_HEIGHT / 2u);
        const uint32_t *b = presenter->previous.words + x * (BADGER_GFX_HEIGHT / 2u);
        total += mark_changed(presenter, column, a[0] ^ b[0]);
        uint32_t index = column + BADGER_FRAME_COLUMNS;
        for (uint32_t word = 1u; word < BADGER_GFX_HEIGHT / 2u - 1u; word += 2u) {
            uint32_t difference = (a[word] ^ b[word]) | (a[word + 1u] ^ b[word + 1u]);
            total += mark_changed(presenter, index, difference);
            index += BADGER_FRAME_COLUMNS;
        }
        total += mark_changed(presenter, index,
            a[BADGER_GFX_HEIGHT / 2u - 1u] ^ b[BADGER_GFX_HEIGHT / 2u - 1u]);
    }
    return total;
}

/* Keep comparison/copy register pressure out of the rectangle planner. */
#if defined(__GNUC__)
__attribute__((noinline))
#endif
static int emit(badger_frame_presenter_t *presenter, const badger_frame_t *frame,
    badger_frame_writer_t writer, void *context, badger_frame_stats_t *stats,
    const badger_frame_region_t *region) {
    if (!writer(context, &frame->canvas, region)) {
        presenter->valid = 0u;
        return 0;
    }
    for (uint32_t x = region->x; x < region->x + region->width; ++x) {
        uint32_t offset = (x * BADGER_GFX_HEIGHT + region->y) / 2u;
        for (uint32_t word = 0u; word < region->height / 2u; ++word)
            presenter->previous.words[offset + word] = frame->words[offset + word];
    }
    stats->pixels += region->width * region->height;
    ++stats->regions;
    return 1;
}

badger_frame_result_t badger_frame_present(badger_frame_presenter_t *presenter,
    const badger_frame_t *frame, badger_frame_writer_t writer, void *context,
    badger_frame_stats_t *stats) {
    if (presenter == 0 || frame == 0 || frame == &presenter->previous ||
        writer == 0 || stats == 0) return BADGER_FRAME_ARGUMENT;
    badger_bytes_zero((uint8_t *)stats, (uint32_t)sizeof(*stats));
    stats->dirty_tiles = presenter->valid ? classify(presenter, frame) : BADGER_FRAME_TILES;
    if (!presenter->valid || stats->dirty_tiles > BADGER_FRAME_TILES * 3u / 4u) {
        const badger_frame_region_t region = {0u, 0u, BADGER_GFX_WIDTH, BADGER_GFX_HEIGHT};
        if (!emit(presenter, frame, writer, context, stats, &region)) return BADGER_FRAME_IO;
        stats->full = 1u;
    } else {
        for (uint32_t row = 0u; row < BADGER_FRAME_ROWS; ++row) {
            for (uint32_t column = 0u; column < BADGER_FRAME_COLUMNS;) {
                if (!presenter->dirty[row * BADGER_FRAME_COLUMNS + column]) { ++column; continue; }
                uint32_t end = column + 1u;
                while (end < BADGER_FRAME_COLUMNS && presenter->dirty[row * BADGER_FRAME_COLUMNS + end]) ++end;
                uint32_t bottom = row + 1u;
                while (bottom < BADGER_FRAME_ROWS) {
                    uint32_t x = column;
                    while (x < end && presenter->dirty[bottom * BADGER_FRAME_COLUMNS + x]) ++x;
                    if (x != end) break;
                    ++bottom;
                }
                for (uint32_t y = row; y < bottom; ++y)
                    for (uint32_t x = column; x < end; ++x)
                        presenter->dirty[y * BADGER_FRAME_COLUMNS + x] = 0u;
                badger_frame_region_t region;
                region.x = boundary(column, BADGER_GFX_WIDTH);
                region.y = boundary(row, BADGER_GFX_HEIGHT);
                region.width = boundary(end, BADGER_GFX_WIDTH) - region.x;
                region.height = boundary(bottom, BADGER_GFX_HEIGHT) - region.y;
                if (!emit(presenter, frame, writer, context, stats, &region)) return BADGER_FRAME_IO;
                column = end;
            }
        }
    }
    presenter->valid = 1u;
    return BADGER_FRAME_OK;
}
