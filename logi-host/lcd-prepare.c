#include "../logi-keypad/keypad-lcd.h"

int main(void) {
    puts("/* Generated numbered JPEGs; macOS encoding is build-time only. */");
    for (unsigned key = 1u; key <= 9u; ++key) {
        CFDataRef jpeg = lcd_jpeg(key);
        if (!jpeg) return 1;
        CGImageSourceRef source = CGImageSourceCreateWithData(jpeg, NULL);
        CGImageRef image = source ? CGImageSourceCreateImageAtIndex(source, 0, NULL) : NULL;
        int valid = image && CGImageGetWidth(image) == LCD_KEY_SIZE &&
                    CGImageGetHeight(image) == LCD_KEY_SIZE;
        if (image) CGImageRelease(image);
        if (source) CFRelease(source);
        if (!valid) { fputs("Prepared JPEG failed dimension/decode validation.\n", stderr); CFRelease(jpeg); return 1; }
        printf("static const uint8_t lcd_jpeg_%u[] = {", key);
        const uint8_t *bytes = CFDataGetBytePtr(jpeg);
        for (CFIndex i = 0; i < CFDataGetLength(jpeg); ++i) {
            if (!(i % 16)) putchar('\n');
            printf("0x%02x,", bytes[i]);
        }
        puts("\n};");
        CFRelease(jpeg);
    }
    puts("const fs_lcd_image_t fs_lcd_images[9] = {");
    for (unsigned key = 1u; key <= 9u; ++key)
        printf("{lcd_jpeg_%u, sizeof(lcd_jpeg_%u)},\n", key, key);
    puts("};");
    if (fflush(stdout) || ferror(stdout)) { fputs("Cannot write prepared JPEG header.\n", stderr); return 1; }
    return 0;
}
