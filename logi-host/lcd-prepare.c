#include "../logi-keypad/keypad-lcd.h"
#include "fs_lcd.h"

static int validate_pixels(CGImageRef image, const uint8_t expected[LCD_PIXELS_SIZE]) {
    if (!image || CGImageGetWidth(image) != LCD_KEY_SIZE || CGImageGetHeight(image) != LCD_KEY_SIZE)
        return 0;
    uint8_t decoded[LCD_PIXELS_SIZE];
    CGColorSpaceRef colors = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = colors ? CGBitmapContextCreate(decoded, LCD_KEY_SIZE, LCD_KEY_SIZE,
        8u, LCD_KEY_SIZE * 4u, colors, kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder32Big) : NULL;
    if (colors) CGColorSpaceRelease(colors);
    if (!context) return 0;
    CGContextDrawImage(context, CGRectMake(0, 0, LCD_KEY_SIZE, LCD_KEY_SIZE), image);
    CGContextRelease(context);
    unsigned error = 0u;
    for (unsigned i = 0u; i < LCD_PIXELS_SIZE; i += 4u)
        for (unsigned c = 0u; c < 3u; ++c) {
            int difference = (int)decoded[i + c] - expected[i + c];
            error += difference < 0 ? (unsigned)-difference : (unsigned)difference;
        }
    return error < 12u * LCD_KEY_SIZE * LCD_KEY_SIZE * 3u;
}

static int emit_jpeg(unsigned group, unsigned index, unsigned key,
                     const uint8_t pixels[LCD_PIXELS_SIZE]) {
    CFDataRef jpeg = lcd_encode_jpeg(key, pixels);
    if (!jpeg) return 0;
    CGImageSourceRef source = CGImageSourceCreateWithData(jpeg, NULL);
    CGImageRef image = source ? CGImageSourceCreateImageAtIndex(source, 0, NULL) : NULL;
    int valid = validate_pixels(image, pixels) &&
                (group != 2u || CFDataGetLength(jpeg) <= LCD_PACKET_SIZE - 20u);
    if (image) CGImageRelease(image);
    if (source) CFRelease(source);
    if (!valid) {
        fprintf(stderr, "Prepared JPEG group %u index %u failed pixel/dimension/size validation.\n", group, index);
        CFRelease(jpeg); return 0;
    }
    printf("static const uint8_t lcd_jpeg_%u_%u[] = {", group, index);
    const uint8_t *bytes = CFDataGetBytePtr(jpeg);
    for (CFIndex i = 0; i < CFDataGetLength(jpeg); ++i) {
        if (!(i % 16)) putchar('\n');
        printf("0x%02x,", bytes[i]);
    }
    puts("\n};");
    CFRelease(jpeg);
    return 1;
}

static void animation_pixels(unsigned frame, uint8_t pixels[LCD_PIXELS_SIZE]) {
    static const uint8_t positions[FS_LCD_ANIMATION_FRAMES][2] = {
        {53,12}, {82,24}, {94,53}, {82,82}, {53,94}, {24,82}, {12,53}, {24,24}
    };
    for (unsigned i = 0u; i < LCD_PIXELS_SIZE; i += 4u) {
        pixels[i] = 8u; pixels[i + 1u] = 12u; pixels[i + 2u] = 22u; pixels[i + 3u] = 255u;
    }
    lcd_rectangle(pixels, 2u, 2u, 114u, 2u);
    lcd_rectangle(pixels, 2u, 114u, 114u, 2u);
    lcd_rectangle(pixels, 2u, 2u, 2u, 114u);
    lcd_rectangle(pixels, 114u, 2u, 2u, 114u);
    for (unsigned segment = 0u; segment < FS_LCD_ANIMATION_FRAMES; ++segment) {
        unsigned age = (frame + FS_LCD_ANIMATION_FRAMES - segment) % FS_LCD_ANIMATION_FRAMES;
        for (unsigned y = positions[segment][1]; y < positions[segment][1] + 12u; ++y)
            for (unsigned x = positions[segment][0]; x < positions[segment][0] + 12u; ++x) {
                uint8_t *p = pixels + (y * LCD_KEY_SIZE + x) * 4u;
                p[0] = age < 3u ? 40u : 20u;
                p[1] = age == 0u ? 230u : age == 1u ? 150u : age == 2u ? 80u : 35u;
                p[2] = age == 0u ? 255u : age == 1u ? 190u : age == 2u ? 120u : 55u;
            }
    }
}

int main(void) {
    puts("/* Generated JPEGs; macOS encoding is build-time only. */");
    for (unsigned variant = 0u; variant < 2u; ++variant) {
        for (unsigned key = 1u; key <= 9u; ++key) {
            uint8_t pixels[LCD_PIXELS_SIZE];
            if (!lcd_pixels(key, pixels)) return 1;
            if (variant)
                for (unsigned i = 0u; i < LCD_PIXELS_SIZE; i += 4u)
                    for (unsigned c = 0u; c < 3u; ++c) pixels[i + c] ^= 255u;
            if (!emit_jpeg(variant, key, key, pixels)) return 1;
        }
        printf("const fs_lcd_image_t %s[9] = {\n", variant ? "fs_lcd_inverted_images" : "fs_lcd_images");
        for (unsigned key = 1u; key <= 9u; ++key)
            printf("{lcd_jpeg_%u_%u, sizeof(lcd_jpeg_%u_%u)},\n", variant, key, variant, key);
        puts("};");
    }
    for (unsigned frame = 0u; frame < FS_LCD_ANIMATION_FRAMES; ++frame) {
        uint8_t pixels[LCD_PIXELS_SIZE];
        animation_pixels(frame, pixels);
        if (!emit_jpeg(2u, frame, FS_LCD_ANIMATION_KEY + 1u, pixels)) return 1;
    }
    puts("const fs_lcd_image_t fs_lcd_animation[FS_LCD_ANIMATION_FRAMES] = {");
    for (unsigned frame = 0u; frame < FS_LCD_ANIMATION_FRAMES; ++frame)
        printf("{lcd_jpeg_2_%u, sizeof(lcd_jpeg_2_%u)},\n", frame, frame);
    puts("};");
    if (fflush(stdout) || ferror(stdout)) { fputs("Cannot write prepared JPEG header.\n", stderr); return 1; }
    return 0;
}
