#ifndef UNIVERSE26_FRAME_DIFF_H
#define UNIVERSE26_FRAME_DIFF_H

#include "../platform/badger/graphics.h"

#define BADGER_FRAME_TILE 4u
#define BADGER_FRAME_OFFSET 2u
#define BADGER_FRAME_COLUMNS (BADGER_GFX_WIDTH / BADGER_FRAME_TILE + 1u)
#define BADGER_FRAME_ROWS (BADGER_GFX_HEIGHT / BADGER_FRAME_TILE + 1u)
#define BADGER_FRAME_TILES (BADGER_FRAME_COLUMNS * BADGER_FRAME_ROWS)

/* Aligned views of the same object representation, without aliasing casts. */
typedef union {
    badger_canvas_t canvas;
    uint32_t words[BADGER_GFX_WIDTH * BADGER_GFX_HEIGHT / 2u];
} badger_frame_t;

typedef struct {
    uint32_t x, y, width, height;
} badger_frame_region_t;

typedef struct {
    uint32_t pixels, regions, dirty_tiles, full;
} badger_frame_stats_t;

typedef struct {
    badger_frame_t previous;
    uint8_t dirty[BADGER_FRAME_TILES];
    uint8_t valid;
} badger_frame_presenter_t;

typedef enum {
    BADGER_FRAME_OK, BADGER_FRAME_ARGUMENT, BADGER_FRAME_IO
} badger_frame_result_t;

typedef int (*badger_frame_writer_t)(void *context, const badger_canvas_t *canvas,
                                     const badger_frame_region_t *region);

/* Invalidate after panel reset or any write outside this presenter. */
void badger_frame_invalidate(badger_frame_presenter_t *presenter);
/* Frame is immutable until return. Writer returns nonzero only after success. */
badger_frame_result_t badger_frame_present(badger_frame_presenter_t *presenter,
    const badger_frame_t *frame, badger_frame_writer_t writer, void *context,
    badger_frame_stats_t *stats);

#endif
