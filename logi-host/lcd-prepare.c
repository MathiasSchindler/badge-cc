#include "../logi-keypad/keypad-lcd.h"

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

int main(void) {
    puts("/* Generated numbered JPEGs; macOS encoding is build-time only. */");
    for (unsigned variant = 0u; variant < 2u; ++variant) {
        for (unsigned key = 1u; key <= 9u; ++key) {
            uint8_t pixels[LCD_PIXELS_SIZE];
            if (!lcd_pixels(key, pixels)) return 1;
            if (variant)
                for (unsigned i = 0u; i < LCD_PIXELS_SIZE; i += 4u)
                    for (unsigned c = 0u; c < 3u; ++c) pixels[i + c] ^= 255u;
            CFDataRef jpeg = lcd_encode_jpeg(key, pixels);
            if (!jpeg) return 1;
            CGImageSourceRef source = CGImageSourceCreateWithData(jpeg, NULL);
            CGImageRef image = source ? CGImageSourceCreateImageAtIndex(source, 0, NULL) : NULL;
            int valid = validate_pixels(image, pixels);
            if (image) CGImageRelease(image);
            if (source) CFRelease(source);
            if (!valid) {
                fprintf(stderr, "Prepared JPEG key %u variant %u failed dimension/pixel validation.\n", key, variant);
                CFRelease(jpeg); return 1;
            }
            printf("static const uint8_t lcd_jpeg_%u_%u[] = {", variant, key);
            const uint8_t *bytes = CFDataGetBytePtr(jpeg);
            for (CFIndex i = 0; i < CFDataGetLength(jpeg); ++i) {
                if (!(i % 16)) putchar('\n');
                printf("0x%02x,", bytes[i]);
            }
            puts("\n};");
            CFRelease(jpeg);
        }
        printf("const fs_lcd_image_t %s[9] = {\n", variant ? "fs_lcd_inverted_images" : "fs_lcd_images");
        for (unsigned key = 1u; key <= 9u; ++key)
            printf("{lcd_jpeg_%u_%u, sizeof(lcd_jpeg_%u_%u)},\n", variant, key, variant, key);
        puts("};");
    }
    if (fflush(stdout) || ferror(stdout)) { fputs("Cannot write prepared JPEG header.\n", stderr); return 1; }
    return 0;
}
