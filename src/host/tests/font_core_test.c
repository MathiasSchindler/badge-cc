#include "../tools/font_platform.h"
#include "../../shared/fontrender/fontrender/fr_ttf.h"
#include "../../shared/fontrender/fontrender/fr_raster.h"
#include "../../shared/fontrender/fontrender/font_backend.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *no_alloc(void *context, size_t size) {
    (void)context; (void)size;
    return NULL;
}
static void *no_realloc(void *context, void *pointer, size_t size) {
    (void)context; (void)pointer; (void)size;
    return NULL;
}

int main(void) {
    assert(host_font_platform_install() == 0);
    const char *names[] = {"CascadiaMono-Regular.ttf", "CascadiaMono-Bold.ttf",
                          "CascadiaCode-Regular.ttf", "CascadiaCode-Bold.ttf"};
    for (unsigned i = 0; i < 4; ++i) {
        char file[128];
        snprintf(file, sizeof(file), "vendor/cascadia-code/%s", names[i]);
        FrTtfFont *ttf = NULL;
        assert(fr_ttf_open(&ttf, file) == FR_TTF_OK);
        FrTtfInfo info;
        assert(fr_ttf_get_info(ttf, &info) == FR_TTF_OK);
        assert(info.units_per_em > 0 && info.num_glyphs > 206);
        uint16_t index;
        assert(fr_ttf_lookup_glyph(ttf, 'A', &index) == FR_TTF_OK && index != 0);
        assert(fr_ttf_lookup_glyph(ttf, 0x10ffffu, &index) == FR_TTF_ERR_NOT_FOUND);
        fr_ttf_close(ttf);
        FrFont *font = NULL;
        assert(fr_font_open(&font, file) == 0);
        const FrGlyph *glyph = fr_font_get_glyph(font, 'A', 24, 0);
        assert(glyph != NULL && glyph->codepoint == 'A' && glyph->width > 0 && glyph->height > 0);
        assert(fr_font_get_glyph(font, 'A', 24, 0) == glyph);
        /* Preserve the imported API's fallback; the asset compiler refuses it. */
        glyph = fr_font_get_glyph(font, 0x10ffffu, 24, 0);
        assert(glyph != NULL && glyph->codepoint == '?');
        fr_font_close(font);
    }
    FrPlatform platform;
    fr_platform_get(&platform);
    FrPlatformFile file;
    assert(platform.load_file(platform.user_data, "vendor/cascadia-code/CascadiaMono-Regular.ttf", &file) == FR_PLATFORM_FILE_OK);
    for (size_t cut = 0; cut < 128; ++cut) {
        FrTtfFont *ttf = NULL;
        assert(fr_ttf_open_memory(&ttf, file.data, cut) != FR_TTF_OK);
        assert(ttf == NULL);
    }
    uint8_t *copy = malloc(file.size);
    assert(copy != NULL);
    memcpy(copy, file.data, file.size);
    memcpy(copy, "OTTO", 4);
    FrTtfFont *ttf = NULL;
    assert(fr_ttf_open_memory(&ttf, copy, file.size) == FR_TTF_ERR_UNSUPPORTED_SFNT);
    memcpy(copy, file.data, file.size);
    memset(copy + 20, 255, 4);
    assert(fr_ttf_open_memory(&ttf, copy, file.size) == FR_TTF_ERR_MALFORMED);
    free(copy);
    FrPlatform failing = platform;
    failing.alloc = no_alloc;
    failing.realloc = no_realloc;
    assert(fr_platform_set(&failing) == 0);
    assert(fr_ttf_open_memory(&ttf, file.data, file.size) == FR_TTF_ERR_NO_MEMORY);
    FrBitmap bitmap;
    fr_bitmap_init(&bitmap);
    assert(fr_bitmap_alloc(&bitmap, 10, 10) != 0);
    assert(bitmap.pixels == NULL);
    assert(fr_platform_set(&platform) == 0);
    platform.unload_file(platform.user_data, &file);

    FrOutline outline;
    fr_outline_init(&outline);
    assert(fr_outline_add_point(&outline, 0, 0, FR_OUTLINE_POINT_ON_CURVE) == 0);
    assert(fr_outline_add_point(&outline, FR_OUTLINE_ONE / 2, 0, FR_OUTLINE_POINT_ON_CURVE) == 0);
    assert(fr_outline_add_point(&outline, FR_OUTLINE_ONE / 2, FR_OUTLINE_ONE, FR_OUTLINE_POINT_ON_CURVE) == 0);
    assert(fr_outline_add_point(&outline, 0, FR_OUTLINE_ONE, FR_OUTLINE_POINT_ON_CURVE) == 0);
    assert(fr_outline_add_contour(&outline, 0, 4) == 0);
    assert(fr_raster_render(&bitmap, &outline) == 0);
    assert(bitmap.width == 1 && bitmap.height == 1 && bitmap.pixels[0] == 128);
    fr_bitmap_free(&bitmap);
    fr_outline_free(&outline);
    puts("font core: static TTF/glyf, metrics/cache, explicit parser errors, allocation failure and fractional raster coverage PASS");
    return 0;
}
