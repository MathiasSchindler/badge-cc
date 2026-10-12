#include "../../shared/nametag.h"
#include "nametag_fonts.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static badger_canvas_t canvas;
static badger_nametag_workspace_t workspace;
static badger_nametag_scene_t scene;
static uint8_t rgb[BADGER_GFX_WIDTH * BADGER_GFX_HEIGHT * 3];

static int read_field(FILE *input, char *text, size_t capacity) {
    size_t length = 0u;
    int value;
    while ((value = fgetc(input)) != EOF && value != '\n') {
        if (value == 0 || length + 1u >= capacity) {
            fputs("profile field contains NUL or exceeds its byte limit\n", stderr);
            return 0;
        }
        text[length++] = (char)value;
    }
    if (value != '\n') { fputs("profile input is incomplete; expected newline\n", stderr); return 0; }
    return 1;
}

static int read_profile(FILE *input, badger_nametag_profile_t *profile) {
    memset(profile, 0, sizeof(*profile));
    if (!read_field(input, profile->name, sizeof(profile->name)) ||
        !read_field(input, profile->role, sizeof(profile->role)) ||
        !read_field(input, profile->employer, sizeof(profile->employer)) ||
        !read_field(input, profile->username, sizeof(profile->username)) ||
        !read_field(input, profile->location, sizeof(profile->location))) return 0;
    char count[4] = {0};
    if (!read_field(input, count, sizeof(count))) return 0;
    if (count[0] < '0' || count[0] > '8' || count[1] != '\0') {
        fputs("interest count must be a single digit 0-8\n", stderr);
        return 0;
    }
    profile->interest_count = (uint32_t)(count[0] - '0');
    for (uint32_t i = 0u; i < profile->interest_count; ++i) {
        if (!read_field(input, profile->interests[i], sizeof(profile->interests[i]))) return 0;
    }
    if (fgetc(input) != EOF) { fputs("unexpected trailing profile input\n", stderr); return 0; }
    return 1;
}

static int prepare(const badger_nametag_profile_t *profile,
                    const badger_nametag_fonts_t *fonts, badger_nametag_variant_t variant) {
    badger_nametag_error_t error;
    badger_nametag_result_t result = badger_nametag_prepare(&scene, profile, fonts, variant, &error);
    if (result != BADGER_NAMETAG_OK) {
        fprintf(stderr, "%s: %s (byte/item %u, U+%04X)\n",
                error.field, badger_nametag_result_name(result), error.byte_offset, error.codepoint);
        return 0;
    }
    return 1;
}

static int render(uint32_t time, int crisp) {
    badger_font_result_t result = badger_nametag_render(&canvas, &workspace, &scene, time, crisp);
    if (result != BADGER_FONT_OK) { fprintf(stderr, "%s\n", badger_font_result_name(result)); return 0; }
    for (uint32_t y = 0u; y < BADGER_GFX_HEIGHT; ++y) {
        for (uint32_t x = 0u; x < BADGER_GFX_WIDTH; ++x) {
            uint16_t pixel = canvas.pixels[x * BADGER_GFX_HEIGHT + y];
            uint32_t index = (y * BADGER_GFX_WIDTH + x) * 3u;
            rgb[index] = (uint8_t)(((pixel >> 11u) * 255u + 15u) / 31u);
            rgb[index + 1u] = (uint8_t)((((pixel >> 5u) & 63u) * 255u + 31u) / 63u);
            rgb[index + 2u] = (uint8_t)(((pixel & 31u) * 255u + 15u) / 31u);
        }
    }
    return 1;
}

int main(int argc, char **argv) {
    int validate = argc == 3 && strcmp(argv[1], "--validate") == 0 &&
        (strcmp(argv[2], "mono") == 0 || strcmp(argv[2], "code") == 0);
    if ((argc == 2 && strcmp(argv[1], "--info") == 0) || validate) {
        badger_nametag_profile_t profile = badger_nametag_default_profile;
        if (validate && !read_profile(stdin, &profile)) return 1;
        const badger_nametag_fonts_t *fonts = validate && strcmp(argv[2], "code") == 0 ?
            &nametag_code_fonts : &nametag_mono_fonts;
        printf("{\"canvas_bytes\":%zu,\"workspace_bytes\":%zu,\"scene_bytes\":%zu,"
               "\"profile_bytes\":%zu,\"coverage_bits\":%u,\"layouts\":[",
               sizeof(canvas), sizeof(workspace), sizeof(scene), sizeof(badger_nametag_profile_t),
               nametag_mono_fonts.regular[0]->coverage_bits);
        for (int variant = 0; variant < 3; ++variant) {
            if (!prepare(&profile, fonts, (badger_nametag_variant_t)variant)) return 1;
            printf("%s{\"variant\":\"%c\",\"name_px\":%u,\"name_lines\":%u,"
                   "\"pages\":%u,\"loop_ms\":%u}", variant ? "," : "", 'A' + variant,
                   scene.name.font->pixel_size, scene.name.line_count, scene.page_count,
                   badger_nametag_loop_ms(&scene));
        }
        fputs("]}\n", stdout);
        return ferror(stdout) ? 1 : 0;
    }
    int frame = argc == 6 && strcmp(argv[1], "--frame") == 0;
    int still = (argc == 7 || argc == 8) && strcmp(argv[1], "--still") == 0;
    if ((!frame && !still) || strlen(argv[2]) != 1u || argv[2][0] < 'A' || argv[2][0] > 'C' ||
        (strcmp(argv[4], "mono") != 0 && strcmp(argv[4], "code") != 0) ||
        (strcmp(argv[5], "natural") != 0 && strcmp(argv[5], "crisp") != 0)) {
        fputs("Usage: nametag_preview --info | --validate mono|code < profile.txt\n"
              "       nametag_preview --frame A|B|C TIME_MS mono|code natural|crisp < profile.txt\n"
              "       nametag_preview --still A|B|C TIME_MS mono|code natural|crisp OUTPUT.ppm [PROFILE.txt]\n", stderr);
        return 2;
    }
    errno = 0;
    char *end;
    unsigned long time = strtoul(argv[3], &end, 10);
    if (errno || argv[3][0] == '\0' || argv[3][0] == '-' || *end != '\0' || time > 0xfffffffful) {
        fputs("time must be a uint32 millisecond value\n", stderr);
        return 2;
    }
    badger_nametag_profile_t profile = badger_nametag_default_profile;
    if (frame) {
        if (!read_profile(stdin, &profile)) return 1;
    } else if (argc == 8) {
        FILE *input = fopen(argv[7], "rb");
        if (input == NULL) { perror(argv[7]); return 1; }
        int ok = read_profile(input, &profile);
        if (fclose(input) != 0) { perror(argv[7]); ok = 0; }
        if (!ok) return 1;
    }
    const badger_nametag_fonts_t *fonts = strcmp(argv[4], "mono") == 0 ? &nametag_mono_fonts : &nametag_code_fonts;
    if (!prepare(&profile, fonts, (badger_nametag_variant_t)(argv[2][0] - 'A')) ||
        !render((uint32_t)time, strcmp(argv[5], "crisp") == 0)) return 1;
    FILE *output = frame ? stdout : fopen(argv[6], "wb");
    if (output == NULL) { perror(argv[6]); return 1; }
    int ok = frame || fprintf(output, "P6\n320 240\n255\n") > 0;
    if (fwrite(rgb, 1u, sizeof(rgb), output) != sizeof(rgb)) ok = 0;
    if (frame ? fflush(output) != 0 : fclose(output) != 0) ok = 0;
    if (!ok) { fputs("preview image write failed\n", stderr); return 1; }
    return 0;
}
