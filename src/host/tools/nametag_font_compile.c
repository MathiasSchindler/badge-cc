#include "font_platform.h"
#include "../../shared/fontrender/fontrender/font_backend.h"
#include "../../shared/fontrender/fontrender/fr_ttf.h"
#include "../../shared/bitmap_font.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int regular_sizes[] = {12, 14, 16, 18};
static const int bold_sizes[] = {24, 28, 32, 36, 40, 44};

static int emit_font(FILE *output, FrFont *font, FrTtfFont *ttf, unsigned number,
                      int size, unsigned bold, unsigned bits, size_t *total) {
    badger_font_glyph_t glyphs[206];
    uint8_t *pixels = malloc(206u * 128u * 128u);
    if (pixels == NULL) { fputs("glyph workspace allocation failed\n", stderr); return 0; }
    size_t bytes = 0u;
    unsigned count = 0u, ascent = 0u, descent = 0u;
    for (uint32_t codepoint = 0x20u; codepoint <= 0x20acu; ++codepoint) {
        if (!badger_font_codepoint_supported(codepoint)) continue;
        uint32_t lookup = codepoint == 0xadu ? '-' : codepoint == 0xa0u ? ' ' : codepoint;
        uint16_t index;
        FrTtfResult found = fr_ttf_lookup_glyph(ttf, lookup, &index);
        if (found != FR_TTF_OK || index == 0u) {
            fprintf(stderr, "required glyph U+%04X missing: %s\n", (unsigned)lookup, fr_ttf_result_string(found));
            free(pixels);
            return 0;
        }
        const FrGlyph *glyph = fr_font_get_glyph(font, lookup, size, 0u);
        if (glyph == NULL || glyph->codepoint != lookup || glyph->width < 0 || glyph->width > 128 ||
            glyph->height < 0 || glyph->height > 128 || glyph->advance < 0 || glyph->advance > INT16_MAX ||
            glyph->left < INT16_MIN || glyph->left > INT16_MAX || glyph->top < INT16_MIN || glyph->top > INT16_MAX ||
            count >= 206u) {
            fprintf(stderr, "invalid raster/metrics for U+%04X at %d px\n", (unsigned)lookup, size);
            free(pixels);
            return 0;
        }
        glyphs[count++] = (badger_font_glyph_t){
            codepoint, (uint32_t)bytes, (int16_t)(codepoint == 0xadu ? 0 : glyph->advance),
            (int16_t)glyph->left, (int16_t)glyph->top,
            (uint16_t)(codepoint == 0xadu ? 0 : glyph->width),
            (uint16_t)(codepoint == 0xadu ? 0 : glyph->height)
        };
        if (codepoint == 0xadu) continue;
        if (glyph->top > (int)ascent) ascent = (unsigned)glyph->top;
        int below = glyph->height - glyph->top;
        if (below > (int)descent) descent = (unsigned)below;
        size_t count_pixels = (size_t)glyph->width * (size_t)glyph->height;
        if (bits == 8u) {
            if (count_pixels != 0u) memcpy(pixels + bytes, glyph->bitmap, count_pixels);
            bytes += count_pixels;
        } else {
            for (size_t i = 0u; i < count_pixels; i += 2u) {
                unsigned high = ((unsigned)glyph->bitmap[i] + 8u) / 17u;
                unsigned low = i + 1u < count_pixels ? ((unsigned)glyph->bitmap[i + 1u] + 8u) / 17u : 0u;
                pixels[bytes++] = (uint8_t)((high << 4u) | low);
            }
        }
    }
    if (count != 206u) { fputs("repertoire count mismatch\n", stderr); free(pixels); return 0; }
    fprintf(output, "static const uint8_t pixels_%u[] = {", number);
    for (size_t i = 0u; i < bytes; ++i) fprintf(output, "%s%u,", i % 24u == 0u ? "\n" : "", pixels[i]);
    fprintf(output, "\n};\nstatic const badger_font_glyph_t glyphs_%u[] = {\n", number);
    for (unsigned i = 0u; i < count; ++i) {
        const badger_font_glyph_t *glyph = &glyphs[i];
        fprintf(output, "{%uu,%uu,%d,%d,%d,%u,%u},\n", glyph->codepoint, glyph->offset,
                glyph->advance, glyph->left, glyph->top, glyph->width, glyph->height);
    }
    unsigned height = (unsigned)fr_font_line_height(font, size);
    if (ascent + descent > height) height = ascent + descent;
    fprintf(output, "};\nstatic const badger_font_t font_%u = "
        "{glyphs_%u,pixels_%u,%zuu,206,%d,%u,%u,%u,%u};\n",
        number, number, number, bytes, size, height, ascent, bits, bold);
    *total += bytes;
    free(pixels);
    return !ferror(output);
}

static int emit_license(FILE *output, const char *name) {
    FILE *input = fopen(name, "rb");
    if (input == NULL) { perror(name); return 0; }
    char license[16385];
    size_t length = fread(license, 1u, sizeof(license) - 1u, input);
    int ok = !ferror(input) && length < sizeof(license) - 1u && feof(input);
    if (fclose(input) != 0) ok = 0;
    license[length] = '\0';
    if (!ok || memchr(license, '\0', length) != NULL ||
        strstr(license, "SIL OPEN FONT LICENSE Version 1.1") == NULL ||
        strstr(license, "Microsoft") == NULL) {
        fputs("missing or invalid Cascadia OFL/copyright notice\n", stderr);
        return 0;
    }
    fputs("static const char nametag_font_license[] =\n\"", output);
    for (size_t i = 0u; i < length; ++i) {
        unsigned char value = (unsigned char)license[i];
        if (value == '"' || value == '\\') fprintf(output, "\\%c", value);
        else if (value == '\n') fputs("\\n\"\n\"", output);
        else if (value < 32u || value > 126u) fprintf(output, "\\%03o", value);
        else fputc(value, output);
    }
    fputs("\";\n", output);
    return !ferror(output);
}

int main(int argc, char **argv) {
    if (argc != 9 || (strcmp(argv[7], "8") != 0 && strcmp(argv[7], "4") != 0)) {
        fputs("Usage: nametag_font_compile OUTPUT.c OUTPUT.h MONO_REGULAR.ttf MONO_BOLD.ttf CODE_REGULAR.ttf CODE_BOLD.ttf 8|4 LICENSE\n", stderr);
        return 2;
    }
    if (host_font_platform_install() != 0) { fputs("font platform initialization failed\n", stderr); return 1; }
    FILE *output = fopen(argv[1], "wb");
    if (output == NULL) { perror(argv[1]); return 1; }
    FILE *header = fopen(argv[2], "wb");
    if (header == NULL) { perror(argv[2]); fclose(output); return 1; }
    int ok;
    size_t bytes = 0u;
    unsigned number = 0u, bits = (unsigned)(argv[7][0] - '0');
    fputs("#include \"nametag_fonts.h\"\n", output);
    ok = emit_license(output, argv[8]);
    fputs("#ifndef UNIVERSE26_NAMETAG_FONTS_H\n#define UNIVERSE26_NAMETAG_FONTS_H\n"
          "#include \"nametag.h\"\nextern const badger_nametag_fonts_t nametag_mono_fonts;\n"
          "extern const badger_nametag_fonts_t nametag_code_fonts;\n#endif\n", header);
    for (unsigned file = 0u; file < 4u && ok; ++file) {
        FrFont *font = NULL;
        FrTtfFont *ttf = NULL;
        FrTtfResult result = fr_ttf_open(&ttf, argv[3u + file]);
        if (result != FR_TTF_OK) {
            fprintf(stderr, "%s: %s\n", argv[3u + file], fr_ttf_result_string(result));
            ok = 0;
        } else if (fr_font_open(&font, argv[3u + file]) != 0) ok = 0;
        unsigned count = (file & 1u) ? 6u : 4u;
        const int *sizes = (file & 1u) ? bold_sizes : regular_sizes;
        for (unsigned i = 0u; i < count && ok; ++i) {
            ok = emit_font(output, font, ttf, number++, sizes[i], file & 1u, bits, &bytes);
        }
        fr_font_close(font);
        fr_ttf_close(ttf);
    }
    if (ok) {
        for (unsigned family = 0u; family < 2u; ++family) {
            fprintf(output, "const badger_nametag_fonts_t nametag_%s_fonts = {{",
                    family == 0u ? "mono" : "code");
            for (unsigned i = 0u; i < 4u; ++i) fprintf(output, "&font_%u,", family * 10u + i);
            fputs("},{", output);
            for (unsigned i = 0u; i < 6u; ++i) fprintf(output, "&font_%u,", family * 10u + 4u + i);
            fputs("},nametag_font_license};\n", output);
        }
    }
    if (ferror(output) || ferror(header)) ok = 0;
    if (fclose(output) != 0) ok = 0;
    if (fclose(header) != 0) ok = 0;
    if (!ok) {
        fputs("font generation failed; removing incomplete outputs\n", stderr);
        if (remove(argv[1]) != 0) perror(argv[1]);
        if (remove(argv[2]) != 0) perror(argv[2]);
        return 1;
    }
    printf("20 fonts, 206 glyphs/font, %u-bit coverage, %zu bitmap bytes\n", bits, bytes);
    return 0;
}
